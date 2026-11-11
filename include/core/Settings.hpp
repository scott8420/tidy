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

// s012a the trigger: Held decays this many years after it became Held (the
// clock: ctime -- late, never early); Historical is due to archive once
// unread and unchanged this many days.
inline constexpr int kHeldDefault = 7, kHeldMin = 1, kHeldMax = 100;
inline constexpr int kArchiveDefault = 365, kArchiveMin = 30, kArchiveMax = 3650;

// s012b background: how often the timer runs `tidy --scan --notify`, and how
// many files needing attention bring a notification.
inline constexpr int kBgDefault = 60, kBgMin = 15, kBgMax = 1440;
inline constexpr int kNotifyDefault = 10, kNotifyMin = 1, kNotifyMax = 100000;

// s013b backup media (Scott, s014: "a mounted folder picked in Settings"):
// the SSD's folder, the Pi's mount. Tidy copies the archive there and never
// moves anything there. `label` is what the Archive page calls it, and what a
// proof is kept under -- rename it and the old proofs stay under the old name.
// In settings.ini: "media=Big SSD|/run/media/scott/Big SSD/Tidy".
struct Media {
    std::string label, path;
    bool operator==(const Media&) const = default;
};
// "Big SSD | /run/media/..." or a bare path -> a Media (label = the folder's
// name when none). false = not a line (no absolute path).
bool media_of(const std::string& line, Media& out);
std::string media_line(const Media& m);   // "Big SSD | /run/media/..."

struct Settings {
    int aging_days = kAgingDefault;
    int bg_minutes = kBgDefault;
    int notify_at = kNotifyDefault;
    int held_years = kHeldDefault;
    int archive_days = kArchiveDefault;
    int rescan_minutes = kRescanDefault;
    std::vector<std::string> junk;
    // s011 files home: the type table, "*.png -> Pictures" per line (core::homing
    // reads it). home_set = false -> the standard table (Scott: "fine"); true ->
    // exactly these lines, even none.
    std::vector<std::string> home;
    bool home_set = false;
    std::vector<Media> media;   // s013b: labels unique, first wins
    bool operator==(const Settings&) const = default;
};

Settings parse(const std::string& text);    // unknown keys and bad lines ignored; days clamped
std::string format(const Settings& s);
// Lines of a text box -> patterns: trimmed, empty and duplicate lines dropped.
std::vector<std::string> patterns_of(const std::string& lines);
// The first pattern `name` matches (glob, case-insensitive), or "".
std::string junk_match(const std::vector<std::string>& patterns, const std::string& name);

}  // namespace tidy::core::settings
