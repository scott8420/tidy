#pragma once
// core::filelists -- CVS-style id lists (s010f2). Scott: "all folders could
// have them". Each folder holding a file with an id gets a `.tidy-files`:
// one line per such file -- its id, size, mtime, name. Like CVS/Entries, the
// folder describes itself and travels with it (a stick, a sync, a copy).
//
//   A BACKUP, not the truth. The id is the xattr (user.tidy.id); the catalog
//   is the index. A list is read only to RE-FIND a file whose xattr is gone
//   (FAT / exFAT, some syncs, an app's save-as): same folder, same name, same
//   size -> its old id comes back (a Tag through the door). A stale line
//   costs nothing; the next write replaces it.
//   GTK-free. READ-ONLY here. s010f3: a NEW list is a door Write (undoable);
//   an EXISTING list is rewritten in place by latch's replace_own (Scott: no
//   undo for Tidy's own dot files -- they are rebuilt from the xattrs, like
//   the catalog; a log line instead). Restore = Tag. Hidden, Protected and
//   fixes/ are never walked (core::scan), so they never get a list; projects do.
#include "core/Facts.hpp"

#include <latch/Door.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tidy::core::filelists {

inline constexpr const char* kFile = ".tidy-files";
inline constexpr const char* kHeader = "# Tidy file ids";   // format()'s first line starts with it

// s010f3: register Tidy's own dot files with latch (.tidy-files under
// kHeader, .tidy-project under "id=") -- once at startup, before any
// replace_own. None registered -> latch refuses every replace.
void register_own();

// The home's top folders to walk (not hidden, not symlinks; core::scan then
// refuses what is Protected). Loose files at the home's top get no list.
std::vector<std::string> roots(const std::string& home);

struct Line {
    std::string   id;
    std::uint64_t size = 0;
    std::int64_t  mtime = 0;
    std::string   name;
    bool          kept = false;   // s010f3: carried forward -- its file lost the id and did not match
    bool operator==(const Line&) const = default;
};

// "<id>\t<size>\t<mtime>\t<name>" per line under a "# " header, sorted by
// name. A name with a tab or newline is left out (it cannot round-trip).
std::string format(std::vector<Line> lines);
std::vector<Line> parse(const std::string& text);         // header, junk and bad ids skipped

// What each folder's list should say: the files and packages in `files` that
// carry an id, grouped by their folder. Folders with none are absent.
// s010f3 (the s010f2 bug): a folder's OLD line whose file is still there
// but lost its id and did not match (its size changed) is CARRIED FORWARD
// (kept = true) -- unless that id is on another file now. Without this the
// next rewrite dropped the line and the id was gone for good. Reads the old
// list only of folders holding a file with no id.
std::map<std::string, std::vector<Line>> wanted(const std::vector<facts::Facts>& files);

// Re-find: a file with no id whose folder's list names it (same name and
// size), unless that id is on another file scanned (then it is a copy) or
// two such files claim it. Reads the lists (READ-ONLY).
struct Found { std::string path, id; };
std::vector<Found> refind(const std::vector<facts::Facts>& files);

// The work: the re-found ids (Tag) first, then each folder whose list on disk
// differs from `want` -- no list yet: a door Write (in `plan`, undoable);
// a list there: a Replace (replace_own, in place, no undo). A folder whose
// list is already right: nothing. An old list in a folder with no ids now:
// left (harmless; nothing reads it but refind).
struct Replace { std::string path, text, reason; };
struct Work {
    latch::door::Plan    plan;       // Tags + Writes -- previewed, run, undone in Activity
    std::vector<Replace> replaces;   // rewritten in place after the plan -- no undo, a log line
    std::size_t          kept = 0;   // lines carried forward (in `want`)
    bool empty() const { return plan.actions.empty() && replaces.empty(); }
    std::size_t steps() const { return plan.actions.size() + replaces.size(); }
    std::string label() const;       // "Id lists: 2 folders, 1 re-found"
};
Work plan(const std::vector<Found>& found, const std::map<std::string, std::vector<Line>>& want);

// s010f3: what Look would do now, counted -- for the background rescan's
// offer ("3 id lists out of date"). READ-ONLY (scans + reads the lists).
struct Stale { std::size_t lists = 0, refound = 0; bool any() const { return lists || refound; } };
Stale stale(const std::vector<facts::Facts>& files);

// The folders `files` live in after `found` is applied (for wanted()).
std::vector<facts::Facts> with_found(std::vector<facts::Facts> files, const std::vector<Found>& found);

}  // namespace tidy::core::filelists
