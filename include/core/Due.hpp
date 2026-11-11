#pragma once
// core::due -- the trigger (s012a on the road). REQUIREMENTS §8: "a trigger
// notifies that some files are due (to archive, to remove). Tidy proposes;
// it does not act on its own." GTK-free. READ-ONLY: it reads facts and
// Tidy's records; the only thing it writes is its own summary (core::own).
//
//   Ephemeral   born AND unread aging_days ago            -> to remove
//   Held        held_years since it became Held (ctime)   -> to remove
//   Historical  unread and unchanged archive_days         -> to archive
//               (not once archived: user.tidy.archived, s013a)
//   Until done  its purpose is closed (or none is known)  -> to settle
//               (Purposes: Close again moves it down)
//   Undecided / Keep / Hands-off: never due. Undecided is the Closet's.
//
// "Needs attention" (the tile's count, the notification's N) = due + the
// Closet's proposed + id lists out of date.
#include "core/Facts.hpp"
#include "core/Purposes.hpp"
#include "core/Retention.hpp"
#include "core/Settings.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::due {

enum class To { Remove, Archive, Settle };
const char* to_id(To t);       // "remove" / "archive" / "settle"
const char* to_title(To t);    // "To remove" / ...

struct Item {
    std::string path;
    retention::Level level = retention::Level::Undecided;
    To to = To::Remove;
    std::string why;           // "Ephemeral, unread 41 days (aging 30)"
};

// Pure: each file judged on its own; sorted by `to`, then path.
std::vector<Item> due(const std::vector<facts::Facts>& files, const purposes::Book& book,
                      const settings::Settings& s, std::int64_t now);

// What the headless scan saves (<data>/attention.ini) and the tile reads.
inline constexpr const char* kFile = "attention.ini";
struct Attention {
    std::size_t remove = 0, archive = 0, settle = 0;   // due
    std::size_t closet = 0;                            // the Closet: proposed
    std::size_t lists = 0;                             // id lists out of date (+ ids to give back)
    std::int64_t when = 0;                             // the scan's time
    std::int64_t ms = 0;                               // how long it took
    std::size_t notified = 0;                          // s012b: the total last notified (0 = none since below N)
    std::size_t total() const { return remove + archive + settle + closet + lists; }
    bool operator==(const Attention&) const = default;
};
std::string format(const Attention& a);
Attention parse(const std::string& text);
std::string line(const Attention& a);   // "14 need attention: 3 to remove, 2 to archive, 9 in the Closet"

// The whole look, headless (tidy --scan; the Due page): walk the home, read
// the purposes and settings from `data_dir`, judge. READ-ONLY.
// `user_dirs` = the text of user-dirs.dirs ("" = the defaults).
Attention look(const std::string& home, const std::string& data_dir, const std::string& user_dirs,
               std::int64_t now, std::vector<Item>* items = nullptr);

// Where the scan looks, resolved like the app: TIDY_HOME (an absolute folder)
// is a FIXTURE home with the records inside it; else HOME + XDG_DATA_HOME.
struct Where { std::string home, data_dir, user_dirs; bool fixture = false; };
Where where(const char* tidy_home, const char* home, const char* xdg_data, const char* xdg_config);

// s012b: whether this look notifies. Once the total reaches N it notifies,
// then again only when the total grows past the last one notified; below N
// the memory resets (a later climb past N notifies again). Pure.
struct Notice {
    bool send = false;
    std::size_t notified = 0;          // what attention.ini keeps
    std::string summary, body;         // "Tidy: 14 need attention" / "3 to remove, ..."
};
Notice notice(const Attention& now, std::size_t last_notified, int notify_at);

// tidy --scan [--notify]: look, save attention.ini (core::own), print the
// line. With `notify`, `out` says whether to notify (main sends it -- the
// core links no GIO). 0 = ok.
int scan_main(std::int64_t now, bool notify = false, Notice* out = nullptr);

}  // namespace tidy::core::due
