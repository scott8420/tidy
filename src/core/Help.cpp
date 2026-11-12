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
          "the type table; no type -> Documents) where you sent the same kind before (a learned route: "
          "cover*.png -> ~/Pictures/Covers, or *.png from the Desktop -> wherever those went), else into a dated "
          "folder named for where they came from: "
          "~/Pictures/Desktop 2026-11, ~/Documents/Downloads 2026-11, ~/Pictures/Loose 2026-11 for one already "
          "loose in Pictures. Old records (Historical) go to ~/Archive/<year>, fingerprinted. Ephemeral files and "
          "your junk list stay where they are, labelled.",
          "Downloads: everything there arrives Ephemeral. Once you have opened it, Sort moves it on: a movie to "
          "~/Entertainment/Movies, an episode (Mobland_s01e02) to ~/Entertainment/Series/Mobland/Season 01, a book "
          "(.epub .mobi .azw3 .cbz .cbr .pdf) to ~/Entertainment/EBooks, music to ~/Entertainment/Music -- all of it "
          "yours to change in Settings (the Entertainment table); a folder of them (a comic series and its "
          "sub-folders) moves whole. Not read yet but you want it moved: select it -> Move on now (it becomes Keep). "
          "Files home lists Downloads too: send one into a folder or a project and it becomes Keep. Anything "
          "else home by type. Unread 30 days (Settings), Due proposes the Trash.",
          "A folder in Downloads or on the Desktop moves whole, never split -- by most of its files (an episode "
          "folder into its season). A <name>-project goes to the category whose template its folders match; with "
          "no match it is listed, and you pick its category on the Projects page.",
          "Hands off, hidden files, your Hands-off folders, Samson's folders, projects and repos are never touched. "
          "A name already taken in a folder gets \"(2)\" -- nothing is overwritten.",
          "Folders you organized by hand: Adopt folders... (on Sort's start, or the main menu). In place, every "
          "file in one is cataloged where it is -- an id, and Keep (Historical when untouched for years) -- and "
          "Tidy learns its routes from what is there. Mark a folder of leftovers Catch-all instead, and Sort, "
          "Files home and the Closet see its files, at any depth, like loose ones."},
         {"Sort -> Look. Everything comes up selected, grouped by where it goes.",
          "Unselect what is wrong (Ctrl+click), or select only some.",
          "Sort... -> the preview: every label, new folder and move.",
          "Do it. The whole sort is ONE step in Activity -> Undo puts it all back.",
          "Then Archive -> Back up, for what went to ~/Archive."},
         "sort"},
        {"map", "Every day", "Map -- your home at a glance",
         {"A treemap of the home: each box is a folder, as big as the number of files in it. Its colours say what "
          "Tidy knows: green cataloged (it carries an id), amber loose (Sort will see it), red untracked (in a "
          "subfolder nobody has adopted), grey inside a -project or a repo (kept whole), blue in ~/Archive. A dark "
          "box is Hands off, Samson's or never-touch -- not looked inside.",
          "Red is the work left: adopt the folder in place if it is already where it belongs, or make it a "
          "catch-all so Sort, Files home and the Closet see its files. Look reads only."},
         {"Map -> Look.",
          "Click a box to go into it; Up (or Backspace) comes back.",
          "Hover a box for its counts. Right-click it: Adopt in place..., Make it a catch-all..., Conform to a template..., Open in Files.",
          "Look again after you adopt or sort to see the red shrink."},
         "map"},
        {"home", "Every day", "Files home",
         {"Sends decided files to their folder: by type (pictures to Pictures, from the type table in Settings), into "
          "a folder at home's top you name (a subfolder of it, never loose), into the project they serve, or into the "
          "archive (~/Archive/<year>, fingerprinted -- as Due archives). A name already taken becomes \"name (2)\" -- nothing is overwritten.",
          "Tidy remembers each send: the file's leading word (cover-final.png -> cover*.png) and its type + where it "
          "sat (*.png from the Desktop) -> the folder. Next time a file of that kind shows \"-> ~/Pictures/Covers\", "
          "already selected; Set to -> Its proposal sends each to its learned home. Sending one elsewhere teaches "
          "that too -- a route you override often enough gives way. Sort uses the same routes."},
         {"Files home -> Look.",
          "Select the files that go to one place (click, Shift, Ctrl, Ctrl+A, or drag a box).",
          "Folder: by type, the subfolder (required -- there: lists the ones already made); into a project, a folder "
          "in it, or leave it empty for the template's route.",
          "Set to -> By type, Into ~/<a folder at home's top> (Photos, Documents ...), Archive, or Into <project> "
          "(or right-click the selection).",
          "Preview -> Do it. Files going elsewhere: select them and Set to again.",
          "Files with a learned home come up selected: Set to -> Its proposal."},
         "home"},

        // ── Projects ──────────────────────────────────────────────────────────
        {"projects", "Projects", "Projects",
         {"A folder named something-project is a project: it moves whole, never file by file. Tidy finds them, "
          "gives each an id, follows them when they move, and keeps their status (active, paused, done, dropped).",
          "Some projects are known by their PLACE instead of a name: a book is any folder at "
          "~/Documents/Bookbinding/<Last, First>/<Title> -- no -project needed. It is kept whole the same way."},
         {"Projects -> Look: what is where it belongs, and what is loose.", "Place a loose project where its kind lives.",
          "New project: pick a template, a name -> preview -> Do it.",
          "A new book: New project -> bookbinding -> the author (Keene, Carolyn) and the title -> preview -> Do it. "
          "Body and Cover come with every stage folder, empty until you fill them.",
          "An old book: Projects -> its row -> Layout... -> each file's folder proposed (scans to Body/OCR/Scans/Archive, "
          "a name with cover or jacket to the Cover) -- change any; what no route fits is asked first and stays until "
          "you pick -> Do it. Missing stage folders are made, folders left empty go to the Trash; one undo in Activity.",
          "A book somewhere else (~/Documents/Nancy Drew/02 The Hidden Staircase): Projects -> Make books... (or the Map: "
          "right-click -> Make it a book... / Its sub-folders are books...) -> the author once, each title guessed (the "
          "number goes) -> Do it: each folder moves WHOLE onto the shelf. Then Layout... on each.",
          "Whole authors at once: the Map -> right-click an author's folder -> Bookbinding author... (or a folder of "
          "authors -> Its sub-folders are authors...), or Projects -> Books -> Bookbinding authors... Each folder in an "
          "author's folder is a book; \"Carolyn Keene\" is offered as \"Keene, Carolyn\". Lay out each book too (on): "
          "its files by the routes. Loose files left in the old author folder go to the Closet; an empty one, the Trash."},
         "projects"},
        {"templates", "Projects", "Templates",
         {"A template is the shape of a project: its folders, the whole tree (six deep at most). Lift one from a "
          "project you like; New project builds the same tree. Drift shows where a project has wandered from its template.",
          "The builder shows the template as a tree, like Files: each folder with what goes in it (\u2190 *.odt). "
          "Right-click a folder (or its \u22ef): New folder, Rename, This goes here... (a pattern, or a sample name: "
          "A0001.png means the numbered run), Remove. Drag a rule to another folder. Drop a file from Files on a folder: "
          "its type goes there. The particular rules always fit first (*cover*.png before *.png).",
          "\"Known by its place\" (the builder's check): its projects are the folders under ~/<category>/<group>/ -- "
          "no -project. The built-in bookbinding is one: Body (OCR, Layout, Print, Finals) and Cover (Layout, Print).",
          "Its routes say where files go: \"*.odt -> Originals\". Top to bottom, the first that fits decides. A route may "
          "name a folder first (\"Archive/*.png\" -- a .png anywhere under Archive) and may say \"stays where it is\" "
          "or \"the project's top\" (\"*.optik\"). Conform folders... puts the files of many project folders -- your "
          "30 books in ~/Documents/EBooks -- where the routes say, all at once: one preview, one undo. A file no route "
          "fits stays and is listed; add a route and Look again."},
         {"Templates -> From a scan... (or the Map: right-click a folder -> Make a template from a scan...): a project "
          "folder, or a folder of many -> every file type found, with counts; a numbered run (A0001.png ..) has a line "
          "of its own.",
          "Tick the folders to keep; add new ones (Originals, Workups/Images).",
          "Give each file type a folder. Tick Source on the types that build the others (.optik): they stay at the top.",
          "Name it; say where projects of this kind live. Save and conform -> Conform's preview -> Do it.",
          "Or: Lift from a project, add routes by hand, Save; Conform folders... -> Look -> Do it."},
         "templates"},
        {"code", "Projects", "Code projects",
         {"A code project's source lives on its remote (GitHub). What is worth keeping in the archive is the "
          "project folder: docs, scripts and the committed source -- not .git, not build folders. Done is your "
          "call; Tidy archives a project only when everything in it is pushed and committed.",
          "Archive done... makes a COPY in ~/Archive/<year>/<its folder>/ with RESTORE.txt (the remote, branch, last "
          "commit and the clone line); backups/ and backups/Old/ come empty, fixes/ is yours and not made. The live "
          "folder goes to the Trash only after Back up, when every archived file reads back the same from your "
          "disks -- and only if nothing changed since."},
         {"Projects -> Code projects...: each one, pushed or not, its size and how much is build and .git.",
          "Select -> Set to Done.", "Archive done... -> preview -> Do it.",
          "Archive -> Back up... -> Do it.", "Code projects -> select -> Trash archived... -> Do it.",
          "Clean build folders... frees the build folders of any selected project (the build makes them again)."},
         "projects"},

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
