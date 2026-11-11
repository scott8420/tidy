#pragma once
// core::starters -- the built-in templates (s010a; Scott, 2026-11-10).
// Pure data, GTK-free, writes nothing. One base tree for creative work
// (creative-project) and a kind for each craft that shares it, each adding a
// few Assets/ folders and its own routes in front of the base's (first fit
// wins). A starter is never saved on its own: the Templates page opens it in
// the builder as a draft, and Save writes it through core::own like any
// other template. Kinds Scott never uses never reach his list; a new kind is
// a new entry here, or a tweak in the builder.
//
//   The base drops the code shell's fixes/ (Scott: "fixes are not needed").
//   Work/ names no children, so drift never looks inside it -- revision
//   folders (id / revision / timestamp) grow there freely.
#include "core/Templates.hpp"

#include <string>
#include <vector>

namespace tidy::core::starters {

struct Starter {
    templates::Template t;
    std::string about;          // one line: what kind of project it is for
};

// The base first, then the kinds by name. Every one passes templates::problems().
const std::vector<Starter>& all();
const Starter* find(const std::string& name);

}  // namespace tidy::core::starters
