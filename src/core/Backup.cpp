#include "core/Backup.hpp"
#include "core/Archive.hpp"
#include "core/Sha256.hpp"

#include <algorithm>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace tidy::core::backup {

namespace dr = latch::door;
namespace fs = std::filesystem;

std::vector<dr::MediaRoot> roots(const std::vector<settings::Media>& media, bool other_disk) {
    std::vector<dr::MediaRoot> out;
    for (const auto& m : media) out.push_back({m.path, m.label, other_disk});
    return out;
}

void register_media(const std::vector<settings::Media>& media, bool other_disk) {
    dr::set_media_roots(roots(media, other_disk));
}

std::string problem(const settings::Media& m, const std::string& home, bool other_disk) {
    return dr::media_root_problem({m.path, m.label, other_disk}, home);
}

std::string where_on(const settings::Media& m, const std::string& home, const std::string& archive_path) {
    const std::string top = home + "/" + archive::kFolder + "/";
    if (archive_path.size() <= top.size() || archive_path.compare(0, top.size(), top) != 0) return "";
    return m.path + "/" + archive_path.substr(top.size());
}

std::string read_back(const std::string& path, std::uint64_t* size) {
    // Flush what we wrote and drop it from the page cache, so the hash reads
    // what the disk (or the Pi) holds -- not the bytes still in our memory.
    int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0) {
        ::fdatasync(fd);
        ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        ::close(fd);
    }
    return sha256::of_file(path, size);
}

Planned plan(const std::string& home, const settings::Media& m, const std::vector<catalog::Archived>& archived,
             const std::vector<catalog::Copy>& copies) {
    Planned out;
    std::set<std::string> proved;
    for (const auto& c : copies)
        if (c.media == m.label) proved.insert(c.id);
    std::set<std::string> dirs;
    std::vector<dr::Action> mkdirs, cps;
    for (const auto& a : archived) {
        if (proved.count(a.id)) { ++out.proved; continue; }
        struct stat st{};
        if (::lstat(a.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            out.skipped.emplace_back(a.path, "not in ~/Archive any more");
            continue;
        }
        const std::string to = where_on(m, home, a.path);
        if (to.empty()) { out.skipped.emplace_back(a.path, "not in ~/Archive"); continue; }
        struct stat ts{};
        if (::lstat(to.c_str(), &ts) == 0) {
            if (S_ISREG(ts.st_mode)) out.checks.push_back({a.id, a.path, to});
            else out.skipped.emplace_back(a.path, "something that is not a file is there on " + m.label);
            continue;
        }
        // The folders between the media folder and the file, top down.
        const std::string rel = fs::path(to.substr(m.path.size() + 1)).parent_path().string();
        std::string cur = m.path;
        if (!rel.empty())
            for (const auto& part : fs::path(rel)) {
                cur += "/" + part.string();
                struct stat ds{};
                if (dirs.count(cur) || ::lstat(cur.c_str(), &ds) == 0) continue;
                dirs.insert(cur);
                dr::Action d;
                d.verb = dr::Verb::MakeDir;
                d.to = cur;
                d.reason = "a folder on " + m.label;
                mkdirs.push_back(d);
            }
        dr::Action c;
        c.verb = dr::Verb::Copy;
        c.from = a.path;
        c.to = to;
        c.bytes = a.size;
        c.reason = "a copy on " + m.label + " -- read back and compared after";
        cps.push_back(c);
    }
    out.plan.actions = std::move(mkdirs);
    out.plan.actions.insert(out.plan.actions.end(), cps.begin(), cps.end());
    const auto n = cps.size();
    out.plan.label = "Back up: " + (n == 1 ? fs::path(cps[0].to).filename().string() : std::to_string(n) + " files") +
                     " -> " + m.label;
    return out;
}

namespace {
Proof prove_one(catalog::Catalog& cat, const catalog::Archived& a, const settings::Media& m, const std::string& to,
                std::uint64_t batch, std::int64_t now) {
    Proof p{a.id, to, "", false};
    std::uint64_t size = 0;
    const auto sha = read_back(to, &size);
    if (sha.empty()) p.why = "could not read it back";
    else if (size != a.size) p.why = "size differs (" + std::to_string(size) + " there, " + std::to_string(a.size) + " archived)";
    else if (sha != a.sha256) p.why = "different bytes (SHA-256 differs)";
    if (!p.why.empty()) return p;
    catalog::Copy c{a.id, m.label, to, sha, size, now, batch};
    p.ok = cat.put_copy(c);
    if (!p.ok) p.why = "the catalog would not keep it";
    return p;
}
}  // namespace

std::vector<Proof> prove(catalog::Catalog& cat, const dr::Door& d, std::uint64_t batch, const settings::Media& m,
                         const std::vector<Check>& checks, std::int64_t now) {
    std::vector<Proof> out;
    const auto all = cat.archived();
    const auto by_path = [&](const std::string& p) -> const catalog::Archived* {
        for (const auto& a : all) if (a.path == p) return &a;
        return nullptr;
    };
    if (batch)
        for (const auto& b : d.batches()) {
            if (b.id != batch) continue;
            for (auto rid : b.records) {
                const auto* r = d.record(rid);
                if (!r || r->verb != dr::Verb::Copy || r->state != dr::State::Done) continue;
                if (const auto* a = by_path(r->src)) out.push_back(prove_one(cat, *a, m, r->to, batch, now));
                else out.push_back({"", r->to, "not in the catalog's archive", false});
            }
        }
    for (const auto& c : checks) {
        const auto a = cat.archived_by_id(c.id);
        Proof p = a ? prove_one(cat, *a, m, c.to, 0, now) : Proof{c.id, c.to, "not in the catalog's archive", false};
        p.was_there = true;
        out.push_back(p);
    }
    return out;
}

std::size_t after_undo(catalog::Catalog& cat, std::uint64_t batch) { return cat.drop_copies_batch(batch); }

std::string status_line(const std::vector<catalog::Copy>& copies_of_file) {
    if (copies_of_file.empty()) return "not on media yet";
    std::string s;
    for (const auto& c : copies_of_file)
        s += std::string(s.empty() ? "" : ", ") + "on " + c.media + " " + archive::day_of(c.proved);
    return s;
}

}  // namespace tidy::core::backup
