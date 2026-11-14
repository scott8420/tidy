#include "core/Media.hpp"
#include "core/Homing.hpp"
#include "core/Protection.hpp"
#include "core/Sort.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <regex>
#include <set>

namespace fs = std::filesystem;
namespace dr = latch::door;

namespace tidy::core::media {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(std::string s) {
    const auto junk = [](char c) { return c == ' ' || c == '-' || c == '.' || c == '_' || c == ',' || c == '\t'; };
    while (!s.empty() && junk(s.back())) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && junk(s[i])) ++i;
    return s.substr(i);
}
bool is_video(const std::string& name) { return sort::kind_of(name) == "Movies"; }
std::string season_dir(int season) {
    char b[16];
    std::snprintf(b, sizeof b, "Season %02d", season);
    return b;
}
// A show already on the shelf, spelled as it is there (case-blind).
std::string shelf_show(const std::string& home, const std::string& show) {
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(home) / kSeries, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec) && lower(it->path().filename().string()) == lower(show)) return it->path().filename().string();
    return show;
}

}  // namespace

std::string movie_title(const std::string& raw) {
    std::string s = raw;
    // "www.UIndex.org    -    In Her Shoes ..." -> "In Her Shoes ..."
    if (lower(s).rfind("www.", 0) == 0) {
        if (const auto d = s.find(" - "); d != std::string::npos) s = s.substr(d + 3);
    }
    if (s.find(' ') == std::string::npos) std::replace(s.begin(), s.end(), '.', ' ');
    std::replace(s.begin(), s.end(), '_', ' ');
    // The year: (2005) or a bare 19xx / 20xx word -- the title is what comes before it.
    static const std::regex year(R"((?:^|[\s(\[])((?:19|20)\d{2})(?:[\s)\]]|$))");
    std::smatch m;
    std::string title, y;
    if (std::regex_search(s, m, year) && m.position(1) > 0) {
        title = s.substr(0, static_cast<std::size_t>(m.position(0)));
        y = m[1].str();
    } else {
        title = s;
    }
    // Brackets out; cut at the first quality word.
    std::string out;
    int depth = 0;
    for (char c : title) {
        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
        if (c == ')' || c == ']' || c == '}') { if (depth) --depth; continue; }
        if (!depth) out += c;
    }
    static const std::set<std::string> quality = {"1080p", "720p", "2160p", "480p", "4k", "bluray", "brrip", "webrip", "web-dl",
                                                  "web", "hdtv", "x264", "x265", "h264", "hevc", "dvdrip", "remastered", "eng"};
    std::string kept;
    std::size_t at = 0;
    while (at < out.size()) {
        const auto sp = out.find(' ', at);
        const std::string w = out.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
        if (quality.count(lower(w))) break;
        kept += (kept.empty() ? "" : " ") + w;
        if (sp == std::string::npos) break;
        at = sp + 1;
    }
    std::string t = trim(kept);
    // squeeze spaces
    std::string q;
    for (char c : t) if (!(c == ' ' && !q.empty() && q.back() == ' ')) q += c;
    for (auto& c : q) if (c == '/') c = '-';
    if (q.empty()) q = trim(raw);
    return y.empty() ? q : q + " (" + y + ")";
}

std::vector<Item> find(const std::string& home) {
    std::vector<Item> out;
    const fs::path ent = fs::path(home) / "Entertainment";
    static const std::set<std::string> shelves = {"movies", "series", "comics", "ebooks", "books", "music", "audiobooks", "tv"};
    std::error_code ec;
    if (!fs::is_directory(ent, ec) || protection::under("Entertainment")) return out;
    std::vector<fs::path> loose_files;
    for (fs::directory_iterator it(ent, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& p = it->path();
        const std::string n = p.filename().string();
        if (n.empty() || n[0] == '.' || it->is_symlink(ec)) continue;
        const std::string rel = "Entertainment/" + n;
        if (protection::under(rel)) continue;
        if (it->is_regular_file(ec)) { loose_files.push_back(p); continue; }
        if (!it->is_directory(ec) || shelves.count(lower(n))) continue;
        // A folder: its videos (deep, a few levels), each an episode or not.
        std::vector<fs::path> videos;
        bool deep_other_video = false;
        for (fs::recursive_directory_iterator r(p, fs::directory_options::skip_permission_denied, ec), e; !ec && r != e; r.increment(ec)) {
            if (r.depth() > 3) { r.disable_recursion_pending(); continue; }
            if (r->is_regular_file(ec) && is_video(r->path().filename().string())) videos.push_back(r->path());
        }
        if (videos.empty()) continue;
        std::set<std::pair<std::string, int>> shows;
        std::size_t eps = 0;
        for (const auto& v : videos) {
            std::string show;
            int season = 0;
            if (sort::episode(v.filename().string(), &show, &season)) { shows.insert({lower(show), season}); ++eps; }
            else deep_other_video = true;
        }
        if (eps == videos.size() && shows.size() == 1) {   // one show, one season: the folder whole
            std::string show;
            int season = 0;
            sort::episode(videos.front().filename().string(), &show, &season);
            show = shelf_show(home, show);
            out.push_back({p.string(), std::string(kSeries) + "/" + show + "/" + season_dir(season), true,
                           "Season " + std::to_string(season) + " of " + show});
        } else if (eps == 0 && videos.size() == 1) {      // one movie (+ subs, nfo): the folder whole
            out.push_back({p.string(), std::string(kMovies) + "/" + movie_title(n), true, "a movie"});
        } else if (eps && !deep_other_video) {            // several seasons / shows: each episode on its own
            for (const auto& v : videos) {
                std::string show;
                int season = 0;
                sort::episode(v.filename().string(), &show, &season);
                show = shelf_show(home, show);
                out.push_back({v.string(), std::string(kSeries) + "/" + show + "/" + season_dir(season), false,
                               "an episode of " + show});
            }
        }
        // else: a mix of movies and other things -- left for you (Put it with...).
    }
    // s030k (Scott: "i have 2 movies in the closet and no where do they show up"): videos
    // waiting in ~/Closet (any depth up to 3) and loose on the Desktop are loose files too.
    for (const char* top : {"Closet", "Desktop"}) {
        const fs::path t = fs::path(home) / top;
        std::error_code e2;
        if (!fs::is_directory(t, e2) || protection::under(top)) continue;
        for (fs::recursive_directory_iterator r(t, fs::directory_options::skip_permission_denied, e2), e; !e2 && r != e; r.increment(e2)) {
            const std::string n = r->path().filename().string();
            if (n.empty() || n[0] == '.' || r->is_symlink(e2)) { if (r->is_directory(e2)) r.disable_recursion_pending(); continue; }
            if (r->is_directory(e2)) {
                if (std::string(top) == "Desktop" || r.depth() >= 2 || protection::under(r->path().string().substr(home.size() + 1)))
                    r.disable_recursion_pending();
                continue;
            }
            if (r->is_regular_file(e2) && (is_video(n) || sort::is_companion(n))) loose_files.push_back(r->path());
        }
    }
    for (const auto& f : loose_files) {
        const std::string n = f.filename().string();
        if (!is_video(n)) continue;
        std::string show;
        int season = 0;
        if (sort::episode(n, &show, &season)) {
            show = shelf_show(home, show);
            out.push_back({f.string(), std::string(kSeries) + "/" + show + "/" + season_dir(season), false, "an episode of " + show});
        } else {
            out.push_back({f.string(), std::string(kMovies) + "/" + movie_title(f.stem().string()), false, "a movie"});
        }
        // its subtitles / nfo of the same name go with it
        for (const auto& c : loose_files) {
            const std::string cn = c.filename().string();
            if (c == f || !sort::is_companion(cn) || c.parent_path() != f.parent_path()) continue;
            if (lower(cn).rfind(lower(f.stem().string()), 0) == 0) out.push_back({c.string(), out.back().to, false, "its subtitles"});
        }
    }
    return out;
}

Planned shelve(const std::string& home, const std::vector<Item>& items) {
    Planned out;
    std::vector<homing::Pick> picks;
    std::set<std::string> made;
    std::vector<dr::Action> wholes;
    for (const auto& it : items) {
        const std::string name = fs::path(it.from).filename().string();
        if (!it.whole) {
            picks.push_back({it.from, it.to, "s030j: " + it.what});
            out.lines.push_back(name + "  ->  ~/" + it.to);
            continue;
        }
        // A whole folder, renamed as it goes: its parent made first, a free name.
        const fs::path dest = fs::path(home) / it.to;
        std::string at = dest.parent_path().string(), leaf = dest.filename().string();
        std::error_code ec;
        if (fs::exists(dest, ec) || made.count(dest.string())) {
            for (int n = 2; n < 100; ++n) {
                leaf = homing::numbered(dest.filename().string(), n);
                if (!fs::exists(fs::path(at) / leaf, ec) && !made.count(at + "/" + leaf)) break;
            }
            out.renamed.push_back(dest.filename().string() + " -> " + leaf + " in ~/" + at.substr(home.size() + 1));
        }
        for (fs::path d = fs::path(at); d.string().size() > home.size() && !fs::exists(d, ec) && !made.count(d.string()); d = d.parent_path())
            made.insert(d.string());
        made.insert(at + "/" + leaf);
        dr::Action a;
        a.verb = dr::Verb::Move;
        a.from = it.from;
        a.to = at + "/" + leaf;
        a.reason = it.what;
        wholes.push_back(a);
        out.lines.push_back(name + "/  ->  ~/" + (at + "/" + leaf).substr(home.size() + 1) + "   (" + it.what + ", whole)");
    }
    auto hp = homing::plan(home, picks);
    out.plan = std::move(hp.plan);
    out.renamed.insert(out.renamed.end(), hp.renamed.begin(), hp.renamed.end());
    // The parents of the whole folders: MakeDirs (shallow first) not already in the plan.
    std::set<std::string> have;
    for (const auto& a : out.plan.actions) if (a.verb == dr::Verb::MakeDir) have.insert(a.to);
    std::vector<std::string> dirs;
    for (const auto& w : wholes) {
        std::error_code ec;
        for (fs::path d = fs::path(w.to).parent_path(); d.string().size() > home.size() && !fs::exists(d, ec); d = d.parent_path())
            if (!have.count(d.string())) { dirs.push_back(d.string()); have.insert(d.string()); }
    }
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
    std::vector<dr::Action> pre;
    for (const auto& d : dirs) {
        dr::Action a;
        a.verb = dr::Verb::MakeDir;
        a.to = d;
        a.reason = "a shelf";
        pre.push_back(a);
    }
    // After homing's own MakeDirs (they may make Entertainment/Series itself), before the whole moves.
    out.plan.actions.insert(out.plan.actions.end(), pre.begin(), pre.end());
    out.plan.actions.insert(out.plan.actions.end(), wholes.begin(), wholes.end());
    out.plan.label = "Movies and series: " + std::to_string(items.size());
    return out;
}

}  // namespace tidy::core::media
