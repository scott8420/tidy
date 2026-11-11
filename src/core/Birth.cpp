#include "core/Birth.hpp"
#include "core/Archive.hpp"
#include "core/Projects.hpp"
#include "core/Scan.hpp"

#include <latch/Package.hpp>
#include <latch/Protect.hpp>

#include <latch/Door.hpp>   // tag_of: reads one xattr, never following a symlink

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace tidy::core::birth {

namespace rt = retention;

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool ends_with(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}
std::string base_name(const std::string& p) {
    const auto k = p.find_last_of('/');
    return k == std::string::npos ? p : p.substr(k + 1);
}

// One XDG_*_DIR value -> an absolute path ("" if unusable). "$HOME/x" and
// "/abs/x" are the two forms xdg-user-dirs writes.
std::string resolve(const std::string& home, std::string v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    if (v.rfind("$HOME", 0) == 0) v = home + v.substr(5);
    while (v.size() > 1 && v.back() == '/') v.pop_back();
    if (v.empty() || v[0] != '/') return "";
    if (v.find("/../") != std::string::npos || ends_with(v, "/..") || v.find("/./") != std::string::npos) return "";
    return v;
}

const std::set<std::string>& image_exts() {
    static const std::set<std::string> s = {"png", "jpg", "jpeg", "gif", "webp", "bmp", "svg", "heic", "avif", "tif", "tiff"};
    return s;
}
const std::set<std::string>& video_exts() {
    static const std::set<std::string> s = {"webm", "mp4", "mkv", "mov"};
    return s;
}
const std::set<std::string>& installer_exts() {
    static const std::set<std::string> s = {"rpm", "deb", "flatpakref", "flatpak", "appimage", "iso", "dmg", "exe", "msi", "pkg"};
    return s;
}
}  // namespace

std::vector<Place> places(const std::string& home, const std::string& user_dirs) {
    std::map<std::string, std::string> dirs = {
        {"XDG_DOWNLOAD_DIR", home + "/Downloads"},
        {"XDG_DESKTOP_DIR", home + "/Desktop"},
        {"XDG_DOCUMENTS_DIR", home + "/Documents"},
        {"XDG_PICTURES_DIR", home + "/Pictures"},
    };
    std::istringstream in(user_dirs);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        if (dirs.count(key)) dirs[key] = resolve(home, line.substr(eq + 1));
    }
    const std::string pics = dirs["XDG_PICTURES_DIR"];
    const std::vector<Place> want = {
        {"downloads",   "Downloads",   dirs["XDG_DOWNLOAD_DIR"], false},
        {"screenshots", "Screenshots", pics.empty() ? "" : pics + "/Screenshots", false},
        {"desktop",     "Desktop",     dirs["XDG_DESKTOP_DIR"], false},
        {"documents",   "Documents",   dirs["XDG_DOCUMENTS_DIR"], true},
    };
    std::vector<Place> out;
    std::set<std::string> seen;
    for (const auto& p : want) {
        if (p.path.size() <= home.size() + 1 || p.path.rfind(home + "/", 0) != 0) continue;   // home itself / outside
        if (!seen.insert(p.path).second) continue;   // two names, one folder: the first keeps it
        out.push_back(p);
    }
    return out;
}

std::vector<Place> sweep(const std::string& home, const std::string& user_dirs) {
    namespace fs = std::filesystem;
    auto out = places(home, user_dirs);
    std::set<std::string> taken;
    for (const auto& p : out) taken.insert(p.path);
    out.push_back({"home", "Home", home, true});
    std::vector<std::string> tops;
    std::error_code ec;
    for (fs::directory_iterator it(home, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.empty() || n[0] == '.' || n == archive::kFolder || projects::is_project_name(n) ||
            latch::pkg::is_package_name(n) || latch::protect::here(n))
            continue;
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || !fs::is_directory(st)) continue;   // a symlink is not a directory here
        if (fs::exists(fs::symlink_status(it->path() / ".git", e2))) continue;   // a repo is whole
        if (taken.count(it->path().string())) continue;   // a landing place already
        tops.push_back(n);
    }
    std::sort(tops.begin(), tops.end());
    for (const auto& n : tops) out.push_back({"top", n, home + "/" + n, true});
    return out;
}

const std::vector<Rule>& rules() {
    static const std::vector<Rule> r = {
        {"stamped", "Already decided", rt::Level::Keep,
         "It carries a retention already (one of our apps, or you) -- birth rules never overrule it."},
        {"screenshot", "A screenshot", rt::Level::Ephemeral,
         "A screenshot is done the same afternoon -- Ephemeral."},
        {"gov-download", "A download from a .gov site", rt::Level::UntilDone,
         "A government form is an input: it lives while its purpose is open, then a hold."},
        {"image-download", "A downloaded image", rt::Level::Ephemeral,
         "A random image off the web is incidental -- Ephemeral."},
        {"installer-download", "A downloaded installer", rt::Level::Ephemeral,
         "An installer or disk image is spent once installed -- Ephemeral. (lean)"},
        {"in-downloads", "Anything in Downloads", rt::Level::Ephemeral,
         "Downloads is another junk drawer: everything there arrives Ephemeral. Read it and Sort moves it on "
         "(~/Entertainment for movies, series, books, music); unread 30 days, Due proposes the Trash."},
        {"old", "Untouched for years", rt::Level::Historical,
         "Nobody has changed or read it in years (Settings) -- a record: Historical, and Due archives it."},
        {"download", "A download no rule knows", rt::Level::Undecided,
         "Downloaded (its origin is recorded) but no longer in Downloads -- Undecided, to the Closet."},
        {"unknown", "Born unannounced", rt::Level::Undecided,
         "Nothing about its birth says its fate -- Undecided, to the Closet."},
    };
    return r;
}

const Rule* rule(const std::string& id) {
    for (const auto& r : rules())
        if (r.id == id) return &r;
    return nullptr;
}

std::string host_of(const std::string& url) {
    const auto s = url.find("://");
    if (s == std::string::npos) return "";
    std::string h = url.substr(s + 3);
    h = h.substr(0, h.find_first_of("/?#"));
    if (const auto at = h.rfind('@'); at != std::string::npos) h = h.substr(at + 1);
    if (!h.empty() && h[0] == '[') return lower(h.substr(0, h.find(']') + 1));   // IPv6 literal
    h = h.substr(0, h.find(':'));
    return lower(h);
}

bool is_screenshot_name(const std::string& name) {
    const std::string n = lower(name);
    for (const char* p : {"screenshot", "screen shot", "screencast"})
        if (n.rfind(p, 0) == 0) return true;
    return false;
}

std::string ext_of(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) return "";
    return lower(name.substr(dot + 1));
}

std::int64_t untouched_since(const facts::Facts& f) { return std::max(f.mtime, f.atime); }

Born classify(const facts::Facts& f, const std::string& place_id, const std::string& origin_url,
              std::int64_t now, int old_years) {
    Born b;
    b.file = f;
    b.place = place_id;
    b.origin = origin_url;
    const std::string name = base_name(f.path);
    const std::string ext = f.dir ? std::string() : ext_of(name);
    const std::string host = host_of(origin_url);
    const auto fit = [&](const char* id, rt::Level l, std::string why) {
        b.rule = id;
        b.level = l;
        b.why = std::move(why);
        return b;
    };

    // 1. A decided retention on the file wins. A stamped "undecided" is not a
    //    decision -- it was waiting -- so the rules below may still propose.
    rt::Level carried{};
    const bool known = rt::parse(f.tags.retention, carried);
    if (known && carried != rt::Level::Undecided)
        return fit("stamped", carried, "carries " + rt::title_of(carried) + " already");
    const std::string was = f.tags.retention.empty() ? ""
                          : known ? " (stamped Undecided)" : " (carries an unknown retention \"" + f.tags.retention + "\")";

    // 2. Screenshots: by where GNOME puts them, or by name.
    const bool media = image_exts().count(ext) || video_exts().count(ext);
    if (!f.dir && media && (place_id == "screenshots" || is_screenshot_name(name)))
        return fit("screenshot", rt::Level::Ephemeral,
                   (place_id == "screenshots" ? "in Screenshots" : "named like a screenshot") + was);

    // 3. Downloads: the origin hints.
    const bool downloaded = place_id == "downloads" || !origin_url.empty();
    if (downloaded && !f.dir) {
        if (!host.empty() && (ends_with(host, ".gov") || host == "gov"))
            return fit("gov-download", rt::Level::UntilDone, "from " + host + was);
        if (image_exts().count(ext))
            return fit("image-download", rt::Level::Ephemeral,
                       (host.empty() ? std::string("an image in Downloads") : "an image from " + host) + was);
        if (installer_exts().count(ext))
            return fit("installer-download", rt::Level::Ephemeral,
                       "." + ext + (host.empty() ? std::string() : " from " + host) + was);
    }
    // 3b. s020a (Scott): everything in Downloads arrives Ephemeral -- "another
    // junk drawer by another name". Never "old": an unread download goes to the Trash, not the Archive.
    if (place_id == "downloads")
        return fit("in-downloads", rt::Level::Ephemeral,
                   (host.empty() ? std::string("in Downloads") : "from " + host) + was);

    // 4. s019: untouched for years -> a record (Historical; Due archives it).
    if (!f.dir && now > 0 && old_years > 0) {
        const std::int64_t t = untouched_since(f);
        if (t > 0 && now - t >= static_cast<std::int64_t>(old_years) * 365 * 86400) {
            const std::time_t tt = static_cast<std::time_t>(t);
            std::tm tm{};
            localtime_r(&tt, &tm);
            return fit("old", rt::Level::Historical, "untouched since " + std::to_string(tm.tm_year + 1900) + was);
        }
    }

    if (downloaded)
        return fit("download", rt::Level::Undecided,
                   (host.empty() ? std::string("no origin recorded") : "from " + host) + was);

    // 5. Anything else.
    return fit("unknown", rt::Level::Undecided, (f.dir ? std::string("a package") : std::string("no rule fits")) + was);
}

std::size_t Report::count(rt::Level l) const {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [&](const Born& b) { return b.level == l; }));
}
std::size_t Report::count_rule(const std::string& id) const {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [&](const Born& b) { return b.rule == id; }));
}

Report report(const std::string& home, const std::vector<Place>& ps, std::int64_t now, int old_years) {
    Report r;
    std::set<std::string> seen;   // a file under two places (nested folders) is reported once
    for (const auto& p : ps) {
        auto files = p.top_only ? scan::top(home, p.path) : scan::walk_loose(home, {p.path});
        if (files.empty()) {
            // walk() returns nothing for a missing folder AND an empty one: keep
            // the place only if it is a folder, so the report can say "empty".
            facts::Facts here;
            if (!facts::of(p.path, here) || !here.dir) continue;
        }
        r.places.push_back(p);
        for (auto& f : files) {
            if (!seen.insert(f.path).second) continue;
            std::string origin;
            latch::door::tag_of(f.path, kOriginUrl, origin);
            r.items.push_back(classify(f, p.id, origin, now, old_years));
        }
    }
    return r;
}

std::string text(const Report& r, const std::string& home) {
    const auto rel = [&](const std::string& p) {
        return p == home ? std::string("~") : p.rfind(home + "/", 0) == 0 ? "~/" + p.substr(home.size() + 1) : p;
    };
    std::ostringstream o;
    o << "Birth report -- REPORT ONLY, nothing tagged\n";
    o << r.items.size() << " items in " << r.places.size() << " places:";
    for (const auto& p : r.places) o << " " << rel(p.path) << (p.top_only ? " (top)" : "");
    o << "\n";
    for (const auto& lv : rt::all()) {
        const auto n = r.count(lv.level);
        if (!n) continue;
        o << "\n" << lv.title << " (" << n << ")\n";
        for (const auto& b : r.items)
            if (b.level == lv.level) o << "  " << rel(b.file.path) << "   -- " << b.rule << ": " << b.why << "\n";
    }
    return o.str();
}

}  // namespace tidy::core::birth
