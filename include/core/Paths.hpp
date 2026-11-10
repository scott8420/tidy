#pragma once
// core::paths -- where Tidy's own records live (s003). GTK-free and pure: the
// caller hands in the environment, so the selftest proves the rules on made-up
// homes without touching a real one.
//
//   data   $XDG_DATA_HOME/tidy  (default ~/.local/share/tidy) -- the catalog,
//          the door's manifest, templates/ (REQUIREMENTS §5, §7)
//
// The spine only RESOLVES these; nothing creates them until the first write
// milestone (s004), and then only through the door. latch refuses them as a
// target ("Tidy's own records").
#include <string>

namespace tidy::core::paths {

// XDG rule: an XDG_DATA_HOME that is empty or not absolute is ignored.
std::string data_dir(const std::string& home, const char* xdg_data_home);
std::string templates_dir(const std::string& data_dir);

}  // namespace tidy::core::paths
