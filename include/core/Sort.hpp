#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::sort -- s020 the one-step Sort (Scott, 2026-11-11: "I thought tidy
// would sort/move and organize all the files into the home folder by logic").
//
// Every loose file the sweep finds gets a LEVEL and a PLACE in one pass:
//   * decided already -> its own level; undecided -> its proposal (birth rule,
//     age, the junk list), or Keep when nothing is proposed;
//   * Keep / Until done / Held -> home BY TYPE (Settings: the type table; no
//     type -> Documents) into a DATED subfolder named for where it came from:
//     ~/Pictures/Desktop 2026-11 (Scott chose the dated default, s020);
//   * Historical -> ~/Archive/<year>/<its folder> (core::archive, fingerprinted);
//   * Ephemeral -> stays (tagged); Due removes it when its days are up;
//   * the junk list -> the Junk Drawer: stays (tagged Keep, role junk-drawer);
//   * Hands off, or already in the Junk Drawer -> left out.
// One door plan: the tags first (on the files where they are), then the new
// folders and moves, then the archive. One undo step in Activity.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Birth.hpp"
#include "core/Homing.hpp"
#include "core/Retention.hpp"
#include "core/Settings.hpp"

#include <latch/Door.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tidy::core::sort {

enum class Go { Home, Archive, Stays };

struct Row {
    birth::Born      born;
    retention::Level level = retention::Level::Keep;   // what it will carry
    bool             tag = false;                      // undecided now: the plan tags it
    bool             junk = false;                     // to the Junk Drawer (role junk-drawer)
    Go               go = Go::Home;
    std::string      folder;                           // Home: relative to home; Archive: "Archive/2026/..."
    std::string      why;                              // one line: "by type *.png -> Pictures, from the Desktop"
};

// "<where it came from> YYYY-MM": "Desktop 2026-11", "Downloads 2026-11",
// "Home 2026-11"; a file already at its type folder's top -> "Loose 2026-11".
std::string dated(const birth::Born& b, const std::string& type_folder, std::int64_t now);

// One row per loose file the sweep found (none for Hands off / the Junk Drawer).
std::vector<Row> rows(const std::string& home, const birth::Report& rep, const settings::Settings& st,
                      std::int64_t now);

struct Planned {
    latch::door::Plan plan;
    std::vector<std::string> renamed;                              // warned in the preview
    std::vector<std::pair<std::string, std::string>> refused;      // (path, why) -- not in the plan
    std::size_t archiving = 0;                                     // files going to ~/Archive
};
Planned plan(const std::string& home, const std::vector<Row>& chosen, std::int64_t now);

}  // namespace tidy::core::sort
