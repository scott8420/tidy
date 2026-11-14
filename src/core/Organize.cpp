// core::organize -- see Organize.hpp.
#include "core/Organize.hpp"
#include "core/Quarantine.hpp"
#include "core/Archive.hpp"
#include "core/Ebooks.hpp"
#include "core/Layout.hpp"
#include "core/Projects.hpp"
#include "core/Protection.hpp"

#include <latch/Package.hpp>

#include <algorithm>
#include <filesystem>
#include <set>

namespace tidy::core::organize {

namespace fs = std::filesystem;
namespace tp = templates;
namespace dr = latch::door;

namespace {
std::string rel_to(const std::string& base, const std::string& p) {
    return p.size() > base.size() + 1 && p.compare(0, base.size() + 1, base + "/") == 0 ? p.substr(base.size() + 1) : "";
}

// The destination folders the routes give the files under `dir`, `depth` levels down.
void dests(const tp::Template& t, const fs::path& dir, const std::string& prefix, int depth, std::set<std::string>& out) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.empty() || n[0] == '.') continue;
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || fs::is_symlink(st)) continue;
        const std::string rel = prefix.empty() ? n : prefix + "/" + n;
        if (fs::is_directory(st) && !quarantine::one(it->path().string())) {
            if (depth > 1) dests(t, it->path(), rel, depth - 1, out);
            continue;
        }
        const auto to = tp::route_for_path(t, rel);
        if (!to.empty() && to != tp::kStays) out.insert(to);
    }
}
}  // namespace

namespace {
bool files_like(const tp::Template& t, const std::string& dir) {
    std::set<std::string> d;
    dests(t, dir, "", 2, d);   // its top and one down (Archive/A0001.png)
    return d.size() >= 2;
}
// A container: a folder of project-like folders (an author's, a series').
bool holds_projects(const tp::Template& t, const std::string& dir) {
    for (const auto& s : layout::candidates(dir, true))
        if (files_like(t, s)) return true;
    return false;
}
}  // namespace

bool project_like(const tp::Template& t, const std::string& dir) {
    return files_like(t, dir) && !holds_projects(t, dir);
}

// s030c: an author from a folder's name -- "Keene, Carolyn" as it is, "Carolyn
// Keene" flipped; never a one-word folder ("Fiction"), never one with digits.
std::string author_from(const std::string& folder_name) {
    if (folder_name.find(", ") != std::string::npos) return folder_name;
    std::size_t words = 1;
    for (char c : folder_name) {
        if (std::isdigit(static_cast<unsigned char>(c))) return "";
        words += c == ' ';
    }
    if (words < 2 || words > 4 || !std::isupper(static_cast<unsigned char>(folder_name[0]))) return "";
    return ebooks::last_first(folder_name);
}

bool named_by_parent(const tp::Template& t, const std::string& name) {
    if (!projects::is_project_name(name)) return false;
    std::string a = name.substr(0, name.size() - std::string(projects::kSuffix).size()), b = t.name;
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a.empty() || a == b || b.rfind(a, 0) == 0;   // "Bookbinding-project" for bookbinding
}

Shape guess(const tp::Template& t, const std::string& dir) {
    // s030b: a -project folder, or one whose top holds 2+ of the type's own top
    // folders (Body/ Cover/), is ONE project -- never a folder of authors.
    if (projects::is_project_name(fs::path(dir).filename().string())) return Shape::One;
    {
        std::size_t tops = 0;
        std::error_code ec;
        for (const auto& fo : t.folders)
            if (fo.path.find('/') == std::string::npos && fs::is_directory(fs::path(dir) / fo.path, ec)) ++tops;
        if (tops >= 2) return Shape::One;
    }
    // Its own top holds files the routes place -> one project.
    std::set<std::string> top;
    dests(t, dir, "", 1, top);
    if (!top.empty()) return Shape::One;
    const auto subs = layout::candidates(dir, true);
    std::size_t like = 0, groups = 0;
    for (const auto& s : subs) {
        if (holds_projects(t, s)) { if (tp::place_depth(t) == 2) ++groups; continue; }
        if (project_like(t, s)) ++like;
    }
    if (like >= 2 || (like == 1 && subs.size() == 1)) return Shape::Many;
    if (groups >= 1) return Shape::Groups;
    return project_like(t, dir) ? Shape::One : (like ? Shape::Many : Shape::One);
}

std::vector<Group> groups_of(const tp::Template& t, const std::string& dir, Shape s) {
    std::vector<Group> out;
    const std::string name = fs::path(dir).filename().string();
    const bool has_group = tp::place_depth(t) == 2;
    const auto group_for = [&](const std::string& folder_name, const std::string& at) {
        if (!has_group) return std::string();
        if (folder_name.find(", ") != std::string::npos) return folder_name;
        return layout::guess_author(at);   // its parent, when that reads "Last, First" (a series is no author)
    };
    if (s == Shape::One) {
        Group g;
        g.name = has_group ? author_from(fs::path(dir).parent_path().filename().string()) : "";
        std::string title = layout::guess_title(name);
        // s030b: "Bookbinding-project" inside "12 The Message In The Hollow Oak" -- the
        // title (and the author) come from the folder above.
        if (named_by_parent(t, name)) {
            const fs::path up = fs::path(dir).parent_path();
            title = layout::guess_title(up.filename().string());
            if (has_group) g.name = author_from(up.parent_path().filename().string());
        }
        g.projects.push_back({dir, title, true});
        out.push_back(std::move(g));
        return out;
    }
    if (s == Shape::Many) {
        Group g;
        g.dir = dir;
        const auto subs = layout::candidates(dir, true);
        g.name = group_for(name, subs.empty() ? dir + "/x" : subs.front());
        for (const auto& p : subs)
            g.projects.push_back({p, layout::guess_title(fs::path(p).filename().string()), project_like(t, p)});
        out.push_back(std::move(g));
        return out;
    }
    for (const auto& gd : layout::candidates(dir, true)) {   // Groups: each sub-folder an author
        Group g;
        g.dir = gd;
        g.name = has_group ? layout::author_name(fs::path(gd).filename().string()) : "";
        for (const auto& p : layout::candidates(gd, true))
            g.projects.push_back({p, layout::guess_title(fs::path(p).filename().string()), project_like(t, p)});
        if (!g.projects.empty()) out.push_back(std::move(g));
    }
    return out;
}

std::string target_of(const std::string& home, const tp::Template& t, const std::string& group, const std::string& title) {
    const int pd = tp::place_depth(t);
    const std::string cat = home + "/" + t.category;
    if (pd == 2) return cat + "/" + group + "/" + title;
    if (pd == 1) return cat + "/" + title;
    return cat + "/" + projects::folder_for(title);
}

std::string refusal(const std::string& home, const tp::Template& t, const std::string& group, const Project& p,
                    bool* in_place) {
    if (in_place) *in_place = false;
    const int pd = tp::place_depth(t);
    if (t.category.empty()) return "the type " + t.name + " has no place (category)";
    if (pd == 2 && group.empty()) return "no " + (t.group.empty() ? std::string("group") : t.group);
    if (pd == 2) if (auto w = tp::part_refusal(group); !w.empty()) return (t.group.empty() ? "group" : t.group) + ": " + w;
    if (p.title.empty()) return "no name";
    if (auto w = tp::part_refusal(p.title); !w.empty()) return "name: " + w;
    const std::string rel = rel_to(home, p.dir);
    if (rel.empty()) return "not a folder in your home";
    for (const auto& part : fs::path(rel))
        if (!part.empty() && part.string()[0] == '.') return "a hidden folder";
    if (rel == archive::kFolder || rel.rfind(std::string(archive::kFolder) + "/", 0) == 0) return "~/Archive is Tidy's archive";
    if (protection::under(rel)) return "Hands off (Settings), Samson's, or never-touch";
    std::error_code ec;
    const auto st = fs::symlink_status(p.dir, ec);
    if (ec || !fs::is_directory(st)) return "not a folder";
    if (latch::pkg::is_package_name(fs::path(p.dir).filename().string())) return "a bundle -- it belongs inside a project";
    if (fs::exists(fs::symlink_status(p.dir + "/.git", ec))) return "a git repo -- the Code projects window";
    const std::string to = target_of(home, t, group, p.title);
    if (to == p.dir) { if (in_place) *in_place = true; return ""; }
    if (std::string at; projects::inside_whole(rel, &at) && at != rel)
        return "inside " + fs::path(at).filename().string() + " -- a project kept whole";
    if ((t.category + "/").rfind(rel + "/", 0) == 0) return "~/" + t.category + " itself, or above it";
    if (fs::exists(fs::symlink_status(to, ec))) return "~/" + rel_to(home, to) + " is taken";
    return "";
}

Organized plan(const std::string& home, const tp::Template& t, const std::vector<Group>& groups, bool lay_out) {
    Organized out;
    std::set<std::string> made, taken;
    std::vector<dr::Action> dirs, laid, moves, last;
    for (const auto& g : groups) {
        std::set<std::string> gone;
        std::size_t moved_here = 0;
        for (const auto& p : g.projects) {
            if (!p.on) continue;
            const std::string name = fs::path(p.dir).filename().string();
            bool here = false;
            if (auto why = refusal(home, t, g.name, p, &here); !why.empty()) { out.refused.push_back(name + " -- " + why); continue; }
            const std::string to = target_of(home, t, g.name, p.title);
            if (!taken.insert(to).second) { out.refused.push_back(name + " -- \"" + p.title + "\" twice"); continue; }
            ++out.projects;
            if (lay_out) {   // laid out where it is, THEN moved whole (the door judges each step on the disk as it is)
                auto lp = layout::plan(home, t, layout::look(t, p.dir));
                for (auto& a : lp.plan.actions) laid.push_back(std::move(a));
                out.laid_out += lp.moves;
            }
            if (here) { ++out.in_place; continue; }
            std::string so_far = home;   // the place's missing levels, once each, parents first
            for (const auto& part : fs::path(rel_to(home, fs::path(to).parent_path().string()))) {
                so_far += "/" + part.string();
                std::error_code ec;
                if (made.count(so_far) || fs::is_directory(fs::symlink_status(so_far, ec))) continue;
                made.insert(so_far);
                dr::Action a;
                a.verb = dr::Verb::MakeDir;
                a.to = so_far;
                a.reason = "where " + t.name + " lives";
                dirs.push_back(a);
            }
            dr::Action m;
            m.verb = dr::Verb::Move;
            m.from = p.dir;
            m.to = to;
            m.reason = "organized as " + t.name + " -- moved whole" +
                       (p.title != name ? " (\"" + name + "\" -> \"" + fs::path(to).filename().string() + "\")" : std::string());
            moves.push_back(m);
            gone.insert(p.dir);
            ++moved_here;
            ++out.moved;
        }
        if (g.dir.empty() || moved_here == 0) continue;
        bool loose = false, hidden = false;   // what the container keeps
        std::error_code ec;
        for (fs::directory_iterator it(g.dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string n = it->path().filename().string();
            if (n.empty() || n[0] == '.') { hidden = true; continue; }
            if (!gone.count(it->path().string())) loose = true;
        }
        const std::string rel = rel_to(home, g.dir);
        if (loose) out.closet.push_back(rel);
        else if (!hidden) {
            dr::Action tr;
            tr.verb = dr::Verb::Trash;
            tr.from = g.dir;
            tr.reason = "left empty once its projects moved";
            last.push_back(tr);
            out.trashed.push_back(rel);
        }
    }
    for (auto* v : {&dirs, &laid, &moves, &last})
        for (auto& x : *v) out.plan.actions.push_back(std::move(x));
    out.plan.label = "Organize as " + t.name + ": " + std::to_string(out.projects) + (out.projects == 1 ? " project" : " projects") +
                     (out.moved ? ", " + std::to_string(out.moved) + " moved" : std::string()) +
                     (out.laid_out ? ", " + std::to_string(out.laid_out) + " files laid out" : std::string());
    return out;
}

}  // namespace tidy::core::organize
