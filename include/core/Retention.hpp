#pragma once
// core::retention -- the seven retention levels (REQUIREMENTS §2), as data
// (s005). One attribute per file drives everything: `user.tidy.retention`
// holds a level's id. GTK-free.
//
// §11.2 (cut / merge?) is open; s005 builds on the seven as written (the lean).
// The ids are what goes on disk, so they never change once a file carries one:
// rename a title freely, never an id.
#include <string>
#include <vector>

namespace tidy::core::retention {

enum class Level { Undecided, Ephemeral, UntilDone, Held, Historical, Keep, HandsOff };

struct Info {
    Level       level;
    std::string id;        // the xattr value: "undecided", "until-done", ...
    std::string title;     // "Until done"
    std::string meaning;   // one line, REQUIREMENTS §2
};

const std::vector<Info>& all();          // in the order of §2's table
const Info& info(Level l);
const std::string& id_of(Level l);
const std::string& title_of(Level l);
bool parse(const std::string& id, Level& out);   // false: not one of the seven

}  // namespace tidy::core::retention
