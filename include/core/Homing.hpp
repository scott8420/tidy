#pragma once
// core::homing -- files move home (s011 on the road; REQUIREMENTS §5). GTK-free.
// READ-ONLY: offered() and the checks read; plan() returns a door plan of
// MakeDirs + Moves the UI previews and the door runs (and Activity undoes).
//
// Scott's answers (2026-11-10): the standard type table is fine; a file NOT
// going into a project lands in a SUBFOLDER of its type folder (Pictures/Covers,
// never loose in Pictures); a taken name -> warned and substituted "name (2)";
// no hands-off flag yet (the never-touch list only).
//
//   By type      the type table (Settings: "*.png -> Pictures", one per line,
//                first fit wins) names the home folder; the user picks or names
//                its subfolder. A new subfolder is a MakeDir in the same plan.
//   Into a project
//                the user picks the project; its template's routes pick the
//                folder (*.curvz -> Graphics). No route -> the row asks. Never
//                the project's top, never fixes/ (the door refuses it anyway).
//   Offered      a regular file in a landing place (s005's places) carrying a
//                decided retention -- not Undecided, Ephemeral (the decay ladder
//                has it) or Hands-off, not in the Junk Drawer (kept, no home by
//                choice), not inside a -project.
//   Name taken   "a.png" -> "a (2).png", "(3)"...; "x.tar.gz" -> "x (2).tar.gz".
//                Also against the other files in the same plan. Never overwrite.
#include "core/Birth.hpp"
#include "core/Settings.hpp"
#include "core/Templates.hpp"

#include <latch/Door.hpp>

#include <set>
#include <string>
#include <vector>

namespace tidy::core::homing {

// One line of the type table: a file-name glob (case-insensitive) -> a home folder.
struct TypeRoute {
    std::string pattern;   // "*.png"
    std::string folder;    // "Pictures" (relative to home; one or two parts)
    bool operator==(const TypeRoute&) const = default;
};
// The standard table (Scott: "fine"): images -> Pictures, curvz / svg ->
// Graphics, pdf / odt / docx / txt ... -> Documents, audio -> Music, video -> Videos.
const std::vector<TypeRoute>& defaults();
// "*.png -> Pictures" <-> TypeRoute. parse: false on a bad line.
std::string format_line(const TypeRoute& r);
bool parse_line(const std::string& line, TypeRoute& out);
// The table in use: Settings' lines (bad ones dropped), or defaults() if never set.
std::vector<TypeRoute> table(const settings::Settings& s);
// The table as Settings lines (what the Settings window shows / saves).
std::vector<std::string> lines_of(const std::vector<TypeRoute>& t);
// "" = fine, else why not (a bad glob, a folder that is hidden / .. / a project / too deep).
std::string route_refusal(const TypeRoute& r);
// s029: a folder under home a file may be sent into (any depth up to 6): "" = fine.
std::string folder_check(const std::string& rel);
// The first route `file_name` fits, or nullptr.
const TypeRoute* type_of(const std::vector<TypeRoute>& table, const std::string& file_name);

// Retention levels that are offered a home (decided, and not left to decay / hands-off).
bool offered_level(const std::string& retention_id);
// What the page offers: from a birth report (the landing places), the files
// that may go home. READ-ONLY.
// s023g: and every file loose in Downloads, decided or not (Scott: "I just can't set
// up untracked comic files in the downloads") -- sending one makes it Keep.
std::vector<birth::Born> offered(const birth::Report& r);

// The folders directly in `dir` a file may go into: not hidden, not a symlink,
// not a -project, not latch-protected (fixes/). Sorted. READ-ONLY.
std::vector<std::string> subfolders(const std::string& home, const std::string& dir);
// s018d (Scott: "files home should allow the std folders too"): the folders at
// home's top a file may be sent into by name -- subfolders(home, home) less
// `skip` (the landing places Downloads / Desktop, by name) and Archive (which
// goes through core::archive, fingerprinted, never a plain move). READ-ONLY.
std::vector<std::string> home_folders(const std::string& home, const std::vector<std::string>& skip);

// Where one file goes.
struct Pick {
    std::string from;      // absolute path of the file
    std::string folder;    // destination folder, relative to home ("Pictures/Covers")
    std::string why;       // "by type: *.png -> Pictures" / "into Jasper-project: *.curvz -> Graphics"
};

// By type: `sub` is the subfolder of `type_folder` ("Covers", or "Covers/2026").
// "" = fine (and *folder set), else why not -- an empty sub is the REQUIRED one.
std::string by_type(const std::string& type_folder, const std::string& sub, std::string* folder);
// Into a project (rel to home, "Writing/Jasper-project"): `chosen` is the folder
// inside it the row names; "" -> the template's route for `file_name`. "" = fine
// (*folder set), else why not (no route and nothing chosen; the top; fixes/).
std::string into_project(const std::string& project_rel, const templates::Template* t,
                         const std::string& file_name, const std::string& chosen, std::string* folder,
                         std::string* route = nullptr);

// "a.png" -> "a (n).png"; "x.tar.gz" -> "x (n).tar.gz"; ".hidden" -> ".hidden (n)".
std::string numbered(const std::string& name, int n);
// A name free in `dir` (absolute): `name` itself, or numbered(name, 2, 3...).
// `taken` = paths already claimed by this plan. READ-ONLY.
std::string free_name(const std::string& dir, const std::string& name, const std::set<std::string>& taken);

struct Planned {
    latch::door::Plan plan;
    std::vector<std::string> renamed;   // "a.png -> a (2).png in ~/Pictures/Covers" -- warned in the preview
};
// MakeDir each missing folder level (parents first, once), then a Move per
// pick to <home>/<folder>/<free name>. READ-ONLY until the door runs it.
Planned plan(const std::string& home, const std::vector<Pick>& picks);
// s023g: a file sent home that would still come due (no level, Undecided,
// Ephemeral) is tagged Keep first (an id too where none) -- Due never trashes
// what you placed. The tags go at the plan's front (before the moves).
void keep_sent(latch::door::Plan& plan, const std::vector<Pick>& picks);

}  // namespace tidy::core::homing
