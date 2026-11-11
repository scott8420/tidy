#include "core/Sort.hpp"
#include "core/Archive.hpp"
#include "core/Closet.hpp"
#include "core/Facts.hpp"
#include "core/Tagging.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>

namespace tidy::core::sort {

namespace fs = std::filesystem;
namespace rt = retention;
namespace dr = latch::door;

namespace {
std::string month_of(std::int64_t now) {
    const std::time_t t = static_cast<std::time_t>(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m", &tm);
    return b;
}
std::string source_of(const birth::Born& b, const std::string& home) {
    if (b.place == "home") return "Home";
    if (b.place == "top" || b.place == "documents") {   // the folder it sits loose in
        return fs::path(b.file.path).parent_path().filename().string();
    }
    if (b.place == "screenshots") return "Screenshots";
    if (b.place == "downloads") return "Downloads";
    if (b.place == "desktop") return "Desktop";
    (void)home;
    return "Loose";
}
}  // namespace

std::string dated(const birth::Born& b, const std::string& type_folder, std::int64_t now) {
    const std::string from = fs::path(b.file.path).parent_path().string();
    std::string src = source_of(b, "");
    // Already loose at its own type folder's top (~/Pictures/x.png -> Pictures): "Loose".
    if (!type_folder.empty() && (from.size() >= type_folder.size() + 1) &&
        from.compare(from.size() - type_folder.size() - 1, std::string::npos, "/" + type_folder) == 0)
        src = "Loose";
    return src + " " + month_of(now);
}

std::vector<Row> rows(const std::string& home, const birth::Report& rep, const settings::Settings& st,
                      std::int64_t now) {
    const auto table = homing::table(st);
    const auto v = closet::view(rep, now, st);
    std::vector<Row> out;
    const auto place = [&](Row& r) {
        const std::string name = fs::path(r.born.file.path).filename().string();
        if (r.junk) { r.go = Go::Stays; r.why = "your junk list: the Junk Drawer -- kept, no home"; return; }
        switch (r.level) {
        case rt::Level::Ephemeral:
            r.go = Go::Stays;
            r.why = "Ephemeral -- stays; Due removes it when its days are up";
            return;
        case rt::Level::Historical: {
            r.go = Go::Archive;
            r.folder = archive::folder_for(home, r.born.file.path, [&] {
                const std::time_t t = static_cast<std::time_t>(now);
                std::tm tm{};
                localtime_r(&t, &tm);
                return tm.tm_year + 1900;
            }());
            r.why = "Historical (" + r.born.why + ") -- archived, fingerprinted";
            return;
        }
        default: break;
        }
        const auto* t = homing::type_of(table, name);
        const std::string tf = t ? t->folder : "Documents";
        r.go = Go::Home;
        std::string folder;
        if (const auto why = homing::by_type(tf, dated(r.born, tf, now), &folder); !why.empty()) {
            r.go = Go::Stays;
            r.why = "no home: " + why;
            return;
        }
        r.folder = folder;
        r.why = (t ? "by type " + t->pattern + " -> " + t->folder : std::string("no type -> Documents")) + ", " +
                rt::title_of(r.level);
    };
    for (const auto& b : v.proposed) {   // undecided, a proposal to take
        Row r;
        r.born = b;
        r.tag = true;
        r.junk = b.rule == "junk-list";
        r.level = r.junk ? rt::Level::Keep : b.level;
        place(r);
        out.push_back(std::move(r));
    }
    for (const auto& b : v.waiting) {    // undecided, nothing proposed: kept, home by type
        Row r;
        r.born = b;
        r.tag = true;
        r.level = rt::Level::Keep;
        place(r);
        out.push_back(std::move(r));
    }
    for (const auto& b : rep.items) {    // decided already, still loose
        if (b.rule != "stamped") continue;
        const auto t = facts::tags_of(b.file.path);
        if (t.role == closet::kJunkDrawer || b.level == rt::Level::HandsOff || b.level == rt::Level::Undecided)
            continue;
        if (!t.archived.empty()) continue;
        Row r;
        r.born = b;
        r.level = b.level;
        place(r);
        if (r.go == Go::Stays) continue;   // decided and staying: nothing to do
        out.push_back(std::move(r));
    }
    return out;
}

Planned plan(const std::string& home, const std::vector<Row>& chosen, std::int64_t now) {
    Planned out;
    // 1. The tags, on the files where they are: grouped by level (+ the Junk Drawer).
    std::map<rt::Level, std::vector<facts::Facts>> by_level;
    std::vector<facts::Facts> junk;
    std::set<std::string> stamped;
    for (const auto& r : chosen) {
        if (!r.tag) continue;
        (r.junk ? junk : by_level[r.level]).push_back(r.born.file);
        stamped.insert(r.born.file.path);
    }
    std::vector<dr::Action> tags;
    for (const auto& [lvl, fs_] : by_level) {
        auto p = tagging::stamp(fs_, {"", rt::id_of(lvl), "", ""}, "Sort: " + rt::title_of(lvl));
        tags.insert(tags.end(), p.actions.begin(), p.actions.end());
    }
    if (!junk.empty()) {
        auto p = tagging::stamp(junk, {"", rt::id_of(rt::Level::Keep), "", closet::kJunkDrawer}, "Sort: the Junk Drawer");
        tags.insert(tags.end(), p.actions.begin(), p.actions.end());
    }
    // 2. Home: new folders + moves.
    std::vector<homing::Pick> picks;
    for (const auto& r : chosen)
        if (r.go == Go::Home) picks.push_back({r.born.file.path, r.folder, r.why});
    auto home_p = homing::plan(home, picks);
    // 3. The archive (its own id tag dropped where step 1 gives one).
    std::vector<std::string> arch;
    for (const auto& r : chosen)
        if (r.go == Go::Archive) arch.push_back(r.born.file.path);
    auto arch_p = archive::plan(home, arch, now);
    out.refused = std::move(arch_p.refused);
    out.archiving = arch.size() - out.refused.size();
    out.renamed = std::move(home_p.renamed);
    out.renamed.insert(out.renamed.end(), arch_p.renamed.begin(), arch_p.renamed.end());
    out.plan.actions = std::move(tags);
    out.plan.actions.insert(out.plan.actions.end(), home_p.plan.actions.begin(), home_p.plan.actions.end());
    for (auto& a : arch_p.plan.actions) {
        if (a.verb == dr::Verb::Tag && a.attr == facts::kId && stamped.count(a.from)) continue;
        out.plan.actions.push_back(std::move(a));
    }
    std::size_t files = 0;
    for (const auto& r : chosen) files += (r.go != Go::Stays || r.tag);
    out.plan.label = "Sort: " + std::to_string(files) + (files == 1 ? " file" : " files");
    return out;
}

}  // namespace tidy::core::sort
