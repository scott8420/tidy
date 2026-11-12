#include "core/Catalog.hpp"
#include "core/Removal.hpp"
#include "core/Due.hpp"
#include "core/Protection.hpp"

#include "core/Birth.hpp"
#include "core/Closet.hpp"
#include "core/FileLists.hpp"
#include "core/Own.hpp"
#include "core/Paths.hpp"
#include "core/Scan.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

namespace tidy::core::due {

namespace rt = retention;

const char* to_id(To t) {
    switch (t) {
        case To::Remove: return "remove";
        case To::Archive: return "archive";
        case To::Settle: return "settle";
    }
    return "";
}
const char* to_title(To t) {
    switch (t) {
        case To::Remove: return "To remove";
        case To::Archive: return "To archive";
        case To::Settle: return "To settle";
    }
    return "";
}

namespace {
std::int64_t born(const facts::Facts& f) { return f.has_btime && f.btime > 0 ? f.btime : f.mtime; }
std::string days(int n) { return std::to_string(n) + (n == 1 ? " day" : " days"); }
}  // namespace

std::vector<Item> due(const std::vector<facts::Facts>& files, const purposes::Book& book,
                      const settings::Settings& s, std::int64_t now) {
    std::vector<Item> out;
    for (const auto& f : files) {
        rt::Level lv;
        if (!rt::parse(f.tags.retention, lv)) continue;
        const int age = closet::days_since(born(f), now), unread = closet::days_since(f.atime, now),
                  unchanged = closet::days_since(f.mtime, now);
        Item it{f.path, lv, To::Remove, ""};
        switch (lv) {
            case rt::Level::Ephemeral:
                if (age < s.aging_days || unread < s.aging_days) continue;
                it.why = "Ephemeral, born " + days(age) + " ago, unread " + days(unread) + " (aging " +
                         std::to_string(s.aging_days) + ")";
                break;
            case rt::Level::Held: {
                const int held = closet::days_since(f.ctime, now);
                if (held < s.held_years * 365) continue;
                it.why = "Held " + std::to_string(held / 365) + " years (held " + std::to_string(s.held_years) + ")";
                break;
            }
            case rt::Level::Historical:
                if (!f.tags.archived.empty()) continue;   // s013a: in ~/Archive already
                if (unread < s.archive_days || unchanged < s.archive_days) continue;
                it.to = To::Archive;
                it.why = "Historical, unread " + days(unread) + ", unchanged " + days(unchanged) + " (archive after " +
                         std::to_string(s.archive_days) + ")";
                break;
            case rt::Level::UntilDone: {
                const auto* p = f.tags.purpose.empty() ? nullptr : book.find(f.tags.purpose);
                if (p && !p->closed) continue;
                it.to = To::Settle;
                it.why = !p ? (f.tags.purpose.empty() ? std::string("Until done, but no purpose")
                                                      : "Until done, purpose \"" + f.tags.purpose + "\" unknown")
                            : "Until done, \"" + p->title + "\" is closed -- Close it again on Purposes";
                break;
            }
            default: continue;   // Undecided (the Closet's), Keep, Hands-off
        }
        out.push_back(std::move(it));
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) {
        return a.to != b.to ? a.to < b.to : a.path < b.path;
    });
    return out;
}

std::string format(const Attention& a) {
    std::ostringstream o;
    o << "# Tidy attention -- written by tidy --scan; read by the tile. Read-only look.\n"
      << "when=" << a.when << "\nms=" << a.ms << "\nremove=" << a.remove << "\narchive=" << a.archive
      << "\nsettle=" << a.settle << "\ncloset=" << a.closet << "\nlists=" << a.lists << "\ntotal=" << a.total()
      << "\nnotified=" << a.notified << "\n";
    return o.str();
}

Attention parse(const std::string& text) {
    Attention a;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        const auto eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
        const auto k = l.substr(0, eq);
        long long v = 0;
        try { v = std::stoll(l.substr(eq + 1)); } catch (...) { continue; }
        if (v < 0) continue;
        const auto u = static_cast<std::size_t>(v);
        if (k == "when") a.when = v;
        else if (k == "ms") a.ms = v;
        else if (k == "remove") a.remove = u;
        else if (k == "archive") a.archive = u;
        else if (k == "settle") a.settle = u;
        else if (k == "closet") a.closet = u;
        else if (k == "lists") a.lists = u;
        else if (k == "notified") a.notified = u;
    }
    return a;
}

std::string line(const Attention& a) {
    if (!a.total()) return "nothing needs attention";
    std::vector<std::string> parts;
    if (a.remove) parts.push_back(std::to_string(a.remove) + " to remove");
    if (a.archive) parts.push_back(std::to_string(a.archive) + " to archive");
    if (a.settle) parts.push_back(std::to_string(a.settle) + " to settle");
    if (a.closet) parts.push_back(std::to_string(a.closet) + " in the Closet");
    if (a.lists) parts.push_back(std::to_string(a.lists) + " id list" + (a.lists == 1 ? "" : "s"));
    std::string s = std::to_string(a.total()) + " need" + (a.total() == 1 ? "s" : "") + " attention: ";
    for (std::size_t i = 0; i < parts.size(); ++i) s += (i ? ", " : "") + parts[i];
    return s;
}

Attention look(const std::string& home, const std::string& data_dir, const std::string& user_dirs,
               std::int64_t now, std::vector<Item>* items) {
    const auto t0 = std::chrono::steady_clock::now();
    Attention a;
    a.when = now;
    const auto st = settings::parse(own::read(data_dir, settings::kFile));
    protection::apply(home, protection::config_home(home), st);   // s019a: the hands-off folders, before any walk
    const auto book = purposes::parse(own::read(data_dir, purposes::kFile));
    const auto files = scan::walk(home, filelists::roots(home));
    auto judged = files;   // s019: and the files at home's own top (the sweep shows them; the id lists don't cover them)
    for (auto& f : scan::top(home, home)) judged.push_back(std::move(f));
    auto list = due(judged, book, st, now);
    {   // s014: archived files proved on media are "to remove" (the bytes are read again before they go)
        catalog::Catalog cat;
        if (cat.open(data_dir, false)) {
            const auto b = removal::book_of(cat);
            for (const auto& a : removal::offerable(b, st))
                if (std::none_of(list.begin(), list.end(), [&](const Item& it) { return it.path == a.path; }))
                    list.push_back({a.path, rt::Level::Historical, To::Remove, removal::offer_why(b, a.id)});
            std::sort(list.begin(), list.end(), [](const Item& x, const Item& y) {
                return x.to != y.to ? x.to < y.to : x.path < y.path;
            });
        }
    }
    for (const auto& it : list)
        (it.to == To::Remove ? a.remove : it.to == To::Archive ? a.archive : a.settle)++;
    const auto stale = filelists::stale(files);
    a.lists = stale.lists + stale.refound;
    a.closet = closet::view(birth::report(home, birth::sweep(home, user_dirs, st.catch_all), now, st.old_years), now, st).proposed.size();   // s019: the whole home
    if (items) *items = std::move(list);
    a.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    return a;
}

namespace {
std::string read_small(const std::string& path) {   // POSIX, read-only (no <fstream> in Tidy)
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return "";
    std::string out;
    char buf[8192];
    for (ssize_t n; (n = ::read(fd, buf, sizeof buf)) > 0 && out.size() < 65536;) out.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    return out;
}
}  // namespace

Where where(const char* tidy_home, const char* home, const char* xdg_data, const char* xdg_config) {
    namespace fs = std::filesystem;
    Where w;
    std::error_code ec;
    if (tidy_home && tidy_home[0] == '/' && fs::is_directory(tidy_home, ec)) {
        w.home = fs::weakly_canonical(tidy_home, ec).string();
        w.fixture = true;
        w.data_dir = paths::data_dir(w.home, nullptr);
    } else {
        w.home = home ? home : "";
        w.data_dir = paths::data_dir(w.home, xdg_data);
    }
    std::string cfg = w.home + "/.config";
    if (!w.fixture && xdg_config && xdg_config[0] == '/') cfg = xdg_config;
    w.user_dirs = read_small(cfg + "/user-dirs.dirs");
    return w;
}

Notice notice(const Attention& a, std::size_t last, int notify_at) {
    Notice n;
    const auto at = static_cast<std::size_t>(std::max(notify_at, 1));
    const auto t = a.total();
    if (t < at) return n;                     // below N: nothing, and the memory resets
    n.notified = last;
    if (t <= last) return n;                  // told already, and no more than then
    n.send = true;
    n.notified = t;
    n.summary = "Tidy: " + std::to_string(t) + " need" + (t == 1 ? "s" : "") + " attention";
    const auto l = line(a);
    const auto colon = l.find(": ");
    n.body = colon == std::string::npos ? l : l.substr(colon + 2);
    return n;
}

int scan_main(std::int64_t now, bool notify, Notice* out) {
    const auto w = where(std::getenv("TIDY_HOME"), std::getenv("HOME"), std::getenv("XDG_DATA_HOME"),
                         std::getenv("XDG_CONFIG_HOME"));
    if (w.home.empty() || w.home[0] != '/') { std::cerr << "tidy --scan: no home\n"; return 2; }
    auto a = look(w.home, w.data_dir, w.user_dirs, now);
    const auto prev = parse(own::read(w.data_dir, kFile));
    a.notified = prev.notified;
    if (notify) {
        const auto st = settings::parse(own::read(w.data_dir, settings::kFile));
        auto n = notice(a, prev.notified, st.notify_at);
        a.notified = n.notified;
        if (out) *out = std::move(n);
    }
    const auto err = own::write(w.data_dir, kFile, format(a));
    std::cout << line(a) << " (" << a.ms << " ms" << (w.fixture ? ", FIXTURE " + w.home : "") << ")\n";
    if (!err.empty()) { std::cerr << "tidy --scan: not saved: " << err << "\n"; return 1; }
    return 0;
}

}  // namespace tidy::core::due
