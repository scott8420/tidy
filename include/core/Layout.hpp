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

}  // namespace tidy::core::layout
