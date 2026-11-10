#pragma once
// core::closet -- the Closet: a way station until determination (REQUIREMENTS
// §4, s006). GTK-free. A module never acts: view() reads a birth report, and
// every other function builds a door::Plan the UI previews and the door runs.
//
// §11.1 lean taken: the Closet is the landing places AS SEEN (s005's report) --
// nobody changes how they save, no ~/Closet folder.
//
//   Holds only the undecided. A file carrying a decided retention never visits.
//   It leaves by:
//     * a proposal accepted  -- its birth rule's level (s005); the JUNK LIST
//                               (s006a: the user's patterns -> the Junk Drawer,
//                               winning over the birth rules); or AGING OUT:
//                               undecided, born and unread aging_days (a
//                               setting) -> Ephemeral (incidental -> the decay ladder);
//     * the user deciding    -- any level, by hand;
//     * the Junk Drawer      -- decided Keep, no home (role "junk-drawer").
//   (Gaining a purpose is s009's way out.)
//
// Every plan is Tag actions only: the Closet never moves, copies or trashes.
#include "core/Birth.hpp"
#include "core/Retention.hpp"
#include "core/Settings.hpp"

#include <latch/Door.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::closet {

inline constexpr const char* kJunkDrawer = "junk-drawer";   // user.tidy.role, with retention keep
inline constexpr int kAgingDays = settings::kAgingDefault;  // the default; Settings sets it

struct View {
    std::vector<birth::Born> proposed;   // a level to accept (birth rule, or aging out)
    std::vector<birth::Born> waiting;    // undecided, nothing to propose yet
    std::size_t decided = 0;             // carry a decided level -- never visit
    std::size_t junk = 0;                // of those, in the Junk Drawer
    std::size_t total() const { return proposed.size() + waiting.size(); }
};

// Days since t (0 if t is unknown or in the future).
int days_since(std::int64_t t, std::int64_t now);
// Undecided, and both born (btime, else mtime) and last read kAgingDays ago.
bool aging(const facts::Facts& f, std::int64_t now, int days = kAgingDays);

View view(const birth::Report& r, std::int64_t now, const settings::Settings& s = {});

namespace door = latch::door;
// Each file to its own proposed level (one batch, grouped by level; a junk-list
// proposal goes to the Junk Drawer: Keep + role junk-drawer). A waiting
// file in the list is stamped Undecided: seen, given its id, still waiting.
door::Plan accept(const std::vector<birth::Born>& items);
// Each file to `level`, by hand.
door::Plan decide(const std::vector<birth::Born>& items, retention::Level level);
// Each file to the Junk Drawer: Keep, role junk-drawer.
door::Plan junk_drawer(const std::vector<birth::Born>& items);

}  // namespace tidy::core::closet
