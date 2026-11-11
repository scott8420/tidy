#pragma once
// core::code -- s021, the code archive. GTK-free.
//
// Scott (2026-11-11): "What is necessary to keep as an archive is the project
// folder, docs, scripts and the source folder but the source itself is in
// github so the git files are only necessary until a fetch." "with 26+
// projects the code and build folders are really wasted space and we need a
// listing to set as done ready for archive." Done is the user's call.
//
//   look()          one code project: its repos' git state (read-only, no
//                   fetch -- "as of the last fetch"), sizes (build/ and .git
//                   shares), its build folders. Runs git; writes nothing.
//   plan_archive()  each Done, fully-pushed project -> a COPY at
//                   ~/Archive/<year>/<its folder>/<name>-project/: everything
//                   outside the repos (docs/, scripts/ ...), each repo's
//                   COMMITTED files (git ls-files) -- no .git/, no build
//                   folders; backups/, backups/Old/ as EMPTY folders (fixes/ is
//                   never made -- latch never touches one; RESTORE.txt says so);
//                   RESTORE.txt (remote, branch, last commit, the clone line).
//                   Refused while anything is not on a remote.
//   record()        after the run: each copy fingerprinted into the catalog's
//                   `archived` table (catalog-only ids; the copies carry no
//                   xattr). Back up / Remove proved see them as usual.
//   plan_trash()    the LIVE folder to the Trash -- only when the archive's
//                   every file is proved on `proofs_needed` media READ BACK NOW
//                   (core::removal's rule), the repos are still pushed and
//                   clean, and the live files are still the bytes archived.
//   plan_clean()    each build folder to the Trash.
// A module never acts: plans go through latch's door, previewed, undone in
// Activity.
#include "core/Catalog.hpp"
#include "core/Projects.hpp"
#include "core/Removal.hpp"
#include "core/Settings.hpp"

#include <latch/Door.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::code {

inline constexpr const char* kRestore = "RESTORE.txt";
inline constexpr int kGitSeconds = 20;   // each git call (read-only, local)

struct Repo {
    std::string dir;              // absolute: the folder holding .git
    std::string rel;              // relative to the project ("tidy", "deps/latch"; "." = the project's top)
    std::string remote, url;      // "origin", its URL ("" = no remote)
    std::string branch;           // "main"; "HEAD" when detached
    std::string head, subject;    // the last commit
    std::int64_t when = 0;        // its committer time
    int unpushed = 0;             // commits on any local branch on no remote branch
    int changed = 0;              // `status --porcelain` lines (changes + untracked)
    std::vector<std::string> changed_names;   // the first few
    bool commits = false;         // has a commit at all
    std::string error;            // git did not answer
    // "" = everything is on the remote (as of the last fetch); else why not.
    std::string problem() const;
    // "pushed" / "3 unpushed" / "2 uncommitted changes" / "no remote" / "no commits".
    std::string state() const;
};

struct Info {
    projects::Project p;
    std::vector<Repo> repos;
    std::vector<std::string> builds;      // build folders (absolute)
    std::uint64_t total = 0, build = 0, git = 0;   // bytes on disk
    std::int64_t last_commit = 0;         // the newest of the repos'
    std::string state() const;            // the worst repo's state; "no git repo" if none
    std::string problem() const;          // "" = may be archived
};

// The repos in `dir` (a folder holding .git, not inside another repo, not in
// a build folder or hidden folder), each asked of git. READ-ONLY.
std::vector<Repo> repos_of(const std::string& dir);
Repo repo_at(const std::string& project_dir, const std::string& repo_dir);
// A build folder: holds CMakeCache.txt, or is named "build" beside a .git or
// CMakeLists.txt.
bool is_build(const std::string& dir);
Info look(const projects::Project& p);
// Every code project in `found`, looked at. READ-ONLY.
std::vector<Info> look_all(const std::vector<projects::Project>& found);

// What a copy keeps: files (absolute, live), the empty placeholders (relative
// to the project), and what is left behind ("build/ 380 MB", ".git ...").
struct Keep {
    std::vector<std::string> files;
    std::vector<std::string> empty;       // "backups", "backups/Old" -- made empty
    std::vector<std::string> remake;      // "fixes" -- the door never makes one; RESTORE.txt says to
    std::vector<std::string> left;        // said in the preview
    std::uint64_t bytes = 0;
};
Keep keep_of(const Info& info);

// ~/Archive/<year>/<its folder>/<folder> -- ~/Develop/gtk/tidy-project ->
// <home>/Archive/2026/Develop/gtk/tidy-project.
std::string archive_dir(const std::string& home, const projects::Project& p, int year);
std::string restore_text(const Info& info, const Keep& keep, const std::string& day, const std::string& shown_from);

// The catalog's archive of a live project: the newest batch whose copies came
// from under it. Empty `rows` = not archived.
struct Archived {
    std::string top;                      // its folder in ~/Archive
    std::uint64_t batch = 0;
    std::int64_t when = 0;
    std::vector<catalog::Archived> rows;  // every file that batch put under `top` (RESTORE.txt too)
};
Archived archived_of(const std::vector<catalog::Archived>& all, const std::string& live);
// "archived 2026-11-11 -- proved on Big SSD" / "-- not backed up yet" ("" = not archived).
std::string archived_line(const Archived& a, const removal::Book& b, const settings::Settings& s);

struct Planned {
    latch::door::Plan plan;
    std::vector<std::pair<std::string, std::string>> refused;   // (project name, why)
    std::vector<std::string> notes;       // per project: what is kept / left behind
    std::size_t projects = 0, files = 0;
    std::uint64_t bytes = 0;
};
// `done`: is this project's status Done (the catalog's)? READ-ONLY until run.
Planned plan_archive(const std::string& home, const std::vector<Info>& chosen,
                     const std::vector<catalog::ProjectRow>& rows,
                     const std::vector<catalog::Archived>& archived, std::int64_t now);
// After the door ran an archive batch: each Copy / Write under ~/Archive
// fingerprinted (SHA-256 + size, a new catalog id, origin = the live file).
std::size_t record(catalog::Catalog& cat, const latch::door::Door& d, std::uint64_t batch, const std::string& home,
                   std::int64_t now);

// The live folders, archived and proved, to the Trash. Reads every live file
// and every media copy NOW.
Planned plan_trash(const std::string& home, const std::vector<Info>& chosen, const removal::Book& b,
                   const settings::Settings& s, bool other_disk = true);
// Each chosen project's build folders to the Trash.
Planned plan_clean(const std::vector<Info>& chosen);

std::string size_text(std::uint64_t bytes);   // "412 MB", "3.1 GB", "18 KB"

}  // namespace tidy::core::code
