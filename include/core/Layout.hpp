#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::layout -- s024a (Scott, 2026-11-12): put ONE old book in the layout.
//
// Most of his ~25 books predate Klimt / Composix / Quire / Optik: lumped
// together, parts missing. One book at a time, Tidy proposes a place per file
// from the template's routes (core::conform's walk: bundles whole, a file
// already under its folder in place), and ASKS where no route fits (which
// .png is the cover?) -- never guesses. Scott fixes any proposal; then one
// plan: every template folder the book lacks (the whole tree, empty stages
// too), the moves, and the folders the moves leave empty -> the Trash. One
// preview, one undo step.
//
// READ-ONLY: look() reads names; plan() builds the door's steps.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Templates.hpp"

#include <latch/Door.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace tidy::core::layout {

inline constexpr const char* kLeave = "(leave)";   // a row's folder: leave it where it is ("" = the book's top)

struct Row {
    std::string from;          // absolute
    std::string rel;           // relative to the book
    std::string folder;        // a template folder, or kLeave
    std::string why;           // "*.odt -> Body/Layout/ODT"; "" = asked
    bool asked = false;        // no route fits: Scott's pick (starts at kLeave)
};
struct Look {
    std::string dir;                     // absolute
    std::vector<Row> rows;               // proposed (by a route) then asked, each by path
    std::size_t in_place = 0;            // already under their folder
    std::size_t whole = 0;               // nested projects / repos / protected
    std::vector<std::string> missing;    // template folders the book lacks (tree order)
    std::size_t proposed() const;
    std::size_t asked() const;
};

Look look(const templates::Template& t, const std::string& dir);

struct Planned {
    latch::door::Plan plan;
    std::size_t moves = 0, made = 0;
    std::vector<std::string> emptied;    // relative to the book, to the Trash (topmost only)
    std::vector<std::string> renamed;    // a taken name -> "(2)"
};
// MakeDir every missing template folder (parents first), then a Move per row
// whose folder is not kLeave (homing's plan: "(2)" for a taken name), then
// Trash each folder the moves leave with nothing in it (topmost; never a
// template folder, never one holding a hidden file, never the book).
Planned plan(const std::string& home, const templates::Template& t, const Look& l);

// ── s024b: make it a book ─────────────────────────────────────────────────
// A folder anywhere in home (the Nancy Drew series' "02 The Hidden Staircase")
// becomes a book by moving it WHOLE to ~/<category>/<author>/<title> -- the
// by-place template's shelf. Its files are not touched (Layout... does that).

// "02 The Hidden Staircase" / "02 - X" / "02_X" / "X-project" -> the title.
std::string guess_title(const std::string& folder_name);
// The parent folder's name when it reads "Last, First" (one comma, words on
// both sides), else "".
std::string guess_author(const std::string& dir);
// The folders to offer: `dir` itself, or (each_sub) each folder in it (as
// conform::folders). READ-ONLY.
std::vector<std::string> candidates(const std::string& dir, bool each_sub);

struct Book {
    std::string dir;       // absolute, where it is now
    std::string title;     // the folder name it gets
};
// "" = it may be made a book, else why not: outside home, hidden, ~/Archive,
// protected, a git repo, a book or a -project already (or inside one), the
// shelf itself or above it, a bad name, the place taken.
std::string book_refusal(const std::string& home, const templates::Template& t, const std::string& author,
                         const Book& b);
struct Made {
    latch::door::Plan plan;
    std::vector<std::string> refused;    // "<folder> -- why"
    std::size_t books = 0;
};
// MakeDir the shelf's missing levels and the author's folder, then a Move per
// book (whole). Two books to one place: the second refused.
Made make_books(const std::string& home, const templates::Template& t, const std::string& author,
                const std::vector<Book>& books);

// ── s024c: bookbinding authors (Scott: "just select the author and say
// bookbinding projects"; "if it finds loose files it goes to the closet") ──
// An author's folder: its name is the author, each folder in it a book. All
// move whole onto the shelf; optionally each book is laid out there too (its
// routes; what no route fits stays at the book's top for Layout...). What is
// left in the old author folder (loose files, a bundle, a repo) stays -- the
// folder becomes a catch-all so the Closet lists it; left empty, it goes to
// the Trash.

// "Keene, Carolyn" stays; "Carolyn Keene" -> "Keene, Carolyn"; one word stays.
std::string author_name(const std::string& folder_name);

struct Author {
    std::string dir;       // absolute: the author's folder as it is now
    std::string name;      // "Keene, Carolyn"
};
// "" = it may be done, else why not: not a folder, on the shelf already, a
// book / -project or inside one, protected, hidden, ~/Archive.
std::string author_refusal(const std::string& home, const templates::Template& t, const Author& a);

struct Authors {
    latch::door::Plan plan;
    std::size_t authors = 0, books = 0, laid_out = 0;   // laid_out = layout moves
    std::vector<std::string> refused;                    // "<folder> -- why"
    std::vector<std::string> closet;                     // rel to home: old author folders with loose things left -> catch-alls
    std::vector<std::string> trashed;                    // rel to home: old author folders left empty -> the Trash
};
Authors plan_authors(const std::string& home, const templates::Template& t, const std::vector<Author>& authors, bool lay_out);

}  // namespace tidy::core::layout
