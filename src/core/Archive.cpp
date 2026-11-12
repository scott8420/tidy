#include "core/Archive.hpp"

#include "core/Facts.hpp"
#include "core/Homing.hpp"
#include "core/Projects.hpp"
#include "core/Sha256.hpp"
#include "core/Tagging.hpp"

#include <ctime>
#include <filesystem>

namespace tidy::core::archive {

namespace dr = latch::door;
namespace fs = std::filesystem;

std::string day_of(std::int64_t t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

namespace {
int year_of(std::int64_t t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    return tm.tm_year + 1900;
}
std::string rel_of(const std::string& home, const std::string& path) {
    if (path.size() <= home.size() + 1 || path.compare(0, home.size() + 1, home + "/") != 0) return "";
    return path.substr(home.size() + 1);
}
}  // namespace

std::string folder_for(const std::string& home, const std::string& path, int year) {
    const auto rel = rel_of(home, path);
    const auto parent = fs::path(rel).parent_path().string();
    std::string f = std::string(kFolder) + "/" + std::to_string(year);
    if (!parent.empty()) f += "/" + parent;
    return f;
}

std::string refusal(const std::string& home, const std::string& path) {
    const auto rel = rel_of(home, path);
    if (rel.empty()) return "not in your home";
    if (rel == kFolder || rel.rfind(std::string(kFolder) + "/", 0) == 0) return "already in ~/Archive";
    for (const auto& part : fs::path(rel).parent_path())
        if (projects::is_project_name(part.string())) return "inside " + part.string() + " -- a project moves whole";
    if (std::string at; projects::inside_whole(fs::path(rel).parent_path().string(), &at))   // s024: a book
        return "inside " + fs::path(at).filename().string() + " -- a project moves whole";
    std::error_code ec;
    const auto st = fs::symlink_status(path, ec);
    if (ec || !fs::is_regular_file(st)) return "not a file here";
    std::string day;
    if (dr::tag_of(path, kTag, day)) return "already archived (" + day + ")";
    std::string level;
    if (dr::tag_of(path, facts::kRetention, level) && level == "hands-off") return "Hands off -- Tidy never touches it";
    return "";
}

Planned plan(const std::string& home, const std::vector<std::string>& paths, std::int64_t now) {
    Planned out;
    const std::string day = day_of(now);
    const int year = year_of(now);
    std::vector<homing::Pick> picks;
    std::vector<dr::Action> tags;
    for (const auto& p : paths) {
        if (auto why = refusal(home, p); !why.empty()) { out.refused.emplace_back(p, why); continue; }
        const auto t = facts::tags_of(p);
        if (t.id.empty()) {
            dr::Action a;
            a.verb = dr::Verb::Tag;
            a.from = p;
            a.attr = facts::kId;
            a.content = facts::new_id();
            a.reason = "a stable id for the catalog";
            tags.push_back(a);
        }
        dr::Action a;
        a.verb = dr::Verb::Tag;
        a.from = p;
        a.attr = kTag;
        a.content = day;
        a.reason = "archived " + day;
        tags.push_back(a);
        // s023d (Scott: archive even what Due would remove): a file archived by hand
        // that would otherwise come due again (Ephemeral, Held, Until done, none)
        // becomes Historical -- so Due never proposes trashing it out of ~/Archive.
        if (t.retention != "historical" && t.retention != "keep") {
            dr::Action h;
            h.verb = dr::Verb::Tag;
            h.from = p;
            h.attr = facts::kRetention;
            h.content = "historical";
            h.reason = "archived by hand: " + (t.retention.empty() ? std::string("no level") : t.retention) + " -> Historical";
            tags.push_back(h);
        }
        picks.push_back({p, folder_for(home, p, year), "archive: " + (t.retention.empty() ? std::string("by hand")
                                                                                           : t.retention)});
    }
    auto moved = homing::plan(home, picks);
    out.renamed = std::move(moved.renamed);
    out.plan.actions = std::move(tags);
    out.plan.actions.insert(out.plan.actions.end(), moved.plan.actions.begin(), moved.plan.actions.end());
    const auto n = picks.size();
    out.plan.label = "Archive: " + (n == 1 ? fs::path(picks[0].from).filename().string()
                                           : std::to_string(n) + " files") + " -> ~/" + kFolder + "/" +
                     std::to_string(year);
    return out;
}

std::size_t record(catalog::Catalog& cat, const dr::Door& d, std::uint64_t batch, std::int64_t now) {
    std::size_t n = 0;
    for (const auto& b : d.batches()) {
        if (b.id != batch) continue;
        for (auto rid : b.records) {
            const auto* r = d.record(rid);
            if (!r || r->verb != dr::Verb::Move || r->state != dr::State::Done) continue;
            facts::Facts f;
            if (!facts::of(r->to, f) || f.tags.id.empty()) continue;
            // s020: a Sort batch also moves files HOME -- only an archived one is fingerprinted.
            if (f.tags.archived.empty()) continue;
            std::uint64_t size = 0;
            const auto sha = sha256::of_file(r->to, &size);
            if (sha.empty()) continue;
            tagging::record(cat, {r->to}, now);
            catalog::Archived a;
            a.id = f.tags.id;
            a.path = r->to;
            a.origin = r->from;
            a.sha256 = sha;
            a.size = size;
            a.when = now;
            a.batch = batch;
            if (cat.put_archived(a)) ++n;
        }
    }
    return n;
}

std::size_t after_undo(catalog::Catalog& cat, std::uint64_t batch) { return cat.drop_archived_batch(batch); }

}  // namespace tidy::core::archive
