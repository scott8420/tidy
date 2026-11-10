#include "core/Scan.hpp"

#include <latch/Package.hpp>
#include <latch/Protect.hpp>

#include <algorithm>
#include <filesystem>

namespace tidy::core::scan {

namespace fs = std::filesystem;

namespace {
void walk_dir(const std::string& home, const fs::path& dir, std::vector<facts::Facts>& out) {
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
        if (fs::is_directory(st) && !latch::pkg::is_package_name(name)) {
            walk_dir(home, e.path(), out);
            continue;
        }
        if (!fs::is_regular_file(st) && !fs::is_directory(st)) continue;
        facts::Facts f;
        if (facts::of(abs, f)) out.push_back(std::move(f));
    }
}
}  // namespace

std::vector<facts::Facts> walk(const std::string& home, const std::vector<std::string>& roots) {
    std::vector<facts::Facts> out;
    for (const auto& r : roots) {
        if (r.size() <= home.size() || r.rfind(home + "/", 0) != 0) continue;   // under home only
        if (latch::protect::here(r.substr(home.size() + 1))) continue;
        std::error_code ec;
        if (!fs::is_directory(fs::symlink_status(r, ec))) continue;
        walk_dir(home, r, out);
    }
    return out;
}

}  // namespace tidy::core::scan
