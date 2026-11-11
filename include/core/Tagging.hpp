#pragma once
// core::tagging -- the tags as PLANS for latch's door, and the catalog kept in
// step with what the door did (s004, REQUIREMENTS §7). A module never acts:
// every function here either builds a door::Plan (the UI previews it, the door
// runs it) or reads the files back into Tidy's own catalog.
//
//   stamp      give each file without an id one (a new UUID) and set the given
//              tags. An id already on a file is never replaced.
//   reconcile  what the catalog knew vs what a scan sees:
//              * ATOMIC SAVE -- the catalog's path now holds a different inode
//                with no tags (an app wrote a temp file and renamed it over):
//                carry the old file's tags to the new inode.
//              * COPY -- two inodes carry one id (cp -a, a restored backup):
//                the one the catalog does not know gets a new id.
//              * GONE -- at neither its path nor its inode, nor its id seen
//                anywhere scanned: marked gone in the catalog (no file touched).
//   record     read facts + tags back for some paths and write the catalog.
#include "core/Catalog.hpp"
#include "core/Facts.hpp"

#include <latch/Door.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::tagging {

namespace door = latch::door;

door::Plan stamp(const std::vector<facts::Facts>& files, const facts::Tags& set, const std::string& label);

struct Carry {                  // an atomic save: tags to carry to the new inode
    catalog::Entry was;
    facts::Facts   now;
};
struct Copy {                   // a second inode with a known id: give it its own
    facts::Facts   file;
    std::string    new_id;
};
struct Findings {
    std::vector<Carry>       carries;
    std::vector<Copy>        copies;
    std::vector<std::string> gone;   // ids
};

Findings reconcile(const catalog::Catalog& cat, const std::vector<facts::Facts>& scanned);
door::Plan plan_for(const Findings& f, const std::string& label);   // carries + copies (gone needs no door)

// Facts + tags read back for each path; files with an id are written to the
// catalog (a copy never takes the original's entry); untagged ones are skipped.
// Returns how many were written.
std::size_t record(catalog::Catalog& cat, const std::vector<std::string>& paths, std::int64_t now);
// After the door undid `batch`: the catalog back in step. An id the undo took
// off a file is DROPPED if this batch gave it (a stamp undone: Tidy never knew
// the file); if an earlier batch gave it (a carry undone), the entry stays with
// its inode unknown -- the next reconcile proposes the carry again.
std::size_t after_undo(catalog::Catalog& cat, const door::Door& d, std::uint64_t batch, std::int64_t now);
// The paths a batch touched (each Tag record's file), for record() after run / undo.
std::vector<std::string> paths_of(const door::Door& d, std::uint64_t batch);
// s011: where each file a batch MOVED is now (`to` if done, `from` once undone),
// so record() can follow it -- the catalog's path is not left stale.
std::vector<std::string> moved_paths(const door::Door& d, std::uint64_t batch);

}  // namespace tidy::core::tagging
