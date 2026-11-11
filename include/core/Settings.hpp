#pragma once
// core::settings -- what the user sets (s006a). GTK-free and pure: text in,
// text out; core::own writes the file (<data>/settings.ini).
//
//   aging_days  the Closet proposes an undecided file to age out (Ephemeral)
//               once it is born AND unread this many days (Scott: a setting).
//   junk        the junk list (Scott, s006): file-name patterns -- *.png,
//               Screenshot* -- one per line. An undecided file in the Closet
//               that matches is proposed for the Junk Drawer (kept, no home).
//               The user's list wins over the built-in birth rules; a decided
//               file is never re-proposed. Starts empty: the user lists them.
#include <string>
#include <vector>

namespace tidy::core::settings {

inline constexpr const char* kFile = "settings.ini";
inline constexpr int kAgingDefault = 30;
inline constexpr int kAgingMin = 1, kAgingMax = 3650;
// s010f1: Tidy looks for projects again this often while it runs (and once
// at launch) -- read-only; the catalog follows moves by id, marks gone. 0 = never.
inline constexpr int kRescanDefault = 10, kRescanMin = 0, kRescanMax = 1440;

struct Settings {
    int aging_days = kAgingDefault;
    int rescan_minutes = kRescanDefault;
    std::vector<std::string> junk;
    bool operator==(const Settings&) const = default;
};

Settings parse(const std::string& text);    // unknown keys and bad lines ignored; days clamped
std::string format(const Settings& s);
// Lines of a text box -> patterns: trimmed, empty and duplicate lines dropped.
std::vector<std::string> patterns_of(const std::string& lines);
// The first pattern `name` matches (glob, case-insensitive), or "".
std::string junk_match(const std::vector<std::string>& patterns, const std::string& name);

}  // namespace tidy::core::settings
