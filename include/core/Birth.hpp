#pragma once
// core::birth -- birth rules: what each file in the landing places would be
// proposed, from how it was born (REQUIREMENTS §3). s005: REPORT ONLY.
//
// READ-ONLY. Nothing here tags, moves or writes: a report is the preview a
// later stamp plan (s006, through latch's door) will come from. GTK-free.
//
//   places()   the landing places (§4's lean: Downloads, Screenshots, Desktop,
//              the loose top of Documents), from ~/.config/user-dirs.dirs when
//              it names them. A place that is home itself, or outside home, is
//              dropped -- a Desktop set to $HOME must never mean "the whole home".
//   rules()    the birth rules as one table, tried in order; the first that
//              fits names the level. Each rule says why, in a line.
//   classify() one file -> its proposed level + the rule + the why.
//   report()   scan the places (read-only) and classify every item.
#include "core/Facts.hpp"
#include "core/Retention.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tidy::core::birth {

inline constexpr const char* kOriginUrl = "user.xdg.origin.url";   // set by browsers on a download

struct Place {
    std::string id;         // "downloads", "screenshots", "desktop", "documents"; s019: "home", "top"
    std::string title;      // "Downloads"
    std::string path;       // absolute, under home
    bool        top_only;   // Documents: only its loose top -- its folders are already somebody's
};

// `user_dirs` = the text of ~/.config/user-dirs.dirs ("" -> the defaults).
std::vector<Place> places(const std::string& home, const std::string& user_dirs);

// s019 the home sweep: the whole home, as loose files. The landing places (as
// places()), then home's own top ("home", top only), then the top of every
// folder at home's top ("top", top only: ~/Pictures/x.png is loose,
// ~/Pictures/Covers/x.png is home). Never a hidden folder, a symlink, a
// <name>-project, a git repo, ~/Archive, a package, or anything protected.
std::vector<Place> sweep(const std::string& home, const std::string& user_dirs);

struct Rule {
    std::string      id;      // "screenshot", "gov-download", ...
    std::string      title;   // "A screenshot"
    retention::Level level;   // what it proposes ("stamped" proposes what the file carries)
    std::string      says;    // the rule in one line (REQUIREMENTS §3)
};
const std::vector<Rule>& rules();
const Rule* rule(const std::string& id);

struct Born {
    facts::Facts     file;
    std::string      place;    // the Place id it was found in
    std::string      origin;   // user.xdg.origin.url ("" = none)
    retention::Level level = retention::Level::Undecided;
    std::string      rule;     // the Rule id that fit
    std::string      why;      // this file's reason, in a line
};

// s019: `now` + `old_years` (Settings; 0 = off) -> the "old" rule: a file
// untouched (neither changed nor read) that many years is proposed Historical.
Born classify(const facts::Facts& f, const std::string& place_id, const std::string& origin_url,
              std::int64_t now = 0, int old_years = 0);
// The last time it was changed or read (the newer of mtime and atime).
std::int64_t untouched_since(const facts::Facts& f);

// Pieces the rules use (and the selftest proves).
std::string host_of(const std::string& url);           // "https://www.IRS.gov:443/x" -> "www.irs.gov"
bool is_screenshot_name(const std::string& name);       // "Screenshot From 2026-11-10 ...png"
std::string ext_of(const std::string& name);            // lowercase, no dot; "" if none

struct Report {
    std::vector<Place> places;   // the ones that exist as folders
    std::vector<Born>  items;    // in place order, then path order
    std::size_t count(retention::Level l) const;
    std::size_t count_rule(const std::string& rule_id) const;
};
Report report(const std::string& home, const std::vector<Place>& places, std::int64_t now = 0, int old_years = 0);

// The report as plain text (tidy_selftest --report HOME): grouped by level.
std::string text(const Report& r, const std::string& home);

}  // namespace tidy::core::birth
