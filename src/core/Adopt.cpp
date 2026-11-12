#include "core/Adopt.hpp"

#include "core/Archive.hpp"
#include "core/Birth.hpp"
#include "core/Projects.hpp"
#include "core/Protection.hpp"
#include "core/Scan.hpp"
#include "core/Tagging.hpp"

#include <latch/Protect.hpp>

#include <algorithm>
#include <filesystem>
#include <map>

namespace tidy::core::adopt {

namespace fs = std::filesystem;
namespace rt = retention;

namespace {
bool inside(const std::string& path, const std::string& dir) {
    return path == dir || path.rfind(dir + "/", 0) == 0;
}
std::string rel_to(const std::string& home, const std::string& path) {
    return path.size() > home.size() + 1 ? path.substr(home.size() + 1) : "";
}
}  // namespace

std::size_t Look::count(rt::Level l) const {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [&](const Item& i) { return i.level == l; }));
}

std::string refusal(const std::string& home, const std::string& user_dirs, const std::string& rel, bool catch_all) {
    if (rel.empty()) return "home itself -- pick a folder in it";
    if (rel[0] == '/' || rel.find("..") != std::string::npos) return "not a folder in home";
    std::string so_far;
    for (const auto& part : fs::path(rel)) {
        const std::string n = part.string();
        if (n.empty()) continue;
        if (n[0] == '.') return "a hidden folder -- Tidy never touches those";
        if (so_far.empty() && n == archive::kFolder) return "~/Archive is Tidy's archive";
        if (projects::is_project_name(n)) return "a -project is kept whole (the Projects page)";
        so_far += (so_far.empty() ? "" : "/") + n;
        if (projects::is_whole_at(so_far)) return "a project by its place is kept whole (the Projects page)";   // s024
        std::error_code ec;
        const auto st = fs::symlink_status(fs::path(home) / so_far, ec);
        if (ec || !fs::exists(st)) return "not there";
        if (fs::is_symlink(st)) return "a symlink -- Tidy never follows one";
        if (fs::exists(fs::symlink_status(fs::path(home) / so_far / ".git", ec)))
            return "a git repo -- the Code projects window";
    }
    std::error_code ec;
    if (!fs::is_directory(fs::symlink_status(fs::path(home) / rel, ec))) return "not a folder";
    if (protection::under(rel)) return "Hands off (Settings), Samson's, or never-touch";
    const std::string p = home + "/" + rel;
    for (const auto& pl : birth::places(home, user_dirs)) {
        if (pl.top_only || !inside(p, pl.path)) continue;   // Downloads, Desktop, Screenshots
        return catch_all ? "a landing place -- Sort already sees all of it"
                         : "a landing place -- Sort handles it (or make it a catch-all)";
    }
    return "";
}

Look look(const std::string& home, const std::string& user_dirs, const std::string& rel, bool catch_all,
          std::int64_t now, int old_years) {
    Look out;
    out.rel = rel;
    out.catch_all = catch_all;
    if (!refusal(home, user_dirs, rel, catch_all).empty()) return out;
    const std::string root = home + "/" + rel;
    const auto files = scan::walk_loose(home, {root});
    if (catch_all) {
        out.offered = files.size();
        return out;
    }
    // The loose spots Sort owns (as the sweep sees them, without catch-alls).
    std::vector<std::string> tops, whole;
    for (const auto& pl : birth::sweep(home, user_dirs)) (pl.top_only ? tops : whole).push_back(pl.path);
    std::map<std::string, std::map<std::string, int>> by_glob;   // glob -> folder -> count
    for (const auto& f : files) {
        const std::string parent = fs::path(f.path).parent_path().string();
        const bool loose = std::find(tops.begin(), tops.end(), parent) != tops.end() ||
                           std::any_of(whole.begin(), whole.end(), [&](const std::string& w) { return inside(f.path, w); });
        if (loose) { ++out.loose; continue; }
        rt::Level have = rt::Level::Undecided;
        const bool has_level = !f.tags.retention.empty() && rt::parse(f.tags.retention, have) && have != rt::Level::Undecided;
        if (has_level && have == rt::Level::HandsOff) { ++out.known; continue; }   // never touched
        if (!f.dir) {
            const auto g = routes::name_glob(fs::path(f.path).filename().string());
            if (!g.empty()) ++by_glob[g][rel_to(home, parent)];
        }
        Item it;
        it.file = f;
        it.give_id = f.tags.id.empty();
        it.set_level = !has_level;
        if (has_level) {
            it.level = have;
            it.why = rt::title_of(have) + " -- it carries that already";
        } else {
            const auto since = birth::untouched_since(f);
            const std::int64_t span = static_cast<std::int64_t>(old_years) * 365 * 86400;
            if (old_years > 0 && since > 0 && now - since >= span) {
                it.level = rt::Level::Historical;
                it.why = "Historical -- untouched " + std::to_string((now - since) / (365 * 86400)) +
                         " years; Due offers to archive it";
            } else {
                it.level = rt::Level::Keep;
                it.why = "Keep -- where you put it";
            }
        }
        if (!it.give_id && !it.set_level) { ++out.known; continue; }
        out.items.push_back(std::move(it));
    }
    for (const auto& [glob, folders] : by_glob) {
        int total = 0;
        for (const auto& [_, n] : folders) total += n;
        for (const auto& [folder, n] : folders)
            if (n >= kRouteMin && n * 3 >= total * 2 && !folder.empty()) out.routes.push_back({glob, folder, n});
    }
    return out;
}

latch::door::Plan plan(const std::vector<Look>& looks) {
    latch::door::Plan p;
    std::size_t files = 0, folders = 0;
    for (const auto& l : looks) {
        if (l.catch_all || l.items.empty()) continue;
        ++folders;
        for (const auto& it : l.items) {
            facts::Tags want;
            if (it.set_level) want.retention = rt::id_of(it.level);
            auto one = tagging::stamp({it.file}, want, "adopt: " + it.why);
            for (auto& a : one.actions) p.actions.push_back(std::move(a));
            ++files;
        }
    }
    p.label = "Adopt " + std::to_string(files) + (files == 1 ? " file" : " files") + " in place (" +
              std::to_string(folders) + (folders == 1 ? " folder)" : " folders)");
    return p;
}

std::size_t learn(routes::Book& book, const std::vector<Look>& looks) {
    std::size_t n = 0;
    for (const auto& l : looks)
        for (const auto& r : l.routes) {
            auto it = std::find_if(book.routes.begin(), book.routes.end(), [&](const routes::Route& x) {
                return x.kind == routes::Route::Kind::Name && x.pattern == r.pattern && x.folder == r.folder;
            });
            if (it == book.routes.end()) {
                routes::Route nr;
                nr.kind = routes::Route::Kind::Name;
                nr.pattern = r.pattern;
                nr.folder = r.folder;
                nr.yes = r.count;
                book.routes.push_back(nr);
                ++n;
            } else if (it->yes < r.count) {
                it->yes = r.count;
                ++n;
            }
        }
    return n;
}

}  // namespace tidy::core::adopt
