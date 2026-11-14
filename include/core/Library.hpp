#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::library -- s030m the old EBooks library (Documents/EBooks) onto the
// shelf (Scott, 14:42 "d) organize it pls"; s028: the shelf is
// ~/Entertainment/EBooks/<Genre>/<Last, First>/<Title>).
//
//   Documents/EBooks/<Genre>/<Author>/<Title>/  -> Entertainment/EBooks/<Genre>/<Last, First>/<Title>/   whole
//   Documents/EBooks/<Genre>/<Author or Title>/ holding ebook files right in it
//                                               -> each file by its own title + author (core::ebooks)
//
// Only titles that hold an ebook (.epub .mobi .azw .fb2 .djvu .pdf ...); never
// an old bookbinding ("maybe an old bookbinding" -- Start's Projects step has
// those), a -project, a folder with no ebook in it (Comic Book Stuff), Hands off.
// Genres: "Nonfiction" / "Non-Fiction" -> Non-Fiction. Folders left empty ->
// the Trash (never Documents/EBooks itself). Start's Sets step does it with
// the comics, movies and series: one preview, one undo.
//
// GTK-free. READ-ONLY (plans only).
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Ebooks.hpp"
#include "core/Recognise.hpp"

#include <latch/Door.hpp>

#include <string>
#include <vector>

namespace tidy::core::library {

std::string genre_of(const std::string& folder);   // "Nonfiction" -> "Non-Fiction"; "Fiction" -> "Fiction"

struct Item {
    std::string from;     // absolute: a title folder (whole) or an ebook file
    std::string to;       // relative to home: the folder it goes AS (whole) / INTO (file)
    bool whole = false;
};
// From the whole-home report's library books (How::Book).
// s030n: `ask` (optional) gets the ebooks found with no author -- asked one by one.
std::vector<Item> find(const std::string& home, const recognise::Report& r, std::vector<ebooks::Row>* ask = nullptr);

struct Planned {
    latch::door::Plan plan;
    std::vector<std::string> lines, renamed, trashed;
};
Planned shelve(const std::string& home, const std::vector<Item>& items);

}  // namespace tidy::core::library
