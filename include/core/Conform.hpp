#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::conform -- s023h (Scott, 2026-11-11: "Let's say I have 30 bookbinding
// projects... not organized but cluttered. I scan or create a template. To say
// these files go in here and those go there. These archived pngs stay in the
// Archive folder but there is now an included optik project and it stays in
// the main area." -- gtg on: write the rules once, apply to all 30, one
// preview, one Do it).
//
// A template's routes, top to bottom, the first that fits deciding, put every
// file of a project folder in its place:
//   "*.odt -> Originals"          a file named so -> Originals/
//   "Archive/*.png -> (stays)"     a .png anywhere under Archive/ stays put
//   "*.optik -> (top)"             -> the project's top
// A file already under its target folder (at any depth) is in place -- never
// flattened. A file no route fits stays, and is LISTED (add a route, look
// again). Nothing ever leaves its project. Hidden entries and symlinks are
// skipped; a package (.scriv, an Optik bundle) is one item; a nested
// -project, a git repo or anything protected is left alone, whole.
//
// READ-ONLY: look() reads names; plan() builds the door's steps (MakeDir for
// missing folders, Move per file -- core::homing's plan, so a taken name gets
// "(2)" and nothing is overwritten); optionally each folder renamed to
// "<name>-project" LAST (after the files inside have moved).
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Templates.hpp"

#include <latch/Door.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace tidy::core::conform {

struct Move {
    std::string from;     // absolute
    std::string folder;   // the target, relative to the project ("" = its top)
    std::string why;      // "*.odt -> Originals"
};
struct Project {
    std::string dir;                      // absolute
    std::vector<Move> moves;
    std::vector<std::string> unmatched;   // relative to the project: no route fits -- stays
    std::size_t in_place = 0;             // already under their target
    std::size_t stays = 0;                // a (stays) route
    std::size_t whole = 0;                // nested projects / repos / protected, left whole
};

// The project folders to conform: `dir` itself, or (each_sub) each folder in
// it -- not hidden, not a symlink, not protected, not a git repo. READ-ONLY.
std::vector<std::string> folders(const std::string& dir, bool each_sub);

// Why `dir` can't be conformed ("" = it can): outside home, home itself,
// ~/Archive, protected, a git repo.
std::string refusal(const std::string& home, const std::string& dir);

Project look(const templates::Template& t, const std::string& dir);

struct Planned {
    latch::door::Plan plan;
    std::vector<std::string> renamed;     // names taken where they go -> "(2)"
    std::vector<std::string> not_renamed; // "-project" already, or the new name is taken
};
Planned plan(const std::string& home, const std::vector<Project>& projects, bool rename_to_project);

}  // namespace tidy::core::conform
