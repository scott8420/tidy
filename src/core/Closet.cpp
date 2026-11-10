#include "core/Closet.hpp"
#include "core/Tagging.hpp"

#include <map>

namespace tidy::core::closet {

namespace rt = retention;

int days_since(std::int64_t t, std::int64_t now) {
    if (t <= 0 || t >= now) return 0;
    return static_cast<int>((now - t) / 86400);
}

bool aging(const facts::Facts& f, std::int64_t now, int days) {
    const std::int64_t born = f.has_btime ? f.btime : f.mtime;
    return days_since(born, now) >= days && days_since(f.atime, now) >= days;
}

View view(const birth::Report& r, std::int64_t now, const settings::Settings& s) {
    View v;
    for (const auto& b : r.items) {
        if (b.rule == "stamped") {   // a decided level on disk: never visits
            ++v.decided;
            if (b.level == rt::Level::Keep && b.file.tags.role == kJunkDrawer) ++v.junk;
            continue;
        }
        const std::string name = b.file.path.substr(b.file.path.find_last_of('/') + 1);
        if (const auto pat = settings::junk_match(s.junk, name); !pat.empty()) {   // the user's list wins
            birth::Born j = b;
            j.level = rt::Level::Keep;
            j.rule = "junk-list";
            j.why = "on your junk list (" + pat + ")";
            v.proposed.push_back(std::move(j));
            continue;
        }
        if (b.level != rt::Level::Undecided) { v.proposed.push_back(b); continue; }
        if (!b.file.dir && aging(b.file, now, s.aging_days)) {
            birth::Born a = b;
            a.level = rt::Level::Ephemeral;
            a.rule = "aged-out";
            a.why = "unread " + std::to_string(days_since(b.file.atime, now)) + " days -- aging out (" + b.why + ")";
            v.proposed.push_back(std::move(a));
            continue;
        }
        v.waiting.push_back(b);
    }
    return v;
}

namespace {
std::string plural(std::size_t n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); }

void append(door::Plan& into, const door::Plan& from) {
    into.actions.insert(into.actions.end(), from.actions.begin(), from.actions.end());
}
}  // namespace

door::Plan accept(const std::vector<birth::Born>& items) {
    door::Plan p;
    p.label = "Closet: accept " + plural(items.size(), "proposal");
    std::map<rt::Level, std::vector<facts::Facts>> by;
    std::vector<rt::Level> order;
    std::vector<facts::Facts> junk;
    for (const auto& b : items) {
        if (b.rule == "stamped") continue;   // decided already: never re-proposed
        if (b.rule == "junk-list") { junk.push_back(b.file); continue; }
        if (!by.count(b.level)) order.push_back(b.level);
        by[b.level].push_back(b.file);
    }
    for (const auto l : order)
        append(p, tagging::stamp(by[l], {"", rt::id_of(l), "", ""}, "Closet: " + rt::title_of(l)));
    if (!junk.empty())
        append(p, tagging::stamp(junk, {"", rt::id_of(rt::Level::Keep), "", kJunkDrawer}, "Closet: the Junk Drawer (your list)"));
    return p;
}

door::Plan decide(const std::vector<birth::Born>& items, rt::Level level) {
    std::vector<facts::Facts> fs;
    for (const auto& b : items) fs.push_back(b.file);
    auto p = tagging::stamp(fs, {"", rt::id_of(level), "", ""}, "Closet: decided " + rt::title_of(level));
    p.label = "Closet: " + plural(fs.size(), "file") + " -> " + rt::title_of(level);
    return p;
}

door::Plan junk_drawer(const std::vector<birth::Born>& items) {
    std::vector<facts::Facts> fs;
    for (const auto& b : items) fs.push_back(b.file);
    auto p = tagging::stamp(fs, {"", rt::id_of(rt::Level::Keep), "", kJunkDrawer}, "Closet: the Junk Drawer");
    p.label = "Closet: " + plural(fs.size(), "file") + " -> the Junk Drawer";
    return p;
}

}  // namespace tidy::core::closet
