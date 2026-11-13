#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::organize -- s024e (Scott, 2026-11-12: "the app is fracturing into a
// bunch of lost processes. A simple way to select a folder and say organize
// this as a [type], then it tries its best to organize and move to its
// location." / "The organization template must be set first.")
//
// One folder + a type (a template) ->
//   the SHAPE guessed: one project / a folder of projects (a series, an
//   author's books) / a folder of groups (authors, each with books -- only for
//   a by-place type with a group level);
//   the NAMES guessed: a title from each folder (the number goes), the group
//   from the folder ("Last, First"; "First Last" flipped);
//   each project LAID OUT by the type's routes, where it is (core::layout);
//   then MOVED whole to where the type lives: ~/<category>/<group>/<Title>,
//   ~/<category>/<Title>, or ~/<category>/<Title>-project. Already there -> laid out only.
//   What is left in a container (a series' bibliography) -> the Closet (a
//   catch-all, saved by the app); a container left empty -> the Trash.
// One plan, one preview, one undo. READ-ONLY until the door runs it.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Templates.hpp"

#include <latch/Door.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace tidy::core::organize {

enum class Shape { One, Many, Groups };

struct Project {
    std::string dir;          // absolute, where it is now
    std::string title;        // its folder name on arrival (no -project: added for such types)
    bool on = true;           // ticked
};
struct Group {
    std::string dir;          // the container whose leftovers matter; "" for Shape::One
    std::string name;         // the group (the author) -- by-place types with a group level; else ignored
    std::vector<Project> projects;
};

// Does `dir` look like a project of this type? Its files (two levels down,
// bundles as one) that the routes send somewhere, by destination folder.
// "Project-like" = they go to two or more different folders (a book: scans
// AND an .odt) -- a part folder (Archive/: only scans) is not. READ-ONLY.
bool project_like(const templates::Template& t, const std::string& dir);
Shape guess(const templates::Template& t, const std::string& dir);
// The groups and projects for a shape, names guessed. A sub-folder that is
// not project-like starts unticked. READ-ONLY.
std::vector<Group> groups_of(const templates::Template& t, const std::string& dir, Shape s);

// Where a project lands: ~/<category>/[<group>/]<title> by place, else
// ~/<category>/<title>-project.
std::string target_of(const std::string& home, const templates::Template& t, const std::string& group,
                      const std::string& title);
// "" = it may be organized (`in_place` set when it is already at its target), else why not.
std::string refusal(const std::string& home, const templates::Template& t, const std::string& group,
                    const Project& p, bool* in_place = nullptr);

struct Organized {
    latch::door::Plan plan;
    std::size_t projects = 0, moved = 0, in_place = 0, laid_out = 0;
    std::vector<std::string> refused;     // "<folder> -- why"
    std::vector<std::string> closet;      // rel to home: containers with loose things left -> catch-alls
    std::vector<std::string> trashed;     // rel to home: containers left empty -> the Trash
};
Organized plan(const std::string& home, const templates::Template& t, const std::vector<Group>& groups, bool lay_out);

}  // namespace tidy::core::organize
