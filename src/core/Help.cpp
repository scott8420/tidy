#include "core/Help.hpp"
#include "core/Retention.hpp"

namespace tidy::core::help {

const std::vector<Section>& sections() {
    static const std::vector<Section> s = {
        {"1. Set up once",
         "Tidy proposes; nothing moves until you see a preview and press Do it. Every Do it can be undone in Activity.",
         {
             {"Main menu -> Settings... -> Backup media -> Add folder...: a folder on the big SSD, and one on the Pi's "
              "mount (make a Tidy folder there first). Save.", ""},
             {"Main menu -> Background scan... -> Turn on: Tidy looks every N minutes and notifies you at N files.", ""},
             {"The tile: ./install.sh, gnome-extensions enable tidy@scott8420.github.io, then log out and in. Quick "
              "Settings shows what needs attention, Scan now, New project.", ""},
             {"Templates: lift a project you like into a template (its folders), so New project can build more.",
              "templates"},
         }},
        {"2. Day to day",
         "New files land in Desktop, Downloads, Documents... Tidy calls those undecided.",
         {
             {"The Closet -> Look: decide each new file's level (below), or the Junk Drawer.", "closet"},
             {"Purposes: tell Tidy what a file is for (\"Taxes 2025\", \"Sunrise book\"). Check several, Set, Assign.",
              "purposes"},
             {"Files home: send decided files to their folder by type, or into the project they serve.", "home"},
             {"Projects: New project from a template; place loose -project folders where their kind lives.",
              "projects"},
         }},
        {"3. When Tidy says something needs attention",
         "The notification, the tile, or the Due page itself.",
         {
             {"Due -> Look.", "due"},
             {"To archive: Historical files long unread -> check -> Archive checked... -> Do it. They move to "
              "~/Archive/<year>/<their folder> and their fingerprint (SHA-256) is kept.", "due"},
             {"To settle: an Until-done file whose purpose closed -- reopen the purpose, or decide the file again.",
              "purposes"},
             {"To remove: check what may go -> Remove checked... -> Do it. To the Trash, never deleted.", "due"},
         }},
        {"4. The archive cycle",
         "Nothing leaves ~/Archive without proof it is on a disk.",
         {
             {"Archive -> Look: each archived file, and where it is proved.", "archive"},
             {"Plug in / mount the disk. Back up to [Big SSD] -> Back up... -> Do it. Each copy is read back from the "
              "disk and compared; a match is a proof (\"on Big SSD 2026-11-10\").", "archive"},
             {"Again for the Pi. Degoo stays by hand, through Samson -- it is never proof.", "archive"},
             {"Remove proved... (or Due's To remove): the copies are read back again, now; only files proved on enough "
              "disks (Settings) go to the Trash. Unplugged disk -> the file stays.", "archive"},
         }},
        {"5. The way back",
         "",
         {
             {"Activity: every batch Tidy did, newest first. Undo takes back the newest -- files back where they were, "
              "tags as they were, copies into the disk's own trash.", "activity"},
         }},
    };
    return s;
}

std::vector<std::pair<std::string, std::string>> levels() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& i : retention::all()) out.emplace_back(i.title, i.meaning);
    return out;
}

}  // namespace tidy::core::help
