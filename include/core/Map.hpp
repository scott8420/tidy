#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::map -- s023f the home map (Scott, 2026-11-11: "a visual chart of the
// home folder to see where there are loose files or untracked files/folders").
// GTK-free and READ-ONLY: names, stat and one xattr (the id) per file.
//
// Every file in home is one of:
//   Cataloged  carries a Tidy id (adopted, sorted, archived, given one)
//   Loose      no id, where Sort looks (a landing place, a top folder's top,
//              home's top, a catch-all)
//   Untracked  no id, in a subfolder Sort does not look at -- Adopt's work
//   Whole      inside a <name>-project or a git repo (kept whole; .git and
//              hidden files not counted)
//   Archived   in ~/Archive
// Hands off, Samson's places and latch's never-touch list are NOT entered: a
// box of their own, weight 1, said ("not looked inside"). Hidden entries and
// symlinks are skipped, as everywhere in Tidy.
//
// squarify(): the treemap layout (Bruls, Huizing, van Wijk) -- pure, tested.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Settings.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace tidy::core::map {

enum class Kind { Cataloged = 0, Loose, Untracked, Whole, Archived };
inline constexpr std::size_t kKinds = 5;
const char* kind_title(Kind k);   // "cataloged", "loose", ...

struct Counts {
    std::array<std::size_t, kKinds> n{};
    std::size_t total() const;
    std::size_t operator[](Kind k) const { return n[static_cast<std::size_t>(k)]; }
    void add(Kind k, std::size_t by = 1) { n[static_cast<std::size_t>(k)] += by; }
    void add(const Counts& o);
};

struct Node {
    std::string name;        // "Documents"; home itself: "~"
    std::string rel;         // relative to home; "" = home
    Counts c;                // everything below (files directly here included)
    Counts here;             // the files directly in this folder
    bool shut = false;       // Hands off / Samson / never-touch: not looked inside
    std::string what;        // "a -project", "a git repo", "Hands off", "~/Archive", "a catch-all", ""
    std::vector<Node> kids;  // sub-folders, biggest first
    std::size_t weight() const { return shut ? 1 : c.total(); }
};

// The whole home. `catch_all` as Settings has it (relative to home).
Node build(const std::string& home, const std::string& user_dirs, const std::vector<std::string>& catch_all);

// The folder at `rel` inside the tree (nullptr if not there).
const Node* find(const Node& root, const std::string& rel);

// One line: "214 files: 12 cataloged, 2 loose, 200 untracked".
std::string line(const Counts& c);

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
};
// Rects for `sizes` (each > 0, any order) filling `area`, in the same order.
std::vector<Rect> squarify(const std::vector<double>& sizes, Rect area);

}  // namespace tidy::core::map
