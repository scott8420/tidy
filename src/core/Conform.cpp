#include "core/Conform.hpp"

#include "core/Archive.hpp"
#include "core/Homing.hpp"
#include "core/Projects.hpp"
#include "core/Protection.hpp"

#include <latch/Package.hpp>
#include <latch/Protect.hpp>

#include <algorithm>
#include <filesystem>

namespace tidy::core::conform {

namespace fs = std::filesystem;
namespace tp = templates;

namespace {
bool is_repo(const fs::path& p) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p / ".git", ec));
}
std::string rel_to(const std::string& base, const std::string& p) {
    return p.size() > base.size() + 1 ? p.substr(base.size() + 1) : std::string();
}
bool under(const std::string& rel, const std::string& folder) {
    if (folder.empty()) return rel.find('/') == std::string::npos;   // the top: directly in it
    return rel.size() > folder.size() + 1 && rel.compare(0, folder.size(), folder) == 0 && rel[folder.size()] == '/';
}

void walk(const tp::Template& t, const std::string& root, const fs::path& dir, Project& out) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || fs::is_symlink(st)) continue;
        const std::string path = it->path().string();
        const std::string rel = rel_to(root, path);
        const bool pkg = fs::is_directory(st) && latch::pkg::is_package_name(name);
        if (fs::is_directory(st) && !pkg) {
            if (projects::is_project_name(name) || is_repo(it->path()) || latch::protect::protected_name(name)) {
                ++out.whole;
                continue;
            }
            walk(t, root, it->path(), out);
            continue;
        }
        if (!pkg && !fs::is_regular_file(st)) continue;
        const std::string to = tp::route_for_path(t, rel);
        if (to.empty()) { out.unmatched.push_back(rel); continue; }
        if (to == tp::kStays) { ++out.stays; continue; }
        const std::string folder = to == tp::kTop ? std::string() : to;
        if (under(rel, folder)) { ++out.in_place; continue; }
        std::string why;
        for (const auto& r : t.routes)
            if (r.folder == to) {   // the route that fit: the first with this folder whose pattern fits
                if (tp::route_for_path(tp::Template{"", "", {}, {r}}, rel) == to) { why = r.pattern + " -> " + to; break; }
            }
        out.moves.push_back({path, folder, why});
    }
}
}  // namespace

std::vector<std::string> folders(const std::string& dir, bool each_sub) {
    if (!each_sub) return {dir};
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || !fs::is_directory(st) || latch::pkg::is_package_name(name) || latch::protect::protected_name(name))
            continue;
        if (is_repo(it->path())) continue;
        out.push_back(it->path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string refusal(const std::string& home, const std::string& dir) {
    if (dir == home) return "home itself";
    if (dir.rfind(home + "/", 0) != 0) return "not in your home";
    const std::string rel = rel_to(home, dir);
    for (const auto& part : fs::path(rel))
        if (!part.empty() && part.string()[0] == '.') return "a hidden folder";
    if (rel == archive::kFolder || rel.rfind(std::string(archive::kFolder) + "/", 0) == 0) return "~/Archive is Tidy's archive";
    if (protection::under(rel)) return "Hands off (Settings), Samson's, or never-touch";
    std::error_code ec;
    const auto st = fs::symlink_status(dir, ec);
    if (ec || !fs::is_directory(st)) return "not a folder";
    if (is_repo(dir)) return "a git repo -- the Code projects window";
    return "";
}

Project look(const tp::Template& t, const std::string& dir) {
    Project p;
    p.dir = dir;
    walk(t, dir, dir, p);
    std::sort(p.unmatched.begin(), p.unmatched.end());
    std::sort(p.moves.begin(), p.moves.end(), [](const Move& a, const Move& b) { return a.from < b.from; });   // s023i
    return p;
}

Planned plan(const std::string& home, const std::vector<Project>& projects, bool rename_to_project) {
    Planned out;
    std::vector<homing::Pick> picks;
    std::size_t files = 0;
    for (const auto& p : projects)
        for (const auto& m : p.moves) {
            const std::string folder = rel_to(home, p.dir) + (m.folder.empty() ? "" : "/" + m.folder);
            picks.push_back({m.from, folder, "conform: " + m.why});
            ++files;
        }
    auto hp = homing::plan(home, picks);
    out.plan = std::move(hp.plan);
    out.renamed = std::move(hp.renamed);
    std::size_t renames = 0;
    if (rename_to_project) {
        for (const auto& p : projects) {
            const std::string name = fs::path(p.dir).filename().string();
            if (projects::is_project_name(name)) { out.not_renamed.push_back(name + " -- already a -project"); continue; }
            const std::string to = fs::path(p.dir).parent_path().string() + "/" + name + "-project";
            std::error_code ec;
            if (fs::exists(fs::symlink_status(to, ec))) { out.not_renamed.push_back(name + " -- \"" + name + "-project\" is taken"); continue; }
            latch::door::Action a;   // LAST: after every file inside has moved
            a.verb = latch::door::Verb::Move;
            a.from = p.dir;
            a.to = to;
            a.reason = "renamed to " + name + "-project -- kept whole from now on";
            out.plan.actions.push_back(a);
            ++renames;
        }
    }
    out.plan.label = "Conform: " + std::to_string(files) + (files == 1 ? " file" : " files") + " in " +
                     std::to_string(projects.size()) + (projects.size() == 1 ? " folder" : " folders") +
                     (renames ? ", " + std::to_string(renames) + " renamed -project" : "");
    return out;
}

}  // namespace tidy::core::conform
