#include "core/Removal.hpp"
#include "core/Archive.hpp"
#include "core/Backup.hpp"
#include "core/Facts.hpp"
#include "core/Retention.hpp"
#include "core/Sha256.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sys/stat.h>

namespace tidy::core::removal {

namespace dr = latch::door;
namespace fs = std::filesystem;

Book book_of(const catalog::Catalog& cat) { return {cat.archived(), cat.copies(), cat.removed()}; }

namespace {
bool in_archive(const std::string& home, const std::string& p) {
    const std::string top = home + "/" + archive::kFolder + "/";
    return p.size() > top.size() && p.compare(0, top.size(), top) == 0;
}
bool is_file(const std::string& p) {
    struct stat st{};
    return ::lstat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
const settings::Media* media_named(const settings::Settings& s, const std::string& label) {
    for (const auto& m : s.media) if (m.label == label) return &m;
    return nullptr;
}
std::vector<const catalog::Copy*> copies_of(const Book& b, const std::string& id) {
    std::vector<const catalog::Copy*> out;
    for (const auto& c : b.copies) if (c.id == id) out.push_back(&c);
    return out;
}
bool removed(const Book& b, const std::string& id) {
    return std::any_of(b.removed.begin(), b.removed.end(), [&](const catalog::Removed& r) { return r.id == id; });
}
std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}
}  // namespace

std::vector<catalog::Archived> offerable(const Book& b, const settings::Settings& s) {
    std::vector<catalog::Archived> out;
    for (const auto& a : b.archived) {
        if (removed(b, a.id) || !is_file(a.path)) continue;
        int n = 0;
        for (const auto* c : copies_of(b, a.id)) n += media_named(s, c->media) != nullptr;
        if (n >= s.proofs_needed) out.push_back(a);
    }
    return out;
}

std::string offer_why(const Book& b, const std::string& id) {
    std::vector<std::string> on;
    for (const auto* c : copies_of(b, id)) on.push_back(c->media + " " + archive::day_of(c->proved));
    return "archived, proved on " + join(on) + " -- read back again before it goes";
}

std::vector<std::string> proved_now(const std::string& home, const Book& b, const settings::Settings& s,
                                    const catalog::Archived& a, bool other_disk, std::vector<std::string>* not_on) {
    std::vector<std::string> on;
    for (const auto* c : copies_of(b, a.id)) {
        const auto* m = media_named(s, c->media);
        if (!m) { if (not_on) not_on->push_back(c->media + " (not in Settings now)"); continue; }
        if (auto why = backup::problem(*m, home, other_disk); !why.empty()) {
            if (not_on) not_on->push_back(c->media + " (" + why + ")");
            continue;
        }
        std::uint64_t csize = 0;
        if (backup::read_back(c->path, &csize) != a.sha256 || csize != a.size) {
            if (not_on) not_on->push_back(c->media + " (the copy there is gone or differs now)");
            continue;
        }
        on.push_back(c->media);
    }
    return on;
}

Planned plan(const std::string& home, const Book& b, const settings::Settings& s,
             const std::vector<std::string>& paths, bool other_disk) {
    Planned out;
    std::set<std::string> seen;
    for (const auto& p : paths) {
        if (!seen.insert(p).second) continue;
        if (!is_file(p)) { out.refused.emplace_back(p, "not a file here any more"); continue; }
        std::string reason;
        if (in_archive(home, p)) {
            const auto it = std::find_if(b.archived.begin(), b.archived.end(),
                                         [&](const catalog::Archived& a) { return a.path == p; });
            if (it == b.archived.end()) {
                out.refused.emplace_back(p, "in ~/Archive but not in the catalog -- no fingerprint, no proof");
                continue;
            }
            const auto& a = *it;
            std::uint64_t size = 0;
            if (sha256::of_file(p, &size) != a.sha256 || size != a.size) {
                out.refused.emplace_back(p, "changed since it was archived -- the copies are of the old bytes; "
                                            "back it up again first");
                continue;
            }
            std::vector<std::string> not_on;
            const auto on = proved_now(home, b, s, a, other_disk, &not_on);
            if (static_cast<int>(on.size()) < s.proofs_needed) {
                std::string why = "proved now on " + std::to_string(on.size()) + " of the " +
                                  std::to_string(s.proofs_needed) + " needed";
                if (!on.empty()) why += " (" + join(on) + ")";
                if (!not_on.empty()) why += "; not on " + join(not_on);
                if (on.empty() && not_on.empty()) why += " -- back it up first (Archive page)";
                out.refused.emplace_back(p, why);
                continue;
            }
            reason = "read back now on " + join(on) + " -- the same bytes";
            out.pending.push_back({a.id, a.path, join(on), 0, 0});
        } else {
            const auto t = facts::tags_of(p);
            retention::Level lv;
            if (!retention::parse(t.retention, lv) ||
                (lv != retention::Level::Ephemeral && lv != retention::Level::Held)) {
                out.refused.emplace_back(p, "not Ephemeral or Held -- only those go without an archive");
                continue;
            }
            reason = std::string(lv == retention::Level::Ephemeral ? "Ephemeral" : "Held") + ", due";
        }
        dr::Action a;
        a.verb = dr::Verb::Trash;
        a.from = p;
        a.reason = reason;
        struct stat st{};
        if (::lstat(p.c_str(), &st) == 0) a.bytes = static_cast<std::uint64_t>(st.st_size);
        out.plan.actions.push_back(a);
    }
    const auto n = out.plan.actions.size();
    out.plan.label = "Remove: " + (n == 1 ? fs::path(out.plan.actions[0].from).filename().string()
                                          : std::to_string(n) + " files") + " -> Trash";
    return out;
}

std::size_t record(catalog::Catalog& cat, const dr::Door& d, std::uint64_t batch,
                   const std::vector<catalog::Removed>& pending, std::int64_t now) {
    std::size_t n = 0;
    for (const auto& b : d.batches()) {
        if (b.id != batch) continue;
        for (auto rid : b.records) {
            const auto* r = d.record(rid);
            if (!r || r->verb != dr::Verb::Trash || r->state != dr::State::Done) continue;
            for (auto p : pending)
                if (p.path == r->from) {
                    p.when = now;
                    p.batch = batch;
                    n += cat.put_removed(p);
                }
        }
    }
    return n;
}

std::size_t after_undo(catalog::Catalog& cat, std::uint64_t batch) { return cat.drop_removed_batch(batch); }

}  // namespace tidy::core::removal
