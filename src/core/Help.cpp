#include "core/Help.hpp"

namespace tidy::core::help {

const std::vector<Topic>& topics() {
    static const std::vector<Topic> t = {
        // ── Start here ────────────────────────────────────────────────────────
        {"intro", "Start here", "Introduction",
         {"Tidy keeps your home folder in order. It notices the files that pile up -- in Downloads, on the "
          "Desktop, loose in Documents -- and helps each one find its place: its folder, its project, the archive, "
          "or the Trash.",
          "Tidy proposes; you decide. Nothing moves until you have seen a preview and pressed Do it, and every "
          "Do it can be taken back in Activity. Tidy never deletes a file -- the farthest anything goes is the Trash.",
          "Each file you decide gets a retention level (how long it matters) and, if you like, a purpose (what it is "
          "for). Tidy keeps these on the file itself, so they travel with it when it moves.",
          "Long-unread files are archived into ~/Archive, backed up to your disks (the SSD, the Pi), proved there by "
          "reading them back, and only then may leave your home."},
         {}, ""},
        {"start", "Start here", "Getting started",
         {"A first hour with Tidy. Each step is its own topic on the left when you want more."},
         {"Settings -> Backup media: add a folder on the big SSD and one on the Pi's mount.",
          "The Closet -> Look: decide a few of the files Tidy found undecided.",
          "Purposes: name what a few files are for.",
          "Files home: send the decided ones to their folders.",
          "Background scan... (main menu): let Tidy look on its own and tell you when things need attention.",
          "Due, then Archive, whenever Tidy says something needs attention."},
         ""},
        {"levels", "Start here", "Retention levels",
         {"Every decided file has one. It says how long the file matters, and so what Tidy will propose for it.",
          "Undecided -- new, in the Closet, waiting for you.",
          "Ephemeral -- matters for days; proposed for the Trash once old and unread (Settings: the aging days).",
          "Until done -- lives while its purpose is open; when the purpose closes, Due asks you to settle it.",
          "Held -- kept for years after it is done (tax papers); proposed for the Trash after Settings' years.",
          "Historical -- worth keeping, not using; archived once long unread and unchanged.",
          "Keep -- active, in place: the product of your work. Tidy leaves it be.",
          "Hands off -- Tidy never touches it."},
         {}, ""},

        // ── Every day ─────────────────────────────────────────────────────────
        {"closet", "Every day", "The Closet",
         {"Where undecided files wait. Look sweeps the whole home for LOOSE files: at home's top, at the top of a "
          "folder there (~/Pictures/x.png), or in a landing place (Downloads, Desktop, Screenshots). A file in a "
          "subfolder (~/Pictures/Covers/x.png) is home already and not shown. Tidy never looks inside hidden "
          "folders, -projects, git repos, ~/Archive, your Hands-off folders (Settings) or Samson's folders.",
          "Tidy reads how each was born (downloaded, a screenshot, saved by an app) and proposes a level. A file "
          "nobody has changed or read in years (Settings, 2) is proposed Historical -- a record, which Due then "
          "archives. Your junk list (Settings) sends matching files to the Junk Drawer: kept, no home."},
         {"The Closet -> Look. Tidy looks through the home (the page stays live); the Proposed files come up selected.",
          "Select like Files: click, Shift+click a range, Ctrl+click one more, Ctrl+A all, or drag a box.",
          "Set to -> Its proposal, a level, or the Junk Drawer (or right-click the selection).",
          "Preview -> Do it.",
          "The first sweep: the old files go Historical here; then Due -> Archive checked (or Files home -> Set to -> "
          "Archive); then Archive -> Back up."},
         "closet"},
        {"purposes", "Every day", "Purposes",
         {"A purpose is what a file is for -- \"Taxes 2025\", \"Sunrise book\". Tidy learns them from you and, in "
          "time, guesses by folder. Closing a purpose tells Tidy its Until-done files are finished."},
         {"Purposes -> add a purpose (or teach a name pattern).",
          "Select the files it covers -- the ones Tidy guesses come up selected.",
          "Set to -> the purpose, or Tidy's guess (or right-click the selection).",
          "Preview -> Do it. Tidy learns from what you chose.", "When the work is finished, Close the purpose."},
         "purposes"},
        {"sort", "Every day", "Sort -- the whole home in one pass",
         {"Sort gives every loose file in the home a level and a place at once. Kept files go home by type (Settings: "
          "the type table; no type -> Documents) into a dated folder named for where they came from: "
          "~/Pictures/Desktop 2026-11, ~/Documents/Downloads 2026-11, ~/Pictures/Loose 2026-11 for one already "
          "loose in Pictures. Old records (Historical) go to ~/Archive/<year>, fingerprinted. Ephemeral files and "
          "your junk list stay where they are, labelled.",
          "Hands off, hidden files, your Hands-off folders, Samson's folders, projects and repos are never touched. "
          "A name already taken in a folder gets \"(2)\" -- nothing is overwritten."},
         {"Sort -> Look. Everything comes up selected, grouped by where it goes.",
          "Unselect what is wrong (Ctrl+click), or select only some.",
          "Sort... -> the preview: every label, new folder and move.",
          "Do it. The whole sort is ONE step in Activity -> Undo puts it all back.",
          "Then Archive -> Back up, for what went to ~/Archive."},
         "sort"},
        {"home", "Every day", "Files home",
         {"Sends decided files to their folder: by type (pictures to Pictures, from the type table in Settings), into "
          "a folder at home's top you name (a subfolder of it, never loose), into the project they serve, or into the "
          "archive (~/Archive/<year>, fingerprinted -- as Due archives). A name already taken becomes \"name (2)\" -- nothing is overwritten."},
         {"Files home -> Look.",
          "Select the files that go to one place (click, Shift, Ctrl, Ctrl+A, or drag a box).",
          "Folder: by type, the subfolder (required -- there: lists the ones already made); into a project, a folder "
          "in it, or leave it empty for the template's route.",
          "Set to -> By type, Into ~/<a folder at home's top> (Photos, Documents ...), Archive, or Into <project> "
          "(or right-click the selection).",
          "Preview -> Do it. Files going elsewhere: select them and Set to again."},
         "home"},

        // ── Projects ──────────────────────────────────────────────────────────
        {"projects", "Projects", "Projects",
         {"A folder named something-project is a project: it moves whole, never file by file. Tidy finds them, "
          "gives each an id, follows them when they move, and keeps their status (active, paused, done, dropped)."},
         {"Projects -> Look: what is where it belongs, and what is loose.", "Place a loose project where its kind lives.",
          "New project: pick a template, a name -> preview -> Do it."},
         "projects"},
        {"templates", "Projects", "Templates",
         {"A template is the shape of a project: its folders. Lift one from a project you like; New project builds "
          "the same tree. Drift shows where a project has wandered from its template."},
         {"Templates -> Lift from a project.", "Keep or remove folders; set where projects of this kind live.", "Save."},
         "templates"},

        // ── When Tidy says so ─────────────────────────────────────────────────
        {"due", "When Tidy says so", "Due",
         {"What needs you now: files to archive (Historical, long unread), to settle (their purpose closed), and to "
          "remove (Ephemeral past its days, Held past its years, or archived and proved on your disks)."},
         {"Due -> Look.", "To archive: check -> Archive checked... -> Do it.",
          "To remove: check what may go -> Remove checked... -> Do it (to the Trash).",
          "To settle: reopen the purpose, or decide the file again."},
         "due"},
        {"background", "When Tidy says so", "Background scan and the tile",
         {"Tidy can look on its own (a timer, every N minutes) and send a notification once N files need attention. "
          "The Quick Settings tile shows the count and offers Scan now and New project."},
         {"Main menu -> Background scan... -> Turn on -> Do it.",
          "The tile, once: ./install.sh, gnome-extensions enable tidy@scott8420.github.io, then log out and in."},
         ""},

        // ── The archive ───────────────────────────────────────────────────────
        {"archive", "The archive", "Archive",
         {"~/Archive keeps what is worth keeping but not using, by year then the file's own folder. When a file is "
          "archived Tidy takes its fingerprint (SHA-256) -- the proof later that a copy is the same bytes."},
         {"Due -> To archive -> Archive checked... -> Do it.", "Archive -> Look: each file and where it is proved."},
         "archive"},
        {"backup", "The archive", "Backing up to a disk",
         {"Tidy copies ~/Archive to the disks you named in Settings -> Backup media, then reads each copy back from "
          "the disk and compares it with the fingerprint. Only a match counts: \"on Big SSD 2026-11-10\". Nothing on "
          "the disk is overwritten. Degoo stays by hand, through Samson -- it is never proof.",
          "A disk shows only after you add it in Settings; mounting it is not enough. Red beside it says why it "
          "can't be used now (not mounted, not writable)."},
         {"Mount the disk.", "Archive -> Back up to [disk] -> Back up... -> Do it.", "Again for each disk."},
         "archive"},
        {"removal", "The archive", "Removing from the archive",
         {"An archived file leaves your home only when enough of its copies (Settings, 1 by default) read back the "
          "same now -- the disk mounted, the copy there, the bytes equal. It goes to the Trash. Unplugged disk, "
          "changed copy, changed file: it stays, and Tidy says why."},
         {"Mount the disks.", "Archive -> Remove proved... (or Due -> To remove) -> preview -> Do it."},
         "archive"},

        // ── Safety ────────────────────────────────────────────────────────────
        {"activity", "Safety", "Activity and Undo",
         {"Every Do it is one batch in Activity, newest first. Undo takes back the newest: files back where they "
          "were, tags as they were, copies on a disk into that disk's own trash."},
         {"Activity -> Undo \"...\"."},
         "activity"},
        {"settings", "Safety", "Settings",
         {"The aging days, the rescan, Due's years and days, the proofs needed before removal, the junk list, the "
          "type table, and your backup media. Main menu -> Settings..."},
         {}, ""},
    };
    return t;
}

const Topic* find(const std::string& id) {
    for (const auto& t : topics()) if (t.id == id) return &t;
    return nullptr;
}

}  // namespace tidy::core::help
