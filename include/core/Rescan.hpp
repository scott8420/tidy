#pragma once
// core::rescan -- what a look at the projects tells the catalog (s010f1).
// GTK-free. Used by the Projects page's Look and by the loose background
// rescan (Settings: every N minutes while Tidy runs, and once at launch).
//
//   READ-ONLY on the home: find() reads; only Tidy's own catalog is written.
//   Followed by id: a project seen at a new path keeps its row (status,
//   outcome, uses) -- it moved. One in the catalog not found -> gone (kept,
//   never deleted; it comes back when seen again). Two folders carrying the
//   same id (a copy) -> the first is recorded, the rest counted as twins.
#include "core/Catalog.hpp"
#include "core/Projects.hpp"
#include "core/Templates.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::rescan {

struct Result {
    std::size_t found = 0;      // -project folders found
    std::size_t with_id = 0;    // of them, with an id
    std::size_t added = 0;      // ids new to the catalog
    std::size_t moved = 0;      // seen at a new path
    std::size_t back = 0;       // were gone, seen again
    std::size_t gone = 0;       // newly gone
    std::vector<std::string> twins;   // paths carrying an id already seen this look
};

// Record what `found` says in `cat` (open for writing): seen / moved / back,
// then mark gone every catalog project not found. `ts` names the templates.
Result record(const std::vector<projects::Project>& found, const std::vector<templates::Template>& ts,
              catalog::Catalog& cat, std::int64_t now);

// One line for a status bar / the log: "1 moved, 1 gone" or "no change".
std::string summary(const Result& r);

}  // namespace tidy::core::rescan
