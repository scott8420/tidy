#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::home -- s029 the Home design (REBUILD 6; Scott, 2026-11-12 22:34-22:41).
//
// Home is a TYPE like any other, its top your home (~). A tree of folders with
// "this goes here" rules -- one place to design where loose files live:
//
//   *.png              -> Pictures
//   *.otf              -> Fonts/<A-Z|OTHER>/<full name>   (named from the font)
//   Downloads/*.mkv    -> Entertainment/Movies             (a READ download)
//
//   * Saved as Tidy's own <data>/templates/home.template (category "~"). Never
//     saved -> SEEDED from Settings' old type table + Entertainment table (or
//     their standard ones) -- nothing lost. templates::list() leaves it out
//     (it is no project type); the Types page shows it first.
//   * Slots: a folder part "<A-Z|OTHER>" is the first letter of the file's
//     name (OTHER when not A-Z), "<full name>" its full name -- for a font the
//     name table's full name (nameID 4), else the file's name less its type.
//   * Sort, What Tidy sees and Files home follow it. A file no rule fits
//     stays in the Closet (no more dated "Desktop 2026-11" folders).
//
// GTK-free. READ-ONLY (a font's name table is read with open + pread).
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Homing.hpp"
#include "core/Settings.hpp"
#include "core/Templates.hpp"

#include <string>
#include <vector>

namespace tidy::core::home {

inline constexpr const char* kName = "home";
inline constexpr const char* kTop = "~";                // the Home type's category
inline constexpr const char* kLetter = "<A-Z|OTHER>";
inline constexpr const char* kFullName = "<full name>";
inline constexpr const char* kDownloads = "Downloads/";  // a route "Downloads/*.mkv" = a READ download
// s030i (Scott: ".sla is trash", ".ics ... .log ... .bak is trash"): a row in the
// Home tree whose rules send a file to the Trash -- through the door, one undo.
inline constexpr const char* kTrash = "<the Trash>";
bool is_trash(const std::string& folder);
// The Home type always shows its Trash row (added when a saved one lacks it).
void ensure_trash(templates::Template& t);

bool is_home(const templates::Template& t);   // name "home", category "~"

// The Home type from Settings' old tables (else the standard ones): every
// type line a route, the Entertainment lines as "Downloads/..." routes,
// *.otf / *.ttf -> Fonts/<A-Z|OTHER>/<full name>. Folders + their parents.
templates::Template seed(const settings::Settings& st);
// Saved? (home.template under the data folder). READ-ONLY.
bool saved(const std::string& data_dir);
// The saved Home type, else seed(settings). READ-ONLY.
templates::Template load(const std::string& data_dir, const settings::Settings& st);
templates::Template load(const std::string& data_dir);   // reads settings.ini itself

// The Home type read for its users.
struct Design {
    templates::Template t;
    std::vector<homing::TypeRoute> types;   // plain name routes ("*.png" -> "Pictures"), first fit wins
    std::vector<homing::TypeRoute> ent;     // the Downloads/ routes, "Downloads/" taken off
};
Design design(const templates::Template& t);
Design design_of(const std::string& data_dir);

// The first plain route a file name fits, or nullptr.
const homing::TypeRoute* rule_for(const Design& d, const std::string& file_name);

// Slots.
bool has_slot(const std::string& folder);
std::string fixed_part(const std::string& folder);       // "Fonts/<A-Z|OTHER>/<full name>" -> "Fonts"
// The folder for one file: slots filled from the file at `abs` (its name when
// it can't be read). "" when the result is not a folder a file may go to.
std::string expand(const std::string& folder, const std::string& abs, std::string* why = nullptr);

// A font's full name from its name table (OTF / TTF / TTC); "" if none. READ-ONLY.
std::string font_name(const std::string& abs);
std::string letter_of(const std::string& name);           // "Garamond" -> "G", "1942 Report" -> "OTHER"
std::string clean_name(const std::string& raw);           // '/' -> '-', trimmed, no leading '.'
// s030: a folder named only for a format or role -- static, variable, ttf, otf,
// fonts, webfonts (any case) -- says nothing of what it holds.
bool generic_folder_name(const std::string& name);

}  // namespace tidy::core::home
