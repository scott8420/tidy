#pragma once
// core::background (s012b on the road) -- Scott: "a daemon that runs like
// cron to do bkgnd scanning and once there are N files needing attention we
// get a notification". On Fedora that is a systemd USER timer:
//   ~/.config/systemd/user/tidy-scan.service   tidy --scan --notify (oneshot, nice, idle IO)
//   ~/.config/systemd/user/tidy-scan.timer     every bg_minutes, first 5 min after login
// The scan is READ-ONLY on the home (core::due); it writes attention.ini.
//
// GTK-free, pure. The units are written by the DOOR (a previewed plan, undo
// in Activity); a later change of minutes rewrites the timer in place
// (latch replace_own -- registered with kHeader). Enabling / disabling is
// `systemctl --user ...` -- commands() lists them; the window runs them
// (never on a fixture home -- it only says what it would run).
#include <latch/Door.hpp>

#include <string>
#include <vector>

namespace tidy::core::background {

inline constexpr const char* kService = "tidy-scan.service";
inline constexpr const char* kTimer   = "tidy-scan.timer";
inline constexpr const char* kHeader  = "# Tidy background scan";

// <XDG_CONFIG_HOME or home/.config>/systemd/user (a fixture: inside it).
std::string unit_dir(const std::string& home, const char* xdg_config, bool fixture);

// `exe` = the tidy binary to run; `fixture_home` non-empty -> TIDY_HOME=it.
std::string service_text(const std::string& exe, const std::string& fixture_home);
std::string timer_text(int minutes);
int minutes_of(const std::string& timer_text);   // 0 = not one of ours / unreadable

struct State {
    bool service = false, timer = false;   // the unit files there
    bool ours = false;                     // both start with kHeader
    int  minutes = 0;                      // from the timer
    bool on() const { return service && timer && ours; }
};
State state_of(const std::string& dir);    // READ-ONLY

// Turn on: MakeDir each missing level of `dir`, Write the service, Write the
// timer (a unit already there and ours: replaced in place instead -> `replaces`).
struct Work {
    latch::door::Plan plan;
    struct Replace { std::string path, text, reason; };
    std::vector<Replace> replaces;
    std::vector<std::vector<std::string>> first;      // systemctl --user ... BEFORE the files (turn off: disable)
    std::vector<std::vector<std::string>> commands;   // systemctl --user ... AFTER the files
};
Work turn_on(const std::string& home, const std::string& dir, const std::string& exe,
             const std::string& fixture_home, int minutes);
// Turn off: disable --now first (`first`), then Trash both units (undo puts
// them back, still disabled), then daemon-reload.
Work turn_off(const std::string& dir);
// On, the minutes changed: the timer rewritten in place, reload + restart.
Work retime(const std::string& dir, int minutes);

std::string shown_command(const std::vector<std::string>& argv);   // "systemctl --user enable --now tidy-scan.timer"

}  // namespace tidy::core::background
