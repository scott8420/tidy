#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::comics -- s030e the Comics shelf (Scott, 2026-11-13 17:12: "I just made
// a cbz file and I want it to go in with the other Flash Gordon cbz files and
// the app has no way of handling a stray file").
//
//   ~/Entertainment/Comics/<Series>/<the file, its own name>
//
// * A comic is .cbz .cbr .cb7 .cbt -- no longer an ebook (s028 shelved them as
//   books, named from their file names: "1934), Monsters Of Mongo (15 Apr ...").
// * The SERIES from a file's name: brackets, site names and years out; cut at
//   the issue ("vol 2", "S045", "06", "#12", " - "); "Alex Raymond's Flash
//   Gordon" -> "Flash Gordon"; a series already on the shelf wins; files with no
//   issue sharing 2+ leading words ("Donald Duck Trick or Treat", "Donald Duck
//   Lost in the Andes") are one series ("Donald Duck").
// * Re-shelve: every comic off the shelf (Entertainment, the EBooks library, the
//   Desktop) -> its series folder; folders left empty -> the Trash. One plan,
//   one undo.
// * A stray (Sort, Loose files, a read download) joins its series the same way.
//
// GTK-free. READ-ONLY (plans only).
// ─────────────────────────────────────────────────────────────────────────────
#include <latch/Door.hpp>

#include <string>
#include <vector>

namespace tidy::core::comics {

inline constexpr const char* kShelf = "Entertainment/Comics";

bool is_comic(const std::string& file_name);
// The name with its noise out: "Alex Raymond's Flash Gordon vol 2 Sundailies 82"
// stays as is; "Flash Gordon S002 (Kitchen Sink) - libgen.li" -> "Flash Gordon S002".
std::string clean(const std::string& file_name);
// The series: a `known` one the name holds (longest first), else the cleaned
// name cut at its issue. `cut` (optional): true when an issue was found.
std::string series_of(const std::string& file_name, const std::vector<std::string>& known, bool* cut = nullptr);
// The series folders on the shelf now (READ-ONLY).
std::vector<std::string> shelf(const std::string& home);

struct Item {
    std::string path;      // absolute
    std::string series;    // its folder under the shelf
};
// Comics off the shelf, each with its series (siblings with no issue joined by
// their shared leading words). Looks in ~/Entertainment (not the shelf), the
// EBooks library (Documents/EBooks) and the Desktop; never hidden, a bundle, a
// -project, Documents/Bookbinding, Hands off. READ-ONLY.
std::vector<Item> find(const std::string& home);
// Series for a set of names together (the grouping find uses; pure).
std::vector<std::string> series_all(const std::vector<std::string>& names, const std::vector<std::string>& known);

struct Planned {
    latch::door::Plan plan;
    std::vector<std::string> renamed;
    std::vector<std::string> trashed;   // folders left empty, rel to home
};
// Re-shelve: the moves (MakeDirs first), then each folder the moves leave empty
// -> the Trash (deepest first; never Entertainment, the library, a genre).
Planned reshelve(const std::string& home, const std::vector<Item>& items);

}  // namespace tidy::core::comics
