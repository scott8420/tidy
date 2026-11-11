#pragma once
// core::removal -- the last episode on the road (s014; REQUIREMENTS §8: "Only
// in a later episode is an archived item marked for removal -- and nothing is
// removed without proof it is on media."). GTK-free.
//
// Scott (s015, 1-6 yes):
//  - What goes: a file to the TRASH, through the door, previewed, undone in
//    Activity. Never a hard delete.
//  - A file in ~/Archive goes only when `proofs_needed` (Settings, 1) of its
//    media copies are read back NOW, from a mounted disk, and match the
//    fingerprint taken at archive time -- an old proof alone is not enough.
//    And the file in ~/Archive must itself still be those bytes (changed
//    since archived -> back it up again first).
//  - Due's other "to remove" (Ephemeral past its days, Held past its years)
//    were never archived: no proof; to the Trash, previewed.
//  - Due counts the proved archive files as "to remove"; the Archive page has
//    Remove proved...
#include "core/Catalog.hpp"
#include "core/Settings.hpp"

#include <latch/Door.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::removal {

// What the catalog says (read once; plan() re-reads the bytes, not this).
struct Book {
    std::vector<catalog::Archived> archived;
    std::vector<catalog::Copy> copies;
    std::vector<catalog::Removed> removed;
};
Book book_of(const catalog::Catalog& cat);

// The archived files that may be offered for removal WITHOUT reading anything:
// in ~/Archive still, not removed, with at least `needed` proofs on media the
// settings still name. (Due's count; the Archive page's button.) READ-ONLY.
std::vector<catalog::Archived> offerable(const Book& b, const settings::Settings& s);
// "proved on Big SSD 2026-11-10" for Due's line.
std::string offer_why(const Book& b, const std::string& id);

struct Planned {
    latch::door::Plan plan;                                       // Trash steps only
    std::vector<std::pair<std::string, std::string>> refused;    // (path, why) -- not in the plan
    std::vector<catalog::Removed> pending;                       // archived ones: what record() keeps (batch 0)
};
// For each path: in ~/Archive -> the proof checked again NOW (reads the file
// and each copy); else it must be Ephemeral or Held. READ-ONLY until run.
Planned plan(const std::string& home, const Book& b, const settings::Settings& s,
             const std::vector<std::string>& paths, bool other_disk = true);

// After the door ran `batch`: each archived file it trashed -> a `removed` row.
std::size_t record(catalog::Catalog& cat, const latch::door::Door& d, std::uint64_t batch,
                   const std::vector<catalog::Removed>& pending, std::int64_t now);
std::size_t after_undo(catalog::Catalog& cat, std::uint64_t batch);

}  // namespace tidy::core::removal
