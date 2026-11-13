#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::ebooks -- s028 ebooks from files (REBUILD 7, meaning from inside the
// file). GTK-free and READ-ONLY until the door runs the plan.
//
// Loose ebook files become books on a shelf, one title folder each:
//   ~/Entertainment/EBooks/<Genre>/<Last, First>/<Title>/<the file>
// The shelf is a TYPE Tidy makes and saves itself, one per genre (Scott: "b"):
// ebook-fiction = by its place, category Entertainment/EBooks/Fiction, group
// "Author (Last, First)", no sub-folders, the ebook kinds -> (the top). Saved,
// it is in Types like any other: reused, edited, seen by Projects / Organize.
//
// Author + title, best first: EPUB metadata (the OPF's dc:creator -- its
// file-as "Keene, Carolyn" when there is one -- and dc:title), a PDF's Info
// /Author /Title, else the file's name ("Title - Author", or "Last, First -
// Title"). Each row says where its guess came from; every guess is editable.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Homing.hpp"
#include "core/Templates.hpp"

#include <string>
#include <vector>

namespace tidy::core::ebooks {

inline constexpr const char* kRoot = "Entertainment/EBooks";
inline constexpr const char* kGroup = "Author (Last, First)";

// .epub .mobi .azw .azw3 .fb2 .djvu .cbz .cbr .cb7 .pdf (lower-case, the dot).
bool is_ebook(const std::string& file_name);

struct Meta {
    std::string title, author;   // author as found ("Carolyn Keene" or "Keene, Carolyn")
    std::string from;            // "epub", "pdf", "the name", or "" (nothing found)
};
// Read a file's own title + author (READ-ONLY: open + pread). Falls back to the name.
Meta read(const std::string& path);
// The name alone: "The Hidden Staircase - Carolyn Keene.epub", "Keene, Carolyn -
// The Hidden Staircase.pdf", "Hidden_Staircase.epub" (title only).
Meta from_name(const std::string& file_name);
// "Carolyn Keene" -> "Keene, Carolyn"; "A. E. Van Vogt" -> "Van Vogt, A. E.";
// "Martin Luther King Jr." -> "King, Martin Luther, Jr."; "Keene, Carolyn" and
// a single name stay as they are. "" -> "".
std::string last_first(const std::string& author);
// A folder name from a title / an author: '/' ':' and control characters
// made safe, spaces trimmed, no leading dot; "" stays "".
std::string folder_name(const std::string& s);

// The type for a genre: "Fiction" -> ebook-fiction (Entertainment/EBooks/Fiction).
std::string type_name(const std::string& genre);
templates::Template type_for(const std::string& genre);

// One loose ebook, as planned.
struct Row {
    std::string path;            // absolute
    std::string genre = "Fiction", author, title;   // author "Last, First"
    std::string from;            // where the guess came from
    bool pick = true;            // a .pdf starts unpicked (it may not be a book)
};
// The loose ebooks under `dir` (abs, in `home`): hidden, symlinks, Hands off /
// never-touch, bundles, projects (a book already on its shelf too) skipped.
std::vector<Row> find(const std::string& home, const std::string& dir, const std::string& genre = "Fiction");

// Where a row goes, rel to home: Entertainment/EBooks/Fiction/Keene, Carolyn/The Hidden Staircase.
std::string target_of(const Row& r);
// "" = it can go, else why not (no author, no title, no genre).
std::string row_problem(const Row& r);
// The picked rows -> MakeDirs + Moves (homing::plan; a taken name gets "(2)").
homing::Planned plan(const std::string& home, const std::vector<Row>& rows);

}  // namespace tidy::core::ebooks
