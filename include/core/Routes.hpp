#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::routes -- s022 learned routes (ARC "s019 the home sweep + s021 learned
// routes", Scott 2026-11-11: "once done it won't be so hard so a little
// automation could help"). GTK-free and pure; the book is <data>/routes.ini,
// written by core::own only.
//
// Each send home is remembered. Two kinds of route, each with how often it was
// followed (yes) and overridden (no) -- the Purposes hints' way:
//   name   the file's leading word + its extension -> the folder
//          ("cover-final.png" -> cover*.png -> Pictures/Covers)
//   from   its extension + where it sat loose -> the folder
//          (*.png from Desktop -> Pictures/Covers)
// A proposal: a name route before a from route, then the best record (yes - no,
// then yes). Only routes right more than wrong (yes > no), and only while the
// folder is still there (a folder you removed proposes nothing).
//
// Learned on Do it in Files home (by type, a top folder, into a project -- not
// Archive) and when Sort moves a file by a learned route. Sort's dated default
// ("Desktop 2026-11") is Tidy's, not yours: never learned. Undo in Activity
// moves files back but does not unlearn (as Purposes; backlog).
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Birth.hpp"

#include <string>
#include <vector>

namespace tidy::core::routes {

inline constexpr const char* kFile = "routes.ini";

struct Route {
    enum class Kind { Name, From } kind = Kind::Name;
    std::string pattern;   // Name: "cover*.png"; From: "*.png"
    std::string from;      // From only: "Desktop", "Downloads", "Home", "Writing" ...
    std::string folder;    // relative to home: "Pictures/Covers"
    int yes = 0, no = 0;
    bool active() const { return yes > no; }
    bool operator==(const Route&) const = default;
};

struct Book {
    std::vector<Route> routes;
    bool operator==(const Book&) const = default;
};

Book parse(const std::string& text);
std::string format(const Book& b);

// "cover-final.png" -> "cover*.png"; "IMG_2034.JPG" -> "img*.jpg". The leading
// letters (3 or more) + the last extension, lowercased. "" when there is no
// such word ("2026-03 invoice.pdf") or no extension.
std::string name_glob(const std::string& file_name);
// "photo.PNG" -> "*.png"; "" without an extension.
std::string ext_glob(const std::string& file_name);
// Where a loose file sat, as a route says it: Desktop, Downloads, Screenshots,
// Home (home's top), or the folder it sits loose in (Writing, Documents).
std::string from_of(const birth::Born& b);

struct Proposal {
    std::string folder;    // "" = none
    std::string because;   // "cover*.png" / "*.png from Desktop"
    int times = 0;         // its yes
    int route = -1;        // index into the book
};
// The learned home for a file, or none. Reads whether the folder is there
// (<home>/<folder> a directory) -- nothing else.
Proposal propose(const Book& b, const std::string& home, const birth::Born& file);

// One send: the file went to `folder` (relative to home). Its name route and
// its from route for that folder each gain a yes (added if new); a proposal
// for ANOTHER folder gains a no. `proposed` is what propose() said before.
void learn(Book& b, const birth::Born& file, const std::string& folder, const Proposal& proposed);

}  // namespace tidy::core::routes
