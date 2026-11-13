#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::quarantine -- s025b the answers to the suspect scan (REBUILD 3, 13.1).
// GTK-free.
//
// Each suspect gets one answer, kept in Tidy's own <data>/quarantine.txt
// (written only through core::own, by the Quarantine window's Save):
//   A bundle            one thing: never looked inside, moved only whole.
//                       By extension (groups 1, 2): the extension joins latch's
//                       extra list, so every walker knows it (latch's own
//                       bundles.json is left as you edit it). By folder
//                       (groups 3, 4): that folder.
//   An ordinary folder  a folder like any other -- walked, never asked again.
//   Not now             still quarantined: held, untouched; asked again later.
// Unanswered = quarantined too (do no harm). "Held" folders -- the last Save's
// suspects not answered ordinary or bundle -- are never entered by Tidy's
// walks (Sort, the Closet, Due, the Map shows them shut) until answered.
// A folder answered "a bundle" is one item: like a .optik, moved only whole.
// ─────────────────────────────────────────────────────────────────────────────
#include <map>
#include <string>
#include <vector>

namespace tidy::core::quarantine {

inline constexpr const char* kFile = "quarantine.txt";

enum class Answer { None = 0, Bundle, Ordinary, NotNow };
const char* answer_title(Answer a);   // "Not answered", "A bundle", "An ordinary folder", "Not now"

struct Answers {
    std::map<std::string, Answer> ext;      // ".itmsp" -> Bundle   (groups 1, 2)
    std::map<std::string, Answer> folders;  // "Documents/DBs" -> Ordinary (groups 3, 4; rel to home)
    std::vector<std::string> held;          // rel to home: quarantined at the last Save
    bool operator==(const Answers&) const = default;
};

// The file's text <-> Answers. One line each, read back by the last " = ":
//   ext .itmsp = bundle        folder Documents/DBs = ordinary
//   held Documents/Degoo/Font Library/N/NewsGothic.BoldOblique
// '#' lines are comments. A bad line is skipped, never an error.
Answers parse(const std::string& text);
std::string to_text(const Answers& a);
Answers load(const std::string& data_dir);   // READ-ONLY; missing = none

// The answer for an extension / a folder ("None" when not answered).
Answer of_ext(const Answers& a, const std::string& ext);
Answer of_folder(const Answers& a, const std::string& rel);

// The extensions answered "a bundle" (lower-case, ".x").
std::vector<std::string> bundle_exts(const Answers& a);

// Set once at startup and after a Save (projects::apply_places calls it when
// given the home). Every walk asks held / one.
void set(const std::string& home, Answers a);
Answers current();
std::string current_home();

// Is the folder at `abs` quarantined -- held, not entered at all?
bool held(const std::string& abs);
// Is the folder at `abs` ONE thing: a package by its name (latch, our extras,
// your bundle extensions), a folder answered "a bundle", or held? Walkers use
// this in place of latch::pkg::is_package_name when they have the path.
bool one(const std::string& abs);

}  // namespace tidy::core::quarantine
