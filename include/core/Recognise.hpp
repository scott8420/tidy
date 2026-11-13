#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::recognise -- s026 the recognition report (REBUILD.md 2, 13.2).
// GTK-free and READ-ONLY: names, lstat, directory listings, and (only with
// --deep) file reads for SHA-256. Nothing moves, nothing is written -- not
// even Tidy's own files.
//
// The home scanned TOP DOWN; each file is claimed once, by the highest level
// that fits (the most descriptive of its use):
//   1 Bundles   one thing, never looked inside: latch's list + ours + your
//               Quarantine answers; a held suspect is not entered at all.
//               A loose bundle (not in a project) gets the home of its kind
//               (Scott: .optik -> Documents/OCR, .curvz -> Documents/Design/Curvz).
//   2 Projects  by name (-project), by place (a shelf), or "looks like one"
//               (working-folder names, or most of a type's tree) -- each with
//               its clues: the type its tree fits best (have / of), where its
//               files sit against that type's routes, its working folders.
//               A type with a shelf, found off it -> "not on its shelf".
//               Sibling look-alikes sharing folder names -> a proposed type.
//   3 Sets      loose files that belong together: a numbered run (5+), a
//               series' episodes (wherever they are), a font family, versions
//               of one name (X, X Backup, X-1, X copy, X OLD).
//   4 Kinds     what is left, by the type table (+ the Entertainment table):
//               at home under its kind folder, or elsewhere.
//   5 Closet    nothing claims it.
//   + Likely copies: bundles / projects / code items of the same size and
//               file count; --deep reads them (SHA-256) to say "the same".
// ~/Develop is code: one line (see --suspects). Hidden, symlinks, Hands off /
// Samson / never-touch, ~/Archive: never entered.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tidy::core::recognise {

struct Bundle {
    std::string rel, ext;
    std::string project;          // the project it is in (rel); "" = loose
    bool held = false;            // quarantined: not answered / not now -- not entered
    std::uint64_t bytes = 0;
    std::size_t files = 0;
    std::string home;             // loose: the home of its kind ("" = none yet)
};

struct Project {
    enum class How { Name = 0, Place, LooksLike };
    std::string rel;
    How how = How::Name;
    std::string tpl;              // the type its tree fits best ("" = none fits half)
    std::size_t have = 0, of = 0; // that type's folders present / in all
    std::size_t placed = 0, routed = 0;   // items where the type's routes say / items a route names
    std::vector<std::string> working;     // working-folder names at its top
    std::size_t bundles = 0;      // bundles inside
    std::string shelf;            // a shelf type found off its shelf: "Documents/Bookbinding/<Author (Last, First)>/<Title>"
    std::uint64_t bytes = 0;
    std::size_t files = 0;
};

struct Proposed {                 // sibling look-alikes: a new type?
    std::string parent;
    std::vector<std::string> members;     // names under parent
    std::vector<std::string> shared;      // folder names they share
    std::string fullest;          // the most complete one
};

struct Set {
    enum class Kind { Run = 0, Episodes, Fonts, Versions };
    Kind kind = Kind::Run;
    std::string title;            // "A0001.png .. A0220.png", "Mobland", "Roboto", "Report"
    std::vector<std::string> folders;     // where its members are (most first)
    std::size_t count = 0;
};

struct KindRow {
    std::string kind;             // the kind's home folder: "Pictures", "Entertainment/EBooks"
    std::size_t count = 0, at_home = 0;
    std::map<std::string, std::size_t> elsewhere;   // folder -> files not at home
};

struct Copies {                   // same size + file count
    std::uint64_t bytes = 0;
    std::size_t files = 0;
    std::vector<std::string> rels;
    int same = -1;                // --deep: 1 the same bytes, 0 they differ; -1 not read
};

struct Report {
    std::string home, from;
    std::vector<Bundle> bundles;
    std::vector<Project> projects;
    std::vector<Proposed> proposed;
    std::vector<Set> sets;
    std::vector<KindRow> kinds;   // most first
    std::map<std::string, std::pair<std::size_t, std::map<std::string, std::size_t>>> closet;   // ext -> count, folder -> n
    std::vector<Copies> copies;
    std::size_t loose_files = 0;  // files outside bundles and projects (sets + kinds + closet)
    std::size_t in_sets = 0, in_kinds = 0, in_closet = 0;
    std::size_t code_items = 0;   // ~/Develop
    std::uint64_t code_bytes = 0;
    std::size_t repos = 0, build = 0, shut = 0, folders = 0;
    long ms = 0;
};

// The home of a loose bundle's kind (Scott, s025 + s026): .optik ->
// Documents/OCR, .curvz -> Documents/Design/Curvz, .composix ->
// Documents/Design/Layout, .klimt -> Documents/Design/Photo. "" = none yet.
std::string bundle_home(const std::string& ext);

// The working-folder names that mean "a project" (REBUILD 4.3), lower case,
// plus every folder name of the given types.
bool is_working_name(const std::string& folder_name);

// "Report Backup", "Report-1", "Report copy", "Report (2)", "Report OLD",
// "report_old", "Report final" -> "report". Lower case.
std::string version_base(const std::string& stem);

// A font file's family: "Roboto-BoldItalic.ttf" -> "roboto"; "" if not a font.
std::string font_family(const std::string& file_name);

// Walk `from` (at or under `home`). Protection + places must be applied first
// (as main_cli does). `data_dir` gives the types and Settings' tables. `deep`:
// read likely copies to say whether they are the same bytes.
Report scan(const std::string& home, const std::string& from, const std::string& data_dir, bool deep = false);

std::string format(const Report& r);

// tidy --recognise [--deep] [folder]: apply protection + places, scan, print. 0 = ok.
int main_cli(int argc, char** argv);

}  // namespace tidy::core::recognise
