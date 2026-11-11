#pragma once
// core::archive -- the archive episode, part one (s013a on the road;
// REQUIREMENTS §8: "First: archive and catalog."). GTK-free.
//
// Scott (2026-11-10): the archive is ~/Archive, by year then the file's own
// folder -- ~/Documents/Letters/a.odt archived in 2026 goes to
// ~/Archive/2026/Documents/Letters/a.odt. Through the door: Tag + MakeDir +
// Move, previewed, undone in Activity. The file keeps its inode, its id and
// its tags; it gains user.tidy.archived = the day ("2026-11-10"), so Due no
// longer lists it. A taken name -> "a (2).odt", never overwritten.
//
// The catalog: after the run each archived file is read back AT the archive
// and its SHA-256 + size kept (catalog `archived` table) -- the fingerprint
// s013b checks the media copy against ("proof it is there").
//
// Refused: a file already in ~/Archive or already archived; inside a
// -project (a project moves whole); not a regular file; outside home.
#include "core/Catalog.hpp"
#include "core/Facts.hpp"

#include <latch/Door.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::archive {

inline constexpr const char* kFolder = "Archive";
inline constexpr const char* kTag = facts::kArchived;   // the day it was archived, "YYYY-MM-DD"

// "2026-11-10" for unix time `t`, local time.
std::string day_of(std::int64_t t);
// The folder (relative to home) `path` goes to when archived in `year`:
// "Archive/2026/Documents/Letters". "" when the file sits at home's top ->
// "Archive/2026".
std::string folder_for(const std::string& home, const std::string& path, int year);
// "" = may be archived; else why not. READ-ONLY.
std::string refusal(const std::string& home, const std::string& path);

struct Planned {
    latch::door::Plan plan;
    std::vector<std::string> renamed;                              // warned in the preview
    std::vector<std::pair<std::string, std::string>> refused;      // (path, why) -- not in the plan
};
// Tags (an id if none; archived = the day) on each file where it is, then
// MakeDirs (parents first), then the Moves. READ-ONLY until the door runs it.
Planned plan(const std::string& home, const std::vector<std::string>& paths, std::int64_t now);

// After the door ran `batch`: each file it moved, read back at the archive
// (facts + SHA-256) into the catalog. Returns how many were recorded.
std::size_t record(catalog::Catalog& cat, const latch::door::Door& d, std::uint64_t batch, std::int64_t now);
// After the door undid `batch`: the archive rows that batch made are dropped.
std::size_t after_undo(catalog::Catalog& cat, std::uint64_t batch);

}  // namespace tidy::core::archive
