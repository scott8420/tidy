#pragma once
// core::own -- the ONE writer of Tidy's own files (s006a; RULES' option A,
// Scott 2026-11-10). latch's door refuses Tidy's records as a target, on
// purpose, so Tidy's settings (and s007's templates) are written here and only
// here. The selftest's gate lets exactly three calls through in Own.cpp --
// making the folder, writing a temp file, renaming it into place -- and only
// under the data folder. Any other write in Own.cpp, or these anywhere else,
// fails the gate. Never a user's file.
#include <string>

namespace tidy::core::own {

// Is `rel` a name this module may write under the data folder? "" = yes, else
// why not: absolute, "..", hidden parts, or one of the door's / catalog's files.
std::string refusal(const std::string& rel);

// Write `text` to <data_dir>/<rel>: a temp beside it, then renamed over (an
// atomic save). Makes the folders. "" = written, else why not.
std::string write(const std::string& data_dir, const std::string& rel, const std::string& text);

// Read <data_dir>/<rel>; "" with found=false if it isn't there.
std::string read(const std::string& data_dir, const std::string& rel, bool* found = nullptr);

}  // namespace tidy::core::own
