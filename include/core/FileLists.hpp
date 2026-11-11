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
//   GTK-free. READ-ONLY here: plans go to the door (Trash the old list, then
//   Write the new; restore = Tag). Hidden, Protected and fixes/ are never
//   walked (core::scan), so they never get a list; projects do.
#include "core/Facts.hpp"

#include <latch/Door.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tidy::core::filelists {

inline constexpr const char* kFile = ".tidy-files";

// The home's top folders to walk (not hidden, not symlinks; core::scan then
// refuses what is Protected). Loose files at the home's top get no list.
std::vector<std::string> roots(const std::string& home);

struct Line {
    std::string   id;
    std::uint64_t size = 0;
    std::int64_t  mtime = 0;
    std::string   name;
    bool operator==(const Line&) const = default;
};

// "<id>\t<size>\t<mtime>\t<name>" per line under a "# " header, sorted by
// name. A name with a tab or newline is left out (it cannot round-trip).
std::string format(std::vector<Line> lines);
std::vector<Line> parse(const std::string& text);         // header, junk and bad ids skipped

// What each folder's list should say: the files and packages in `files` that
// carry an id, grouped by their folder. Folders with none are absent.
std::map<std::string, std::vector<Line>> wanted(const std::vector<facts::Facts>& files);

// Re-find: a file with no id whose folder's list names it (same name and
// size), unless that id is on another file scanned (then it is a copy) or
// two such files claim it. Reads the lists (READ-ONLY).
struct Found { std::string path, id; };
std::vector<Found> refind(const std::vector<facts::Facts>& files);

// The plan: the re-found ids (Tag) first, then each folder whose list on
// disk differs from `want` -- Trash the old one (if any), Write the new. A
// folder whose list is already right: nothing. An old list in a folder with
// no ids now: left (harmless; nothing reads it but refind).
latch::door::Plan plan(const std::vector<Found>& found, const std::map<std::string, std::vector<Line>>& want);

// The folders `files` live in after `found` is applied (for wanted()).
std::vector<facts::Facts> with_found(std::vector<facts::Facts> files, const std::vector<Found>& found);

}  // namespace tidy::core::filelists
