#pragma once
// core::backup -- the archive episode, part two (s013b on the road;
// REQUIREMENTS §8: "Then: back up the archive to media."). GTK-free.
//
// Scott (s014): media = a mounted folder picked in Settings (the big SSD, the
// Pi's standard mount); Tidy copies there and never moves anything there.
// Proof = the copy read back FROM the disk, its SHA-256 + size equal to the
// fingerprint taken at archive time -> "on <label>" + the day. Degoo never
// counts (it is not a media folder). Removal (s014 on the road) will need one
// proof on the SSD or the Pi.
//
// Where: ~/Archive/2026/Documents/a.odt -> <media folder>/2026/Documents/a.odt
// (the archive's own layout below the folder Scott picked).
//
// Through the door (latch 0.4 media roots): MakeDir + Copy only, never over
// anything. A file already there is not copied again -- it is read back and
// proved, or reported as different bytes and left alone. Undo (Activity) puts
// the copies in that disk's own trash and drops their proofs.
#include "core/Catalog.hpp"
#include "core/Settings.hpp"

#include <latch/Door.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::backup {

// The settings' media -> latch's media roots (other_disk on, always, in the
// app; the selftest turns it off for its one-disk fixtures).
std::vector<latch::door::MediaRoot> roots(const std::vector<settings::Media>& media, bool other_disk = true);
// Hand them to the door (at startup, and when Settings saves).
void register_media(const std::vector<settings::Media>& media, bool other_disk = true);
// "" = usable now; else why not ("not mounted?", "missing", ...). READ-ONLY.
std::string problem(const settings::Media& m, const std::string& home, bool other_disk = true);

// Where `archive_path` (in ~/Archive) goes on `m`; "" if it is not in ~/Archive.
std::string where_on(const settings::Media& m, const std::string& home, const std::string& archive_path);

// Read a copy back from the disk itself: flush it, drop it from the page
// cache, then hash it (O_NOATIME). "" = unreadable.
std::string read_back(const std::string& path, std::uint64_t* size);

struct Check {          // already on the disk: read back only, nothing copied
    std::string id, from, to;
};
struct Planned {
    latch::door::Plan plan;                                        // MakeDirs (parents first) + Copies
    std::vector<Check> checks;                                     // there already -> read back after Do it
    std::vector<std::pair<std::string, std::string>> skipped;     // (archive path, why) -- not in the plan
    std::size_t proved = 0;                                        // already proved on this media
};
// Each archived row not yet proved on `m`. READ-ONLY until the door runs it.
Planned plan(const std::string& home, const settings::Media& m, const std::vector<catalog::Archived>& archived,
             const std::vector<catalog::Copy>& copies);

struct Proof {
    std::string id, path, why;   // why = "" when proved; else what did not match
    bool ok = false;
    bool was_there = false;      // a check (already on the disk), not a copy Tidy just made
};
// After the door ran `batch` (0 = nothing copied, checks only): each Done
// Copy and each check read back from the disk and compared; matches -> a
// catalog `copies` row (checks with batch 0). Mismatches are returned, not kept.
std::vector<Proof> prove(catalog::Catalog& cat, const latch::door::Door& d, std::uint64_t batch,
                         const settings::Media& m, const std::vector<Check>& checks, std::int64_t now);
// After the door undid `batch`: its proofs dropped.
std::size_t after_undo(catalog::Catalog& cat, std::uint64_t batch);

// The Archive page's line for one file: "on Big SSD 2026-11-10, on Pi 2026-11-11"
// or "not on media yet".
std::string status_line(const std::vector<catalog::Copy>& copies_of_file);

}  // namespace tidy::core::backup
