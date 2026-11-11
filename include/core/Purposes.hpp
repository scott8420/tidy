#pragma once
// core::purposes -- one decision per purpose, not per file (s009; REQUIREMENTS
// §5, §11.4). GTK-free and pure; the book is written by core::own
// (<data>/purposes.ini); tags go through latch's door as plans.
//
//   §11.4, Scott (2026-11-10): "a list built up over guesses and corrections."
//   The book holds HINTS: a folder name or a file-name glob -> a purpose, each
//   with how often it was right (yes) and corrected (no). Tidy guesses from
//   the hints that are right more than wrong; every assignment teaches it --
//   a confirmed guess is a yes, a corrected one a no, and the folder the file
//   sat in becomes a hint for the purpose chosen. Starts empty.
//   §11.5 (an SI reading content): "cool eventually" -- not here (backlog).
//
//   Close a purpose: its files follow their role -- product -> Keep, history
//   -> Historical (archive), input / incidental -> Ephemeral (the decay
//   ladder), NO role -> Historical (archived, never lost: the safe side).
//   Tags only; nothing moves.
#include "core/Facts.hpp"

#include <latch/Door.hpp>

#include <string>
#include <vector>

namespace tidy::core::purposes {

inline constexpr const char* kFile = "purposes.ini";

struct Purpose {
    std::string id;              // "taxes-2025" -- the tag's value, never changes
    std::string title;           // "Taxes 2025"
    bool closed = false;
    bool operator==(const Purpose&) const = default;
};

struct Hint {
    enum class Kind { Folder, Name } kind = Kind::Folder;
    std::string pattern;         // Folder: a folder's name ("Taxes"); Name: a glob ("*W-2*")
    std::string purpose;         // a purpose id
    int yes = 0, no = 0;
    bool active() const { return yes > no; }
    bool operator==(const Hint&) const = default;
};

struct Book {
    std::vector<Purpose> purposes;
    std::vector<Hint> hints;
    bool operator==(const Book&) const = default;
    const Purpose* find(const std::string& id) const;
};

Book parse(const std::string& text);
std::string format(const Book& b);

std::string slug_of(const std::string& title);   // "Taxes 2025" -> "taxes-2025"
// Add a purpose; "" = added (its id in *id), else why not.
std::string add(Book& b, const std::string& title, std::string* id = nullptr);
std::string set_closed(Book& b, const std::string& id, bool closed);
// Teach a name pattern by hand (counts as one yes).
std::string teach(Book& b, const std::string& glob, const std::string& purpose);

struct Guess {
    std::string purpose;         // "" = no guess
    std::string because;         // "folder Taxes" / "name *W-2*"
    int hint = -1;               // index into the book's hints
};
// A guess for a file: name hints before folder hints, then the best record.
// Only open purposes are guessed. `landing` = folder names that say nothing
// (Downloads, Desktop...): never a folder hint.
Guess guess(const Book& b, const std::string& path, const std::vector<std::string>& landing);
// Learn from one assignment: the guess confirmed (yes) or corrected (no), and
// the file's folder (unless a landing place) a hint for the chosen purpose.
void learn(Book& b, const std::string& path, const std::string& chosen, const Guess& g,
           const std::vector<std::string>& landing);

// Plans (never run here).
latch::door::Plan assign(const std::vector<facts::Facts>& files, const Purpose& p);
// Close: each file's retention from its role. Files already there are skipped.
latch::door::Plan close(const std::vector<facts::Facts>& files, const Purpose& p);
std::string retention_at_close(const std::string& role);   // "keep" / "historical" / "ephemeral" / "" (junk drawer: as is); no role -> historical

}  // namespace tidy::core::purposes
