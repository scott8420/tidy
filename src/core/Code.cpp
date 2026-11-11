#include "core/Code.hpp"

#include "core/Archive.hpp"
#include "core/Facts.hpp"
#include "core/Sha256.hpp"

#include <latch/Vcs.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>

// Code.cpp -- READ-ONLY on the home: git is asked (status, rev-list, log,
// remote, ls-files -- never fetch, never anything that writes), files are
// stat'ed and hashed. Every change is a door plan the page previews.

namespace tidy::core::code {

namespace dr = latch::door;
namespace fs = std::filesystem;

namespace {

bool lst(const std::string& p, struct stat& st) { return ::lstat(p.c_str(), &st) == 0; }
bool is_dir(const std::string& p) {
    struct stat st{};
    return lst(p, st) && S_ISDIR(st.st_mode);
}
bool exists(const std::string& p) {
    struct stat st{};
    return lst(p, st);
}
bool has_git(const std::string& dir) { return exists(dir + "/.git"); }
bool hidden(const std::string& name) { return !name.empty() && name[0] == '.'; }

std::vector<std::string> children(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec))
        out.push_back(it->path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string l; std::getline(in, l);)
        if (!trim(l).empty()) out.push_back(l);
    return out;
}
std::string plural(std::size_t n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); }
std::string day_of(std::int64_t t) { return archive::day_of(t); }
int year_of(std::int64_t t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    return tm.tm_year + 1900;
}
std::string rel_to(const std::string& base, const std::string& p) {
    if (p == base) return ".";
    if (p.size() > base.size() + 1 && p.compare(0, base.size() + 1, base + "/") == 0) return p.substr(base.size() + 1);
    return p;
}
bool under(const std::string& p, const std::string& dir) {
    return p.size() > dir.size() + 1 && p.compare(0, dir.size() + 1, dir + "/") == 0;
}
std::string shown(const std::string& home, const std::string& p) {
    if (p == home) return "~";
    if (under(p, home)) return "~" + p.substr(home.size());
    return p;
}

// Disk use of a tree (st_blocks, as du does); symlinks not followed.
std::uint64_t du(const std::string& p) {
    struct stat st{};
    if (!lst(p, st)) return 0;
    std::uint64_t n = static_cast<std::uint64_t>(st.st_blocks) * 512;
    if (S_ISDIR(st.st_mode))
        for (const auto& c : children(p)) n += du(p + "/" + c);
    return n;
}

void find_repos(const std::string& dir, int depth, std::vector<std::string>& out) {
    if (has_git(dir)) { out.push_back(dir); return; }
    if (depth <= 0 || is_build(dir)) return;
    for (const auto& c : children(dir)) {
        if (hidden(c)) continue;
        const std::string p = dir + "/" + c;
        if (is_dir(p)) find_repos(p, depth - 1, out);
    }
}

}  // namespace

// ── state ─────────────────────────────────────────────────────────────────────

std::string Repo::problem() const {
    if (!error.empty()) return rel + ": git did not answer (" + error + ")";
    if (url.empty()) return rel + ": no remote -- the source is only here";
    if (!commits) return rel + ": no commits";
    if (changed > 0) {
        std::string names;
        for (const auto& n : changed_names) names += (names.empty() ? "" : ", ") + n;
        if (changed > static_cast<int>(changed_names.size()))
            names += " (+" + std::to_string(changed - static_cast<int>(changed_names.size())) + ")";
        return rel + ": " + plural(static_cast<std::size_t>(changed), "uncommitted change") + " -- " + names;
    }
    if (unpushed > 0)
        return rel + ": " + plural(static_cast<std::size_t>(unpushed), "commit") + " not pushed (as of the last fetch)";
    return "";
}

std::string Repo::state() const {
    if (!error.empty()) return "git did not answer";
    if (!commits) return "no commits";
    if (changed > 0) return plural(static_cast<std::size_t>(changed), "uncommitted change");
    if (url.empty()) return "no remote";
    if (unpushed > 0) return std::to_string(unpushed) + " unpushed";
    return "pushed";
}

std::string Info::state() const {
    if (repos.empty()) return "no git repo";
    for (const auto& r : repos)
        if (r.state() != "pushed") return repos.size() == 1 ? r.state() : r.rel + ": " + r.state();
    return "pushed";
}

std::string Info::problem() const {
    if (repos.empty()) return "no git repo -- the source is only here";
    for (const auto& r : repos)
        if (auto why = r.problem(); !why.empty()) return why;
    return "";
}

bool is_build(const std::string& dir) {
    if (exists(dir + "/CMakeCache.txt")) return true;
    const auto name = fs::path(dir).filename().string();
    if (name != "build") return false;
    const auto parent = fs::path(dir).parent_path().string();
    return has_git(parent) || exists(parent + "/CMakeLists.txt");
}

Repo repo_at(const std::string& project_dir, const std::string& repo_dir) {
    Repo r;
    r.dir = repo_dir;
    r.rel = rel_to(project_dir, repo_dir);
    const auto git = [&](const std::vector<std::string>& args) {
        return latch::vcs::git(repo_dir, args, kGitSeconds);
    };
    const auto st = git({"status", "--porcelain"});
    if (st.code != 0) {
        r.error = trim(st.err.empty() ? std::string("exit ") + std::to_string(st.code) : st.err);
        return r;
    }
    for (const auto& l : lines(st.out)) {
        // Tidy's own dot files (the project's id, the folder id lists) are not the user's changes.
        const std::string name = l.size() > 3 ? l.substr(3) : l;
        const auto base = fs::path(name).filename().string();
        if (base == projects::kIdFile || base == ".tidy-files") continue;
        ++r.changed;
        if (r.changed_names.size() < 3) r.changed_names.push_back(l.size() > 3 ? l.substr(3) : l);
    }
    const auto lg = git({"log", "-1", "--format=%H%x1f%ct%x1f%s"});
    if (lg.code == 0 && !trim(lg.out).empty()) {
        r.commits = true;
        const auto t = trim(lg.out);
        const auto a = t.find('\x1f'), b = t.find('\x1f', a == std::string::npos ? a : a + 1);
        r.head = t.substr(0, a);
        if (a != std::string::npos && b != std::string::npos) {
            try { r.when = std::stoll(t.substr(a + 1, b - a - 1)); } catch (...) {}
            r.subject = t.substr(b + 1);
        }
    }
    r.branch = trim(git({"rev-parse", "--abbrev-ref", "HEAD"}).out);
    const auto remotes = lines(git({"remote"}).out);
    if (!remotes.empty()) {
        r.remote = std::find(remotes.begin(), remotes.end(), "origin") != remotes.end() ? "origin" : trim(remotes[0]);
        r.url = trim(git({"remote", "get-url", r.remote}).out);
    }
    if (r.commits) {
        // Commits on any local branch that no remote-tracking branch has (no fetch).
        const auto n = git({"rev-list", "--count", "--branches", "--not", "--remotes"});
        try { r.unpushed = n.code == 0 ? std::stoi(trim(n.out)) : 0; } catch (...) {}
    }
    return r;
}

std::vector<Repo> repos_of(const std::string& dir) {
    std::vector<std::string> dirs;
    find_repos(dir, 3, dirs);
    std::vector<Repo> out;
    for (const auto& d : dirs) out.push_back(repo_at(dir, d));
    return out;
}

namespace {
void walk_sizes(const std::string& p, bool in_git, bool in_build, Info& info) {
    struct stat st{};
    if (!lst(p, st)) return;
    const std::uint64_t n = static_cast<std::uint64_t>(st.st_blocks) * 512;
    info.total += n;
    if (in_git) info.git += n;
    else if (in_build) info.build += n;
    if (!S_ISDIR(st.st_mode)) return;
    for (const auto& c : children(p)) {
        const std::string q = p + "/" + c;
        bool g = in_git, b = in_build;
        if (!g && !b && is_dir(q)) {
            if (c == ".git") g = true;
            else if (is_build(q)) { b = true; info.builds.push_back(q); }
        }
        walk_sizes(q, g, b, info);
    }
}
}  // namespace

Info look(const projects::Project& p) {
    Info info;
    info.p = p;
    info.repos = repos_of(p.path);
    for (const auto& r : info.repos) info.last_commit = std::max(info.last_commit, r.when);
    walk_sizes(p.path, false, false, info);
    std::sort(info.builds.begin(), info.builds.end());
    return info;
}

std::vector<Info> look_all(const std::vector<projects::Project>& found) {
    std::vector<Info> out;
    for (const auto& p : found)
        if (p.code) out.push_back(look(p));
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return a.p.name < b.p.name; });
    return out;
}

// ── what a copy keeps ─────────────────────────────────────────────────────────

namespace {
struct Left {
    std::size_t files = 0;
    std::uint64_t bytes = 0;
};
void count_tree(const std::string& p, Left& l) {
    struct stat st{};
    if (!lst(p, st)) return;
    if (S_ISDIR(st.st_mode)) {
        for (const auto& c : children(p)) count_tree(p + "/" + c, l);
        return;
    }
    ++l.files;
    l.bytes += static_cast<std::uint64_t>(st.st_size);
}
std::string left_line(const std::string& what, const Left& l) {
    return what + " (" + plural(l.files, "file") + ", " + size_text(l.bytes) + ")";
}
}  // namespace

Keep keep_of(const Info& info) {
    Keep k;
    const std::string top = info.p.path;
    std::set<std::string> repo_dirs;
    for (const auto& r : info.repos) repo_dirs.insert(r.dir);
    std::map<std::string, Left> left;   // by what
    const auto add = [&](const std::string& path, const struct stat& st) {
        k.files.push_back(path);
        k.bytes += static_cast<std::uint64_t>(st.st_size);
    };

    // Each repo: its committed files only.
    for (const auto& r : info.repos) {
        const auto ls = latch::vcs::git(r.dir, {"ls-files", "-z"}, kGitSeconds);
        std::set<std::string> tracked;
        std::size_t from = 0;
        while (from < ls.out.size()) {
            const auto end = ls.out.find('\0', from);
            const auto name = ls.out.substr(from, end == std::string::npos ? std::string::npos : end - from);
            from = end == std::string::npos ? ls.out.size() : end + 1;
            if (name.empty()) continue;
            const std::string p = r.dir + "/" + name;
            tracked.insert(p);
            struct stat st{};
            if (!lst(p, st)) continue;
            if (S_ISREG(st.st_mode)) add(p, st);
            else if (S_ISLNK(st.st_mode)) { ++left["links (git keeps them; not copied)"].files; }
            else if (S_ISDIR(st.st_mode)) { ++left["submodules (their own repos)"].files; }
        }
        // What git ignores (not build folders, not .git): left, said.
        std::function<void(const std::string&)> ignored = [&](const std::string& d) {
            for (const auto& c : children(d)) {
                const std::string p = d + "/" + c;
                struct stat st{};
                if (!lst(p, st)) continue;
                if (S_ISDIR(st.st_mode)) {
                    if (c == ".git" || is_build(p) || (p != r.dir && repo_dirs.count(p))) continue;
                    ignored(p);
                } else if (!tracked.count(p)) {
                    auto& l = left["files git ignores"];
                    ++l.files;
                    l.bytes += static_cast<std::uint64_t>(st.st_size);
                }
            }
        };
        ignored(r.dir);
    }

    // Outside the repos: everything, but build folders, hidden folders,
    // links; backups/ and fixes/ at the top become empty placeholders.
    std::function<void(const std::string&)> walk = [&](const std::string& d) {
        for (const auto& c : children(d)) {
            const std::string p = d + "/" + c;
            struct stat st{};
            if (!lst(p, st)) continue;
            if (S_ISLNK(st.st_mode)) { ++left["links (not copied)"].files; continue; }
            if (S_ISDIR(st.st_mode)) {
                if (repo_dirs.count(p)) continue;
                if (d == top && (c == "backups" || c == "fixes")) {
                    // fixes/ is the user's, never touched by the door at any depth: not even an empty one is
                    // made -- RESTORE.txt says to make it.
                    if (c == "backups") { k.empty.push_back(c); k.empty.push_back("backups/Old"); }
                    else k.remake.push_back(c);
                    Left l;
                    count_tree(p, l);
                    if (l.files)
                        left[c == "backups" ? std::string("backups/ contents -- kept as an empty folder")
                                            : std::string("fixes/ contents -- yours, not copied")] = l;
                    continue;
                }
                if (is_build(p)) continue;   // counted with the build share
                if (hidden(c)) {
                    Left l;
                    count_tree(p, l);
                    left["hidden folder " + rel_to(top, p)] = l;
                    continue;
                }
                walk(p);
            } else if (S_ISREG(st.st_mode)) {
                add(p, st);
            }
        }
    };
    if (!repo_dirs.count(top)) walk(top);
    else
    {
        if (is_dir(top + "/backups")) { k.empty.push_back("backups"); k.empty.push_back("backups/Old"); }
        if (is_dir(top + "/fixes")) k.remake.push_back("fixes");
        const std::string idf = top + "/" + projects::kIdFile;   // Tidy's id file, untracked
        if (struct stat st{}; lst(idf, st) && S_ISREG(st.st_mode) &&
                              std::find(k.files.begin(), k.files.end(), idf) == k.files.end())
            add(idf, st);
    }

    std::sort(k.files.begin(), k.files.end());
    k.files.erase(std::unique(k.files.begin(), k.files.end()), k.files.end());
    if (info.git) k.left.push_back(".git (" + size_text(info.git) + ") -- the history is on the remote");
    if (info.build) k.left.push_back("build folders (" + size_text(info.build) + ") -- the build makes them again");
    for (const auto& [what, l] : left)
        k.left.push_back(l.bytes ? left_line(what, l) : what + " (" + std::to_string(l.files) + ")");
    return k;
}

// ── the archive ───────────────────────────────────────────────────────────────

std::string archive_dir(const std::string& home, const projects::Project& p, int year) {
    const auto parent = fs::path(p.rel).parent_path().string();
    std::string d = home + "/" + archive::kFolder + "/" + std::to_string(year);
    if (!parent.empty()) d += "/" + parent;
    return d + "/" + fs::path(p.path).filename().string();
}

std::string restore_text(const Info& info, const Keep& keep, const std::string& day, const std::string& shown_from) {
    std::ostringstream o;
    const auto folder = fs::path(info.p.path).filename().string();
    o << folder << " -- archived by Tidy on " << day << "\n"
      << "It was: " << shown_from << "\n\n"
      << "The source is on its remote (as of the last fetch on the day it was archived):\n";
    for (const auto& r : info.repos) {
        o << "\n" << (r.rel == "." ? folder : r.rel) << "/\n"
          << "  remote   " << r.remote << "  " << r.url << "\n"
          << "  branch   " << r.branch << "\n"
          << "  commit   " << r.head << "  " << day_of(r.when) << "  " << r.subject << "\n"
          << "  back:    git clone " << r.url << " " << (r.rel == "." ? folder : r.rel) << " && git -C "
          << (r.rel == "." ? folder : r.rel) << " checkout " << r.head << "\n";
    }
    o << "\nKept here: " << plural(keep.files.size(), "file") << " (" << size_text(keep.bytes)
      << ") -- everything outside the repos, and each repo's committed files (no .git, no build folders).\n";
    if (!keep.empty.empty()) {
        o << "Empty on purpose (placeholders):";
        for (const auto& e : keep.empty) o << " " << e << "/";
        o << "\n(A backup disk keeps only files -- make these folders again if they are missing.)\n";
    }
    if (!keep.remake.empty()) {
        o << "Make again when you work on it:";
        for (const auto& e : keep.remake) o << " " << e << "/";
        o << " (yours -- Tidy never makes or touches a fixes/ folder)\n";
    }
    if (!keep.left.empty()) {
        o << "Left behind:\n";
        for (const auto& l : keep.left) o << "  - " << l << "\n";
    }
    o << "\nTo work on it again: copy this folder home; for each source folder above, move the copied one aside,\n"
         "run its \"back:\" line in the folder that held it, then build as usual.\n";
    return o.str();
}

Archived archived_of(const std::vector<catalog::Archived>& all, const std::string& live) {
    Archived a;
    for (const auto& r : all)
        if (under(r.origin, live) && r.batch >= a.batch) {
            if (r.batch > a.batch) a.top.clear();
            a.batch = r.batch;
            const std::string suffix = r.origin.substr(live.size());   // "/tidy/src/x.cpp"
            if (a.top.empty() && r.path.size() > suffix.size() &&
                r.path.compare(r.path.size() - suffix.size(), suffix.size(), suffix) == 0)
                a.top = r.path.substr(0, r.path.size() - suffix.size());
        }
    if (a.top.empty()) return {};
    for (const auto& r : all)
        if (r.batch == a.batch && under(r.path, a.top)) {
            a.rows.push_back(r);
            a.when = std::max(a.when, r.when);
        }
    return a;
}

std::string archived_line(const Archived& a, const removal::Book& b, const settings::Settings& s) {
    if (a.rows.empty()) return "";
    std::map<std::string, std::size_t> on;   // media -> files proved there
    std::size_t enough = 0;
    for (const auto& r : a.rows) {
        int n = 0;
        for (const auto& c : b.copies)
            if (c.id == r.id && std::any_of(s.media.begin(), s.media.end(),
                                            [&](const settings::Media& m) { return m.label == c.media; })) {
                ++n;
                ++on[c.media];
            }
        enough += n >= s.proofs_needed;
    }
    std::string line = "archived " + day_of(a.when) + " -- ";
    if (enough == a.rows.size()) {
        std::string where;
        for (const auto& [m, n] : on)
            if (n == a.rows.size()) where += (where.empty() ? "" : ", ") + m;
        return line + "proved on " + where + " -- the live folder may go to the Trash";
    }
    if (enough == 0) return line + "not backed up yet (Archive -> Back up)";
    return line + std::to_string(enough) + " of " + std::to_string(a.rows.size()) + " files backed up";
}

namespace {
// Is every file the copy would keep still the bytes archived? READS each.
// "" = the same; else what differs (the first few).
std::string differs(const Info& info, const Archived& a) {
    std::map<std::string, const catalog::Archived*> by_origin;
    for (const auto& r : a.rows)
        if (!r.origin.empty()) by_origin[r.origin] = &r;
    std::vector<std::string> changed, added;
    for (const auto& f : keep_of(info).files) {
        const auto it = by_origin.find(f);
        if (it == by_origin.end()) { added.push_back(rel_to(info.p.path, f)); continue; }
        std::uint64_t size = 0;
        if (sha256::of_file(f, &size) != it->second->sha256 || size != it->second->size)
            changed.push_back(rel_to(info.p.path, f));
    }
    const auto some = [](const std::vector<std::string>& v) {
        std::string s;
        for (std::size_t i = 0; i < v.size() && i < 3; ++i) s += (s.empty() ? "" : ", ") + v[i];
        if (v.size() > 3) s += " (+" + std::to_string(v.size() - 3) + ")";
        return s;
    };
    std::string why;
    if (!changed.empty()) why = "changed: " + some(changed);
    if (!added.empty()) why += std::string(why.empty() ? "" : "; ") + "new: " + some(added);
    return why;
}
}  // namespace

Planned plan_archive(const std::string& home, const std::vector<Info>& chosen,
                     const std::vector<catalog::ProjectRow>& rows,
                     const std::vector<catalog::Archived>& archived, std::int64_t now) {
    Planned out;
    const int year = year_of(now);
    const std::string day = day_of(now);
    std::set<std::string> made;   // folders this plan makes (and that exist)
    std::vector<dr::Action> dirs, copies, writes;
    for (const auto& info : chosen) {
        const auto& p = info.p;
        const std::string name = fs::path(p.path).filename().string();
        if (p.id.empty()) { out.refused.emplace_back(name, "no id yet -- Give ids on the Projects page"); continue; }
        const auto row = std::find_if(rows.begin(), rows.end(), [&](const catalog::ProjectRow& r) { return r.id == p.id; });
        const std::string status = row == rows.end() ? "active" : row->status;
        if (status != "done") {
            out.refused.emplace_back(name, "not Done (it is " + status + ") -- set it to Done first; that is your call");
            continue;
        }
        if (auto why = info.problem(); !why.empty()) {
            out.refused.emplace_back(name, "not all on the remote -- " + why);
            continue;
        }
        const auto before = archived_of(archived, p.path);
        if (!before.rows.empty()) {
            const auto d = differs(info, before);
            if (d.empty()) {
                out.refused.emplace_back(name, "archived already on " + day_of(before.when) + " (" +
                                                   shown(home, before.top) + ") -- nothing changed since");
                continue;
            }
            out.notes.push_back(name + ": changed since its archive of " + day_of(before.when) + " (" + d +
                                ") -- a second copy beside it");
        }
        std::string dest = archive_dir(home, p, year);
        for (int n = 2; exists(dest) || made.count(dest); ++n) dest = archive_dir(home, p, year) + " (" + std::to_string(n) + ")";
        const auto keep = keep_of(info);

        // Folders, parents first: ~/Archive down to the copy, then inside it.
        std::set<std::string> want;
        for (fs::path a = fs::path(dest).parent_path(); a.string().size() > home.size(); a = a.parent_path())
            want.insert(a.string());
        want.insert(dest);
        for (const auto& f : keep.files)
            for (fs::path a = fs::path(dest + "/" + rel_to(p.path, f)).parent_path(); a.string().size() > dest.size();
                 a = a.parent_path())
                want.insert(a.string());
        for (const auto& e : keep.empty) want.insert(dest + "/" + e);
        for (const auto& w : want) {   // sorted: a parent sorts before what is in it
            if (made.count(w)) continue;
            made.insert(w);
            if (w != dest && !under(w, dest) && is_dir(w)) continue;   // ~/Archive/2026 already there
            dr::Action a;
            a.verb = dr::Verb::MakeDir;
            a.to = w;
            a.reason = under(w, dest) || w == dest ? "the archive copy of " + name : "the archive";
            dirs.push_back(a);
        }
        for (const auto& f : keep.files) {
            dr::Action a;
            a.verb = dr::Verb::Copy;
            a.from = f;
            a.to = dest + "/" + rel_to(p.path, f);
            a.reason = "archive copy of " + name;
            struct stat st{};
            if (lst(f, st)) a.bytes = static_cast<std::uint64_t>(st.st_size);
            copies.push_back(a);
        }
        std::string restore = dest + "/" + kRestore;
        if (std::find(keep.files.begin(), keep.files.end(), p.path + "/" + kRestore) != keep.files.end())
            restore = dest + "/RESTORE (Tidy).txt";
        dr::Action w;
        w.verb = dr::Verb::Write;
        w.to = restore;
        w.content = restore_text(info, keep, day, shown(home, p.path));
        w.reason = "how to bring " + name + " back (its remote, branch, last commit)";
        w.bytes = w.content.size();
        writes.push_back(w);

        std::string note = name + " -> " + shown(home, dest) + ": " + plural(keep.files.size(), "file") + " (" +
                           size_text(keep.bytes) + ")";
        if (!keep.empty.empty()) {
            note += "; empty:";
            for (const auto& e : keep.empty) note += " " + e + "/";
        }
        for (const auto& e : keep.remake) note += "; " + e + "/ not made (yours -- RESTORE.txt says so)";
        out.notes.push_back(note);
        for (const auto& l : keep.left) out.notes.push_back("    not kept: " + l);
        ++out.projects;
        out.files += keep.files.size();
        out.bytes += keep.bytes;
    }
    // Every folder first (a plan's MakeDirs before the copies into them), then the copies, then RESTORE.txt.
    out.plan.actions = std::move(dirs);
    out.plan.actions.insert(out.plan.actions.end(), copies.begin(), copies.end());
    out.plan.actions.insert(out.plan.actions.end(), writes.begin(), writes.end());
    out.plan.label = "Archive code: " +
                     (out.projects == 1 && !writes.empty()
                          ? fs::path(writes[0].to).parent_path().filename().string()
                          : std::to_string(out.projects) + " projects") +
                     " -> ~/" + archive::kFolder + "/" + std::to_string(year);
    return out;
}

std::size_t record(catalog::Catalog& cat, const dr::Door& d, std::uint64_t batch, const std::string& home,
                   std::int64_t now) {
    const std::string top = home + "/" + archive::kFolder;
    std::size_t n = 0;
    for (const auto& b : d.batches()) {
        if (b.id != batch) continue;
        for (auto rid : b.records) {
            const auto* r = d.record(rid);
            if (!r || r->state != dr::State::Done) continue;
            if (r->verb != dr::Verb::Copy && r->verb != dr::Verb::Write) continue;
            if (!under(r->to, top)) continue;
            std::uint64_t size = 0;
            const auto sha = sha256::of_file(r->to, &size);
            if (sha.empty()) continue;
            catalog::Archived a;
            a.id = facts::new_id();
            a.path = r->to;
            a.origin = r->verb == dr::Verb::Copy ? r->src : "";
            a.sha256 = sha;
            a.size = size;
            a.when = now;
            a.batch = batch;
            n += cat.put_archived(a);
        }
    }
    return n;
}

// ── the live folder, the build folders ────────────────────────────────────────

Planned plan_trash(const std::string& home, const std::vector<Info>& chosen, const removal::Book& b,
                   const settings::Settings& s, bool other_disk) {
    Planned out;
    for (const auto& info : chosen) {
        const std::string name = fs::path(info.p.path).filename().string();
        const auto a = archived_of(b.archived, info.p.path);
        if (a.rows.empty()) { out.refused.emplace_back(name, "not archived yet -- Archive done... first"); continue; }
        if (auto why = info.problem(); !why.empty()) {
            out.refused.emplace_back(name, "not all on the remote now -- " + why);
            continue;
        }
        if (auto d = differs(info, a); !d.empty()) {
            out.refused.emplace_back(name, "changed since it was archived on " + day_of(a.when) + " (" + d +
                                               ") -- Archive done... again, back it up, then here");
            continue;
        }
        std::size_t short_of = 0;
        std::string first;
        std::set<std::string> on_all;
        bool started = false;
        for (const auto& r : a.rows) {
            std::vector<std::string> not_on;
            const auto on = removal::proved_now(home, b, s, r, other_disk, &not_on);
            if (static_cast<int>(on.size()) < s.proofs_needed) {
                ++short_of;
                if (first.empty()) {
                    first = rel_to(a.top, r.path) + ": proved now on " + std::to_string(on.size()) + " of the " +
                            std::to_string(s.proofs_needed) + " needed";
                    for (std::size_t i = 0; i < not_on.size(); ++i) first += (i ? ", " : "; not on ") + not_on[i];
                    if (on.empty() && not_on.empty()) first += " -- back it up first (Archive page)";
                }
                continue;
            }
            std::set<std::string> here(on.begin(), on.end());
            if (!started) { on_all = here; started = true; }
            else {
                std::set<std::string> both;
                for (const auto& m : on_all) if (here.count(m)) both.insert(m);
                on_all = both;
            }
        }
        if (short_of) {
            out.refused.emplace_back(name, plural(short_of, "archive file") + " not proved -- " + first);
            continue;
        }
        std::string where;
        for (const auto& m : on_all) where += (where.empty() ? "" : ", ") + m;
        dr::Action t;
        t.verb = dr::Verb::Trash;
        t.from = info.p.path;
        t.bytes = info.total;
        t.reason = "archived " + day_of(a.when) + " at " + shown(home, a.top) + "; its " + plural(a.rows.size(), "file") +
                   " read back now" + (where.empty() ? std::string() : " on " + where) +
                   " -- the same bytes. Restore from the archive (RESTORE.txt).";
        out.plan.actions.push_back(t);
        out.notes.push_back(name + ": " + size_text(info.total) + " to the Trash");
        ++out.projects;
        out.bytes += info.total;
    }
    out.plan.label = "Trash archived: " +
                     (out.projects == 1 ? fs::path(out.plan.actions[0].from).filename().string()
                                        : std::to_string(out.projects) + " projects");
    return out;
}

Planned plan_clean(const std::vector<Info>& chosen) {
    Planned out;
    for (const auto& info : chosen) {
        const std::string name = fs::path(info.p.path).filename().string();
        if (info.builds.empty()) { out.refused.emplace_back(name, "no build folder"); continue; }
        for (const auto& b : info.builds) {
            dr::Action t;
            t.verb = dr::Verb::Trash;
            t.from = b;
            t.bytes = du(b);
            t.reason = "the build folder of " + name + " (" + size_text(t.bytes) + ") -- the build makes it again";
            out.plan.actions.push_back(t);
            out.bytes += t.bytes;
        }
        ++out.projects;
    }
    std::string one;
    for (const auto& info : chosen)
        if (!info.builds.empty()) { one = fs::path(info.p.path).filename().string(); break; }
    out.plan.label = "Clean build folders: " + (out.projects == 1 ? one : std::to_string(out.projects) + " projects") +
                     " (" + size_text(out.bytes) + ")";
    return out;
}

std::string size_text(std::uint64_t bytes) {
    char buf[32];
    const double b = static_cast<double>(bytes);
    if (bytes < 1024) std::snprintf(buf, sizeof buf, "%llu bytes", static_cast<unsigned long long>(bytes));
    else if (b < 1024.0 * 1024) std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024);
    else if (b < 1024.0 * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.0f MB", b / (1024 * 1024));
    else std::snprintf(buf, sizeof buf, "%.1f GB", b / (1024.0 * 1024 * 1024));
    return buf;
}

}  // namespace tidy::core::code
