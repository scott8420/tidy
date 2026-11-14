#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::put -- s030g "Put it with..." (Scott, 2026-11-13 19:08: "how to just
// find a file or select one and say it is in this folder").
//
// Any files, any folder under home -- whatever Tidy guessed. One plan (MakeDirs,
// the moves, a Keep tag so Due never trashes what you placed), one undo. After
// a Do it each file that moved teaches its learned route (s022): next time a
// file with a name like it ("flash*.cbz") is proposed for that folder.
//
// GTK-free. READ-ONLY until the door runs the plan; learn() writes Tidy's own
// routes.ini (core::own).
// ─────────────────────────────────────────────────────────────────────────────
#include <latch/Door.hpp>

#include <string>
#include <vector>

namespace tidy::core::put {

// The folder typed or picked -> relative to home. "" = fine (*rel set), else why
// not (outside home, home itself, hidden, Hands off, too deep).
std::string folder_rel(const std::string& home, const std::string& picked, std::string* rel);

struct Planned {
    latch::door::Plan plan;
    std::vector<std::string> renamed;   // "a.cbz -> a (2).cbz in ~/..."
    std::vector<std::string> lines;     // the preview: "Flash Gordon S046.cbz  ->  ~/Entertainment/Comics/Flash Gordon"
    std::vector<std::string> skipped;   // files not under home, hidden, already there
};
Planned plan(const std::string& home, const std::vector<std::string>& files, const std::string& folder_rel);

// After the door ran it: each file no longer at its old path teaches its route.
// Returns how many routes were taught; `why` gets a write error, if any.
std::size_t learn(const std::string& home, const std::string& data_dir, const std::vector<std::string>& files,
                  const std::string& folder_rel, std::string* why = nullptr);

}  // namespace tidy::core::put
