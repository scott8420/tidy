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
};

// The id in <dir>/.tidy-project ("" if none or not a uuid). READ-ONLY.
std::string read_id(const std::string& dir);
std::string id_text(const std::string& id);          // the dot file's text
bool is_uuid(const std::string& s);

// New project (s010d): <home>/<category>/<Name>-project with the template's
// folders and a .tidy-project carrying `id`. Name: a folder name, the suffix
// added if not typed. A door plan -- MakeDir each missing category level,
// the project, each template folder (parents first), then Write the dot file.
// Empty plan + `why` if not: bad name, no category, it exists already.
latch::door::Plan create(const templates::Template& t, const std::string& name, const std::string& home,
                         const std::string& id, std::string* why);
std::string folder_for(const std::string& name);      // "Jasper" -> "Jasper-project"; "x-project" kept

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
