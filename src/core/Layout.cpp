// core::layout -- see Layout.hpp.
#include "core/Layout.hpp"
#include "core/Conform.hpp"
#include "core/Homing.hpp"

#include <latch/Package.hpp>

#include <algorithm>
#include <filesystem>
#include <set>

namespace tidy::core::layout {

namespace fs = std::filesystem;
namespace tp = templates;
namespace dr = latch::door;

namespace {
std::string rel_to(const std::string& base, const std::string& p) {
    return p.size() > base.size() + 1 && p.compare(0, base.size() + 1, base + "/") == 0 ? p.substr(base.size() + 1) : "";
}

// Walk `d`: does anything stay in it after the moves? `touched` = something
// moved out from under it. Emptied sub-folders (touched, nothing left, not a
// template folder) go to `out`.
bool remains(const fs::path& d, const std::set<std::string>& moved, const std::set<std::string>& keep,
             bool& touched, std::vector<std::string>& out) {
    bool rem = false;
    std::error_code ec;
    for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string p = it->path().string();
        if (name.empty() || name[0] == '.') { rem = true; continue; }   // a hidden entry: the folder is not empty
        if (moved.count(p)) { touched = true; continue; }
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (!e2 && fs::is_directory(st) && !latch::pkg::is_package_name(name)) {
            bool t = false;
            const bool r = remains(it->path(), moved, keep, t, out);
            if (t) touched = true;
            if (r || keep.count(p)) rem = true;
            else if (t) out.push_back(p);      // emptied by the moves
            else rem = true;                   // empty before: not ours to trash
            continue;
        }
        rem = true;
    }
    return rem;
}
}  // namespace

std::size_t Look::proposed() const {
    return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](const Row& r) { return !r.asked; }));
}
std::size_t Look::asked() const {
    return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](const Row& r) { return r.asked; }));
}

Look look(const tp::Template& t, const std::string& dir) {
    Look l;
    l.dir = dir;
    const auto c = conform::look(t, dir);
    l.in_place = c.in_place;
    l.whole = c.whole;
    for (const auto& m : c.moves) l.rows.push_back({m.from, rel_to(dir, m.from), m.folder, m.why, false});
    for (const auto& u : c.unmatched) {
        // In a stage folder already, and no route says otherwise: you put it there -- in place.
        const bool placed = std::any_of(t.folders.begin(), t.folders.end(),
                                        [&](const tp::Folder& f) { return u.rfind(f.path + "/", 0) == 0; });
        if (placed) { ++l.in_place; continue; }
        l.rows.push_back({dir + "/" + u, u, kLeave, "", true});
    }
    std::error_code ec;
    for (const auto& f : t.folders)
        if (!fs::is_directory(fs::symlink_status(dir + "/" + f.path, ec))) l.missing.push_back(f.path);
    return l;
}

Planned plan(const std::string& home, const tp::Template& t, const Look& l) {
    Planned out;
    const std::string book = rel_to(home, l.dir);
    std::set<std::string> made;
    for (const auto& m : l.missing) {
        dr::Action a;
        a.verb = dr::Verb::MakeDir;
        a.to = l.dir + "/" + m;
        a.reason = "the layout names it";
        out.plan.actions.push_back(a);
        made.insert(a.to);
        ++out.made;
    }
    std::vector<homing::Pick> picks;
    std::set<std::string> moved;
    for (const auto& r : l.rows) {
        if (r.folder == kLeave) continue;
        picks.push_back({r.from, book + (r.folder.empty() ? "" : "/" + r.folder),
                         "layout: " + (r.asked ? std::string("your pick") : r.why)});
        moved.insert(r.from);
    }
    auto hp = homing::plan(home, picks);
    for (auto& a : hp.plan.actions) {
        if (a.verb == dr::Verb::MakeDir && made.count(a.to)) continue;   // made above, in tree order
        if (a.verb == dr::Verb::Move) ++out.moves;
        out.plan.actions.push_back(std::move(a));
    }
    out.renamed = std::move(hp.renamed);

    std::set<std::string> keep;
    for (const auto& f : t.folders) keep.insert(l.dir + "/" + f.path);
    std::vector<std::string> emptied;
    bool touched = false;
    remains(l.dir, moved, keep, touched, emptied);
    std::sort(emptied.begin(), emptied.end());
    for (const auto& e : emptied) {   // topmost only: a folder's Trash takes its emptied children with it
        if (std::any_of(out.emptied.begin(), out.emptied.end(),
                        [&](const std::string& top) { return e.rfind(l.dir + "/" + top + "/", 0) == 0; }))
            continue;
        out.emptied.push_back(rel_to(l.dir, e));
        dr::Action a;
        a.verb = dr::Verb::Trash;
        a.from = e;
        a.reason = "left empty by the moves";
        out.plan.actions.push_back(a);
    }
    const std::string title = fs::path(l.dir).filename().string();
    out.plan.label = "Layout: " + title + " -- " + std::to_string(out.moves) + (out.moves == 1 ? " move" : " moves") +
                     ", " + std::to_string(out.made) + " new folders" +
                     (out.emptied.empty() ? "" : ", " + std::to_string(out.emptied.size()) + " emptied to the Trash");
    return out;
}

}  // namespace tidy::core::layout
