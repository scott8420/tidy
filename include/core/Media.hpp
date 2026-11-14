#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core::media -- s030j the loose movies and series at the top of ~/Entertainment
// (Scott's report, 2026-11-13: movie folders "Kingdom Of Heaven (2005)
// [DIRECTOR'S CUT ...] [YTS.MX]", season folders "Mayor of Kingstown (2021)
// Season 1 S01 (1080p ...)", loose videos).
//
//   a movie folder (one video + its subs / nfo / sample) -> Entertainment/Movies/<Title (Year)>   whole
//   a loose movie                                        -> Entertainment/Movies/<Title (Year)>/<file>
//   a season folder (every video one show, one season)   -> Entertainment/Series/<Show>/Season NN  whole
//   a loose episode (S01E02, 1x02)                       -> Entertainment/Series/<Show>/Season NN/<file>
//
// Only the TOP of ~/Entertainment (and folders right under it); never Movies/,
// Series/, Comics/, EBooks/, Books/, Music/, Audiobooks/, hidden, Hands off.
// A show already on the shelf keeps its spelling (Mayor Of / of Kingstown).
// Start's Sets step does it with the comics: one preview, one undo.
//
// GTK-free. READ-ONLY (plans only).
// ─────────────────────────────────────────────────────────────────────────────
#include <latch/Door.hpp>

#include <string>
#include <vector>

namespace tidy::core::media {

inline constexpr const char* kMovies = "Entertainment/Movies";
inline constexpr const char* kSeries = "Entertainment/Series";

// "Kingdom Of Heaven (2005) [DIRECTOR'S CUT] [1080p] [YTS.MX]" -> "Kingdom Of Heaven (2005)";
// "The.Descendants.2011.1080p.BluRay.H264" -> "The Descendants (2011)";
// "www.UIndex.org    -    In Her Shoes 2005 1080p BluRay x264-OFT" -> "In Her Shoes (2005)".
std::string movie_title(const std::string& name);

struct Item {
    std::string from;     // absolute: a folder (whole) or a file
    std::string to;       // relative to home: the folder it goes INTO (file) or AS (whole)
    bool whole = false;   // a folder moved as one, named `to`
    std::string what;     // "a movie" / "Season 1 of Mayor Of Kingstown" -- the preview's words
};
std::vector<Item> find(const std::string& home);

struct Planned {
    latch::door::Plan plan;
    std::vector<std::string> lines;      // the preview
    std::vector<std::string> renamed;
};
Planned shelve(const std::string& home, const std::vector<Item>& items);

}  // namespace tidy::core::media
