#pragma once
// core::scan -- walk folders under home and read each item's facts (s004).
// READ-ONLY. Never follows a symlink; never enters anything Protected (latch's
// never-touch list, any fixes/) or a hidden entry; a package (.scriv, our
// apps' bundles) is ONE item, never entered.
#include "core/Facts.hpp"

#include <string>
#include <vector>

namespace tidy::core::scan {

// Every file and package under each root (absolute, inside `home`).
// Folders themselves are not listed (they are walked).
std::vector<facts::Facts> walk(const std::string& home, const std::vector<std::string>& roots);

}  // namespace tidy::core::scan
