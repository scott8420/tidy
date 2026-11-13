#include "core/Scan.hpp"
#include "core/Quarantine.hpp"

#include "core/Projects.hpp"

#include <latch/Package.hpp>
#include <latch/Protect.hpp>

#include <algorithm>
#include <filesystem>

namespace tidy::core::scan {

namespace fs = std::filesystem;

namespace {
// s019: a tree that is somebody's whole -- a <name>-project, a git repo -- is
// never entered by the loose walk (its files are home already).
bool whole_tree(const std::string& rel, const fs::path& dir) {
    if (projects::is_whole_at(rel)) return true;   // s024: a -project or a shelf's book
    std::error_code ec;
    return fs::exists(fs::symlink_status(dir / ".git", ec));
}

void walk_dir(const std::string& home, const fs::path& dir, std::vector<facts::Facts>& out, bool deep,
              bool loose = false) {
    std::error_code ec;
    std::vector<fs::directory_entry> kids;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) kids.push_back(*it);
    std::sort(kids.begin(), kids.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
    for (const auto& e : kids) {
        const std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        const std::string abs = e.path().string();
        const std::string rel = abs.substr(home.size() + 1);
        if (latch::protect::here(rel)) continue;
        std::error_code e2;
        const auto st = e.symlink_status(e2);
        if (e2 || fs::is_symlink(st)) continue;
        if (fs::is_directory(st) && quarantine::held(abs)) continue;   // s025b: quarantined -- untouched until answered
        if (fs::is_directory(st) && !quarantine::one(abs)) {
            if (deep && !(loose && whole_tree(rel, e.path()))) walk_dir(home, e.path(), out, true, loose);
            continue;
        }
        if (!fs::is_regular_file(st) && !fs::is_directory(st)) continue;
        facts::Facts f;
        if (facts::of(abs, f)) out.push_back(std::move(f));
    }
}
}  // namespace

namespace {
bool root_ok(const std::string& home, const std::string& r) {
    if (r.size() <= home.size() || r.rfind(home + "/", 0) != 0) return false;   // under home only
    if (latch::protect::here(r.substr(home.size() + 1))) return false;
    std::error_code ec;
    return fs::is_directory(fs::symlink_status(r, ec));
}
}  // namespace

std::vector<facts::Facts> walk(const std::string& home, const std::vector<std::string>& roots) {
    std::vector<facts::Facts> out;
    for (const auto& r : roots)
        if (root_ok(home, r)) walk_dir(home, r, out, true);
    return out;
}

std::vector<facts::Facts> top(const std::string& home, const std::string& dir) {
    std::vector<facts::Facts> out;
    std::error_code ec;
    if (dir == home ? fs::is_directory(fs::symlink_status(home, ec)) : root_ok(home, dir))   // s019: home's own top
        walk_dir(home, dir, out, false);
    return out;
}

std::vector<facts::Facts> walk_loose(const std::string& home, const std::vector<std::string>& roots) {
    std::vector<facts::Facts> out;
    for (const auto& r : roots)
        if (root_ok(home, r) && !whole_tree(r.substr(home.size() + 1), r)) walk_dir(home, r, out, true, true);
    return out;
}

}  // namespace tidy::core::scan
