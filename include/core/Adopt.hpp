#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::adopt -- s023 (Scott, 2026-11-11: "I meant tidy to help me organize and
// catalog my loose files and see them moved about or set for archive"; on
// adopt + a Catalog page: "It would make for a quicker process").
//
// You pick folders you have already organized by hand. Two ways to adopt one:
//
//   in place   every file in it is CATALOGED WHERE IT IS -- an id (if it has
//              none) and a level (if it has none: Keep, or Historical when
//              untouched for Settings' "old" years, so Due offers to archive
//              it). Nothing moves. Tidy also LEARNS from what is there: a name
//              word that keeps landing in one folder (cover*.png, 2 or more,
//              and at least two thirds of them in that folder) becomes a
//              learned route (s022) -- Files home and Sort propose it next time.
//   catch-all  a folder of leftovers (Documents/Misc): nothing is tagged now;
//              its files, at any depth, are offered to Sort / Files home / the
//              Closet like loose ones (birth::sweep's "catchall" places).
//
// Never: home itself, a hidden folder, ~/Archive, a <name>-project or inside
// one, a git repo or inside one, Hands off / Samson / latch's never-touch list.
// In place never adopts a landing place (Downloads, the Desktop, Screenshots --
// Sort's), and inside a picked folder the LOOSE files Sort owns (the top of
// Documents, of a top folder like ~/Writing) are left to Sort, said.
//
// READ-ONLY. look() reads; plan() builds the door's Tag steps (one batch, one
// undo in Activity); learn() changes a routes book in memory -- core::own
// writes it, and settings.ini remembers the picked folders.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Facts.hpp"
#include "core/Retention.hpp"
#include "core/Routes.hpp"

#include <latch/Door.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::adopt {

inline constexpr int kRouteMin = 2;   // a name word seen this often in one folder is a route

// Why `rel` (relative to home) cannot be adopted this way; "" = it can.
std::string refusal(const std::string& home, const std::string& user_dirs, const std::string& rel, bool catch_all);

struct Item {
    facts::Facts     file;
    retention::Level level = retention::Level::Keep;   // what it will carry
    bool             give_id = false;                  // no id yet
    bool             set_level = false;                // no level yet
    std::string      why;                              // "Keep -- where you put it" / "Historical -- untouched 3 years"
};
struct Learned {
    std::string pattern;   // "cover*.png"
    std::string folder;    // relative to home: "Pictures/Covers"
    int count = 0;         // how many files there fit it
};
struct Look {
    std::string rel;
    bool catch_all = false;
    std::vector<Item> items;           // in place: files that gain an id or a level
    std::size_t known = 0;             // in place: files already carrying both
    std::size_t loose = 0;             // left to Sort (a landing place's / top folder's loose top)
    std::size_t offered = 0;           // catch-all: files Sort will now see
    std::vector<Learned> routes;       // in place: what it teaches
    std::size_t count(retention::Level l) const;
};

Look look(const std::string& home, const std::string& user_dirs, const std::string& rel, bool catch_all,
          std::int64_t now, int old_years);

// The tags for every look, one plan: an id first, then the level, per file.
latch::door::Plan plan(const std::vector<Look>& looks);

// Each learned route into the book as a NAME route for its folder: yes = how
// many files showed it (never lowered; adopting twice does not double it).
// Returns how many routes were added or raised.
std::size_t learn(routes::Book& book, const std::vector<Look>& looks);

}  // namespace tidy::core::adopt
