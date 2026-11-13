#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::suspects -- s025 the suspect scan (REBUILD.md 3 + 13.1). GTK-free and
// READ-ONLY: names, lstat, a directory listing. Nothing moves, nothing is
// written -- not even Tidy's own files.
//
// A suspect is a folder that may be ONE thing (a bundle), not a folder of
// loose files. Four groups:
//   App       Name.ext/ with a .json at its top -- one of Scott's apps
//   Ext       Name.ext/ without one
//   Manifest  a database / manifest at its top: *.db *.sqlite *.sqlite3
//             metadata.* *.json Info.plist (Calibre Library)
//   Marker    an app's markers at its top: cache/ thumbnails/
// Known = latch's package list + ours (.composix, .quire, bundles.json): shown,
// not asked.
//
// Do no harm (Scott, s025): a suspect is held WHOLE -- never looked inside
// for more suspects; its size and file count are the whole of it.
// Not suspects, by Scott's answers (2026-11-12 19:03):
//   * a git repo -- a code project is one item: counted, not entered;
//   * a project (-project, or by its place) -- a group: its head is never a
//     Manifest/Marker suspect, but a bundle inside it is listed, marked as
//     in that project (a loose one is "by itself" and will get a home of its
//     own kind -- .optik -> Documents/OCR -- later, not here);
//   * home's top folders (Documents, Downloads ...) -- they are home's design;
//   * hidden, symlinks, Hands off / Samson / never-touch, ~/Archive.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Quarantine.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::suspects {

enum class Group { App = 0, Ext, Manifest, Marker };
const char* group_title(Group g);

struct Suspect {
    std::string rel;              // relative to home
    Group group = Group::Ext;
    std::string ext;              // ".optik" (App / Ext); "" otherwise
    std::vector<std::string> why; // the names that made it one: "metadata.db", "cache/", "doc.json"
    std::string project;          // the project it is in (rel); "" = loose, by itself
    bool known = false;           // already a bundle (latch's list + ours + your answers)
    quarantine::Answer answer = quarantine::Answer::None;   // s025b: Bundle (known), NotNow; Ordinary ones are not listed
    std::uint64_t bytes = 0;      // all of it, hidden files too
    std::size_t files = 0;        // regular files, all of it
};

// s025a (Scott, 19:20): everything under ~/Develop is code -- one item per
// folder, never walked; the report says which are not on GitHub. A container
// (Develop/gtk: it holds -project folders or repos) is walked one level for
// its items. Build output (build/ with CMakeCache.txt or beside a repo /
// CMakeLists.txt, builddir/, autom4te.cache, CMakeFiles) is never a suspect:
// listed to clean later, not walked.
inline constexpr const char* kCodeRoot = "Develop";
enum class Git { GitHub = 0, Remote, Local, None };   // on GitHub / another remote / git, no remote / not in git
const char* git_title(Git g);
struct Item {
    std::string rel;
    Git git = Git::None;          // code items
    std::uint64_t bytes = 0;
    std::size_t files = 0;
};
// The best git state in `dir`: its .git or one in a sub-folder (2 deep,
// not hidden, not build output). Reads .git/config only. READ-ONLY.
Git git_of(const std::string& dir);
bool is_build_output(const std::string& dir);

struct Report {
    std::vector<Item> code;       // ~/Develop's items
    std::vector<Item> build;      // build output found outside code
    std::string home, from;       // from: the folder walked (absolute)
    std::vector<Suspect> list;    // in walk order (sorted by rel)
    std::size_t repos = 0;        // git repos: one item each, not entered
    std::size_t projects = 0;     // projects walked through (heads)
    std::size_t shut = 0;         // Hands off / Samson / never-touch: not entered
    std::size_t folders = 0;      // folders looked at
    std::vector<std::string> quiet;   // s025b: answered "an ordinary folder" -- walked, not listed
    long ms = 0;
};

// "Name.optik" -> ".optik"; "Mr. Smith", "v1.2", ".hidden", "notes" -> "".
// An extension: 1..12 letters/digits, at least one letter, a name before it.
std::string ext_of(const std::string& folder_name);

// Walk `from` (a folder at or under `home`). Protection + places must already
// be applied (protection::apply, projects::apply_places with the home) -- as
// the app does. s025b: the Quarantine's answers are honoured -- "a bundle" is
// known, "an ordinary folder" is walked like any folder (in `quiet`), "not
// now" is listed with its answer.
Report scan(const std::string& home, const std::string& from);

// "3.1 GB", "120 MB", "4 KB", "17 B".
std::string size_text(std::uint64_t bytes);

// The report as text, grouped: Known, then 1..4; App/Ext by extension
// (count, loose / in projects, size, files) with each folder under it.
std::string format(const Report& r);

// s025b: the suspects still quarantined after a scan -- not known, not
// answered ordinary: the "held" list a Save writes.
std::vector<std::string> held_of(const Report& r);
// s025b2: `a` with its held + hands_off lists worked out from a look -- no
// rescan (each answer is saved the moment it is picked). Held: suspects not
// known, answered nothing or not now; hands off: answered don't touch, plus
// the ones still answered so that a look no longer sees (they are shut).
quarantine::Answers with_lists(quarantine::Answers a, const Report& r);

// s025b1: the suspects answered "don't touch" (rel) -- made Hands off by a Save.
std::vector<std::string> hands_off_of(const Report& r);

// tidy --suspects [folder]: apply protection + places, scan, print. 0 = ok.
int main_cli(int argc, char** argv);

}  // namespace tidy::core::suspects
