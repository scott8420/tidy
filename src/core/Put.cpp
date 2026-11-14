#include "core/Put.hpp"
#include "core/Birth.hpp"
#include "core/Facts.hpp"
#include "core/Homing.hpp"
#include "core/Own.hpp"
#include "core/Protection.hpp"
#include "core/Routes.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace tidy::core::put {

namespace {
std::string place_of(const std::string& home, const std::string& path) {
    const fs::path parent = fs::path(path).parent_path();
    if (parent == fs::path(home)) return "home";
    if (parent == fs::path(home) / "Desktop") return "desktop";
    if (parent == fs::path(home) / "Downloads") return "downloads";
    return "";
}
}  // namespace

std::string folder_rel(const std::string& home, const std::string& picked, std::string* rel) {
    std::string r;
    if (const auto why = protection::rel_of(home, picked, r); !why.empty()) return why;
    if (protection::under(r)) return "~/" + r + " is Hands off -- Tidy never puts anything there";
    if (const auto why = homing::folder_check(r); !why.empty()) return why;
    if (rel) *rel = r;
    return "";
}

Planned plan(const std::string& home, const std::vector<std::string>& files, const std::string& folder) {
    Planned out;
    std::vector<homing::Pick> picks;
    const fs::path to = fs::path(home) / folder;
    for (const auto& f : files) {
        const fs::path p(f);
        const std::string n = p.filename().string();
        if (f.rfind(home + "/", 0) != 0 || n.empty() || n[0] == '.') { out.skipped.push_back(n + " -- not a file Tidy may move"); continue; }
        if (p.parent_path() == to) { out.skipped.push_back(n + " -- already there"); continue; }
        if (protection::under(f.substr(home.size() + 1))) { out.skipped.push_back(n + " -- Hands off"); continue; }
        picks.push_back({f, folder, "put it with ~/" + folder + " (by hand)"});
        out.lines.push_back(n + "  ->  ~/" + folder);
    }
    auto hp = homing::plan(home, picks);
    out.plan = std::move(hp.plan);
    out.renamed = std::move(hp.renamed);
    homing::keep_sent(out.plan, picks);
    out.plan.label = "Put " + std::to_string(picks.size()) + (picks.size() == 1 ? " file" : " files") + " with ~/" + folder;
    return out;
}

std::size_t learn(const std::string& home, const std::string& data_dir, const std::vector<std::string>& files,
                  const std::string& folder, std::string* why) {
    auto book = routes::parse(own::read(data_dir, routes::kFile));
    std::size_t taught = 0;
    for (const auto& f : files) {
        std::error_code ec;
        if (fs::exists(f, ec)) continue;   // not moved: nothing learned
        birth::Born b;
        b.file.path = f;
        b.place = place_of(home, f);
        routes::learn(book, b, folder, routes::propose(book, home, b));
        ++taught;
    }
    if (taught) {
        const auto w = own::write(data_dir, routes::kFile, routes::format(book));
        if (why) *why = w;
    }
    return taught;
}

}  // namespace tidy::core::put
