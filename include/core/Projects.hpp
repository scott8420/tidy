#pragma once
// core::projects -- every <name>-project in the home, as found (s008;
// REQUIREMENTS §5). GTK-free. find() READS ONLY; place() / fix() return door
// plans (a module never acts): Move the project whole, MakeDir what its
// template names and it lacks. The UI previews; the door runs and undoes.
//
//   The -project suffix is the declaration: a tree that moves whole and is
//   never split. Tidy never looks inside one for more projects.
//   Category = where it lives: ~/Develop/gtk/tidy-project is Develop. A project
//   in the home's top, Desktop or Downloads is UNCATEGORIZED -- Tidy asks once
//   (place it, or leave it -- "left" is remembered in <data>/projects.ini,
//   written by core::own, and never asked again).
//   A code project (.git or CMakeLists.txt at its top) stays where it is; it is
//   never proposed a move (REQUIREMENTS §5).
#include "core/Templates.hpp"

#include <latch/Door.hpp>

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace tidy::core::projects {

inline constexpr const char* kSuffix = "-project";
inline constexpr const char* kFile = "projects.ini";
inline constexpr int kDepth = 4;            // how deep under home find() looks
// s010d: each project's id lives in a dot file at its top, so it travels with
// the folder (moves, copies, USB sticks). Written only through the door
// (Write), at New project; read by find(). "id=<uuid v4>" + newline.
inline constexpr const char* kIdFile = ".tidy-project";

struct Project {
    std::string path;                       // absolute
    std::string rel;                        // relative to home
    std::string name;                       // "Jasper" (the folder name less -project)
    std::string category;                   // "Writing"; "" = uncategorized
    bool code = false;                      // .git or CMakeLists.txt at its top
    bool left = false;                      // uncategorized, and the user said leave it
    std::string id;                         // from .tidy-project; "" = none yet (s010d)
    bool by_place = false;                  // s024: known by its place (a shelf), no -project
    std::string group;                      // s024: the shelf's middle folder ("Keene, Carolyn"); ""
};

// ── s024: shelves -- projects known BY THEIR PLACE ──────────────────────────
// A shelf = a by-place template's category + how deep its projects sit under
// it (templates::place_depth). ~/Documents/Bookbinding/<Last, First>/<Title>
// IS a bookbinding project: kept whole, never sorted file by file, never
// looked inside for loose files -- the same as a -project, without the name.
struct Shelf {
    std::string category;                   // "Documents/Bookbinding"
    int depth = 0;                          // 2 = <group>/<Name>
    std::string tpl;                        // the template's name
};
std::vector<Shelf> shelves_of(const std::vector<templates::Template>& ts);
// Set once at startup and after a template is saved (the app; the selftest).
// Every walk asks is_whole_at / inside_whole.
void set_shelves(std::vector<Shelf> s);
std::vector<Shelf> shelves();
// The saved templates' shelves + the built-in bookbinding's (always: a book
// is never split, saved template or not), and the package extras: Tidy's own
// bundles latch does not list (.composix, .quire) + <data>/bundles.json.
// READ-ONLY (a directory listing + reads).
// s025b: given the home, also the Quarantine's answers (core::quarantine):
// your bundle extensions join the extras, held / bundle folders are set.
// Without it the answers already set stay as they are.
void apply_places(const std::string& data_dir, const std::string& home = "");
// The saved templates + the built-in bookbinding when none of that name is
// saved (READ-ONLY). What a book is checked against before you save your own.
std::vector<templates::Template> known_templates(const std::string& data_dir);
inline const std::vector<std::string>& own_bundles() {
    static const std::vector<std::string> v = {".composix", ".quire", ".mort"};   // s030: Mortimer's too
    return v;
}
// Is the folder at `rel` (relative to home) a project: a -project name, or a
// shelf's place? `shelf_out` (optional) gets the shelf when by place.
bool is_whole_at(const std::string& rel, Shelf* shelf_out = nullptr);
// Is `rel` a project or strictly inside one (any ancestor is)? The project's
// rel in `at` (optional).
bool inside_whole(const std::string& rel, std::string* at = nullptr);

// The id in <dir>/.tidy-project ("" if none or not a uuid). READ-ONLY.
std::string read_id(const std::string& dir);
std::string id_text(const std::string& id);          // the dot file's text
bool is_uuid(const std::string& s);

// New project (s010d): <home>/<category>/<Name>-project with the template's
// folders and a .tidy-project carrying `id`. Name: a folder name, the suffix
// added if not typed. A door plan -- MakeDir each missing category level,
// the project, each template folder (parents first), then Write the dot file.
// Empty plan + `why` if not: bad name, no category, it exists already.
// s024: a by-place template -> <home>/<category>/[<group>/]<name>, no suffix;
// `group` required when the template names one ("Keene, Carolyn").
latch::door::Plan create(const templates::Template& t, const std::string& name, const std::string& home,
                         const std::string& id, std::string* why, const std::string& group = "");
std::string folder_for(const std::string& name);

// s010e. Ids for projects found without one: a Write of .tidy-project each,
// one plan (code projects too -- the dot file sits at the workspace's top).
// `new_id` makes each id (facts::new_id in the app; fixed ones in tests).
latch::door::Plan give_ids(const std::vector<Project>& found, const std::function<std::string()>& new_id);

// Status of a project's work -- the catalog's, the user's choice.
const std::vector<std::string>& statuses();          // active, paused, done, dropped (ids, on disk)
bool is_status(const std::string& s);
// Search: every word of `query` found (case-insensitive) in one of `fields`.
bool fits(const std::string& query, const std::vector<std::string>& fields);      // "Jasper" -> "Jasper-project"; "x-project" kept

// s010g. Uses: a project made from other projects, by id (a Folio book uses a
// Curvz cover project). A project used by nothing is its own product. The
// links are Tidy's catalog's (the user's choice, like status), not on disk.
struct Use { std::string project, used; };          // `project` is made with `used`
// May `project` use `used`? Not itself, not twice, not something made from it
// (no loop: A uses B uses A). `why` says why not.
bool can_use(const std::vector<Use>& uses, const std::string& project, const std::string& used, std::string* why);
std::vector<std::string> uses_of(const std::vector<Use>& uses, const std::string& id);   // what it is made with
std::vector<std::string> used_by(const std::vector<Use>& uses, const std::string& id);   // what is made with it

// Is `rel` (relative to home, the folder's own path) a -project folder name?
bool is_project_name(const std::string& folder_name);
// The category a project at `rel` has: the first part, unless it sits in the
// home's top, Desktop or Downloads -> "".
std::string category_of(const std::string& rel);
// Code: .git or CMakeLists.txt at the top or one folder down (s010c).
bool is_code(const std::filesystem::path& dir);

// Every -project under home, kDepth deep (READ-ONLY): hidden, symlinks,
// latch-protected places, Tidy's records and the Trash skipped; never inside
// a project. `left` = the remembered "leave it" answers (rel paths).
std::vector<Project> find(const std::string& home, const std::set<std::string>& left = {});

// The template for a project: the saved one whose category holds it, the
// deepest winning (Develop/gtk over Develop; "" category -> none). nullptr = none.
const templates::Template* template_for(const Project& p, const std::vector<templates::Template>& ts);

// Plans (never run here).
// Place an uncategorized project: MakeDir ~/<category> if missing, then Move
// the project whole to ~/<category>/<folder>. Empty plan + `why` if not.
latch::door::Plan place(const Project& p, const std::string& home, const std::string& category, std::string* why);
// Fix drift on the project side: MakeDir each folder the template names and
// the project lacks (parents first). Extra folders are not moved (adopt them
// into the template, or leave them). Empty plan if nothing is missing.
latch::door::Plan fix(const Project& p, const std::vector<templates::Drift>& drift);

// <data>/projects.ini -- "left=<rel>" per line.
std::set<std::string> parse_left(const std::string& text);
std::string format_left(const std::set<std::string>& left);

}  // namespace tidy::core::projects
