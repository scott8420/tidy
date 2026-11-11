#pragma once
// Protection -- s019a: the folders Tidy never looks inside and never moves
// anything into or out of, beyond latch's own never-touch list.
//
//   * yours: Settings -> Hands-off folders (any user, any folder in home);
//   * Samson's: its folder, catalog and second home -- only when Samson is
//     installed (its config folder exists), shown in Settings as locked.
//
// apply() hands both to latch (protect::set_owned) so every walk, page and
// the door refuse the same places. Called at startup, after Settings change,
// and by the headless look (tidy --scan).
#include "core/Settings.hpp"

#include <string>
#include <vector>

namespace tidy::core::protection {

// $XDG_CONFIG_HOME when home is the real $HOME, else <home>/.config (a fixture).
std::string config_home(const std::string& home);

// Samson's places relative to home; empty when <cfg>/samson does not exist.
std::vector<std::string> samson(const std::string& home, const std::string& cfg);

// Yours + Samson's, de-duplicated, relative to home.
std::vector<std::string> all(const std::string& home, const std::string& cfg, const settings::Settings& st);

void apply(const std::string& home, const std::string& cfg, const settings::Settings& st);

// Is `rel` (relative to home) protected, or inside something that is? (latch's
// here() asks one folder; a destination is refused anywhere under one.)
bool under(const std::string& rel);

// A typed or picked folder -> relative to home: "~/Taxes", "/home/scott/Taxes",
// "Taxes" all give "Taxes". "" when it reads; else why not (outside home,
// home itself, "..").
std::string rel_of(const std::string& home, const std::string& text, std::string& rel);

}  // namespace tidy::core::protection
