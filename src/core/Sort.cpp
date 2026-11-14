#include "core/Sort.hpp"
#include "core/Comics.hpp"
#include "core/Recognise.hpp"
#include "core/Archive.hpp"
#include "core/Closet.hpp"
#include "core/Facts.hpp"
#include "core/Tagging.hpp"

#include <algorithm>
#include <cctype>
#include <fnmatch.h>
#include <ctime>
#include <regex>
#include <filesystem>
#include <map>
#include <set>

namespace tidy::core::sort {

namespace fs = std::filesystem;
namespace rt = retention;
namespace dr = latch::door;

namespace {
std::string month_of(std::int64_t now) {
    const std::time_t t = static_cast<std::time_t>(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m", &tm);
    return b;
}
std::string source_of(const birth::Born& b, const std::string& home) {
    if (b.place == "home") return "Home";
    if (b.place == "top" || b.place == "documents" || b.place == "catchall") {   // the folder it sits loose in
        return fs::path(b.file.path).parent_path().filename().string();
    }
    if (b.place == "screenshots") return "Screenshots";
    if (b.place == "downloads") return "Downloads";
    if (b.place == "desktop") return "Desktop";
    (void)home;
    return "Loose";
}
}  // namespace

const std::vector<homing::TypeRoute>& ent_defaults() {
    static const std::vector<homing::TypeRoute> t = [] {
        std::vector<homing::TypeRoute> v;
        const auto add = [&](std::initializer_list<const char*> exts, const char* folder) {
            for (const char* e : exts) v.push_back({std::string("*.") + e, folder});
        };
        add({"mkv", "mp4", "avi", "mov", "m4v", "webm", "wmv", "mpg", "mpeg", "ts"}, "Entertainment/Movies");
        add({"epub", "mobi", "azw", "azw3", "pdf", "djvu", "fb2", "cbz", "cbr", "cb7"}, "Entertainment/EBooks");
        add({"mp3", "flac", "ogg", "opus", "m4a", "aac", "wav"}, "Entertainment/Music");
        return v;
    }();
    return t;
}

std::vector<homing::TypeRoute> ent_table(const settings::Settings& s) {
    if (!s.ent_set) return ent_defaults();
    std::vector<homing::TypeRoute> out;
    for (const auto& l : s.ent)
        if (homing::TypeRoute r; homing::parse_line(l, r)) out.push_back(r);
    return out;
}

std::string ent_folder(const std::vector<homing::TypeRoute>& table, const std::string& file_name) {
    const auto* r = homing::type_of(table, file_name);
    return r ? r->folder : std::string();
}

std::string kind_of(const std::string& file_name) {
    const auto dot = file_name.rfind('.');
    if (dot == std::string::npos) return "";
    std::string e = file_name.substr(dot + 1);
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::set<std::string> video = {"mkv", "mp4", "avi", "mov", "m4v", "webm", "wmv", "mpg", "mpeg", "ts"};
    static const std::set<std::string> books = {"epub", "mobi", "azw", "azw3", "cbz", "cbr", "pdf", "djvu", "fb2"};
    static const std::set<std::string> music = {"mp3", "flac", "ogg", "opus", "m4a", "aac", "wav"};
    if (video.count(e)) return "Movies";
    if (books.count(e)) return "Books";
    if (music.count(e)) return "Music";
    return "";
}

bool episode(const std::string& name, std::string* show, int* season) {
    static const std::regex sxe(R"(^(.*?)[\s._\-]*[Ss](\d{1,2})[\s._\-]?[Ee]\d{1,3})");
    static const std::regex nxn(R"(^(.*?)[\s._\-]+(\d{1,2})x\d{2}(?:\D|$))");
    std::smatch m;
    if (!std::regex_search(name, m, sxe) && !std::regex_search(name, m, nxn)) return false;
    std::string raw = m[1].str(), out;
    bool up = true;
    for (char c : raw) {
        if (c == '.' || c == '_' || c == '-' || c == ' ') {
            if (!out.empty() && out.back() != ' ') out += ' ';
            up = true;
            continue;
        }
        if (c == '/' || std::iscntrl(static_cast<unsigned char>(c))) continue;
        out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        up = false;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty()) return false;
    if (show) *show = out;
    if (season) *season = std::stoi(m[2].str());
    return true;
}

bool read_since_arrival(const facts::Facts& f) {
    const std::int64_t arrived = std::max(f.mtime, f.btime);
    return arrived > 0 && f.atime > arrived + 60;
}

std::string dated(const birth::Born& b, const std::string& type_folder, std::int64_t now) {
    const std::string from = fs::path(b.file.path).parent_path().string();
    std::string src = source_of(b, "");
    // Already loose at its own type folder's top (~/Pictures/x.png -> Pictures): "Loose".
    if (!type_folder.empty() && (from.size() >= type_folder.size() + 1) &&
        from.compare(from.size() - type_folder.size() - 1, std::string::npos, "/" + type_folder) == 0)
        src = "Loose";
    return src + " " + month_of(now);
}

namespace {
// ~/Entertainment/<Kind>[/<Show>/Season NN] for a read download; "" if not entertainment.
// s023g: the Entertainment table's folder; an episode (a video named S01E02)
// -> Entertainment/Series/<Show>/Season NN instead. "" if not entertainment.
std::string entertainment_for(const std::vector<homing::TypeRoute>& table, const std::string& name,
                              const std::string& as_file, std::string* why) {
    const std::string folder = ent_folder(table, as_file);
    if (folder.empty()) return "";
    std::string show;
    int season = 0;
    if (kind_of(as_file) == "Movies" && episode(name, &show, &season)) {
        char s2[16];
        std::snprintf(s2, sizeof s2, "Season %02d", season);
        if (why) *why = "an episode of " + show;
        return std::string(kEntertainment) + "/Series/" + show + "/" + s2;
    }
    if (why) {
        for (const auto& r : table)
            if (::fnmatch(r.pattern.c_str(), as_file.c_str(), FNM_CASEFOLD) == 0) { *why = r.pattern + " -> ~/" + r.folder; break; }
    }
    return folder;
}
std::string month_now(std::int64_t now) { return month_of(now); }

// s030 (Scott: "srt files need to be with the video"): a subtitle / nfo goes
// where its video goes -- the video of the same name first, else the one video
// in its folder (or, for a Subs/ folder, in the folder above). No video in the
// plan beside it -> it stays (never the Closet).
std::string stem_lower(const std::string& name) {
    std::string s = fs::path(name).stem().string();
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
void follow_videos(std::vector<Row>& out) {
    std::map<std::string, std::vector<std::size_t>> videos;   // folder -> video rows
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i].folder_whole || out[i].is_project) continue;
        const fs::path p(out[i].born.file.path);
        if (kind_of(p.filename().string()) == "Movies") videos[p.parent_path().string()].push_back(i);
    }
    for (auto& r : out) {
        if (r.folder_whole || r.is_project) continue;
        const fs::path p(r.born.file.path);
        if (!is_companion(p.filename().string())) continue;
        const Row* v = nullptr;
        for (fs::path dir = p.parent_path(); !v && !dir.empty(); dir = dir.parent_path()) {
            const auto it = videos.find(dir.string());
            if (it != videos.end()) {
                const std::string st = stem_lower(p.filename().string());
                for (auto i : it->second)   // the same name ("Movie.en.srt" -> "Movie.mkv") first
                    if (st.rfind(stem_lower(fs::path(out[i].born.file.path).filename().string()), 0) == 0) { v = &out[i]; break; }
                if (!v && it->second.size() == 1) v = &out[it->second.front()];
            }
            if (dir == p.parent_path() && dir.filename() != "Subs" && dir.filename() != "subs" && dir.filename() != "Subtitles") break;
        }
        r.closet = false;
        if (!v || v->go != Go::Home) {
            r.go = Go::Stays;
            r.tag = false;
            r.why = v ? "with its video -- stays where it is" : "a subtitle -- stays (no video beside it)";
            continue;
        }
        r.go = Go::Home;
        r.folder = v->folder;
        r.level = v->level;
        r.tag = v->tag;
        r.why = "with its video -> ~/" + v->folder;
    }
}
}  // namespace

// s030e (Scott: "I want it to go in with the other Flash Gordon cbz files"): a
// comic going home goes to its series on the Comics shelf instead of by type.
void join_series(const std::string& home, std::vector<Row>& out) {
    std::vector<std::string> known;
    bool looked = false;
    for (auto& r : out) {
        if (r.folder_whole || r.is_project || !(r.go == Go::Home || r.closet)) continue;
        // s030g: a NAME route you taught ("flash*.cbz") wins; a broad from-route ("*.cbz from
        // Downloads") never outranks the series -- a Donald Duck is no Flash Gordon.
        if (!r.learned.folder.empty() && r.folder == r.learned.folder && r.learned.because.find(" from ") == std::string::npos) continue;
        const std::string n = fs::path(r.born.file.path).filename().string();
        if (!comics::is_comic(n)) continue;
        if (r.closet) {   // no Home rule for it: a comic still has a place -- its series
            r.closet = false;
            r.go = Go::Home;
            r.level = retention::Level::Keep;
            r.tag = true;
        }
        if (!looked) { known = comics::shelf(home); looked = true; }
        const std::string s = comics::series_of(n, known);
        const bool there = std::find(known.begin(), known.end(), s) != known.end();
        r.folder = std::string(comics::kShelf) + "/" + s;
        r.why = there ? "joins its series: " + s + " (on the Comics shelf)" : "a new series on the Comics shelf: " + s;
    }
}

bool is_companion(const std::string& file_name) {
    static const std::set<std::string> k = {".srt", ".sub", ".idx", ".ass", ".ssa", ".vtt", ".nfo", ".smi"};
    std::string e = fs::path(file_name).extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return k.count(e) > 0;
}

std::vector<Row> rows(const std::string& home, const birth::Report& rep, const settings::Settings& st,
                      std::int64_t now, const std::vector<projects::Project>& found,
                      const std::vector<templates::Template>& ts, const routes::Book* learned,
                      const home::Design* design) {
    // s029: the Home design -- its plain routes by name, its Downloads/ routes for a read download.
    const home::Design dz = design ? *design : home::design(home::seed(st));
    const auto& table = dz.types;
    const auto& ent = dz.ent;
    const auto v = closet::view(rep, now, st);
    std::vector<Row> out;
    // s020a: the plain folders in Downloads and on the Desktop -- each ONE row.
    std::map<std::string, std::string> roots;   // place path -> place id
    for (const auto& p : rep.places)
        if (p.id == "downloads" || p.id == "desktop") roots[p.path] = p.id;
    const auto folder_of = [&](const std::string& path, std::string* place) -> std::string {
        for (const auto& [root, id] : roots) {
            if (path.rfind(root + "/", 0) != 0) continue;
            const std::string rest = path.substr(root.size() + 1);
            const auto slash = rest.find('/');
            if (slash == std::string::npos) return "";
            if (place) *place = id;
            return root + "/" + rest.substr(0, slash);
        }
        return "";
    };
    struct Folder { std::string place; std::vector<const birth::Born*> files; };
    std::map<std::string, Folder> folders;
    for (const auto& b : rep.items) {
        std::string pl;
        const auto f = folder_of(b.file.path, &pl);
        if (f.empty()) continue;
        folders[f].place = pl;
        folders[f].files.push_back(&b);
    }
    const auto in_folder = [&](const birth::Born& b) { return !folder_of(b.file.path, nullptr).empty(); };

    const auto place = [&](Row& r) {
        const std::string name = fs::path(r.born.file.path).filename().string();
        if (r.junk) { r.go = Go::Stays; r.why = "your junk list: the Junk Drawer -- kept, no home"; return; }
        // s020a: a download -- read: on to Entertainment (or home by type); unread: stays.
        bool was_read = false;
        if (r.born.place == "downloads" && r.level == rt::Level::Ephemeral) {
            if (!read_since_arrival(r.born.file)) {
                r.go = Go::Stays;
                r.why = "unread download -- stays; Due proposes the Trash after " + std::to_string(st.aging_days) +
                        " days unread";
                // s023g: where "Move on now" sends it -- the Entertainment table, else home by type.
                std::string what;
                if (const auto e = entertainment_for(ent, name, name, &what); !e.empty()) {
                    r.now_folder = e;
                    r.now_why = "moved on now -- " + what;
                } else if (const auto* t = homing::type_of(table, name)) {
                    if (const auto f = home::expand(t->folder, r.born.file.path); !f.empty() && !home::is_trash(f)) {
                        r.now_folder = f;
                        r.now_why = "moved on now -- Home: " + t->pattern + " -> ~/" + f;
                    }
                }
                return;
            }
            r.level = rt::Level::Keep;
            r.tag = true;
            was_read = true;
            std::string what;
            if (const auto e = entertainment_for(ent, name, name, &what); !e.empty()) {
                r.go = Go::Home;
                r.folder = e;
                r.why = "read since it arrived -- " + what;
                return;
            }
        }
        switch (r.level) {
        case rt::Level::Ephemeral:
            r.go = Go::Stays;
            r.why = "Ephemeral -- stays; Due removes it when its days are up";
            return;
        case rt::Level::Historical: {
            r.go = Go::Archive;
            const std::time_t t = static_cast<std::time_t>(now);
            std::tm tm{};
            localtime_r(&t, &tm);
            r.folder = archive::folder_for(home, r.born.file.path, tm.tm_year + 1900);
            r.why = "Historical (" + r.born.why + ") -- archived, fingerprinted";
            return;
        }
        default: break;
        }
        r.go = Go::Home;
        // s022: where you sent the same kind before, before the dated default.
        if (learned) {
            if (auto p = routes::propose(*learned, home, r.born); !p.folder.empty()) {
                r.folder = p.folder;
                r.learned = p;
                r.why = "learned: " + p.because + " -> " + p.folder + " (" + std::to_string(p.times) +
                        (p.times == 1 ? " time" : " times") + "), " + rt::title_of(r.level) +
                        (was_read ? " (read since it arrived)" : "");
                return;
            }
        }
        // s029: the Home design names the folder; no rule fits -> it waits in the Closet.
        const auto* t = homing::type_of(table, name);
        if (!t) {
            // s030i: an app's file (.mort -> Develop/Mortimer) has its app's home even with no Home rule.
            std::string ext = fs::path(name).extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (const auto bh = recognise::bundle_home(ext); !bh.empty() && bh != "=" && homing::folder_check(bh).empty()) {
                if (fs::path(r.born.file.path).parent_path() == fs::path(home) / bh) {
                    r.go = Go::Stays;
                    r.why = "at its app's home already (~/" + bh + ")";
                    return;
                }
                r.folder = bh;
                r.why = "its app's home: " + ext + " -> ~/" + bh;
                return;
            }
            r.go = Go::Stays;
            r.tag = false;
            r.closet = true;
            r.why = "no Home rule fits -- waits in the Closet (Design -> Types -> Home)";
            return;
        }
        std::string why;
        const std::string folder = home::expand(t->folder, r.born.file.path, &why);
        if (folder.empty()) {
            r.go = Go::Stays;
            r.tag = false;
            r.closet = true;
            r.why = "no home: " + why;
            return;
        }
        if (home::is_trash(folder)) {   // s030i: your Home design says it is trash
            r.go = Go::Trash;
            r.tag = false;
            r.why = "Home: " + t->pattern + " -> the Trash (one undo in Activity)";
            return;
        }
        if (fs::path(r.born.file.path).parent_path() == fs::path(home) / folder) {
            r.go = Go::Stays;
            r.why = "at home already (" + t->pattern + " -> ~/" + folder + ")";
            return;
        }
        r.folder = folder;
        r.why = "Home: " + t->pattern + " -> ~/" + folder + ", " + rt::title_of(r.level) +
                (was_read ? " (read since it arrived)" : "");
    };
    for (const auto& b : v.proposed) {   // undecided, a proposal to take
        if (in_folder(b)) continue;
        Row r;
        r.born = b;
        r.tag = true;
        r.junk = b.rule == "junk-list";
        r.level = r.junk ? rt::Level::Keep : b.level;
        place(r);
        out.push_back(std::move(r));
    }
    for (const auto& b : v.waiting) {    // undecided, nothing proposed: kept, home by type
        if (in_folder(b)) continue;
        Row r;
        r.born = b;
        r.tag = true;
        r.level = rt::Level::Keep;
        place(r);
        out.push_back(std::move(r));
    }
    for (const auto& b : rep.items) {    // decided already, still loose
        if (b.rule != "stamped" || in_folder(b)) continue;
        const auto t = facts::tags_of(b.file.path);
        if (t.role == closet::kJunkDrawer || b.level == rt::Level::HandsOff || b.level == rt::Level::Undecided)
            continue;
        if (!t.archived.empty()) continue;
        Row r;
        r.born = b;
        r.level = b.level;
        place(r);
        if (r.go == Go::Stays && !r.tag) continue;   // decided and staying: nothing to do
        out.push_back(std::move(r));
    }
    // s020a: each plain folder whole -- its kind by most of its files.
    for (const auto& [path, f] : folders) {
        Row r;
        if (!facts::of(path, r.born.file)) continue;
        r.born.place = f.place;
        r.born.rule = "folder";
        r.folder_whole = true;
        r.level = rt::Level::Keep;
        const std::string name = fs::path(path).filename().string();
        std::map<std::string, std::size_t> kinds, types;
        std::map<std::string, std::string> sample;   // an Entertainment folder -> a file name that went there
        bool read = false;
        for (const auto* b : f.files) {
            const std::string n = fs::path(b->file.path).filename().string();
            const auto ef = ent_folder(ent, n);
            ++kinds[ef];
            if (!ef.empty() && !sample.count(ef)) sample[ef] = n;
            if (const auto* t = homing::type_of(table, n)) ++types[t->folder];
            read = read || read_since_arrival(b->file);
        }
        const auto most = [](const std::map<std::string, std::size_t>& m) {
            std::string best;
            std::size_t n = 0;
            for (const auto& [k, c] : m) if (c > n) { best = k; n = c; }
            return best;
        };
        const std::string count = std::to_string(f.files.size()) + (f.files.size() == 1 ? " file" : " files");
        if (f.place == "downloads") {
            std::string kind = most(kinds), what;
            if (kind.empty() && kinds.size() > 1) {   // the subtitles and nfo are not the kind: the media is
                std::map<std::string, std::size_t> media = kinds;
                media.erase("");
                kind = most(media);
            }
            // s023g: a comic folder with sub-folders, a season of episodes: by the table, WHOLE.
            const std::string e = kind.empty() ? std::string() : entertainment_for(ent, name, sample[kind], &what);
            if (!read) {   // unread: it stays -- s023g: listed now, so "Move on now" can take it
                r.go = Go::Stays;
                r.tag = false;
                r.why = "a folder (" + count + "), unread -- stays";
                if (!e.empty()) {
                    r.now_folder = e;
                    r.now_why = "a folder (" + count + "), moved on now -- " + what + ", moved whole";
                }
                out.push_back(std::move(r));
                continue;
            }
            if (!e.empty()) {
                r.go = Go::Home;
                r.folder = e;
                r.why = "a folder (" + count + "), read -- " + what + ", moved whole";
                out.push_back(std::move(r));
                continue;
            }
        }
        // s029: a folder goes whole to the Home folder most of its files have (a slot's
        // fixed part: a folder of fonts -> ~/Fonts/<its name>); none -> it stays (the Closet).
        if (most(types).empty()) continue;
        std::string folder = home::fixed_part(most(types));
        if (!homing::folder_check(folder).empty()) continue;
        // s030: a slot folder (fonts) named only for its format -- Roboto/static,
        // Oswald/ttf -- goes under its parent's name: ~/Fonts/Roboto/static.
        if (home::has_slot(most(types)) && home::generic_folder_name(name)) {
            const std::string parent = home::clean_name(fs::path(path).parent_path().filename().string());
            const bool top = parent == "Desktop" || parent == "Downloads" || fs::path(path).parent_path() == fs::path(home);
            if (!top && !parent.empty() && homing::folder_check(folder + "/" + parent).empty()) folder += "/" + parent;
        }
        r.go = Go::Home;
        r.folder = folder;
        r.why = "a folder (" + count + "), mostly ~/" + folder + " (Home), moved whole";
        out.push_back(std::move(r));
    }
    // s020a: the <name>-projects not yet in a category.
    for (const auto& p : found) {
        if (!p.category.empty() || p.left) continue;
        Row r;
        if (!facts::of(p.path, r.born.file)) continue;
        r.born.place = "project";
        r.born.rule = "project";
        r.is_project = true;
        r.project = p;
        r.level = rt::Level::Keep;
        if (p.code) { r.go = Go::Stays; r.why = "a code project stays where it is"; out.push_back(std::move(r)); continue; }
        const templates::Template* best = nullptr;
        std::size_t best_hits = 0;
        for (const auto& t : ts) {
            if (t.category.empty()) continue;
            std::size_t tops = 0, hits = 0;
            for (const auto& fo : t.folders) {
                if (fo.path.find('/') != std::string::npos) continue;
                ++tops;
                std::error_code ec;
                hits += fs::is_directory(fs::path(p.path) / fo.path, ec);
            }
            if (hits >= 2 && hits * 2 >= tops && hits > best_hits) { best = &t; best_hits = hits; }
        }
        if (!best) {
            r.go = Go::Stays;
            r.why = "uncategorized -- pick its category on the Projects page";
        } else {
            r.go = Go::Home;
            r.folder = best->category;
            r.why = "matches " + best->name + " (" + std::to_string(best_hits) + " of its folders), moved whole";
        }
        out.push_back(std::move(r));
    }
    (void)month_now;
    follow_videos(out);
    join_series(home, out);   // s030e
    return out;
}

bool move_on_now(Row& r) {
    if (r.now_folder.empty()) return false;
    r.go = Go::Home;
    r.folder = r.now_folder;
    r.why = r.now_why;
    if (!r.folder_whole) {
        r.level = rt::Level::Keep;
        r.tag = true;
    }
    return true;
}

Planned plan(const std::string& home, const std::vector<Row>& chosen, std::int64_t now) {
    Planned out;
    // 1. The tags, on the files where they are: grouped by level (+ the Junk Drawer).
    std::map<rt::Level, std::vector<facts::Facts>> by_level;
    std::vector<facts::Facts> junk;
    std::set<std::string> stamped;
    for (const auto& r : chosen) {
        if (!r.tag) continue;
        (r.junk ? junk : by_level[r.level]).push_back(r.born.file);
        stamped.insert(r.born.file.path);
    }
    std::vector<dr::Action> tags;
    for (const auto& [lvl, fs_] : by_level) {
        auto p = tagging::stamp(fs_, {"", rt::id_of(lvl), "", ""}, "Sort: " + rt::title_of(lvl));
        tags.insert(tags.end(), p.actions.begin(), p.actions.end());
    }
    if (!junk.empty()) {
        auto p = tagging::stamp(junk, {"", rt::id_of(rt::Level::Keep), "", closet::kJunkDrawer}, "Sort: the Junk Drawer");
        tags.insert(tags.end(), p.actions.begin(), p.actions.end());
    }
    // 2. Home: new folders + moves.
    std::vector<homing::Pick> picks;
    std::vector<dr::Action> placed;
    for (const auto& r : chosen) {
        if (r.go != Go::Home) continue;
        if (r.is_project) {   // s020a: whole, through the Projects page's own place()
            std::string why;
            auto pp = projects::place(r.project, home, r.folder, &why);
            if (!why.empty()) { out.refused.emplace_back(r.born.file.path, why); continue; }
            placed.insert(placed.end(), pp.actions.begin(), pp.actions.end());
            continue;
        }
        picks.push_back({r.born.file.path, r.folder, r.why});
    }
    auto home_p = homing::plan(home, picks);
    // s030i: the Trash rows -- after the moves.
    std::vector<dr::Action> trash;
    for (const auto& r : chosen) {
        if (r.go != Go::Trash) continue;
        dr::Action a;
        a.verb = dr::Verb::Trash;
        a.from = r.born.file.path;
        a.reason = r.why;
        trash.push_back(a);
    }
    // 3. The archive (its own id tag dropped where step 1 gives one).
    std::vector<std::string> arch;
    for (const auto& r : chosen)
        if (r.go == Go::Archive) arch.push_back(r.born.file.path);
    auto arch_p = archive::plan(home, arch, now);
    out.refused.insert(out.refused.end(), arch_p.refused.begin(), arch_p.refused.end());
    out.archiving = arch.size() - out.refused.size();
    out.renamed = std::move(home_p.renamed);
    out.renamed.insert(out.renamed.end(), arch_p.renamed.begin(), arch_p.renamed.end());
    out.plan.actions = std::move(tags);
    out.plan.actions.insert(out.plan.actions.end(), home_p.plan.actions.begin(), home_p.plan.actions.end());
    out.plan.actions.insert(out.plan.actions.end(), placed.begin(), placed.end());
    out.plan.actions.insert(out.plan.actions.end(), trash.begin(), trash.end());
    for (auto& a : arch_p.plan.actions) {
        if (a.verb == dr::Verb::Tag && (a.attr == facts::kId || a.attr == facts::kRetention) && stamped.count(a.from))
            continue;   // step 1 gives it (s023d: its level too -- Historical already)
        out.plan.actions.push_back(std::move(a));
    }
    std::size_t files = 0;
    for (const auto& r : chosen) files += (r.go != Go::Stays || r.tag);
    out.plan.label = "Sort: " + std::to_string(files) + (files == 1 ? " file" : " files");
    return out;
}

}  // namespace tidy::core::sort
