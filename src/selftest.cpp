// tidy_selftest -- the headless harness (s003, the spine).
//
// No display, no real home: fixtures under a temp dir only (RULES: safety is
// the product). What it proves in the spine:
//   * the look   -- page states, the sidebar lints clean, every tint in look.css;
//   * the pages  -- every nav item has its page text, and every text its item;
//   * the paths  -- Tidy's records resolve by the XDG rule;
//   * latch      -- Tidy's name in refusals; the door refuses outside home,
//                   home itself, every never-touch entry, fixes/, Tidy's records;
//   * the gate   -- no Tidy source calls a filesystem write. The spine writes
//                   nothing; from s004 every write goes through latch's door.
//   * shortcuts  -- the registry paradigm (from Cairn).
// s004, tags and catalog:
//   * facts      -- statx times, ids;
//   * scan       -- read-only; Protected, hidden, symlinks skipped; packages whole;
//   * the catalog + tags -- stamp / reconcile (atomic save, copy, move, gone)
//                   as door plans, undone through the door; the catalog in step;
//                   no file's bytes or mtime changed by tagging;
//   * the gate   -- also: no xattr writes in Tidy; SQLite only in Catalog.cpp.
// s005, birth rules (REPORT ONLY):
//   * levels     -- the seven, ids stable and unique;
//   * places     -- the landing places from user-dirs.dirs; home itself and
//                   outside home dropped;
//   * rules      -- each rule fits its file; a decided stamp is never overruled;
//   * the report -- on a fixture: every rule fits something, Documents top
//                   only; and it changed NOTHING (inode, size, mtime, ctime,
//                   every xattr) and created no record.
//
// s006, the Closet (TAGS ONLY, through the door, on a FIXTURE):
//   * the view   -- only the undecided; decided never visit; aging -> proposed;
//   * the plans  -- accept / decide / Junk Drawer: Tag actions only; refused
//                   unpreviewed; bytes, inode and mtime untouched; undone back
//                   into the Closet; the catalog in step.
//
// s006a, settings (Scott): aging days a setting; the junk list (patterns ->
//   the Junk Drawer, winning over birth rules); core::own the ONE writer of
//   Tidy's own files -- under the data folder only, gate-listed.
//
// s007, templates (Tidy's own files, through core::own):
//   * the model  -- names, depth two only, parents, routes; text round trip;
//   * the builder -- every edit undoable; lifted folders block Save until each
//                   is kept or removed; a new template never takes a saved name;
//   * lift       -- a proposal at template depth; READ-ONLY (nothing changed);
//   * drift      -- missing / extra at template depth; free below it; adopt.
//
// s008, projects (MOVES, through the door, on a FIXTURE):
//   * find       -- every -project, never inside one, never in fixes / hidden;
//                   category = where it lives; code projects known; READ-ONLY;
//   * place      -- an uncategorized one moved WHOLE (every inode kept), undone
//                   back exactly; code / placed / landing refused;
//   * fix        -- the template's missing folders made (fixes/ refused by the
//                   door: Scott's); undone; "left" remembered.
//
// s009, purposes (TAGS ONLY, through the door, on a FIXTURE):
//   * the book   -- purposes + hints, round trip; a learned list: guesses
//                   confirmed gain, corrected lose; landing folders teach nothing;
//   * assign / close -- Tag actions only; close follows each file's role; undone.
//
//   ./build/tidy_selftest
//   ./build/tidy_selftest --fixture DIR   make a fixture home at DIR (tagged,
//                                         one atomic save carried) and stop:
//                                         TIDY_HOME=DIR ./build/tidy shows it
//   ./build/tidy_selftest --report HOME   print the birth report for HOME
//                                         (read-only: nothing is tagged)

#include "core/Look.hpp"
#include "core/Pages.hpp"
#include "core/Paths.hpp"
#include "core/Protection.hpp"
#include "core/Sort.hpp"
#include "Busy.hpp"
#include "core/Adopt.hpp"
#include "core/Map.hpp"
#include "core/Conform.hpp"
#include "core/Shape.hpp"
#include "core/Routes.hpp"
#include "core/Shortcuts.hpp"
#include "core/Catalog.hpp"
#include "core/Facts.hpp"
#include "core/Scan.hpp"
#include "core/Tagging.hpp"
#include "core/Retention.hpp"
#include "core/Birth.hpp"
#include "core/Closet.hpp"
#include "core/Settings.hpp"
#include "core/Own.hpp"
#include "core/Templates.hpp"
#include "core/Starters.hpp"
#include "core/Projects.hpp"
#include "core/Rescan.hpp"
#include "core/FileLists.hpp"
#include "core/Homing.hpp"
#include "core/Purposes.hpp"
#include "core/Due.hpp"
#include "core/Background.hpp"
#include "core/Archive.hpp"
#include "core/Sha256.hpp"
#include "core/Backup.hpp"
#include "core/Removal.hpp"
#include "core/Help.hpp"
#include "core/Code.hpp"
#include <latch/Vcs.hpp>

#include <latch/Door.hpp>
#include <latch/Package.hpp>
#include <fcntl.h>
#include <latch/Protect.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/xattr.h>  // the harness copies tags as cp -a would
#include <unistd.h>     // getpid: a fixture dir per run

namespace core = tidy::core;
namespace fs = std::filesystem;

namespace {
int g_pass = 0, g_fail = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { ++g_pass; std::cout << "  PASS  " << name; }
    else    { ++g_fail; std::cout << "  FAIL  " << name; }
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ── The source gate's matcher ────────────────────────────────────────────────
// Comments and string literals are blanked first (a comment may SAY "rename";
// a log line may too), then each forbidden call is a regex. A hit names the
// file and the call. Mutation-tested below: a planted ::rename( must be found.
std::string code_only(const std::string& s) {
    std::string o = s;
    enum { Code, Line, Block, Str, Chr, Raw } st = Code;
    std::string raw_end;
    for (std::size_t i = 0; i < o.size(); ++i) {
        const char c = o[i], n = i + 1 < o.size() ? o[i + 1] : '\0';
        switch (st) {
            case Code:
                if (c == '/' && n == '/') { st = Line; o[i] = ' '; }
                else if (c == '/' && n == '*') { st = Block; o[i] = ' '; }
                else if (c == 'R' && n == '"') {
                    const std::size_t p = o.find('(', i + 2);
                    if (p == std::string::npos) break;
                    raw_end = ")" + o.substr(i + 2, p - i - 2) + "\"";
                    st = Raw;
                    i = p;
                } else if (c == '"') st = Str;
                else if (c == '\'') st = Chr;
                break;
            case Line:  if (c == '\n') st = Code; else o[i] = ' '; break;
            case Block: if (c == '*' && n == '/') { o[i] = o[i + 1] = ' '; ++i; st = Code; } else if (c != '\n') o[i] = ' '; break;
            case Str:   if (c == '\\') { o[i] = ' '; if (i + 1 < o.size()) o[++i] = ' '; } else if (c == '"') st = Code; else if (c != '\n') o[i] = ' '; break;
            case Chr:   if (c == '\\') { ++i; } else if (c == '\'') st = Code; break;
            case Raw:   if (o.compare(i, raw_end.size(), raw_end) == 0) { i += raw_end.size() - 1; st = Code; } else if (c != '\n') o[i] = ' '; break;
        }
    }
    return o;
}

struct Forbidden { const char* name; std::regex re; };
const std::vector<Forbidden>& forbidden() {
    static const std::vector<Forbidden> f = {
        {"rename()",           std::regex(R"((^|[^\w.>])(::)?rename(at2?)?\s*\()")},
        {"std::remove/unlink", std::regex(R"((std::remove|(^|[^\w:])::remove|\bunlink(at)?|\brmdir)\s*\()")},
        {"filesystem writes",  std::regex(R"(\b(fs|filesystem)::(remove|remove_all|rename|copy|copy_file|create_directory|create_directories|create_symlink|create_hard_link|resize_file|permissions|last_write_time)\s*\()")},
        {"ofstream / fopen",   std::regex(R"(\bofstream\b|\bfstream\b|\bfopen\s*\(|\bfreopen\s*\()")},
        {"open for writing",   std::regex(R"(\bO_(WRONLY|RDWR|CREAT|TRUNC|APPEND)\b)")},
        {"mkdir / link",       std::regex(R"((^|[^\w.>])(::)?(mkdir(at)?|symlink(at)?|link(at)?|truncate|ftruncate)\s*\()")},
        {"GIO file writes",    std::regex(R"(\bg_file_(delete|trash|move|copy|replace|create|make_directory|set_contents)|->(trash|move|copy|replace|make_directory|make_directory_with_parents|create_file)\s*\()")},
        {"glib file writes",   std::regex(R"(\bg_(rename|remove|unlink|mkdir|mkdir_with_parents|file_set_contents)\s*\(|Glib::file_set_contents)")},
        {"system / popen",     std::regex(R"((^|[^\w.>])(std::)?(system|popen)\s*\()")},
        {"xattr writes",       std::regex(R"(\b[lf]?(setxattr|removexattr)\s*\()")},   // tags: the door's Tag verb only
        {"sqlite",             std::regex(R"(\bsqlite3_\w+\s*\()")},                   // Tidy's catalog: Catalog.cpp only
    };
    return f;
}

// Catalog.cpp, Tidy's own record: SQLite, and ONE filesystem write -- making
// the data folder (fs::create_directories). Any other write there still hits.
std::vector<std::string> gate_hits(const std::string& file, const std::string& text) {
    std::vector<std::string> hits;
    std::string code = code_only(text);
    const bool catalog = file == "Catalog.cpp";
    if (catalog) code = std::regex_replace(code, std::regex(R"(\bfs::create_directories\s*\()"), "made_dirs(");
    // Own.cpp, the ONE writer of Tidy's own files (s006a): make the folder,
    // write a temp, rename it into place. Those three only.
    if (file == "Own.cpp") {
        code = std::regex_replace(code, std::regex(R"(\bfs::create_directories\s*\()"), "made_dirs(");
        code = std::regex_replace(code, std::regex(R"(\bstd::ofstream\b)"), "own_out");
        code = std::regex_replace(code, std::regex(R"(#include\s*<fstream>)"), "");
        code = std::regex_replace(code, std::regex(R"(\bstd::rename\s*\()"), "own_swap(");
    }
    for (const auto& f : forbidden()) {
        if (catalog && std::string(f.name) == "sqlite") continue;
        if (std::regex_search(code, f.re)) hits.push_back(file + ": " + f.name);
    }
    return hits;
}

// ── s004 fixtures ─────────────────────────────────────────────────────────────
void put_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << body;
}
// A fixture home: landing places, a package, a Protected folder, a hidden
// file, a symlink, a folder Tidy is not asked to scan.
void make_home(const fs::path& home) {
    put_file(home / "Downloads/f1040.pdf", "irs form");
    put_file(home / "Downloads/cat.jpg", "jpeg");
    put_file(home / "Downloads/.part", "hidden");
    put_file(home / "Desktop/Screenshot 2026-11-10.png", "png");
    put_file(home / "Documents/report.odt", "report v1");
    put_file(home / "Documents/Jasper.scriv/Files/Data/1/content.rtf", "chapter one");
    put_file(home / "Documents/fixes/patch.txt", "p");
    put_file(home / "Pictures/landscape.png", "not scanned");
    fs::create_directories(home / ".local/share/tidy");
    std::error_code ec;
    fs::create_symlink(home / "Documents/report.odt", home / "Downloads/report-link.odt", ec);
}
// An app's atomic save: write a temp beside it, rename over -- a new inode,
// no tags.
void atomic_save(const fs::path& p, const std::string& body) {
    const fs::path tmp = p.parent_path() / (".~" + p.filename().string() + ".tmp");
    put_file(tmp, body);
    fs::rename(tmp, p);
}
std::vector<std::string> roots_of(const std::string& H) {
    return {H + "/Downloads", H + "/Desktop", H + "/Documents"};
}
std::int64_t now_s() { return static_cast<std::int64_t>(std::time(nullptr)); }

// s005: what the birth rules need on top of make_home -- a Screenshots folder,
// downloads with their origin (as a browser leaves it), a loose file and a
// folder at the top of Documents, a file carrying a decision. The harness sets
// xattrs itself (as a browser or one of our apps would); Tidy never does.
void xset(const fs::path& p, const char* name, const std::string& v) {
    ::lsetxattr(p.c_str(), name, v.data(), v.size(), 0);
}
void make_birth_extras(const fs::path& home) {
    namespace cb = core::birth;
    put_file(home / "Pictures/Screenshots/Screenshot From 2026-11-10 09-14-02.png", "png");
    put_file(home / "Pictures/Screenshots/Screencast From 2026-11-10 09-20-11.webm", "webm");
    put_file(home / "Downloads/fedora-44.rpm", "rpm");
    put_file(home / "Downloads/notes.txt", "no origin");
    put_file(home / "Downloads/brochure.pdf", "pdf");
    put_file(home / "Documents/loose.txt", "loose");
    put_file(home / "Documents/Taxes/w2.pdf", "inside a folder: not loose");
    put_file(home / "Desktop/plan.txt", "plan");
    put_file(home / "Desktop/cover-final.png", "decided");
    xset(home / "Downloads/f1040.pdf", cb::kOriginUrl, "https://www.IRS.gov/pub/irs-pdf/f1040.pdf");
    xset(home / "Downloads/cat.jpg", cb::kOriginUrl, "https://images.example.com/cat.jpg");
    xset(home / "Downloads/fedora-44.rpm", cb::kOriginUrl, "https://github.com/x/y/releases/fedora-44.rpm");
    xset(home / "Downloads/brochure.pdf", cb::kOriginUrl, "https://shop.example.com/brochure.pdf");
    xset(home / "Desktop/cover-final.png", core::facts::kRetention, "keep");
}

// s007: two writing projects under ~/Writing. Jasper carries drift (a stray
// "old covers" folder), a hidden .git, a symlink, a file, a folder three deep.
// Mary misses fixes/ and Final/, has an extra Notes/, and a Manuscript/Ch1
// below template depth (free).
void make_projects(const fs::path& home) {
    const fs::path j = home / "Writing/Jasper-project";
    for (const char* d : {"backups", "docs", "fixes", "scripts", "Graphics/Covers", "Graphics/old covers",
                          "Manuscript", "Research/Sources/1850s", "Final", ".git/objects"})
        fs::create_directories(j / d);
    put_file(j / "README.txt", "jasper");
    put_file(j / "Graphics/Covers/cover.png", "png");
    std::error_code ec;
    fs::create_directory_symlink(j / "docs", j / "link-to-docs", ec);
    // s008: a code project in Develop/gtk, loose ones to place, ones never listed.
    // s010c: our real workspace layout -- the repo one folder down.
    const fs::path t = home / "Develop/gtk/tidy-project";
    fs::create_directories(t / "tidy/.git");
    put_file(t / "tidy/CMakeLists.txt", "project(tidy)");
    for (const char* d : {"backups/Old", "docs", "scripts"}) fs::create_directories(t / d);
    fs::create_directories(t / "nested-project");                 // never inside a project
    put_file(home / "Desktop/Essay-project/draft.odt", "draft");
    put_file(home / "Desktop/Essay-project/Research/notes.txt", "notes");
    fs::create_directories(home / "Stray-project");
    fs::create_directories(home / "Develop/fixes/Hidden-project");  // protected: never listed
    fs::create_directories(home / ".cache/Cached-project");        // hidden: never listed
    const fs::path m = home / "Writing/Mary-project";
    for (const char* d : {"backups", "docs", "scripts", "Graphics/Covers", "Manuscript/Ch1", "Research/Sources/Deep", "Notes"})
        fs::create_directories(m / d);
}
// s019 the home sweep: loose files across the home -- at home's top, at a top
// folder's top, untouched for years -- and the trees the sweep never enters
// (~/Archive, a git repo at the top or in Downloads, a -project, hidden).
void age(const fs::path& p, int year) {
    std::tm tm{};
    tm.tm_year = year - 1900; tm.tm_mon = 2; tm.tm_mday = 1; tm.tm_hour = 12;
    const std::time_t t = std::mktime(&tm);
    struct timespec ts[2] = {{t, 0}, {t, 0}};   // atime, mtime
    ::utimensat(AT_FDCWD, p.c_str(), ts, AT_SYMLINK_NOFOLLOW);
}
void make_sweep_extras(const fs::path& home) {
    put_file(home / "old-letter.txt", "dear");             age(home / "old-letter.txt", 2021);
    put_file(home / "todo.txt", "today");
    put_file(home / "Music/song.mp3", "mp3");              age(home / "Music/song.mp3", 2022);
    put_file(home / "Pictures/Covers/blue.png", "home already");
    put_file(home / "Writing/ideas.txt", "ideas");
    put_file(home / "Downloads/old-movie.mkv", "mkv");     age(home / "Downloads/old-movie.mkv", 2020);
    put_file(home / "Archive/2025/Documents/a.odt", "archived");
    put_file(home / "Archive/stray.txt", "in the archive");
    put_file(home / "notes-repo/README.md", "a repo");     fs::create_directories(home / "notes-repo/.git");
    put_file(home / "Downloads/src-repo/main.c", "c");     fs::create_directories(home / "Downloads/src-repo/.git");
    put_file(home / "Stray-project/a.txt", "a project's");
    put_file(home / ".cache/x.txt", "hidden");
}
// s009: a stamp of purpose + role (the harness marking a product).
latch::door::Plan tagging_set(const std::vector<core::facts::Facts>& fs_, const std::string& purpose, const std::string& role) {
    core::facts::Tags t;
    t.purpose = purpose;
    t.role = role;
    return core::tagging::stamp(fs_, t, "harness: roles");
}
// Every entry under a folder: inode, size, mtime, ctime (atime left out:
// reading a folder may touch it). A read-only pass leaves this unchanged.
std::map<std::string, std::string> tree_facts(const std::string& dir) {
    std::map<std::string, std::string> m;
    for (auto it = fs::recursive_directory_iterator(dir); it != fs::recursive_directory_iterator(); ++it) {
        struct stat st{};
        if (::lstat(it->path().c_str(), &st) != 0) continue;
        m[it->path().string()] = std::to_string(st.st_ino) + "/" + std::to_string(st.st_size) + "/" +
            std::to_string(st.st_mtim.tv_sec) + "." + std::to_string(st.st_mtim.tv_nsec) + "/" +
            std::to_string(st.st_ctim.tv_sec) + "." + std::to_string(st.st_ctim.tv_nsec);
    }
    return m;
}
}  // namespace

namespace {
// --fixture DIR: a home to look at in the app. Downloads + Desktop stamped
// Undecided, Documents stamped with a purpose, then report.odt saved over by
// "an app" and its tags carried back -- three batches in Activity.
int make_fixture(const std::string& dir) {
    namespace tg = core::tagging;
    std::error_code ec;
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec)) {
        std::cerr << "--fixture: " << dir << " exists and is not empty\n";
        return 2;
    }
    const fs::path home = dir;
    make_home(home);
    const std::string H = fs::canonical(home).string();
    const std::string D = core::paths::data_dir(H, nullptr);
    latch::door::set_app_name("Tidy");
    latch::door::Door door(H, D);
    door.load();
    core::catalog::Catalog cat;
    std::string err;
    if (!cat.open(D, true, &err)) { std::cerr << "catalog: " << err << "\n"; return 1; }
    const auto run = [&](latch::door::Plan p) {
        p.previewed = true;   // the harness IS the preview here
        const auto r = door.run(p);
        tg::record(cat, tg::paths_of(door, r.batch), now_s());
        return r.ok;
    };
    auto land = core::scan::walk(H, {H + "/Downloads", H + "/Desktop"});
    bool ok = run(tg::stamp(land, {"", "undecided", "", ""}, "Stamp: landing places (undecided)"));
    auto docs = core::scan::walk(H, {H + "/Documents"});
    ok = run(tg::stamp(docs, {"", "keep", "jasper", "product"}, "Stamp: Documents (Jasper)")) && ok;
    atomic_save(home / "Documents/report.odt", "report v2");
    const auto f = tg::reconcile(cat, core::scan::walk(H, roots_of(H)));
    ok = run(tg::plan_for(f, "Reconcile: 1 atomic save")) && ok && f.carries.size() == 1;
    make_birth_extras(home);   // s005: untagged by Tidy -- the birth report has something to say
    make_projects(home);       // s007: two writing projects to lift from and check for drift
    make_sweep_extras(home);   // s019: loose across the home, some untouched for years
    std::cout << (ok ? "fixture home made: " : "fixture home made WITH ERRORS: ") << H << "\n"
              << "  " << door.batches().size() << " batches, " << cat.count() << " files in the catalog\n"
              << "  TIDY_HOME=" << H << " ./build/tidy\n";
    return ok ? 0 : 1;
}
}  // namespace

// --report HOME: the birth report as text. READ-ONLY (statx + xattr reads).
int print_report(const std::string& dir) {
    std::error_code ec;
    const std::string H = fs::canonical(dir, ec).string();
    if (ec || !fs::is_directory(H)) { std::cerr << "--report: " << dir << " is not a folder\n"; return 2; }
    const auto ps = core::birth::sweep(H, slurp(fs::path(H) / ".config/user-dirs.dirs"));   // s019: the whole home
    std::cout << core::birth::text(core::birth::report(H, ps, now_s(), core::settings::kOldDefault), H);
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--fixture") return make_fixture(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "--report") return print_report(argv[2]);
    // ── The look (Cairn's stone): page states, Tidy's sidebar, every tint ────
    {
        std::cout << "[look]\n";
        namespace lk = core::look;
        using S = lk::State;
        lk::PageStates p;
        check("look: a page starts at the hero", p.state() == S::Start && p.child() == "start");
        check("look: no results without a run", !p.finish());
        check("look: Stop goes back to the hero", p.begin() && p.cancel() && p.state() == S::Start);
        check("look: run, finish -> results", p.begin() && p.finish() && p.child() == "results");

        const auto nav = lk::tidy_nav();
        std::string said;
        for (const auto& i : lk::lint(nav)) said += i + "; ";
        check("look: Tidy's sidebar lints clean", said.empty(), said);
        check("look: the first page shown is The Closet",
              lk::first_item(nav) >= 0 && nav[lk::first_item(nav)].id == "closet");

        const std::string css = slurp(TIDY_LOOK_CSS);
        check("look: look.css is readable", !css.empty(), TIDY_LOOK_CSS);
        std::string missing;
        for (const auto& t : lk::tints())
            for (const std::string& rule : {"window.look.tint-" + t + " {",
                                            "window.tint-" + t + " button.orb {",
                                            "window.tint-" + t + " button.act {",
                                            "window.tint-" + t + " .done-disc {"})
                if (css.find(rule) == std::string::npos) missing += rule + "  ";
        check("look: every tint has its gradient, orb, act and done disc", missing.empty(), missing);
    }

    // ── The pages: one table, the sidebar and the heroes can't drift ────────
    {
        std::cout << "[pages]\n";
        const auto nav = core::look::tidy_nav();
        std::string no_text, no_item, no_art;
        std::set<std::string> ids;
        for (const auto& e : nav) {
            if (e.section) continue;
            ids.insert(e.id);
            if (!core::pages::find(e.id)) no_text += e.id + " ";
            for (const std::string& icon : {"tidy-hero-" + e.id + ".svg", e.icon + ".svg"})
                if (!fs::exists(fs::path(TIDY_ICON_DIR) / icon)) no_art += icon + " ";
        }
        for (const auto& p : core::pages::all())
            if (!ids.count(p.id)) no_item += p.id + " ";
        check("pages: every sidebar page has its promise", no_text.empty(), no_text);
        check("pages: every promise has its sidebar page", no_item.empty(), no_item);
        check("pages: every page has its hero emblem and its symbolic", no_art.empty(), no_art);
        bool ms = true;
        for (const auto& p : core::pages::all())
            ms = ms && std::regex_match(p.milestone, std::regex(R"(s0\d\d)"));
        check("pages: every page names its milestone (sNNN)", ms);
    }

    // ── Paths: Tidy's records by the XDG rule (made-up homes; nothing made) ──
    {
        std::cout << "[paths]\n";
        namespace pa = core::paths;
        check("paths: default ~/.local/share/tidy", pa::data_dir("/home/u", nullptr) == "/home/u/.local/share/tidy");
        check("paths: XDG_DATA_HOME wins", pa::data_dir("/home/u", "/data/x/") == "/data/x/tidy");
        check("paths: an empty XDG_DATA_HOME is ignored", pa::data_dir("/home/u", "") == "/home/u/.local/share/tidy");
        check("paths: a relative XDG_DATA_HOME is ignored", pa::data_dir("/home/u", "rel") == "/home/u/.local/share/tidy");
        check("paths: templates under the data dir",
              pa::templates_dir("/home/u/.local/share/tidy") == "/home/u/.local/share/tidy/templates");
    }

    // ── latch: Tidy's name, and the door's refusals, on a FIXTURE home ──────
    {
        std::cout << "[latch]\n";
        latch::door::set_app_name("Tidy");
        check("latch: the door speaks as Tidy", latch::door::app_name() == "Tidy");

        // A generated tree under a temp dir -- the selftest's own fixture,
        // never the real home. Only refusal() is asked: nothing is moved.
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_" + std::to_string(::getpid()));
        const fs::path home = root / "home";
        const fs::path data = home / ".local/share/tidy";
        fs::create_directories(home / "Documents");
        fs::create_directories(data);
        std::ofstream(home / "Documents/a.txt") << "a";
        std::ofstream(data / "catalog.db") << "x";
        const std::string H = fs::canonical(home).string();
        latch::protect::set_owned({});
        latch::door::Door door(H, fs::canonical(data).string());

        const auto move = [&](const std::string& from, const std::string& to) {
            latch::door::Action a;
            a.verb = latch::door::Verb::Move;
            a.from = from;
            a.to = to;
            return door.refusal(a);
        };
        check("latch: a plain move inside home is allowed",
              move(H + "/Documents/a.txt", H + "/Documents/b.txt").empty(),
              move(H + "/Documents/a.txt", H + "/Documents/b.txt"));
        check("latch: outside home is refused", !move("/etc/hostname", H + "/x").empty());
        check("latch: home itself is refused", !move(H, H + "/x").empty());
        const std::string own = move(H + "/.local/share/tidy/catalog.db", H + "/Documents/c.db");
        check("latch: Tidy's own records are refused, by name", own.find("Tidy") != std::string::npos, own);

        // RULES: the never-touch list is code; try EACH entry, expect a refusal.
        std::string let_through;
        for (const auto& rel : latch::protect::never_touch()) {
            fs::create_directories(home / rel);
            const std::string why = move(H + "/" + rel, H + "/Documents/moved");
            if (why.empty()) let_through += rel + " ";
        }
        check("latch: every never-touch entry is refused (" +
              std::to_string(latch::protect::never_touch().size()) + ")", let_through.empty(), let_through);
        fs::create_directories(home / "Develop/x-project/fixes");
        std::ofstream(home / "Develop/x-project/fixes/patch.txt") << "p";
        check("latch: anything under a fixes/ is refused",
              !move(H + "/Develop/x-project/fixes/patch.txt", H + "/Documents/patch.txt").empty());

        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s004: facts, scan, the catalog, the tags -- on a FIXTURE home ───────
    {
        std::cout << "[catalog]\n";
        namespace fc = core::facts;
        namespace tg = core::tagging;
        latch::door::set_app_name("Tidy");
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_cat_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        const std::string H = fs::canonical(home).string();
        const std::string D = core::paths::data_dir(H, nullptr);
        const std::string report = H + "/Documents/report.odt";

        // facts
        fc::Facts f;
        struct stat st{};
        ::lstat(report.c_str(), &st);
        check("facts: statx reads the inode lstat sees (dev, ino, size, mtime)",
              fc::of(report, f) && f.dev == st.st_dev && f.ino == st.st_ino && f.size == 9 &&
              f.mtime == st.st_mtim.tv_sec && !f.tags.any());
        check("facts: birth time", f.has_btime && f.btime > 0, f.has_btime ? "btime" : "this filesystem gives no btime");
        {
            std::set<std::string> ids;
            bool all = true;
            for (int i = 0; i < 1000; ++i) { auto id = fc::new_id(); all = all && fc::is_id(id); ids.insert(id); }
            check("facts: 1000 new ids, all well-formed v4 UUIDs, all different", all && ids.size() == 1000);
        }

        // scan
        const auto scanned = core::scan::walk(H, roots_of(H));
        std::set<std::string> names;
        for (const auto& s : scanned) names.insert(s.path.substr(H.size() + 1));
        const std::set<std::string> want = {"Desktop/Screenshot 2026-11-10.png", "Documents/Jasper.scriv",
                                            "Documents/report.odt", "Downloads/cat.jpg", "Downloads/f1040.pdf"};
        std::string got;
        for (const auto& n : names) got += n + "; ";
        check("scan: files + the package as one item; no hidden, symlink, fixes/, nor an unasked folder",
              names == want, got);
        check("scan: refuses a root outside home or Protected",
              core::scan::walk(H, {"/etc", H + "/Documents/fixes"}).empty());

        // the catalog
        {
            core::catalog::Catalog ro;
            check("catalog: read-only open of no catalog fails and makes nothing",
                  !ro.open(D + "-none", false) && !fs::exists(D + "-none"));
        }
        core::catalog::Catalog cat;
        std::string err;
        check("catalog: opened for writing at <data>/catalog.db",
              cat.open(D, true, &err) && fs::exists(D + "/catalog.db"), err);

        // a snapshot of every user file: bytes + mtime (tagging must change neither)
        const auto snap = [&] {
            std::string out;
            for (const auto& e : fs::recursive_directory_iterator(home)) {
                const std::string p = e.path().string();
                if (p.find("/.local/share/tidy") != std::string::npos || !e.is_regular_file()) continue;
                struct stat s2{};
                ::lstat(p.c_str(), &s2);
                out += p + " " + std::to_string(s2.st_ino) + " " + std::to_string(s2.st_size) + " " +
                       std::to_string(s2.st_mtim.tv_sec) + "." + std::to_string(s2.st_mtim.tv_nsec) + " " +
                       slurp(p) + "\n";
            }
            return out;
        };

        latch::door::Door door(H, D);
        door.load();
        const auto run = [&](latch::door::Plan p) {
            p.previewed = true;
            const auto r = door.run(p);
            tg::record(cat, tg::paths_of(door, r.batch), now_s());
            return r;
        };
        const auto undo = [&] {
            std::uint64_t b = 0;   // the batch undo will take back: the newest not undone
            for (auto it = door.batches().rbegin(); it != door.batches().rend(); ++it)
                if (!it->undone) { b = it->id; break; }
            const auto r = door.undo();
            tg::after_undo(cat, door, b, now_s());
            return r;
        };

        // stamp
        const std::string before = snap();
        auto plan = tg::stamp(scanned, {}, "Stamp: ids");
        check("stamp: one id action per item, each a Tag of user.tidy.id with a fresh UUID",
              plan.actions.size() == 5 &&
              std::all_of(plan.actions.begin(), plan.actions.end(), [](const auto& a) {
                  return a.verb == latch::door::Verb::Tag && a.attr == fc::kId && fc::is_id(a.content);
              }));
        {
            auto unpreviewed = plan;
            unpreviewed.previewed = false;
            const auto r = door.run(unpreviewed);
            check("stamp: an unpreviewed plan is refused by the door, nothing tagged",
                  !r.ok && fc::tags_of(report).id.empty());
        }
        auto r = run(plan);
        check("stamp: through the door, one batch, 5 steps", r.ok && r.done == 5 && door.batches().size() == 1);
        check("stamp: catalog holds the 5, by id / inode / path", cat.count() == 5 && [&] {
            const auto t = fc::tags_of(report);
            fc::Facts g;
            fc::of(report, g);
            const auto a = cat.by_id(t.id), b = cat.by_inode(g.dev, g.ino), c = cat.by_path(report);
            return a && b && c && a->id == b->id && b->id == c->id && a->path == report;
        }());
        check("stamp: no user file's bytes, inode or mtime changed", snap() == before);
        check("stamp: again finds nothing to do (an id is never replaced)",
              tg::stamp(core::scan::walk(H, roots_of(H)), {}, "again").actions.empty());

        // a retention, undone
        const std::string report_id = fc::tags_of(report).id;
        r = run(tg::stamp(core::scan::walk(H, {H + "/Documents"}), {"", "keep", "jasper", ""}, "Stamp: keep"));
        check("tags: retention + purpose set on Documents' 2 items (4 steps), catalog follows",
              r.ok && r.done == 4 && fc::tags_of(report).retention == "keep" &&
              cat.by_id(report_id)->purpose == "jasper");
        auto u = undo();
        check("tags: undo takes them back, the id stays, catalog follows",
              u.ok && fc::tags_of(report).retention.empty() && fc::tags_of(report).id == report_id &&
              cat.by_id(report_id)->retention.empty());
        run(tg::stamp(core::scan::walk(H, {H + "/Documents"}), {"", "keep", "", ""}, "Stamp: keep"));

        // reconcile: an atomic save
        fc::Facts was;
        fc::of(report, was);
        atomic_save(report, "report v2");
        auto fnd = tg::reconcile(cat, core::scan::walk(H, roots_of(H)));
        check("reconcile: an atomic save is found (same name, new inode, no tags)",
              fnd.carries.size() == 1 && fnd.carries[0].was.id == report_id && fnd.copies.empty() && fnd.gone.empty());
        r = run(tg::plan_for(fnd, "Reconcile"));
        fc::Facts now;
        fc::of(report, now);
        check("reconcile: id + retention carried to the new inode; catalog has the new inode",
              r.ok && now.tags.id == report_id && now.tags.retention == "keep" && now.ino != was.ino &&
              cat.by_id(report_id)->ino == now.ino);
        {   // the carry undone: the id stays known, the inode unknown -> proposed again; redone
            const auto cu = undo();
            const auto e = cat.by_id(report_id);
            const auto again = tg::reconcile(cat, core::scan::walk(H, roots_of(H)));
            check("reconcile: the carry undone -> tags off, catalog keeps the id (inode unknown), carry proposed again",
                  cu.ok && fc::tags_of(report).id.empty() && e && e->ino == 0 && again.carries.size() == 1);
            r = run(tg::plan_for(again, "Reconcile again"));
            check("reconcile: ...and carried again", r.ok && fc::tags_of(report).id == report_id &&
                                                    cat.by_id(report_id)->ino == now.ino);
        }
        check("reconcile: nothing more to do after", [&] {
            const auto g = tg::reconcile(cat, core::scan::walk(H, roots_of(H)));
            return g.carries.empty() && g.copies.empty() && g.gone.empty();
        }());

        // a move keeps its tags (xattrs travel with mv); record follows by id
        const std::string cat_jpg = H + "/Downloads/cat.jpg", moved = H + "/Documents/cat.jpg";
        const std::string cat_id = fc::tags_of(cat_jpg).id;
        fs::rename(cat_jpg, moved);
        fnd = tg::reconcile(cat, core::scan::walk(H, roots_of(H)));
        tg::record(cat, {moved}, now_s());
        check("reconcile: a moved file is no carry and not gone; the catalog follows its id",
              fnd.carries.empty() && fnd.gone.empty() && cat.by_id(cat_id)->path == moved);

        // a copy carrying the id (cp -a)
        const std::string dup = H + "/Downloads/f1040 copy.pdf", orig = H + "/Downloads/f1040.pdf";
        put_file(dup, "irs form");
        const std::string orig_id = fc::tags_of(orig).id;
        ::setxattr(dup.c_str(), fc::kId, orig_id.data(), orig_id.size(), 0);
        fnd = tg::reconcile(cat, core::scan::walk(H, roots_of(H)));
        check("reconcile: a copy carrying a known id is found; the catalog's inode keeps it",
              fnd.copies.size() == 1 && fnd.copies[0].file.path == dup && fc::is_id(fnd.copies[0].new_id));
        tg::record(cat, {dup}, now_s());
        check("record: a copy carrying a known id never takes the original's entry",
              cat.by_id(orig_id)->path == orig);
        r = run(tg::plan_for(fnd, "Reconcile: copy"));
        check("reconcile: the copy gets its own id, the original keeps its",
              r.ok && fc::tags_of(dup).id == fnd.copies[0].new_id && fc::tags_of(orig).id == orig_id &&
              cat.count() == 6);

        // gone
        const std::string shot = H + "/Desktop/Screenshot 2026-11-10.png";
        const std::string shot_id = fc::tags_of(shot).id;
        fs::remove(shot);
        fnd = tg::reconcile(cat, core::scan::walk(H, roots_of(H)));
        check("reconcile: a vanished file is gone (no door step: nothing to touch)",
              fnd.gone.size() == 1 && fnd.gone[0] == shot_id && tg::plan_for(fnd, "x").actions.empty());
        check("catalog: marked gone, out of the count, still on record",
              cat.mark_gone(shot_id, now_s()) && cat.count() == 5 && cat.count(true) == 6 && cat.by_id(shot_id)->gone);

        // undo all the way down: every tag back off, catalog empty, bytes as left
        const std::string files_now = snap();
        int undone = 0;
        std::string refusals;
        while (door.can_undo()) {
            u = undo();
            if (u.ok) ++undone;
            for (const auto& m : u.messages) refusals += m.substr(m.rfind(" -- ") + 4) + "; ";
        }
        std::set<std::string> left;
        for (const auto& s : core::scan::walk(H, roots_of(H)))
            if (s.tags.any()) left.insert(s.path.substr(H.size() + 1));
        std::string l;
        for (const auto& x : left) l += x + "; ";
        // What the door can't take back is what changed behind its back: the
        // file moved by plain mv (no longer where it was tagged), the vanished
        // screenshot, and the copy whose id the harness wrote as cp -a would.
        check("undo: every batch walked; the door leaves only what changed behind its back",
              !door.can_undo() && left == std::set<std::string>{"Documents/cat.jpg", "Downloads/f1040 copy.pdf"},
              std::to_string(undone) + " clean; left: " + l + " refused: " + refusals);
        check("undo: no user file's bytes or mtime changed by any of it", snap() == files_now);
        {
            std::string kept;
            for (const auto& e : cat.all()) kept += e.path.substr(H.size() + 1) + "; ";
            check("undo: the stamps undone leave the catalog only what the door could not take back",
                  cat.count(true) == 3, kept);
        }
        check("untouched: a folder Tidy was not asked to scan has no tag",
              !fc::tags_of(H + "/Pictures/landscape.png").any());

        // reload: the manifest and the catalog survive a restart
        {
            latch::door::Door again(H, D);
            again.load();
            core::catalog::Catalog ro;
            check("reload: the door's batches and the catalog read back (read-only)",
                  again.batches().size() == door.batches().size() && ro.open(D, false) && !ro.writable() &&
                  ro.count(true) == cat.count(true));
        }
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── The source gate: no Tidy source writes a file ────────────────────────────
    {
        std::cout << "[gate]\n";
        check("gate: the matcher finds a planted ::rename(",
              !gate_hits("planted", "void f(){ ::rename(\"a\",\"b\"); }").empty());
        check("gate: the matcher finds a planted std::ofstream",
              !gate_hits("planted", "std::ofstream out(p);").empty());
        check("gate: the matcher finds fs::remove_all",
              !gate_hits("planted", "fs::remove_all(p);").empty());
        check("gate: ...but not the word in a comment or a string",
              gate_hits("planted", "// rename( it\nauto s = \"::rename(\";").empty());
        check("gate: ...nor a widget's remove()", gate_hits("planted", "box.remove(*child);").empty());
        check("gate: ...nor registry::remove()", gate_hits("planted", "registry::remove(this);").empty());
        check("gate: ...but a global ::remove( is found", !gate_hits("planted", "x = ::remove(p);").empty());
        check("gate: a planted lsetxattr( is found (tags: the door only)",
              !gate_hits("planted", "lsetxattr(p, n, v, l, 0);").empty());
        check("gate: SQLite outside Catalog.cpp is found",
              !gate_hits("Shell.cpp", "sqlite3_exec(db, q, 0, 0, 0);").empty());
        check("gate: ...Catalog.cpp may use SQLite and make its folder",
              gate_hits("Catalog.cpp", "sqlite3_exec(db, q, 0, 0, 0); fs::create_directories(d, ec);").empty());
        check("gate: Own.cpp may make its folder, write a temp, rename it in",
              gate_hits("Own.cpp", "fs::create_directories(d, ec); std::ofstream out(t); std::rename(a, b);").empty());
        check("gate: ...but a remove in Own.cpp is found, and its three calls anywhere else",
              !gate_hits("Own.cpp", "fs::remove(p);").empty() &&
              !gate_hits("Shell.cpp", "std::ofstream out(t);").empty() && !gate_hits("Shell.cpp", "std::rename(a, b);").empty());
        check("gate: ...but any other write in Catalog.cpp is found",
              !gate_hits("Catalog.cpp", "fs::create_directories(d); fs::remove(p);").empty());

        std::string hits;
        int files = 0;
        for (const char* dir : {TIDY_SRC_DIR "/src", TIDY_SRC_DIR "/include"}) {
            for (const auto& e : fs::recursive_directory_iterator(dir)) {
                if (!e.is_regular_file()) continue;
                const auto ext = e.path().extension();
                if (ext != ".cpp" && ext != ".hpp" && ext != ".h") continue;
                if (e.path().filename() == "selftest.cpp") continue;   // the harness makes its fixtures
                ++files;
                for (const auto& h : gate_hits(e.path().filename().string(), slurp(e.path()))) hits += h + "; ";
            }
        }
        check("gate: no Tidy source writes a file, tags only through the door, SQLite only in Catalog.cpp (" + std::to_string(files) + " files read)",
              files > 10 && hits.empty(), hits);
    }

    // ── s005: birth rules + retention levels -- REPORT ONLY, on a FIXTURE ──
    {
        std::cout << "[birth]\n";
        namespace rt = core::retention;
        namespace cb = core::birth;
        namespace fc = core::facts;

        // The seven levels (§2; §11.2 open -- the lean: as written).
        std::set<std::string> ids;
        bool round = true;
        for (const auto& i : rt::all()) {
            ids.insert(i.id);
            rt::Level l{};
            round = round && rt::parse(i.id, l) && l == i.level && rt::id_of(l) == i.id;
        }
        check("levels: seven, ids unique, each parses back to itself", rt::all().size() == 7 && ids.size() == 7 && round);
        rt::Level dummy{};
        check("levels: the on-disk ids are the ones s004 stamped",
              rt::id_of(rt::Level::Undecided) == "undecided" && rt::id_of(rt::Level::Keep) == "keep" &&
              rt::id_of(rt::Level::UntilDone) == "until-done" && rt::id_of(rt::Level::HandsOff) == "hands-off");
        check("levels: \"\", a title, a stranger -- none parse",
              !rt::parse("", dummy) && !rt::parse("Keep", dummy) && !rt::parse("forever", dummy));

        // The landing places, from user-dirs.dirs (made-up homes; nothing read).
        const auto def = cb::places("/home/u", "");
        check("places: the defaults -- Downloads, Screenshots, Desktop, Documents (top only)",
              def.size() == 4 && def[0].path == "/home/u/Downloads" && def[1].path == "/home/u/Pictures/Screenshots" &&
              def[2].path == "/home/u/Desktop" && def[3].path == "/home/u/Documents" && def[3].top_only && !def[0].top_only);
        const auto moved = cb::places("/home/u", "# comment\nXDG_DOWNLOAD_DIR=\"$HOME/Incoming\"\n"
                                                 "XDG_PICTURES_DIR=\"/home/u/Bilder\"\n");
        check("places: user-dirs.dirs moves them ($HOME and absolute forms)",
              moved.size() == 4 && moved[0].path == "/home/u/Incoming" && moved[1].path == "/home/u/Bilder/Screenshots");
        const auto bad = cb::places("/home/u", "XDG_DESKTOP_DIR=\"$HOME/\"\nXDG_DOWNLOAD_DIR=\"/tmp/dl\"\n"
                                               "XDG_DOCUMENTS_DIR=\"docs\"\nXDG_PICTURES_DIR=\"$HOME/../x\"\n");
        check("places: home itself, outside home, relative, '..' -- all dropped (never the whole home)", bad.empty(),
              std::to_string(bad.size()) + " kept");
        const auto same = cb::places("/home/u", "XDG_DESKTOP_DIR=\"$HOME/Downloads\"\n");
        check("places: two names for one folder -- walked once", same.size() == 3);

        // The pieces.
        check("rules: host_of", cb::host_of("https://www.IRS.gov:443/f.pdf") == "www.irs.gov" &&
                                cb::host_of("https://u:p@x.org/a?b") == "x.org" && cb::host_of("nonsense").empty() &&
                                cb::host_of("file:///home/u/a").empty());
        check("rules: screenshot names (GNOME old and new, macOS, screencasts)",
              cb::is_screenshot_name("Screenshot from 2024-01-02 10-11-12.png") &&
              cb::is_screenshot_name("Screenshot From 2026-11-10 09-14-02.png") &&
              cb::is_screenshot_name("Screen Shot 2019-03-04 at 1.png") && cb::is_screenshot_name("Screencast.webm") &&
              !cb::is_screenshot_name("my screenshot.png"));
        check("rules: ext_of", cb::ext_of("A.TAR.GZ") == "gz" && cb::ext_of(".bashrc").empty() && cb::ext_of("x").empty() &&
                               cb::ext_of("Fedora.AppImage") == "appimage");

        // classify(), one rule at a time, on made-up facts.
        const auto F = [](const std::string& path, const std::string& retention = "", bool dir = false) {
            fc::Facts f;
            f.path = path;
            f.dir = dir;
            f.tags.retention = retention;
            return f;
        };
        const auto is = [](const cb::Born& b, const char* rule, rt::Level l) { return b.rule == rule && b.level == l; };
        check("rules: a decided stamp wins over any birth (a screenshot stamped Keep stays Keep)",
              is(cb::classify(F("/h/Pictures/Screenshots/Screenshot.png", "keep"), "screenshots", ""), "stamped", rt::Level::Keep));
        check("rules: a stamped Undecided is still proposed for (it was waiting, not decided)",
              is(cb::classify(F("/h/Downloads/a.png", "undecided"), "downloads", ""), "image-download", rt::Level::Ephemeral));
        check("rules: an unknown retention value is not a decision",
              is(cb::classify(F("/h/Desktop/a.txt", "forever"), "desktop", ""), "unknown", rt::Level::Undecided));
        check("rules: anything in Screenshots that is an image -> Ephemeral",
              is(cb::classify(F("/h/Pictures/Screenshots/whatever.png"), "screenshots", ""), "screenshot", rt::Level::Ephemeral));
        check("rules: a screenshot by name on the Desktop -> Ephemeral",
              is(cb::classify(F("/h/Desktop/Screenshot 2026.png"), "desktop", ""), "screenshot", rt::Level::Ephemeral));
        check("rules: ...but not a text file named Screenshot",
              is(cb::classify(F("/h/Desktop/Screenshot notes.txt"), "desktop", ""), "unknown", rt::Level::Undecided));
        check("rules: a .gov download -> Until done",
              is(cb::classify(F("/h/Downloads/f1040.pdf"), "downloads", "https://www.irs.gov/f1040.pdf"), "gov-download", rt::Level::UntilDone));
        check("rules: a .gov origin counts outside Downloads too (the origin travels)",
              is(cb::classify(F("/h/Desktop/w9.pdf"), "desktop", "https://irs.gov/w9.pdf"), "gov-download", rt::Level::UntilDone));
        check("rules: 'gov' inside a host is not .gov (gov.example.com)",
              is(cb::classify(F("/h/Downloads/a.pdf"), "downloads", "https://gov.example.com/a.pdf"), "in-downloads", rt::Level::Ephemeral));
        check("rules: a downloaded image -> Ephemeral",
              is(cb::classify(F("/h/Downloads/cat.JPG"), "downloads", "https://x.com/cat.jpg"), "image-download", rt::Level::Ephemeral));
        check("rules: a downloaded installer -> Ephemeral",
              is(cb::classify(F("/h/Downloads/tool.AppImage"), "downloads", ""), "installer-download", rt::Level::Ephemeral));
        check("rules: anything else in Downloads -> Ephemeral (s020a), its origin named",
              [&] { auto b = cb::classify(F("/h/Downloads/b.pdf"), "downloads", "https://shop.example.com/b.pdf");
                    return is(b, "in-downloads", rt::Level::Ephemeral) && b.why.find("shop.example.com") != std::string::npos; }());
        check("rules: a download moved out of Downloads (its origin kept) -> Undecided, to the Closet",
              is(cb::classify(F("/h/Desktop/a.zip"), "desktop", "https://files.example.com/a.zip"), "download",
                 rt::Level::Undecided));
        check("rules: a package is one thing -- never a screenshot, never an installer",
              is(cb::classify(F("/h/Downloads/Screenshot.scriv", "", true), "downloads", ""), "in-downloads", rt::Level::Ephemeral));
        check("rules: a loose document with no story -> Undecided",
              is(cb::classify(F("/h/Documents/loose.txt"), "documents", ""), "unknown", rt::Level::Undecided));

        // The report, on a FIXTURE home.
        latch::door::set_app_name("Tidy");
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_birth_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_birth_extras(home);
        fs::remove_all(home / ".local");   // no records at all: the report must not make any
        const std::string H = fs::canonical(home).string();

        // Everything about every file under home, before.
        const auto snapshot = [&] {
            std::map<std::string, std::string> m;
            for (auto it = fs::recursive_directory_iterator(H); it != fs::recursive_directory_iterator(); ++it) {
                struct stat st{};
                if (::lstat(it->path().c_str(), &st) != 0) continue;
                std::string v = std::to_string(st.st_ino) + "/" + std::to_string(st.st_size) + "/" +
                                std::to_string(st.st_mtim.tv_sec) + "." + std::to_string(st.st_mtim.tv_nsec) + "/" +
                                std::to_string(st.st_ctim.tv_sec) + "." + std::to_string(st.st_ctim.tv_nsec);
                char names[4096];
                const ssize_t n = ::llistxattr(it->path().c_str(), names, sizeof names);
                for (ssize_t k = 0; k < n; k += static_cast<ssize_t>(std::strlen(names + k)) + 1) {
                    char val[1024];
                    const ssize_t vl = ::lgetxattr(it->path().c_str(), names + k, val, sizeof val);
                    v += std::string("|") + (names + k) + "=" + (vl > 0 ? std::string(val, static_cast<std::size_t>(vl)) : "");
                }
                m[it->path().string()] = v;
            }
            return m;
        };
        const auto before = snapshot();
        const auto ps = cb::places(H, "");
        const auto rep = cb::report(H, ps);
        const auto after = snapshot();

        const auto find = [&](const std::string& rel) -> const cb::Born* {
            for (const auto& b : rep.items)
                if (b.file.path == H + "/" + rel) return &b;
            return nullptr;
        };
        const auto has = [&](const std::string& rel, const char* rule, rt::Level l) {
            const auto* b = find(rel);
            return b && b->rule == rule && b->level == l;
        };
        check("report: all four landing places found", rep.places.size() == 4, std::to_string(rep.places.size()));
        check("report: the IRS form, by its origin -> Until done",
              has("Downloads/f1040.pdf", "gov-download", rt::Level::UntilDone));
        check("report: cat.jpg -> Ephemeral; the .rpm -> Ephemeral",
              has("Downloads/cat.jpg", "image-download", rt::Level::Ephemeral) &&
              has("Downloads/fedora-44.rpm", "installer-download", rt::Level::Ephemeral));
        check("report: Screenshots (png + webm) and the Desktop screenshot -> Ephemeral",
              has("Pictures/Screenshots/Screenshot From 2026-11-10 09-14-02.png", "screenshot", rt::Level::Ephemeral) &&
              has("Pictures/Screenshots/Screencast From 2026-11-10 09-20-11.webm", "screenshot", rt::Level::Ephemeral) &&
              has("Desktop/Screenshot 2026-11-10.png", "screenshot", rt::Level::Ephemeral));
        check("report: a file carrying Keep stays Keep", has("Desktop/cover-final.png", "stamped", rt::Level::Keep));
        check("report: notes.txt (no origin), brochure.pdf (no rule) -> Undecided",
              has("Downloads/notes.txt", "in-downloads", rt::Level::Ephemeral) &&
              has("Downloads/brochure.pdf", "in-downloads", rt::Level::Ephemeral));
        check("report: Documents -- the loose top only (loose.txt, report.odt, the .scriv whole; not Taxes/w2.pdf)",
              has("Documents/loose.txt", "unknown", rt::Level::Undecided) && find("Documents/report.odt") &&
              find("Documents/Jasper.scriv") && !find("Documents/Taxes/w2.pdf") &&
              !find("Documents/Jasper.scriv/Files/Data/1/content.rtf"));
        check("report: never hidden, a symlink, fixes/, or a folder not a landing place",
              !find("Downloads/.part") && !find("Downloads/report-link.odt") && !find("Documents/fixes/patch.txt") &&
              !find("Pictures/landscape.png"));
        std::string unused;
        for (const auto& r : cb::rules())
            if (!rep.count_rule(r.id) && r.id != "old" && r.id != "download")   // old: s019's checks; download: below
                unused += r.id + " ";
        check("report: every birth rule fits at least one fixture file", unused.empty(), unused);
        std::size_t by_level = 0;
        for (const auto& l : rt::all()) by_level += rep.count(l.level);
        check("report: every item has exactly one proposed level", by_level == rep.items.size() && !rep.items.empty(),
              std::to_string(rep.items.size()) + " items");

        std::string changed;
        for (const auto& [p, v] : before) {
            const auto it = after.find(p);
            if (it == after.end() || it->second != v) changed += p.substr(H.size()) + " ";
        }
        check("report: REPORT ONLY -- no file's inode, size, mtime, ctime or xattrs changed",
              changed.empty() && before.size() == after.size(), changed);
        check("report: ...and it made no records (no data folder, no catalog, no manifest)",
              !fs::exists(H + "/.local"));
        const std::string t = cb::text(rep, H);
        check("report: the text form says REPORT ONLY and groups by level",
              t.find("REPORT ONLY") != std::string::npos && t.find("\nEphemeral (") != std::string::npos &&
              t.find("~/Downloads/f1040.pdf") != std::string::npos);

        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s019: the home sweep -- the whole home, loose; the "old" rule -- on a FIXTURE ──
    {
        std::cout << "[sweep]\n";
        namespace rt = core::retention;
        namespace cb = core::birth;
        namespace cl = core::closet;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_sweep_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_birth_extras(home);
        make_projects(home);
        make_sweep_extras(home);
        fs::remove_all(home / ".local");
        const std::string H = fs::canonical(home).string();
        const auto before = tree_facts(H);
        const auto ps = cb::sweep(H, "");
        const std::int64_t now = now_s();
        const auto rep = cb::report(H, ps, now, 2);
        const auto find = [&](const std::string& rel) -> const cb::Born* {
            for (const auto& b : rep.items)
                if (b.file.path == H + "/" + rel) return &b;
            return nullptr;
        };
        const auto has = [&](const std::string& rel, const char* rule, rt::Level l) {
            const auto* b = find(rel);
            return b && b->rule == rule && b->level == l;
        };
        std::string tops;
        for (const auto& p : ps) if (p.id == "top") tops += p.title + " ";
        check("sweep: the landing places, then home's top, then each top folder (not Archive, a repo, a -project, hidden, a landing place)",
              ps.size() >= 6 && ps[4].id == "home" && ps[4].path == H &&
              tops == "Develop Music Pictures Writing ", tops);
        check("sweep: home's own top -- todo.txt (Undecided) and old-letter.txt",
              has("todo.txt", "unknown", rt::Level::Undecided) && find("old-letter.txt"));
        check("sweep: a top folder's top is loose (Pictures/landscape.png, Writing/ideas.txt); its subfolder is home (Pictures/Covers/blue.png)",
              find("Pictures/landscape.png") && find("Writing/ideas.txt") && !find("Pictures/Covers/blue.png"));
        check("sweep: Screenshots still walked whole (a landing place inside Pictures)",
              has("Pictures/Screenshots/Screenshot From 2026-11-10 09-14-02.png", "screenshot", rt::Level::Ephemeral));
        check("sweep: never ~/Archive, a repo at home's top, a repo in Downloads, a -project (top or on the Desktop), hidden",
              !find("Archive/stray.txt") && !find("Archive/2025/Documents/a.odt") && !find("notes-repo/README.md") &&
              !find("Downloads/src-repo/main.c") && !find("Stray-project/a.txt") &&
              !find("Desktop/Essay-project/draft.odt") && !find("Desktop/Essay-project/Research/notes.txt") &&
              !find(".cache/x.txt") && !find("Develop/gtk/tidy-project/tidy/CMakeLists.txt"));
        check("sweep: a file is reported once", [&] {
            std::set<std::string> seen;
            for (const auto& b : rep.items) if (!seen.insert(b.file.path).second) return false;
            return true;
        }());
        check("old: untouched 2 years -> Historical, \"untouched since 2021\"",
              has("old-letter.txt", "old", rt::Level::Historical) && find("old-letter.txt")->why == "untouched since 2021",
              find("old-letter.txt") ? find("old-letter.txt")->rule + ": " + find("old-letter.txt")->why : "missing");
        check("old: at a top folder's top (Music/song.mp3, 2022) and in Downloads (an old .mkv, before \"a download\")",
              has("Music/song.mp3", "old", rt::Level::Historical) &&
              has("Downloads/old-movie.mkv", "in-downloads", rt::Level::Ephemeral));   // s020a: Downloads is never "old"
        check("old: a decided file is never re-proposed; a fresh one is not old",
              has("Desktop/cover-final.png", "stamped", rt::Level::Keep) && !has("todo.txt", "old", rt::Level::Historical));
        {
            const auto off = cb::report(H, ps, now, 0);
            bool none = true;
            for (const auto& b : off.items) none = none && b.rule != "old";
            check("old: Settings 0 = off -- nothing proposed for its age", none);
            const auto ten = cb::report(H, ps, now, 10);
            bool young = true;
            for (const auto& b : ten.items) young = young && b.rule != "old";
            check("old: 10 years -- 2020 is not old enough", young);
        }
        {
            core::facts::Facts f;
            f.path = "/h/x.txt";
            f.mtime = now - 3 * 365 * 86400;
            f.atime = now - 10 * 86400;   // read last week
            check("old: read lately is not untouched (atime counts)",
                  cb::classify(f, "home", "", now, 2).rule == "unknown");
            f.atime = 0;
            check("old: an unknown atime falls back to mtime", cb::classify(f, "home", "", now, 2).rule == "old");
            f.dir = true;
            check("old: a package is never old by its folder's time", cb::classify(f, "home", "", now, 2).rule == "unknown");
        }
        {
            core::settings::Settings st;
            const auto v = cl::view(rep, now, st);
            const auto in = [&](const std::string& rel) {
                for (const auto& b : v.proposed) if (b.file.path == H + "/" + rel) return b.level == rt::Level::Historical;
                return false;
            };
            check("closet: the old files are Proposed, Historical", in("old-letter.txt") && in("Music/song.mp3"));
            check("settings: old_years default 2, round trip, clamped, 0 kept",
                  st.old_years == 2 && core::settings::parse("old_years=7\n").old_years == 7 &&
                  core::settings::parse("old_years=99\n").old_years == 50 &&
                  core::settings::parse(core::settings::format(core::settings::parse("old_years=0\n"))).old_years == 0);
        }
        check("sweep: READ-ONLY -- no inode, size, mtime or ctime changed", tree_facts(H) == before);
        {
            const auto a = core::due::look(H, H + "/.tidy-data-none", "", now);
            check("due: the headless look counts the sweep's Closet", a.closet >= 3, std::to_string(a.closet));
            // Found in the drive: a Historical file at home's top never came due (the walk was top folders only).
            const fs::path letter = home / "old-letter.txt";
            ::lsetxattr(letter.c_str(), core::facts::kRetention, "historical", 10, 0);
            age(letter, 2021);
            std::vector<core::due::Item> items;
            core::due::look(H, H + "/.tidy-data-none", "", now, &items);
            bool due_letter = false;
            for (const auto& it : items) due_letter = due_letter || (it.path == H + "/old-letter.txt" && it.to == core::due::To::Archive);
            check("due: a Historical file at home's top comes due to archive", due_letter);
            check("archive: a file at home's top goes to ~/Archive/<year> itself",
                  core::archive::folder_for(H, H + "/old-letter.txt", 2026) == "Archive/2026");
        }
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s019a: Hands-off folders + Samson's places -- on a FIXTURE ─────────────
    {
        std::cout << "[hands-off]\n";
        namespace pr = core::protection;
        namespace cb = core::birth;
        namespace rt = core::retention;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_handsoff_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_sweep_extras(home);
        fs::remove_all(home / ".local");
        put_file(home / "Samson/notes.txt", "samson's");               age(home / "Samson/notes.txt", 2020);
        put_file(home / "Vault/ledger.txt", "samson's, moved");
        put_file(home / "Taxes/2025.pdf", "yours");                    age(home / "Taxes/2025.pdf", 2021);
        put_file(home / "Downloads/Keep-out/x.bin", "deep in a landing place");
        const std::string H = fs::canonical(home).string();
        const std::string cfg = H + "/.config";
        {
            std::string r;
            check("rel_of: ~/Taxes, /home/.../Taxes, Taxes/ -> Taxes",
                  pr::rel_of(H, "~/Taxes", r).empty() && r == "Taxes" && pr::rel_of(H, H + "/Taxes", r).empty() &&
                  r == "Taxes" && pr::rel_of(H, " Taxes/ ", r).empty() && r == "Taxes");
            check("rel_of: refuses home itself, outside home, ..",
                  !pr::rel_of(H, "~", r).empty() && !pr::rel_of(H, H, r).empty() &&
                  !pr::rel_of(H, "/etc", r).empty() && !pr::rel_of(H, "~/a/../b", r).empty() &&
                  !pr::rel_of(H, "", r).empty());
        }
        check("samson: not installed (no ~/.config/samson) -> nothing of its own", pr::samson(H, cfg).empty());
        core::settings::Settings st;
        st.hands_off = {"Taxes", "Downloads/Keep-out"};
        check("settings: hands_off round trip, a bad line dropped",
              core::settings::parse(core::settings::format(st)).hands_off == st.hands_off &&
              core::settings::parse("hands_off=/etc\nhands_off=../x\nhands_off=Taxes\n").hands_off ==
                  std::vector<std::string>{"Taxes"});
        latch::protect::set_owned({});
        const auto rep0 = cb::report(H, cb::sweep(H, ""), now_s(), 2);
        const auto in = [&](const cb::Report& r, const std::string& rel) {
            for (const auto& b : r.items) if (b.file.path == H + "/" + rel) return true;
            return false;
        };
        check("control: unprotected, the sweep sees ~/Samson, ~/Taxes, Downloads/Keep-out",
              in(rep0, "Samson/notes.txt") && in(rep0, "Taxes/2025.pdf") && in(rep0, "Downloads/Keep-out/x.bin"));
        fs::create_directories(home / ".config/samson");   // Samson installed, its defaults
        check("samson: installed -> ~/Samson and its catalog",
              pr::samson(H, cfg) == std::vector<std::string>{"Samson", ".local/share/samson"});
        put_file(home / ".config/samson/settings.json", R"({"samson_dir":"~/Vault","second_home":"/elsewhere"})");
        check("samson: its own settings -- samson_dir ~/Vault; a place outside home left out",
              pr::samson(H, cfg) == std::vector<std::string>{"Vault", ".local/share/samson"});
        fs::remove(home / ".config/samson/settings.json");
        check("all: yours + Samson's, once each",
              pr::all(H, cfg, st) == std::vector<std::string>{"Taxes", "Downloads/Keep-out", "Samson", ".local/share/samson"});
        pr::apply(H, cfg, st);
        const auto rep = cb::report(H, cb::sweep(H, ""), now_s(), 2);
        std::string tops;
        for (const auto& p : cb::sweep(H, "")) if (p.id == "top") tops += p.title + " ";
        check("sweep: never ~/Samson, ~/Taxes (top folders), nor Downloads/Keep-out (deep)",
              !in(rep, "Samson/notes.txt") && !in(rep, "Taxes/2025.pdf") && !in(rep, "Downloads/Keep-out/x.bin") &&
              tops.find("Samson") == std::string::npos && tops.find("Taxes") == std::string::npos, tops);
        check("sweep: the rest still seen (old-letter.txt, Downloads/old-movie.mkv)",
              in(rep, "old-letter.txt") && in(rep, "Downloads/old-movie.mkv"));
        check("under: the folder and anything inside it; not a neighbour",
              pr::under("Samson") && pr::under("Samson/a/b") && pr::under("Downloads/Keep-out/x") &&
              !pr::under("Samsonite") && !pr::under("Downloads") && !pr::under("Downloads/Keep"));
        check("walk: the id lists' walk never enters them either", [&] {
            for (const auto& f : core::scan::walk(H, core::filelists::roots(H)))
                if (f.path.rfind(H + "/Samson/", 0) == 0 || f.path.rfind(H + "/Taxes/", 0) == 0) return false;
            return true;
        }());
        {   // the headless look applies them itself (Samson's from its config; yours from Settings)
            latch::protect::set_owned({});
            const std::string data = H + "/.tidy-data-ho";
            fs::create_directories(data);
            core::own::write(data, core::settings::kFile, core::settings::format(st));
            std::vector<core::due::Item> items;
            core::due::look(H, data, "", now_s(), &items);
            bool clean = true;
            for (const auto& it : items)
                clean = clean && it.path.rfind(H + "/Samson/", 0) != 0 && it.path.rfind(H + "/Taxes/", 0) != 0;
            check("due: the headless look protects them before it walks",
                  clean && pr::under("Samson/x") && pr::under("Taxes/x"));
        }
        {   // the door refuses a move into or out of one, at any depth
            pr::apply(H, cfg, st);
            latch::door::Door door(H, H + "/.tidy-data-ho");
            door.load();
            latch::door::Plan plan;
            plan.label = "into Taxes";
            plan.previewed = true;
            latch::door::Action in_;
            in_.verb = latch::door::Verb::Move;
            in_.from = H + "/old-letter.txt";
            in_.to = H + "/Taxes/old-letter.txt";
            plan.actions.push_back(in_);
            const auto r1 = door.run(plan);
            latch::door::Plan out_ = plan;
            out_.label = "out of Samson";
            out_.actions[0].from = H + "/Samson/notes.txt";
            out_.actions[0].to = H + "/notes.txt";
            const auto r2 = door.run(out_);
            check("door: a move into a hands-off folder, or out of Samson's, is refused -- nothing moved",
                  !r1.ok && !r2.ok && fs::exists(home / "old-letter.txt") && fs::exists(home / "Samson/notes.txt") &&
                  !fs::exists(home / "Taxes/old-letter.txt") && !fs::exists(home / "notes.txt"));
        }
        latch::protect::set_owned({});
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s020: the one-step Sort -- a level and a place for every loose file ──
    {
        std::cout << "[sort]\n";
        namespace so = core::sort;
        namespace cb = core::birth;
        namespace rt = core::retention;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_sort_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_birth_extras(home);
        make_projects(home);
        make_sweep_extras(home);
        fs::remove_all(home / ".local");
        put_file(home / "Downloads/scratch.tmp", "junk");
        put_file(home / "Desktop/leave-me.txt", "hands off");
        ::lsetxattr((home / "Desktop/leave-me.txt").c_str(), core::facts::kRetention, "hands-off", 9, 0);
        ::lsetxattr((home / "Desktop/leave-me.txt").c_str(), core::facts::kId, "11111111-2222-3333-4444-555555555555", 36, 0);
        latch::protect::set_owned({});
        const std::string H = fs::canonical(home).string();
        const std::int64_t now = now_s();
        const std::string month = [&] {
            const std::time_t t = static_cast<std::time_t>(now);
            std::tm tm{};
            localtime_r(&t, &tm);
            char b[16];
            std::strftime(b, sizeof b, "%Y-%m", &tm);
            return std::string(b);
        }();
        const int year = std::stoi(month.substr(0, 4));
        core::settings::Settings st;
        st.junk = {"*.tmp"};
        const auto before = tree_facts(H);
        const auto rep = cb::report(H, cb::sweep(H, ""), now, st.old_years);
        const auto rows = so::rows(H, rep, st, now);
        const auto row = [&](const std::string& rel) -> const so::Row* {
            for (const auto& r : rows) if (r.born.file.path == H + "/" + rel) return &r;
            return nullptr;
        };
        const auto home_to = [&](const std::string& rel, const std::string& folder) {
            const auto* r = row(rel);
            return r && r->go == so::Go::Home && r->folder == folder;
        };
        check("sort: READ-ONLY until run", tree_facts(H) == before);
        check("dated: the Desktop, Downloads, home's top; Loose at its own type folder",
              home_to("Desktop/cover-final.png", "Pictures/Desktop " + month) &&
              home_to("Downloads/f1040.pdf", "Documents/Downloads " + month) &&
              home_to("todo.txt", "Documents/Home " + month) &&
              home_to("Pictures/landscape.png", "Pictures/Loose " + month),
              row("Pictures/landscape.png") ? row("Pictures/landscape.png")->folder : "missing");
        check("sort: a top folder's loose file goes by type, named for its folder (Writing/ideas.txt)",
              home_to("Writing/ideas.txt", "Documents/Writing " + month));
        check("sort: undecided -> tagged; waiting -> Keep; decided -> its own level, untagged",
              row("todo.txt")->tag && row("todo.txt")->level == rt::Level::Keep &&
              row("Downloads/f1040.pdf")->level == rt::Level::UntilDone &&
              !row("Desktop/cover-final.png")->tag && row("Desktop/cover-final.png")->level == rt::Level::Keep);
        check("sort: Historical -> ~/Archive/<year>/<its folder>",
              row("old-letter.txt") && row("old-letter.txt")->go == so::Go::Archive &&
              row("old-letter.txt")->folder == "Archive/" + std::to_string(year) &&
              row("Music/song.mp3")->folder == "Archive/" + std::to_string(year) + "/Music");
        check("sort: Ephemeral stays (tagged); the junk list -> the Junk Drawer, stays",
              row("Downloads/cat.jpg") && row("Downloads/cat.jpg")->go == so::Go::Stays && row("Downloads/cat.jpg")->tag &&
              row("Downloads/scratch.tmp") && row("Downloads/scratch.tmp")->junk &&
              row("Downloads/scratch.tmp")->go == so::Go::Stays);
        check("sort: Hands off is left out", !row("Desktop/leave-me.txt"));
        auto planned = so::plan(H, rows, now);
        {
            std::size_t first_move = planned.plan.actions.size(), last_tag = 0;
            std::map<std::string, int> ids;
            for (std::size_t i = 0; i < planned.plan.actions.size(); ++i) {
                const auto& a = planned.plan.actions[i];
                if (a.verb == latch::door::Verb::Move && first_move == planned.plan.actions.size()) first_move = i;
                if (a.verb == latch::door::Verb::Tag && a.attr == core::facts::kRetention) last_tag = i;
                if (a.verb == latch::door::Verb::Tag && a.attr == core::facts::kId) ++ids[a.from];
            }
            bool once = true;
            for (const auto& [p, n] : ids) once = once && n == 1;
            check("plan: the level tags come before any move; one id per file (the archive's own dropped)",
                  last_tag < first_move && once && planned.archiving >= 2);
        }
        latch::door::Door door(H, H + "/.tidy-data-sort");
        door.load();
        planned.plan.previewed = true;
        const auto r = door.run(planned.plan);
        check("run: every step done", r.ok && r.failed == 0, r.messages.empty() ? "" : r.messages.front());
        const auto t_cover = core::facts::tags_of(H + "/Pictures/Desktop " + month + "/cover-final.png");
        const auto t_todo = core::facts::tags_of(H + "/Documents/Home " + month + "/todo.txt");
        const auto t_letter = core::facts::tags_of(H + "/Archive/" + std::to_string(year) + "/old-letter.txt");
        check("run: home by type into the dated folders, tagged", !fs::exists(home / "todo.txt") &&
              t_todo.retention == "keep" && !t_todo.id.empty() && t_cover.retention == "keep");
        check("run: archived, tagged Historical + archived", t_letter.retention == "historical" && !t_letter.archived.empty());
        check("run: Ephemeral + the Junk Drawer stayed, tagged",
              core::facts::tags_of(H + "/Downloads/cat.jpg").retention == "ephemeral" &&
              core::facts::tags_of(H + "/Downloads/scratch.tmp").role == core::closet::kJunkDrawer);
        check("run: Hands off untouched", fs::exists(home / "Desktop/leave-me.txt"));
        {   // found in the drive: "12 fingerprinted" for 3 archived -- record() took every move in the batch
            core::catalog::Catalog cat;
            const bool open = cat.open(H + "/.tidy-data-sort", true);
            const auto n = open ? core::archive::record(cat, door, r.batch, now) : 0;
            check("record: only the archived files are fingerprinted, not the ones sent home",
                  open && n == planned.archiving && cat.archived().size() == planned.archiving,
                  std::to_string(n) + " of " + std::to_string(planned.archiving));
        }
        {
            const auto again = so::rows(H, cb::report(H, cb::sweep(H, ""), now, st.old_years), st, now);
            std::size_t moving = 0;
            for (const auto& x : again) moving += x.go != so::Go::Stays || x.tag;
            check("sort again: nothing left to do", moving == 0, std::to_string(moving));
        }
        const auto u = door.undo();
        const auto shape = [&](const std::map<std::string, std::string>& m) {   // path -> inode/size, files only
            std::map<std::string, std::string> o;
            for (const auto& [p, f] : m) {
                if (p.find("/.tidy-data-sort") != std::string::npos || p.find("/.local/share/Trash") != std::string::npos ||
                    fs::is_directory(fs::symlink_status(p)))
                    continue;   // the door's own records; an undone new folder goes to the Trash (latch)
                o[p] = f.substr(0, f.find('/', f.find('/') + 1));
            }
            return o;
        };
        check("undo: one step -- every file back at its path (same inode, size), the new folders gone",
              u.ok && shape(tree_facts(H)) == shape(before) && !fs::exists(home / ("Pictures/Desktop " + month)) &&
              !fs::exists(home / ("Archive/" + std::to_string(year) + "/Music")));
        check("undo: the tags it gave are gone (todo.txt, cat.jpg, old-letter.txt)",
              core::facts::tags_of(H + "/todo.txt").retention.empty() &&
              core::facts::tags_of(H + "/Downloads/cat.jpg").retention.empty() &&
              core::facts::tags_of(H + "/old-letter.txt").archived.empty());
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s022: learned routes -- each send home remembered, proposed again ──
    {
        std::cout << "[routes]\n";
        namespace ro = core::routes;
        namespace so = core::sort;
        namespace cb = core::birth;
        check("name_glob: cover-final.png -> cover*.png; IMG_2034.JPG -> img*.jpg",
              ro::name_glob("cover-final.png") == "cover*.png" && ro::name_glob("IMG_2034.JPG") == "img*.jpg",
              ro::name_glob("IMG_2034.JPG"));
        check("name_glob: no leading word, a short one, no extension -> none; cover.png -> cover*.png",
              ro::name_glob("2026-03 invoice.pdf").empty() && ro::name_glob("ab-1.png").empty() &&
              ro::name_glob("README").empty() && ro::name_glob("cover.png") == "cover*.png");
        check("ext_glob: photo.PNG -> *.png; none without one",
              ro::ext_glob("photo.PNG") == "*.png" && ro::ext_glob("Makefile").empty());

        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_routes_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_birth_extras(home);
        fs::remove_all(home / ".local");
        fs::create_directories(home / "Pictures/Covers");
        fs::create_directories(home / "Pictures/Art");
        put_file(home / "Desktop/cover-2.png", "png");
        put_file(home / "Desktop/holiday.png", "png");
        latch::protect::set_owned({});
        const std::string H = fs::canonical(home).string();
        const auto born = [&](const std::string& rel, const std::string& place) {
            cb::Born b;
            b.place = place;
            b.file.path = H + "/" + rel;
            return b;
        };
        check("from_of: Desktop, Downloads, Home, the folder it sits loose in",
              ro::from_of(born("Desktop/a.png", "desktop")) == "Desktop" &&
              ro::from_of(born("Downloads/a.png", "downloads")) == "Downloads" &&
              ro::from_of(born("a.png", "home")) == "Home" && ro::from_of(born("Writing/a.txt", "top")) == "Writing");

        ro::Book b;
        check("propose: an empty book proposes nothing", ro::propose(b, H, born("Desktop/cover-2.png", "desktop")).folder.empty());
        ro::learn(b, born("Desktop/cover-final.png", "desktop"), "Pictures/Covers", {});
        check("learn: one send -> a name route and a from route, a yes each",
              b.routes.size() == 2 && b.routes[0].kind == ro::Route::Kind::Name && b.routes[0].pattern == "cover*.png" &&
              b.routes[1].kind == ro::Route::Kind::From && b.routes[1].pattern == "*.png" && b.routes[1].from == "Desktop" &&
              b.routes[0].yes == 1 && b.routes[1].yes == 1, ro::format(b));
        const auto p1 = ro::propose(b, H, born("Desktop/cover-2.png", "desktop"));
        check("propose: the same kind -> the folder, by its name route", p1.folder == "Pictures/Covers" && p1.because == "cover*.png",
              p1.because);
        const auto p2 = ro::propose(b, H, born("Desktop/holiday.png", "desktop"));
        check("propose: another .png from the Desktop -> by the from route", p2.folder == "Pictures/Covers" &&
              p2.because == "*.png from Desktop", p2.because);
        check("propose: a .png from Downloads, a .jpg from the Desktop -> nothing",
              ro::propose(b, H, born("Downloads/holiday.png", "downloads")).folder.empty() &&
              ro::propose(b, H, born("Desktop/holiday.jpg", "desktop")).folder.empty());
        check("propose: a file already at the folder's top -> nothing",
              ro::propose(b, H, born("Pictures/Covers/cover-9.png", "top")).folder.empty());
        ro::learn(b, born("Desktop/cover-2.png", "desktop"), "Pictures/Covers", p1);
        check("learn: a proposal followed is ONE yes (not two), no no",
              b.routes.size() == 2 && b.routes[0].yes == 2 && b.routes[0].no == 0 && b.routes[1].yes == 2, ro::format(b));
        const auto p3 = ro::propose(b, H, born("Desktop/cover-3.png", "desktop"));
        ro::learn(b, born("Desktop/cover-3.png", "desktop"), "Pictures/Art", p3);
        check("learn: sent elsewhere -> a no for the proposal, a yes for the new place",
              b.routes[0].no == 1 && b.routes.size() == 4, ro::format(b));
        check("propose: right more than wrong still wins (2-1 over 1-0: more yes)",
              ro::propose(b, H, born("Desktop/cover-4.png", "desktop")).folder == "Pictures/Covers");
        for (const char* n : {"Desktop/cover-4.png", "Desktop/cover-5.png"}) {
            const auto p = ro::propose(b, H, born(n, "desktop"));
            ro::learn(b, born(n, "desktop"), "Pictures/Art", p);
        }
        check("propose: corrected often enough, it turns", ro::propose(b, H, born("Desktop/cover-6.png", "desktop")).folder == "Pictures/Art");
        const auto text = ro::format(b);
        check("book: format / parse round trip", ro::parse(text) == b, text);
        check("book: bad lines dropped (hidden, .., absolute, counts, a from without its place)",
              ro::parse("route=name|a*.png||.secret|1|0\nroute=name|a*.png||Pictures/../..|1|0\n"
                        "route=name|a*.png||/etc|1|0\nroute=name|a*.png||Pictures|x|0\nroute=from|*.png||Pictures|1|0\n")
                  .routes.empty());
        check("own: routes.ini is Tidy's own file to write", core::own::refusal(ro::kFile).empty(),
              core::own::refusal(ro::kFile));
        {   // a folder removed proposes nothing
            ro::Book g;
            ro::learn(g, born("Desktop/gone-1.png", "desktop"), "Pictures/Gone", {});
            check("propose: the folder no longer there -> nothing",
                  ro::propose(g, H, born("Desktop/gone-2.png", "desktop")).folder.empty());
        }

        // Sort: a learned route before the dated default; nothing learned -> dated as before.
        const std::int64_t now = now_s();
        core::settings::Settings st;
        ro::Book sb;
        ro::learn(sb, born("Desktop/cover-1.png", "desktop"), "Pictures/Covers", {});
        const auto before = tree_facts(H);
        const auto rep = cb::report(H, cb::sweep(H, ""), now, st.old_years);
        const auto rows = so::rows(H, rep, st, now, {}, {}, &sb);
        const auto row = [&](const std::string& rel) -> const so::Row* {
            for (const auto& r : rows) if (r.born.file.path == H + "/" + rel) return &r;
            return nullptr;
        };
        const auto* rc = row("Desktop/cover-final.png");
        const auto* rp = row("Desktop/plan.txt");
        check("sort: cover-final.png goes where you sent covers -- learned, said so",
              rc && rc->go == so::Go::Home && rc->folder == "Pictures/Covers" && !rc->learned.folder.empty() &&
              rc->why.rfind("learned: cover*.png -> Pictures/Covers (1 time)", 0) == 0, rc ? rc->why : "missing");
        check("sort: holiday.png (a .png from the Desktop) too; plan.txt (nothing learned) -> the dated default",
              row("Desktop/holiday.png") && row("Desktop/holiday.png")->folder == "Pictures/Covers" &&
              rp && rp->learned.folder.empty() && rp->folder.rfind("Documents/Desktop ", 0) == 0, rp ? rp->folder : "missing");
        check("sort: READ-ONLY with a book", tree_facts(H) == before);
        const auto planned = so::plan(H, rows, now);
        bool moved = false, made = false;
        for (const auto& a : planned.plan.actions) {
            moved |= a.verb == latch::door::Verb::Move && a.to == H + "/Pictures/Covers/cover-final.png";
            made |= a.verb == latch::door::Verb::MakeDir && a.to == H + "/Pictures/Covers";
        }
        check("plan: into the learned folder, which is there already (no new folder)", moved && !made);
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s023: adopt -- picked folders cataloged in place; catch-alls swept ──
    {
        std::cout << "[adopt]\n";
        namespace ad = core::adopt;
        namespace cb = core::birth;
        namespace so = core::sort;
        namespace ro = core::routes;
        namespace rt = core::retention;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_adopt_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        for (const char* f : {"Pictures/Covers/cover-a.png", "Pictures/Covers/cover-b.png", "Pictures/Covers/cover-c.png",
                              "Pictures/Trips/cover-x.png", "Pictures/Trips/beach.jpg", "Pictures/loose.png",
                              "Pictures/Covers/old-notes.txt", "Pictures/Covers/kept.png", "Pictures/Covers/eph.png",
                              "Pictures/Covers/Book-project/x.txt", "Pictures/Covers/.hidden.png",
                              "Pictures/Repo/.git/HEAD", "Pictures/Repo/a.png", "Documents/Misc/a.txt",
                              "Documents/Misc/deep/b.txt", "Documents/top.txt", "Downloads/d.pdf", "Taxes/w2.pdf"})
            put_file(home / f, f);
        fs::create_directories(home / "Archive/2025");
        fs::create_directories(home / ".config");
        xset(home / "Pictures/Covers/kept.png", core::facts::kId, "11111111-2222-3333-4444-555555555555");
        xset(home / "Pictures/Covers/kept.png", core::facts::kRetention, "keep");
        xset(home / "Pictures/Covers/eph.png", core::facts::kRetention, "ephemeral");
        age(home / "Pictures/Covers/old-notes.txt", 2019);
        const std::string H = fs::canonical(home).string();
        latch::protect::set_owned({"Taxes"});
        const auto refused = [&](const std::string& rel, bool ca) { return !ad::refusal(H, "", rel, ca).empty(); };
        check("refused: home, hidden, ~/Archive, a -project, a repo, Hands off, missing",
              refused("", false) && refused(".config", false) && refused("Archive/2025", false) &&
              refused("Pictures/Covers/Book-project", false) && refused("Pictures/Repo", false) &&
              refused("Taxes", false) && refused("Nope", false),
              ad::refusal(H, "", "Pictures/Repo", false));
        check("refused: Downloads in place and as a catch-all (Sort sees it all); Documents is fine",
              refused("Downloads", false) && refused("Downloads", true) &&
              !refused("Documents/Misc", true) && !refused("Pictures", false));
        const auto before = tree_facts(H);
        const std::int64_t now = now_s();
        const auto lk = ad::look(H, "", "Pictures", false, now, 2);
        check("look: READ-ONLY", tree_facts(H) == before);
        const auto item = [&](const std::string& rel) -> const ad::Item* {
            for (const auto& i : lk.items) if (i.file.path == H + "/" + rel) return &i;
            return nullptr;
        };
        check("look: Keep where you put it, an id and a level", item("Pictures/Covers/cover-a.png") &&
              item("Pictures/Covers/cover-a.png")->level == rt::Level::Keep &&
              item("Pictures/Covers/cover-a.png")->give_id && item("Pictures/Covers/cover-a.png")->set_level);
        check("look: untouched for years -> Historical", item("Pictures/Covers/old-notes.txt") &&
              item("Pictures/Covers/old-notes.txt")->level == rt::Level::Historical);
        check("look: a level already carried stays, only an id added; both carried -> known",
              item("Pictures/Covers/eph.png") && item("Pictures/Covers/eph.png")->level == rt::Level::Ephemeral &&
              !item("Pictures/Covers/eph.png")->set_level && !item("Pictures/Covers/kept.png") && lk.known == 1);
        check("look: the loose top of Pictures is Sort's, said", !item("Pictures/loose.png") && lk.loose == 1,
              std::to_string(lk.loose));
        check("look: never inside a -project, a repo, or a hidden file",
              !item("Pictures/Covers/Book-project/x.txt") && !item("Pictures/Repo/a.png") &&
              !item("Pictures/Covers/.hidden.png") && lk.items.size() == 7, std::to_string(lk.items.size()));
        check("learned: cover*.png -> Pictures/Covers (3 of 4); none for Trips (1)",
              lk.routes.size() == 1 && lk.routes[0].pattern == "cover*.png" && lk.routes[0].folder == "Pictures/Covers" &&
              lk.routes[0].count == 3, lk.routes.empty() ? "none" : lk.routes[0].pattern + " " + lk.routes[0].folder);
        ro::Book book;
        check("learn: one route, yes 3; again -> unchanged (not doubled)",
              ad::learn(book, {lk}) == 1 && book.routes.size() == 1 && book.routes[0].yes == 3 &&
              ad::learn(book, {lk}) == 0 && book.routes[0].yes == 3);
        {
            put_file(home / "Desktop/cover-new.png", "c");
            const auto rep = cb::report(H, cb::sweep(H, ""), now, 2);
            cb::Born b;
            for (const auto& x : rep.items) if (x.file.path == H + "/Desktop/cover-new.png") b = x;
            check("learn: the adopted route proposes a new cover on the Desktop",
                  ro::propose(book, H, b).folder == "Pictures/Covers");
            fs::remove(home / "Desktop/cover-new.png");
        }
        const auto ca = ad::look(H, "", "Documents/Misc", true, now, 2);
        check("catch-all: nothing tagged, 2 files offered", ca.items.empty() && ca.offered == 2, std::to_string(ca.offered));
        {
            const auto rep0 = cb::report(H, cb::sweep(H, ""), now, 2);
            const auto rep1 = cb::report(H, cb::sweep(H, "", {"Documents/Misc"}), now, 2);
            const auto has = [&](const cb::Report& r, const std::string& rel) {
                for (const auto& x : r.items) if (x.file.path == H + "/" + rel) return true;
                return false;
            };
            check("sweep: a catch-all's files at every depth are loose; without it they are home",
                  !has(rep0, "Documents/Misc/deep/b.txt") && has(rep1, "Documents/Misc/deep/b.txt") &&
                  has(rep1, "Documents/Misc/a.txt") && has(rep1, "Documents/top.txt"));
            core::settings::Settings st;
            const auto rows = so::rows(H, rep1, st, now);
            bool named = false;
            for (const auto& r : rows) named |= r.born.file.path == H + "/Documents/Misc/a.txt" &&
                                                r.folder.find("/Misc ") != std::string::npos;
            check("sort: a catch-all's file goes home by type, named for its folder (Misc YYYY-MM)", named);
            const auto rep2 = cb::report(H, cb::sweep(H, "", {"Documents"}), now, 2);
            check("sweep: Documents as a catch-all is walked whole (no second place)",
                  has(rep2, "Documents/Misc/deep/b.txt") &&
                  std::count_if(rep2.places.begin(), rep2.places.end(), [](const cb::Place& p) { return p.id == "catchall"; }) == 0);
        }
        {
            core::settings::Settings st;
            st.adopted = {"Pictures"};
            st.catch_all = {"Documents/Misc"};
            const auto back = core::settings::parse(core::settings::format(st));
            check("settings: adopted + catch_all round trip; bad lines dropped",
                  back == st && core::settings::parse("adopted=/etc\nadopted=../x\ncatch_all=a\ncatch_all=a\n").catch_all.size() == 1 &&
                  core::settings::parse("adopted=/etc\nadopted=../x\n").adopted.empty());
        }
        auto p = ad::plan({lk, ca});
        {
            std::size_t ids = 0, levels = 0;
            for (const auto& a : p.actions) {
                ids += a.attr == core::facts::kId;
                levels += a.attr == core::facts::kRetention;
            }
            check("plan: 7 ids, 6 levels (eph keeps its own), tags only", ids == 7 && levels == 6 &&
                  std::all_of(p.actions.begin(), p.actions.end(), [](const auto& a) { return a.verb == latch::door::Verb::Tag; }),
                  std::to_string(ids) + "/" + std::to_string(levels) + " " + p.label);
        }
        latch::door::Door door(H, H + "/.tidy-data-adopt");
        door.load();
        p.previewed = true;
        const auto r = door.run(p);
        check("run: every step done", r.ok && r.failed == 0, r.messages.empty() ? "" : r.messages.front());
        const auto ta = core::facts::tags_of(H + "/Pictures/Covers/cover-a.png");
        check("run: tagged in place, nothing moved", ta.retention == "keep" && core::facts::is_id(ta.id) &&
              core::facts::tags_of(H + "/Pictures/Covers/old-notes.txt").retention == "historical" &&
              core::facts::tags_of(H + "/Pictures/Covers/eph.png").retention == "ephemeral" &&
              fs::exists(home / "Pictures/loose.png") && core::facts::tags_of(H + "/Pictures/loose.png").id.empty());
        {
            core::catalog::Catalog cat;
            const bool open = cat.open(H + "/.tidy-data-adopt", true);
            const auto n = open ? core::tagging::record(cat, core::tagging::paths_of(door, r.batch), now) : 0;
            check("catalog: 7 adopted files recorded", n == 7, std::to_string(n));
        }
        check("again: everything known, nothing to do",
              ad::look(H, "", "Pictures", false, now, 2).items.empty());
        const auto u = door.undo();
        check("undo: one step, the tags off again", u.ok && core::facts::tags_of(H + "/Pictures/Covers/cover-a.png").id.empty() &&
              core::facts::tags_of(H + "/Pictures/Covers/eph.png").retention == "ephemeral");
        latch::protect::set_owned({});
        std::error_code ec;
        fs::remove_all(root, ec);
        // s023a: Busy's bar follows the manifest -- the batch line, then an intent + a done per step.
        check("busy: the bar -- 0 at the batch line, half at 2 of 4 steps, full at the end, never past it",
              tidy::busy::door_fraction(0, 4) == 0.0 && tidy::busy::door_fraction(1, 4) == 0.0 &&
              tidy::busy::door_fraction(5, 4) == 0.5 && tidy::busy::door_fraction(9, 4) == 1.0 &&
              tidy::busy::door_fraction(50, 4) == 1.0 && tidy::busy::door_fraction(0, 0) == 1.0);
    }

    // ── s023f: the home map -- what is cataloged, loose, untracked; the treemap ──
    {
        std::cout << "[map]\n";
        namespace mp = core::map;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_map_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        for (const char* f : {"Desktop/loose.txt", "Pictures/x.png", "Pictures/Covers/a.png", "Pictures/Covers/b.png",
                              "Writing/Book-project/ch1.odt", "Writing/Book-project/ch2.odt", "notes-repo/a.md",
                              "notes-repo/.git/HEAD", "Archive/2025/old.txt", "Taxes/w2.pdf", ".hidden/x",
                              "Documents/Misc/deep/b.txt", "Documents/top.txt", "todo.txt"})
            put_file(home / f, f);
        xset(home / "Pictures/Covers/b.png", core::facts::kId, "77777777-7777-4777-8777-777777777777");
        const std::string H = fs::canonical(home).string();
        latch::protect::set_owned({"Taxes"});
        const auto before = tree_facts(H);
        const auto t = mp::build(H, "", {});
        check("map: READ-ONLY", tree_facts(H) == before);
        using K = mp::Kind;
        check("map: loose (Desktop, a top folder's top, Documents' top, home's top) / untracked / cataloged",
              t.c[K::Loose] == 4 && t.c[K::Untracked] == 2 && t.c[K::Cataloged] == 1, mp::line(t.c));
        check("map: a -project and a repo kept whole (no .git); ~/Archive archived",
              t.c[K::Whole] == 3 && t.c[K::Archived] == 1 && mp::find(t, "notes-repo") &&
              mp::find(t, "notes-repo")->what.find("repo") != std::string::npos);
        const auto* tx = mp::find(t, "Taxes");
        check("map: Hands off not looked inside (a box of weight 1); hidden skipped",
              tx && tx->shut && tx->c.total() == 0 && tx->weight() == 1 && !mp::find(t, ".hidden") &&
              t.c.total() == 11, std::to_string(t.c.total()));
        check("map: find a folder; its files here vs below",
              mp::find(t, "Pictures/Covers") && mp::find(t, "Pictures/Covers")->c.total() == 2 &&
              mp::find(t, "Pictures")->here.total() == 1 && !mp::find(t, "Nope"));
        check("map: the biggest folder first", t.kids.size() >= 2 && t.kids[0].weight() >= t.kids[1].weight());
        const auto t2 = mp::build(H, "", {"Documents/Misc"});
        check("map: a catch-all's files are loose, at any depth",
              mp::find(t2, "Documents/Misc")->c[K::Loose] == 1 && mp::find(t2, "Documents/Misc")->what == "a catch-all");
        check("map: the line", mp::line(mp::find(t, "Pictures/Covers")->c) == "2 files: 1 cataloged, 1 untracked",
              mp::line(mp::find(t, "Pictures/Covers")->c));
        {
            const std::vector<double> sz = {6, 6, 4, 3, 2, 2, 1};
            const auto rs = mp::squarify(sz, {0, 0, 6, 4});
            bool ok = rs.size() == sz.size();
            double sum = 0;
            for (std::size_t i = 0; ok && i < rs.size(); ++i) {
                const auto& r = rs[i];
                ok = r.x >= -1e-9 && r.y >= -1e-9 && r.x + r.w <= 6 + 1e-9 && r.y + r.h <= 4 + 1e-9 &&
                     std::abs(r.w * r.h - sz[i]) < 1e-6;
                sum += r.w * r.h;
                for (std::size_t j = 0; ok && j < i; ++j) {
                    const auto& q = rs[j];
                    const double ox = std::min(r.x + r.w, q.x + q.w) - std::max(r.x, q.x);
                    const double oy = std::min(r.y + r.h, q.y + q.h) - std::max(r.y, q.y);
                    ok = !(ox > 1e-6 && oy > 1e-6);
                }
            }
            check("squarify: each box its share of the area, inside it, none overlapping", ok && std::abs(sum - 24) < 1e-6);
            const auto one = mp::squarify({5}, {10, 20, 30, 40});
            check("squarify: one fills it; none or zero -> empty boxes",
                  one[0].x == 10 && one[0].y == 20 && one[0].w == 30 && one[0].h == 40 &&
                  mp::squarify({}, {0, 0, 1, 1}).empty() && mp::squarify({0}, {0, 0, 1, 1})[0].w == 0);
        }
        latch::protect::set_owned({});
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s023h: Conform -- a template's routes put every book folder in order ──
    {
        std::cout << "[conform]\n";
        namespace cf = core::conform;
        namespace tp = core::templates;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_conform_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        const std::string B = "Documents/EBooks/";
        for (const char* f : {"01 Clock/Archive.zip", "01 Clock/cover.png", "01 Clock/Images/BookPlate.jpg",
                              "01 Clock/Clock.odt", "01 Clock/Clock.odt.bak", "01 Clock/Clock.pdf",
                              "01 Clock/Clock_dustjacket.svg", "01 Clock/Clock.epub", "01 Clock/Clock.optik",
                              "01 Clock/Clock-print.pdf", "01 Clock/Workups/Images/old/plate.jpg",
                              "01 Clock/Bind-project/notes.txt", "01 Clock/.hidden.png",
                              "04 Lilac/Archive/A0001.png", "04 Lilac/Archive/A0002.png", "04 Lilac/Archive/A0003.png",
                              "04 Lilac/Lilac.odt", "Nancy Drew Bibliography.ods"})
            put_file(home / (B + f), f);
        const std::string H = fs::canonical(home).string();
        tp::Template t;
        t.name = "book-project";
        t.category = "Entertainment/EBooks";
        t.folders = {{"Archive", tp::Role::History}, {"Originals", tp::Role::Input}, {"Print", tp::Role::Product},
                     {"Workups", tp::Role::Input}, {"Workups/Images", tp::Role::Input}};
        t.routes = {{"Archive/*.png", tp::kStays}, {"*.optik", tp::kTop}, {"*.odt", "Originals"}, {"*.bak", "Originals"},
                    {"*.epub", "Originals"}, {"*-print.pdf", "Print"}, {"*.xcf", "Workups"}, {"*.svg", "Workups"},
                    {"*.jpg", "Workups/Images"}, {"*.png", "Workups/Images"}};
        check("template: (stays), (top) and Folder/glob routes are fine; a bad one is not",
              tp::problems(t).empty() && !tp::pattern_refusal("Archive/").empty() && !tp::pattern_refusal("a -> b").empty(),
              tp::problems(t).empty() ? "" : tp::problems(t).front());
        check("template: round trip with the new routes", tp::parse(tp::format(t)) == t);
        check("route_for_path: Archive/*.png stays; any other .png -> Workups/Images; .optik -> top",
              tp::route_for_path(t, "Archive/A0001.png") == tp::kStays && tp::route_for_path(t, "Archive/deep/x.png") == tp::kStays &&
              tp::route_for_path(t, "cover.png") == "Workups/Images" && tp::route_for_path(t, "x.optik") == tp::kTop &&
              tp::route_for_path(t, "x.zip").empty());
        check("route_for (Files home into a project) skips the Conform-only routes",
              tp::route_for(t, "x.optik").empty() && tp::route_for(t, "A0001.png") == "Workups/Images");
        const auto fs_ = cf::folders(H + "/" + B, true);
        check("folders: each sub-folder of EBooks (not the loose .ods)", fs_.size() == 2, std::to_string(fs_.size()));
        check("refusal: home, ~/Archive, outside -> refused; a book folder fine",
              !cf::refusal(H, H).empty() && !cf::refusal(H, "/etc").empty() && cf::refusal(H, fs_[0]).empty());
        const auto before = tree_facts(H);
        std::vector<cf::Project> ps;
        for (const auto& d : fs_) ps.push_back(cf::look(t, d));
        check("look: READ-ONLY", tree_facts(H) == before);
        const auto& c = ps[0];
        const auto moved = [&](const std::string& rel, const std::string& folder) {
            for (const auto& m : c.moves) if (m.from == fs_[0] + "/" + rel) return m.folder == folder;
            return false;
        };
        check("look: the clutter routed -- cover.png + Images/BookPlate.jpg -> Workups/Images; .odt/.bak/.epub -> "
              "Originals; -print.pdf -> Print; .svg -> Workups",
              moved("cover.png", "Workups/Images") && moved("Images/BookPlate.jpg", "Workups/Images") &&
              moved("Clock.odt", "Originals") && moved("Clock.odt.bak", "Originals") && moved("Clock.epub", "Originals") &&
              moved("Clock-print.pdf", "Print") && moved("Clock_dustjacket.svg", "Workups") && c.moves.size() == 7,
              std::to_string(c.moves.size()));
        check("look: the .optik at the top and Workups/Images/old/plate.jpg in place (never flattened); "
              "Archive.zip + Clock.pdf listed; a nested -project whole; hidden skipped",
              c.in_place == 2 && c.unmatched == std::vector<std::string>{"Archive.zip", "Clock.pdf"} && c.whole == 1,
              std::to_string(c.in_place) + "/" + std::to_string(c.whole));
        check("look: Lilac's Archive scans stay (3); its .odt -> Originals",
              ps[1].stays == 3 && ps[1].moves.size() == 1 && ps[1].moves[0].folder == "Originals");
        auto pl = cf::plan(H, ps, true);
        {
            std::size_t mk = 0, mv = 0;
            for (const auto& a : pl.plan.actions) { mk += a.verb == latch::door::Verb::MakeDir; mv += a.verb == latch::door::Verb::Move; }
            const auto& last = pl.plan.actions.back();
            check("plan: the new folders (Originals, Print in Clock -- its Workups/Images is there already; Originals in "
                  "Lilac), 8 file moves + the 2 renames LAST",
                  mk == 3 && mv == 10 && last.verb == latch::door::Verb::Move && last.to == fs_[1] + "-project" &&
                  pl.plan.actions[pl.plan.actions.size() - 2].to == fs_[0] + "-project",
                  std::to_string(mk) + " dirs, " + std::to_string(mv) + " moves");
        }
        latch::door::Door door(H, H + "/.tidy-data-conform");
        door.load();
        pl.plan.previewed = true;
        const auto r = door.run(pl.plan);
        check("run: every step", r.ok && r.failed == 0, r.messages.empty() ? "" : r.messages.front());
        const std::string C = H + "/" + B + "01 Clock-project/";
        check("run: on disk -- Workups/Images/cover.png, Originals/Clock.odt, Print/Clock-print.pdf, the .optik at the top, "
              "the scans still in Lilac's Archive, the bibliography untouched",
              fs::exists(C + "Workups/Images/cover.png") && fs::exists(C + "Workups/Images/BookPlate.jpg") &&
              fs::exists(C + "Originals/Clock.odt") && fs::exists(C + "Print/Clock-print.pdf") && fs::exists(C + "Clock.optik") &&
              fs::exists(C + "Clock.pdf") && fs::exists(H + "/" + B + "04 Lilac-project/Archive/A0002.png") &&
              fs::exists(H + "/" + B + "Nancy Drew Bibliography.ods"));
        const auto u = door.undo();
        check("undo: one step -- back as it was", u.ok && fs::exists(home / (B + "01 Clock/cover.png")) &&
              fs::exists(home / (B + "01 Clock/Images/BookPlate.jpg")) && !fs::exists(home / (B + "01 Clock-project")) &&
              !fs::exists(home / (B + "01 Clock/Originals")), u.messages.empty() ? "" : u.messages.front());
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s023i: a template made from a scan -- types (numbered runs apart) + folders -> routes ──
    {
        std::cout << "[shape]\n";
        namespace sh = core::shape;
        namespace cf = core::conform;
        namespace tp = core::templates;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_shape_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        const std::string B = "Documents/EBooks/";
        // Two books: scans loose at the top of one, in Archive/ in the other; covers; an .optik; a nested -project.
        std::vector<std::string> fl = {"01 Clock/Clock.odt", "01 Clock/Clock.optik", "01 Clock/cover.png",
                                       "01 Clock/Images/plate.png", "01 Clock/Clock-print.pdf", "01 Clock/Bind-project/x.txt",
                                       "01 Clock/README", "01 Clock/.hidden.png",
                                       "02 Lilac/Lilac.odt", "02 Lilac/Archive/old/deep.txt", "02 Lilac/cover.png"};
        for (int i = 1; i <= 6; ++i) {
            char n[16];
            std::snprintf(n, sizeof n, "A%04d.png", i);
            fl.push_back(std::string("01 Clock/") + n);
            fl.push_back(std::string("02 Lilac/Archive/") + n);
        }
        fl.push_back("02 Lilac/Chapter1.odt");   // a numbered name, but only one: not a run
        for (const auto& f : fl) put_file(home / (B + f), f);
        const std::string H = fs::canonical(home).string();

        check("numbered: A0001 -> (A, 4); Clock -> none; 01 -> (\"\", 2)",
              sh::numbered("A0001") == std::pair<std::string, int>{"A", 4} && sh::numbered("Clock").second == 0 &&
              sh::numbered("01") == std::pair<std::string, int>{"", 2});
        const auto dirs = cf::folders(H + "/" + B, true);
        const auto before = tree_facts(H);
        const auto sc = sh::scan(dirs);
        check("scan: READ-ONLY", tree_facts(H) == before);
        const auto row = [&](const std::string& key) -> const sh::Row* {
            for (const auto& r : sc.rows) if (r.key() == key) return &r;
            return nullptr;
        };
        const auto* run = row(".png#A#4");
        const auto* png = row(".png");
        check("scan: the scans are a numbered run of their own (12, in 2 of 2, A0001.png .. A0006.png), the other .png apart (3)",
              run && run->count == 12 && run->projects == 2 && run->first == "A0001.png" && run->last == "A0006.png" &&
              png && png->count == 3, run ? std::to_string(run->count) : "no run");
        check("scan: the run's pattern is exact digits; a run row comes before its type's others",
              run && run->pattern() == "A[0-9][0-9][0-9][0-9].png" && sc.rows.front().key() == ".png#A#4");
        check("scan: .odt 3 (Chapter1 not a run), .optik 1, no extension 1 (not routable); hidden + nested -project skipped",
              row(".odt") && row(".odt")->count == 3 && row(".optik") && row(".optik")->count == 1 && row("") &&
              !row("")->routable() && sc.whole == 1 && sc.files == 22, std::to_string(sc.files));
        bool images = false, archive = false, deep = false, two = false;
        for (const auto& f : sc.folders) {
            images |= f.path == "Images" && f.files == 1 && f.projects == 1;
            archive |= f.path == "Archive" && f.files == 6;
            deep |= f.path == "Archive/old";
            two |= std::count(f.path.begin(), f.path.end(), '/') > 1;
        }
        check("scan: the folders found, two deep (Images, Archive, Archive/old), files directly in each", images && archive && deep && !two);
        check("scan: where the run is now -- Archive/ 6, the top 6 (a tie: half at the top -> mostly the top)", run && run->where.size() == 2 && run->mostly().empty());
        check("name_from: EBooks -> ebooks-project", sh::name_from("EBooks") == "ebooks-project" &&
              sh::name_from("book-project") == "book-project");

        std::map<std::string, sh::Pick> picks;
        picks[".png#A#4"] = {"Archive", false};
        picks[".png"] = {"Workups/Images", false};
        picks[".odt"] = {"Originals", false};
        picks[".optik"] = {"", true};
        picks[".pdf"] = {"", false};   // left alone
        const auto t = sh::make("book-project", "Entertainment/EBooks", {"Archive", "Originals", "Workups/Images"}, sc.rows, picks);
        check("make: no problems; Workups added as the parent; Archive a history folder",
              tp::problems(t).empty() && tp::find(t, "Workups") && tp::find(t, "Archive")->role == tp::Role::History,
              tp::problems(t).empty() ? "" : tp::problems(t).front());
        check("make: routes -- the source first (-> top), the run, then the rest; the left-alone .pdf none",
              t.routes.size() == 4 && t.routes[0] == tp::Route{"*.optik", tp::kTop} &&
              t.routes[1] == tp::Route{"A[0-9][0-9][0-9][0-9].png", "Archive"} &&
              t.routes[2].pattern == "*.png" && t.routes[3].pattern == "*.odt");
        check("make: round trip through the template file", tp::parse(tp::format(t)) == t);
        check("route_for_path: a scan -> Archive, a cover -> Workups/Images, 'A0001 copy.png' is not a scan",
              tp::route_for_path(t, "A0003.png") == "Archive" && tp::route_for_path(t, "cover.png") == "Workups/Images" &&
              tp::route_for_path(t, "A0001 copy.png") == "Workups/Images");
        std::vector<cf::Project> ps;
        for (const auto& d : dirs) ps.push_back(cf::look(t, d));
        std::size_t to_archive = 0;
        for (const auto& m : ps[0].moves) to_archive += m.folder == "Archive";
        check("conform with it: Clock's loose scans -> Archive (6), Lilac's in place (6), the .optik in place, the .pdf listed",
              to_archive == 6 && ps[1].in_place >= 6 &&
              std::find(ps[0].unmatched.begin(), ps[0].unmatched.end(), "Clock-print.pdf") != ps[0].unmatched.end(),
              std::to_string(to_archive) + "/" + std::to_string(ps[1].in_place));
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s020a: Downloads on to Entertainment; folders whole; projects placed ──
    {
        std::cout << "[sort-a]\n";
        namespace so = core::sort;
        namespace cb = core::birth;
        namespace rt = core::retention;
        std::string show;
        int season = 0;
        check("episode: Mobland_s01e02.mkv -> Mobland, season 1",
              so::episode("Mobland_s01e02.mkv", &show, &season) && show == "Mobland" && season == 1, show);
        check("episode: Mobland.S01E02.1080p.WEB.mkv, mobland 1x02.mp4 -> Mobland",
              so::episode("Mobland.S01E02.1080p.WEB.mkv", &show, &season) && show == "Mobland" &&
              so::episode("mobland 1x02.mp4", &show, &season) && show == "Mobland" && season == 1, show);
        check("episode: the.last.of.us.S02E05.mkv -> The Last Of Us, season 2",
              so::episode("the.last.of.us.S02E05.mkv", &show, &season) && show == "The Last Of Us" && season == 2, show);
        check("episode: a movie is not one (Dune.2021.1080p.mkv, 1917.mkv)",
              !so::episode("Dune.2021.1080p.mkv", nullptr, nullptr) && !so::episode("1917.mkv", nullptr, nullptr));
        check("kind: .mkv Movies, .epub/.pdf Books, .flac Music, .txt none",
              so::kind_of("a.MKV") == "Movies" && so::kind_of("b.epub") == "Books" && so::kind_of("c.pdf") == "Books" &&
              so::kind_of("d.flac") == "Music" && so::kind_of("e.txt").empty());
        {
            core::facts::Facts f;
            f.mtime = 1000; f.btime = 900; f.atime = 1030;
            const bool fresh = so::read_since_arrival(f);
            f.atime = 5000;
            check("read: opened a minute or more after it arrived; not at arrival", !fresh && so::read_since_arrival(f));
        }
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_sorta_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_projects(home);
        fs::remove_all(home / ".local");
        latch::protect::set_owned({});
        const std::int64_t now = now_s();
        const auto arrive_read = [&](const fs::path& p, bool read) {   // arrived yesterday; read an hour ago, or never
            struct timespec ts[2];
            ts[1] = {static_cast<time_t>(now - 86400), 0};
            ts[0] = {static_cast<time_t>(read ? now + 3600 : now - 86400), 0};   // btime is now (it cannot be set back)
            ::utimensat(AT_FDCWD, p.c_str(), ts, AT_SYMLINK_NOFOLLOW);
        };
        for (const auto& [rel, read] : std::vector<std::pair<std::string, bool>>{
                 {"Downloads/Mobland_s01e02.mkv", true}, {"Downloads/Dune.2021.1080p.mkv", true},
                 {"Downloads/Unwatched.mkv", false}, {"Downloads/book.epub", true}, {"Downloads/scan.pdf", true},
                 {"Downloads/readme.txt", true},
                 {"Downloads/Show.S02E01.1080p/Show.S02E01.1080p.mkv", true},
                 {"Downloads/Show.S02E01.1080p/Show.S02E01.srt", false},
                 {"Downloads/Unread.Folder/x.mkv", false},
                 {"Desktop/Trip/a.jpg", false}, {"Desktop/Trip/b.jpg", false}, {"Desktop/Trip/notes.txt", false}}) {
            put_file(home / rel, rel);
            arrive_read(home / rel, read);
        }
        const std::string H = fs::canonical(home).string();
        core::settings::Settings st;
        const auto rep = cb::report(H, cb::sweep(H, ""), now, st.old_years);
        core::templates::Template wt;
        wt.name = "writing-project";
        wt.category = "Writing";
        wt.folders = {{"Drafts"}, {"Research"}, {"Research/Notes"}};
        fs::create_directories(home / "Desktop/Essay-project/Drafts");
        const auto found = core::projects::find(H);
        const auto rows = so::rows(H, rep, st, now, found, {wt});
        const auto row = [&](const std::string& rel) -> const so::Row* {
            for (const auto& r : rows) if (r.born.file.path == H + "/" + rel) return &r;
            return nullptr;
        };
        const auto to = [&](const std::string& rel, const std::string& folder) {
            const auto* r = row(rel);
            return r && r->go == so::Go::Home && r->folder == folder;
        };
        check("downloads: a read episode -> Entertainment/Series/Mobland/Season 01, Keep",
              to("Downloads/Mobland_s01e02.mkv", "Entertainment/Series/Mobland/Season 01") &&
              row("Downloads/Mobland_s01e02.mkv")->level == rt::Level::Keep && row("Downloads/Mobland_s01e02.mkv")->tag);
        check("downloads: a read movie -> Entertainment/Movies; a read .epub and .pdf -> Entertainment/EBooks (s023g)",
              to("Downloads/Dune.2021.1080p.mkv", "Entertainment/Movies") && to("Downloads/book.epub", "Entertainment/EBooks") &&
              to("Downloads/scan.pdf", "Entertainment/EBooks"));
        check("downloads: read, not entertainment -> home by type (Documents/Downloads <month>)",
              row("Downloads/readme.txt") && row("Downloads/readme.txt")->go == so::Go::Home &&
              row("Downloads/readme.txt")->folder.rfind("Documents/Downloads ", 0) == 0);
        check("downloads: unread -> stays, Ephemeral, tagged (Due's Trash after the aging days)",
              row("Downloads/Unwatched.mkv") && row("Downloads/Unwatched.mkv")->go == so::Go::Stays &&
              row("Downloads/Unwatched.mkv")->level == rt::Level::Ephemeral && row("Downloads/Unwatched.mkv")->tag);
        check("folder: a read episode folder moves whole into its season (never split)",
              to("Downloads/Show.S02E01.1080p", "Entertainment/Series/Show/Season 02") &&
              row("Downloads/Show.S02E01.1080p")->folder_whole &&
              !row("Downloads/Show.S02E01.1080p/Show.S02E01.1080p.mkv") && !row("Downloads/Show.S02E01.1080p/Show.S02E01.srt"));
        check("folder: an unread download folder stays, untouched -- listed (s023g), with where Move on now sends it",
              row("Downloads/Unread.Folder") && row("Downloads/Unread.Folder")->go == so::Go::Stays &&
              row("Downloads/Unread.Folder")->folder_whole && row("Downloads/Unread.Folder")->now_folder == "Entertainment/Movies" &&
              !row("Downloads/Unread.Folder/x.mkv"));
        {   // s023g: Move on now -- a file becomes Keep, home by the table; a folder moves whole, untagged
            auto f = *row("Downloads/Unwatched.mkv");
            auto d = *row("Downloads/Unread.Folder");
            const bool okf = so::move_on_now(f), okd = so::move_on_now(d);
            check("move on now: the unread movie -> Entertainment/Movies, Keep; the unread folder whole",
                  okf && f.go == so::Go::Home && f.folder == "Entertainment/Movies" && f.level == rt::Level::Keep && f.tag &&
                  okd && d.go == so::Go::Home && d.folder == "Entertainment/Movies" && !d.tag);
            so::Row none;
            check("move on now: nowhere to go -> no", !so::move_on_now(none));
        }
        {   // s023g: the Entertainment table -- standard, yours, round trip
            const auto std_t = so::ent_table(core::settings::Settings{});
            check("ent table: standard -- .cbz / .epub -> Entertainment/EBooks, .mkv -> Movies, .txt none",
                  so::ent_folder(std_t, "Batman 01.CBZ") == "Entertainment/EBooks" &&
                  so::ent_folder(std_t, "a.epub") == "Entertainment/EBooks" &&
                  so::ent_folder(std_t, "a.mkv") == "Entertainment/Movies" && so::ent_folder(std_t, "a.txt").empty());
            core::settings::Settings st2;
            st2.ent_set = true;
            st2.ent = {"*.cbz -> Entertainment/Comics", "*.epub -> Entertainment/EBooks"};
            const auto back = core::settings::parse(core::settings::format(st2));
            const auto mine = so::ent_table(back);
            check("ent table: yours, saved and read back -- .cbz -> Comics; .mkv not listed -> none",
                  back == st2 && so::ent_folder(mine, "x.cbz") == "Entertainment/Comics" && so::ent_folder(mine, "x.mkv").empty());
        }
        check("folder: a Desktop folder moves whole by most of its files (Trip -> Pictures/Desktop <month>)",
              row("Desktop/Trip") && row("Desktop/Trip")->go == so::Go::Home &&
              row("Desktop/Trip")->folder.rfind("Pictures/Desktop ", 0) == 0 && !row("Desktop/Trip/a.jpg"),
              row("Desktop/Trip") ? row("Desktop/Trip")->folder : "missing");
        check("project: Desktop/Essay-project matches writing-project -> ~/Writing, whole; a code project stays",
              row("Desktop/Essay-project") && row("Desktop/Essay-project")->is_project &&
              to("Desktop/Essay-project", "Writing") && !row("Desktop/Essay-project/draft.odt"));
        check("project: one with no matching template is listed, not moved (Stray-project)",
              row("Stray-project") && row("Stray-project")->go == so::Go::Stays);
        const auto before = tree_facts(H);
        auto planned = so::plan(H, rows, now);
        latch::door::Door door(H, H + "/.tidy-data-sorta");
        door.load();
        planned.plan.previewed = true;
        const auto r = door.run(planned.plan);
        check("run: all done", r.ok && r.failed == 0, r.messages.empty() ? "" : r.messages.front());
        check("run: on disk -- the season folder, the episode folder whole, the movie, the book, the Trip folder, Essay in Writing",
              fs::exists(home / "Entertainment/Series/Mobland/Season 01/Mobland_s01e02.mkv") &&
              fs::exists(home / "Entertainment/Series/Show/Season 02/Show.S02E01.1080p/Show.S02E01.srt") &&
              fs::exists(home / "Entertainment/Movies/Dune.2021.1080p.mkv") &&
              fs::exists(home / "Entertainment/EBooks/book.epub") && fs::exists(home / "Writing/Essay-project/draft.odt") &&
              !fs::exists(home / "Desktop/Trip") && fs::exists(home / "Downloads/Unwatched.mkv") &&
              fs::exists(home / "Downloads/Unread.Folder/x.mkv"));
        check("run: the moved episode is Keep; the unread one Ephemeral",
              core::facts::tags_of(H + "/Entertainment/Series/Mobland/Season 01/Mobland_s01e02.mkv").retention == "keep" &&
              core::facts::tags_of(H + "/Downloads/Unwatched.mkv").retention == "ephemeral");
        const auto u = door.undo();
        check("undo: one step -- the episode, the folders and Essay-project back", u.ok &&
              fs::exists(home / "Downloads/Mobland_s01e02.mkv") && fs::exists(home / "Desktop/Trip/a.jpg") &&
              fs::exists(home / "Desktop/Essay-project/draft.odt") && fs::exists(home / "Downloads/Show.S02E01.1080p") &&
              !fs::exists(home / "Entertainment/Series"));
        (void)before;
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s006: the Closet -- decide / accept / Junk Drawer, on a FIXTURE ─────
    {
        std::cout << "[closet]\n";
        namespace rt = core::retention;
        namespace cb = core::birth;
        namespace cl = core::closet;
        namespace fc = core::facts;
        namespace tg = core::tagging;
        latch::door::set_app_name("Tidy");
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_closet_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_birth_extras(home);
        fs::remove_all(home / ".local");
        const std::string H = fs::canonical(home).string();
        const std::string D = core::paths::data_dir(H, nullptr);
        const auto ps = cb::places(H, "");
        const std::int64_t now = now_s();
        const auto look = [&](std::int64_t when = 0) { return cl::view(cb::report(H, ps), when ? when : now); };
        const auto names = [](const std::vector<cb::Born>& v) {
            std::set<std::string> s;
            for (const auto& b : v) s.insert(b.file.path.substr(b.file.path.find_last_of('/') + 1));
            return s;
        };
        const auto pick = [&](const std::vector<cb::Born>& v, std::set<std::string> want) {
            std::vector<cb::Born> out;
            for (const auto& b : v)
                if (want.count(b.file.path.substr(b.file.path.find_last_of('/') + 1))) out.push_back(b);
            return out;
        };
        struct Id { ino_t ino; off_t size; std::int64_t mt, mtn; };
        const auto ident = [](const std::string& p) {
            struct stat st{};
            ::lstat(p.c_str(), &st);
            return Id{st.st_ino, st.st_size, st.st_mtim.tv_sec, st.st_mtim.tv_nsec};
        };
        const auto same = [](const Id& a, const Id& b) { return a.ino == b.ino && a.size == b.size && a.mt == b.mt && a.mtn == b.mtn; };

        check("closet: days_since / aging -- fresh files are not aging",
              cl::days_since(now - 3 * 86400, now) == 3 && cl::days_since(0, now) == 0 &&
              cl::days_since(now + 99, now) == 0 && !cl::aging(fc::Facts{}, now));

        auto v = look();
        check("closet: proposed = the births with a level (8; s020a: all of Downloads)",
              names(v.proposed) == std::set<std::string>{"f1040.pdf", "cat.jpg", "fedora-44.rpm", "brochure.pdf", "notes.txt",
                                                         "Screenshot From 2026-11-10 09-14-02.png",
                                                         "Screencast From 2026-11-10 09-20-11.webm",
                                                         "Screenshot 2026-11-10.png"},
              std::to_string(v.proposed.size()));
        check("closet: waiting = the undecided with nothing to propose (4, the .scriv whole)",
              names(v.waiting) == std::set<std::string>{"plan.txt", "loose.txt", "report.odt", "Jasper.scriv"},
              std::to_string(v.waiting.size()));
        check("closet: a decided file never visits (cover-final, Keep)", v.decided == 1 && v.junk == 0 &&
              !names(v.proposed).count("cover-final.png") && !names(v.waiting).count("cover-final.png"));
        const auto later = look(now + (cl::kAgingDays + 10) * 86400LL);
        std::size_t aged = 0;
        for (const auto& b : later.proposed) aged += b.rule == "aged-out" && b.level == rt::Level::Ephemeral;
        check("closet: 40 days on, unread -> aging out to Ephemeral; a package waits (never ages as a file)",
              aged == 3 && later.waiting.size() == 1 && names(later.waiting).count("Jasper.scriv"),
              std::to_string(aged) + " aged");

        latch::door::Door door(H, D);
        door.load();
        core::catalog::Catalog cat;
        check("closet: the catalog opens (made by the first write)", cat.open(D, true));

        auto plan = cl::accept(v.proposed);
        bool tags_only = !plan.actions.empty();
        for (const auto& a : plan.actions) tags_only = tags_only && a.verb == latch::door::Verb::Tag;
        check("closet: accept is Tag actions only -- an id + a retention for each of the 8",
              tags_only && plan.actions.size() == 16, std::to_string(plan.actions.size()));
        std::map<std::string, Id> before;
        for (const auto& b : v.proposed) before[b.file.path] = ident(b.file.path);
        const auto refused = door.run(plan);
        check("closet: an unpreviewed plan is refused, nothing tagged",
              !refused.ok && refused.done == 0 && fc::tags_of(H + "/Downloads/cat.jpg").retention.empty());
        plan.previewed = true;
        const auto r1 = door.run(plan);
        tg::record(cat, tg::paths_of(door, r1.batch), now);
        bool each = r1.ok;
        for (const auto& b : v.proposed) {
            const auto t = fc::tags_of(b.file.path);
            each = each && t.retention == rt::id_of(b.level) && fc::is_id(t.id);
        }
        check("closet: accepted -- each file carries its own proposed level and an id", each,
              std::to_string(r1.done) + " steps");
        bool untouched = true;
        for (const auto& [p, id] : before) untouched = untouched && same(id, ident(p));
        check("closet: ...bytes, inode and mtime untouched", untouched);
        check("closet: ...the IRS form Until done, the screenshots Ephemeral",
              fc::tags_of(H + "/Downloads/f1040.pdf").retention == "until-done" &&
              fc::tags_of(H + "/Desktop/Screenshot 2026-11-10.png").retention == "ephemeral");
        check("closet: ...in the catalog (8)", cat.count() == 8, std::to_string(cat.count()));
        v = look();
        check("closet: ...and they left the Closet (0 proposed, 9 decided)",
              v.proposed.empty() && v.waiting.size() == 4 && v.decided == 9);
        check("closet: accept never takes a decided file back", [&] {
            cb::Born b = cb::classify(fc::Facts{}, "desktop", "");
            fc::of(H + "/Desktop/cover-final.png", b.file);
            b = cb::classify(b.file, "desktop", "");
            return cl::accept({b}).actions.empty();
        }());

        auto jp = cl::junk_drawer(pick(v.waiting, {"plan.txt"}));
        jp.previewed = true;
        const auto r2 = door.run(jp);
        tg::record(cat, tg::paths_of(door, r2.batch), now);
        const auto jt = fc::tags_of(H + "/Desktop/plan.txt");
        v = look();
        check("closet: the Junk Drawer -- Keep, role junk-drawer; counted, out of the Closet",
              r2.ok && jt.retention == "keep" && jt.role == cl::kJunkDrawer && v.junk == 1 && v.waiting.size() == 3 &&
              !names(v.waiting).count("plan.txt"));

        auto dp = cl::decide(pick(v.waiting, {"report.odt"}), rt::Level::Held);
        dp.previewed = true;
        const auto r3 = door.run(dp);
        tg::record(cat, tg::paths_of(door, r3.batch), now);
        check("closet: decided by hand -- report.odt Held", r3.ok && fc::tags_of(H + "/Documents/report.odt").retention == "held" &&
              dp.label.find("Held") != std::string::npos);

        v = look();
        auto wp = cl::accept(pick(v.waiting, {"loose.txt"}));
        wp.previewed = true;
        const auto r4 = door.run(wp);
        tg::record(cat, tg::paths_of(door, r4.batch), now);
        const auto lt = fc::tags_of(H + "/Documents/loose.txt");
        const std::string loose_id = lt.id;
        v = look();
        check("closet: accepting a waiting file = seen (an id, Undecided) -- it stays in the Closet",
              r4.ok && lt.retention == "undecided" && fc::is_id(lt.id) && names(v.waiting).count("loose.txt"));
        auto dp2 = cl::decide(pick(v.waiting, {"loose.txt"}), rt::Level::Historical);
        dp2.previewed = true;
        door.run(dp2);
        check("closet: deciding it later keeps its id (an id is never replaced)",
              fc::tags_of(H + "/Documents/loose.txt").id == loose_id &&
              fc::tags_of(H + "/Documents/loose.txt").retention == "historical");

        // Undo, newest first, down to (not through) the accept: the files go
        // back to the Closet, and the catalog follows.
        std::size_t undone = 0;
        while (door.can_undo() && door.undo_label().find("accept 8") == std::string::npos) {
            std::uint64_t b = 0;
            for (auto it = door.batches().rbegin(); it != door.batches().rend(); ++it)
                if (!it->undone) { b = it->id; break; }
            undone += door.undo().ok;
            tg::after_undo(cat, door, b, now);
        }
        v = look();
        check("closet: undo x4 -- back in the Closet as they were (junk, held, seen, historical)",
              undone == 4 && v.junk == 0 && v.waiting.size() == 4 && v.decided == 9 &&
              fc::tags_of(H + "/Desktop/plan.txt").id.empty() &&
              fc::tags_of(H + "/Documents/loose.txt").retention.empty(),
              std::to_string(undone));
        check("closet: ...and the catalog holds only the 8 accepted", cat.count() == 8, std::to_string(cat.count()));

        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // ── s006a: settings, the junk list, Tidy's own writer -- on a FIXTURE ──
    {
        std::cout << "[settings]\n";
        namespace st = core::settings;
        namespace ow = core::own;
        namespace cb = core::birth;
        namespace cl = core::closet;
        namespace rt = core::retention;

        const auto d = st::parse("");
        check("settings: the defaults -- 30 days, an empty junk list", d.aging_days == 30 && d.junk.empty());
        st::Settings s;
        s.aging_days = 12;
        s.junk = {"*.png", "Screenshot*"};
        check("settings: format -> parse is the same settings", st::parse(st::format(s)) == s, st::format(s));
        check("settings: days clamped 1..3650; junk deduped; junk text, bad lines ignored",
              st::parse("aging_days=0").aging_days == 1 && st::parse("aging_days=99999").aging_days == 3650 &&
              st::parse("aging_days=x").aging_days == 30 && st::parse("junk=a\njunk=a\nnonsense\n# c\n").junk.size() == 1);
        check("settings: a text box -> patterns (trimmed, blanks and repeats dropped)",
              st::patterns_of("  *.png \n\n*.png\nScreenshot*\r\n") == std::vector<std::string>{"*.png", "Screenshot*"});
        check("settings: junk_match -- glob, case-insensitive, names the pattern",
              st::junk_match(s.junk, "CAT.PNG") == "*.png" && st::junk_match(s.junk, "screenshot 1.jpg") == "Screenshot*" &&
              st::junk_match(s.junk, "notes.txt").empty() && st::junk_match({}, "a.png").empty());

        check("own: refuses absolute, '..', hidden, and the catalog's / door's files",
              !ow::refusal("/etc/x").empty() && !ow::refusal("a/../b").empty() && !ow::refusal(".hidden").empty() &&
              !ow::refusal("catalog.db").empty() && !ow::refusal("catalog.db-wal").empty() && !ow::refusal("manifest").empty() &&
              !ow::refusal("trash/x").empty() && ow::refusal(st::kFile).empty() && ow::refusal("templates/writing.tpl").empty());

        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_settings_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_birth_extras(home);
        fs::remove_all(home / ".local");
        const std::string H = fs::canonical(home).string();
        const std::string D = core::paths::data_dir(H, nullptr);
        bool found = true;
        ow::read(D, st::kFile, &found);
        check("own: nothing there yet -> read says not found", !found);
        const std::string err = ow::write(D, st::kFile, st::format(s));
        const std::string back = ow::read(D, st::kFile, &found);
        check("own: written (folder made), read back the same", err.empty() && found && st::parse(back) == s, err);
        s.aging_days = 45;
        ow::write(D, st::kFile, st::format(s));
        check("own: written again -- replaced whole, no temp left behind",
              st::parse(ow::read(D, st::kFile)).aging_days == 45 && !fs::exists(D + "/.settings.ini.tmp"));
        check("own: a refused name writes nothing", !ow::write(D, "../escape.txt", "x").empty() && !fs::exists(H + "/.local/share/escape.txt"));

        const std::int64_t now = now_s();
        const auto ps = cb::places(H, "");
        st::Settings junk;
        junk.junk = {"Screenshot*", "*.txt"};
        const auto v = cl::view(cb::report(H, ps), now, junk);
        std::size_t listed = 0;
        bool shots = true;
        for (const auto& b : v.proposed) {
            if (b.rule != "junk-list") continue;
            ++listed;
            if (b.file.path.find("Screenshot") != std::string::npos) shots = shots && b.level == rt::Level::Keep;
        }
        // Screenshot From.png + Screenshot 2026.png (over the birth rule), notes, plan, loose (txt).
        check("junk list: matching Closet files proposed for the Junk Drawer -- over the birth rule",
              listed == 5 && shots, std::to_string(listed));
        check("junk list: a decided file is never re-proposed (cover-final stays decided)", v.decided == 1);

        latch::door::set_app_name("Tidy");
        latch::door::Door door(H, D);
        door.load();
        auto plan = cl::accept(v.proposed);
        plan.previewed = true;
        const auto r = door.run(plan);
        const auto t = core::facts::tags_of(H + "/Downloads/notes.txt");
        const auto shot = core::facts::tags_of(H + "/Desktop/Screenshot 2026-11-10.png");
        const auto v2 = cl::view(cb::report(H, ps), now, junk);
        check("junk list: accepted -> Keep + junk-drawer; counted in the Junk Drawer, out of the Closet",
              r.ok && t.retention == "keep" && t.role == cl::kJunkDrawer && shot.role == cl::kJunkDrawer &&
              v2.junk == 5 && v2.proposed.empty(), std::to_string(v2.junk));

        st::Settings soon;
        soon.aging_days = 5;
        const auto later = cl::view(cb::report(H, ps), now + 6 * 86400LL, soon);
        std::size_t aged = 0;
        for (const auto& b : later.proposed) aged += b.rule == "aged-out";
        check("aging: the setting decides -- 5 days set, 6 days on, the waiting age out",
              aged == 1 && cl::view(cb::report(H, ps), now + 6 * 86400LL).proposed.empty(), std::to_string(aged));

        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // -- Shortcuts: the registry paradigm, exercised ---------------------------
    // A seed's claim isn't a fact until a consumer exercises it. The registry's
    // whole promise is that a key and its advertised row can't drift, because
    // both read one list -- so the guard that matters is pure and lives here:
    // no two chords may collide. (App wires from this same list; ShortcutsDialog
    // renders from it.)
    {
        namespace sc = tidy::core;

        check("shortcuts: format_accel renders modifiers + key",
              sc::format_accel("<Ctrl><Shift>n") == "Ctrl+Shift+N",
              sc::format_accel("<Ctrl><Shift>n"));
        check("shortcuts: format_accel maps a named key to its glyph",
              sc::format_accel("<Ctrl>question") == "Ctrl+?",
              sc::format_accel("<Ctrl>question"));

        {
            sc::ShortcutSpec derived; derived.accels = {"<Ctrl>question", "<Ctrl>slash"};
            check("shortcuts: display_keys derives + joins accels",
                  derived.display_keys() == "Ctrl+?  /  Ctrl+/",
                  derived.display_keys());
            sc::ShortcutSpec doc; doc.keys = "Click a recent";
            check("shortcuts: display_keys prefers an explicit key string",
                  doc.display_keys() == "Click a recent");
        }

        check("shortcuts: registry is populated",
              !sc::shortcut_registry().empty(),
              std::to_string(sc::shortcut_registry().size()) + " specs");
        check("shortcuts: registry is accel-collision-free",
              sc::find_accel_collisions().empty(),
              sc::find_accel_collisions().empty()
                  ? "clean" : sc::find_accel_collisions().front());

        // Sections authored A-Z + contiguous: the dialog walks linearly and
        // starts a heading on change, so a stray out-of-order row would split a
        // section into two headings.
        {
            std::vector<std::string> secs;
            for (const auto& r : sc::shortcut_registry())
                if (secs.empty() || secs.back() != r.section) secs.push_back(r.section);
            std::vector<std::string> sorted = secs;
            std::sort(sorted.begin(), sorted.end());
            check("shortcuts: sections are contiguous and A-Z ordered", secs == sorted);
        }

        // The doc-only half of the paradigm: a gesture row documents without
        // binding (no accels), but must still show keys.
        {
            bool ok = true, saw_doc_only = false;
            for (const auto& r : sc::shortcut_registry())
                if (r.action.empty()) {
                    saw_doc_only = true;
                    if (!r.accels.empty() || r.keys.empty()) ok = false;
                }
            check("shortcuts: a doc-only row binds nothing but still displays keys",
                  ok && saw_doc_only);
        }
    }


    // ── s007: templates + the tree builder -- on a FIXTURE ─────────────────
    {
        std::cout << "[templates]\n";
        namespace tp = core::templates;
        using R = tp::Role;

        check("templates: three roles, ids stable", tp::roles().size() == 3 &&
              std::string(tp::id_of(R::Product)) == "product" && std::string(tp::id_of(R::Input)) == "input" &&
              std::string(tp::id_of(R::History)) == "history");
        const auto sh = tp::shell("writing-project", "Writing");
        check("templates: the common shell -- backups (history), docs, fixes, scripts",
              sh.folders.size() == 4 && tp::find(sh, "backups")->role == R::History && tp::find(sh, "fixes") &&
              tp::problems(sh).empty(), std::to_string(tp::problems(sh).size()));
        check("templates: names -- a-z 0-9 '-' only; a folder never hidden, '..', or seven deep (s024: six allowed)",
              tp::name_refusal("writing-project").empty() && !tp::name_refusal("Writing").empty() &&
              !tp::name_refusal("").empty() && !tp::name_refusal("-x").empty() &&
              tp::path_refusal("Graphics/Covers").empty() && tp::path_refusal("Body/OCR/Scans/Archive").empty() &&
              tp::path_refusal("a/b/c/d/e/f").empty() && !tp::path_refusal("a/b/c/d/e/f/g").empty() &&
              !tp::path_refusal(".git").empty() && !tp::path_refusal("a/..").empty() && !tp::path_refusal("").empty());

        tp::Template t = sh;
        t.folders.push_back({"Graphics", R::Input});
        t.folders.push_back({"Graphics/Covers", R::Product});
        t.folders.push_back({"Manuscript", R::Product});
        t.routes = {{"*.curvz", "Graphics"}, {"*.png", "Graphics/Covers"}};
        t = tp::parse(tp::format(t));
        check("templates: format -> parse is the same template (tree order)", tp::parse(tp::format(t)) == t &&
              t.folders.size() == 7 && t.folders[3].path == "Graphics" && t.folders[4].path == "Graphics/Covers",
              tp::format(t));
        check("templates: routing -- first fit, case-insensitive", tp::route_for(t, "Cover.PNG") == "Graphics/Covers" &&
              tp::route_for(t, "jasper.curvz") == "Graphics" && tp::route_for(t, "notes.txt").empty());
        {
            auto bad = t;
            bad.folders.push_back({"Orphan/Child", R::Input});
            bad.routes.push_back({"*.txt", "Nowhere"});
            bad.category = ".hidden";
            check("templates: problems -- an orphan child, a route to nowhere, a hidden category",
                  tp::problems(bad).size() == 3, std::to_string(tp::problems(bad).size()));
            check("templates: parse drops a seven-deep folder and a bad role",
                  tp::parse("name=x\ncategory=X\nfolder=input a/b/c/d/e/f/g\nfolder=nope a\nfolder=input a\n").folders.size() == 1);
        }

        // The builder: every edit undoable.
        tp::Builder b(tp::Draft{tp::shell("writing-project", "Writing"), {}, ""});
        const bool e1 = b.add_folder("Graphics", R::Input).empty();
        const bool e2 = b.add_folder("Graphics/Covers/", R::Product).empty();   // trailing '/' tolerated
        const bool r1 = !b.add_folder("Research/Notes", R::Input).empty();       // parent first
        const bool r2 = !b.add_folder("a/b/c", R::Input).empty();
        const bool r3 = !b.add_folder("Graphics", R::Input).empty();
        const bool e3 = b.add_route("*.curvz", "Graphics").empty();
        const bool r4 = !b.add_route("*.CURVZ", "Graphics").empty() && !b.add_route("*.x", "Nowhere").empty();
        check("builder: add folders and a route; refuses orphans, three deep, repeats, routes to nowhere",
              e1 && e2 && e3 && r1 && r2 && r3 && r4 && b.edits() == 3, std::to_string(b.edits()));
        b.set_role("Graphics", R::Product);
        b.remove_folder("Graphics");
        check("builder: removing a folder takes its children and its routes",
              !tp::find(b.tpl(), "Graphics/Covers") && b.tpl().routes.empty());
        b.undo();
        check("builder: undo puts them back, the role as it was", tp::find(b.tpl(), "Graphics/Covers") &&
              b.tpl().routes.size() == 1 && tp::find(b.tpl(), "Graphics")->role == R::Product);
        while (b.undo()) {}
        check("builder: undo all the way -- the shell it started from", b.tpl() == tp::shell("writing-project", "Writing") && !b.can_undo());
        check("builder: a refused edit changes nothing and is not an edit",
              !b.set_name("Bad Name").empty() && b.tpl().name == "writing-project" && !b.can_undo());
        check("builder: a new template never takes a saved name", !b.save_refusal({"writing-project"}).empty() &&
              b.save_refusal({"other"}).empty());

        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_templates_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_projects(home);
        fs::remove_all(home / ".local");
        const std::string H = fs::canonical(home).string();
        const std::string D = core::paths::data_dir(H, nullptr);
        const std::string J = H + "/Writing/Jasper-project";

        // Lift: a proposal, read-only.
        const auto before = tree_facts(H);
        const auto writing = tree_facts(H + "/Writing");
        auto prop = tp::proposal(J);
        const auto after = tree_facts(H);
        std::string paths;
        for (const auto& f : prop.t.folders) paths += f.path + " ";
        check("lift: the project's folders two deep -- Research's deep folders, hidden, symlinks, files left out",
              prop.t.folders.size() == 11 && !tp::find(prop.t, "Research/Sources/1850s") && !tp::find(prop.t, ".git") &&
              !tp::find(prop.t, "link-to-docs") && tp::find(prop.t, "Research/Sources"), paths);
        check("lift: name and category from where it lives; roles guessed",
              prop.t.name == "jasper-project" && prop.t.category == "Writing" &&
              tp::find(prop.t, "backups")->role == R::History && tp::find(prop.t, "Final")->role == R::Product &&
              tp::find(prop.t, "Graphics")->role == R::Input, prop.t.name + " / " + prop.t.category);
        check("lift: READ-ONLY -- every file and folder's inode, size, mtime, ctime the same; no record made",
              before == after && !fs::exists(D), std::to_string(before.size()));
        check("lift: every lifted folder arrives unconfirmed", prop.unconfirmed.size() == prop.t.folders.size());

        tp::Builder lb(prop);
        const auto refused = lb.save_refusal({});
        lb.set_name("writing-project");
        lb.remove_folder("Graphics/old covers");           // the drift in the example: not the rule
        for (const auto& f : lb.tpl().folders) if (lb.draft().unconfirmed.count(f.path)) lb.confirm(f.path);
        check("lift: never adopted as-is -- Save refused until each folder is kept or removed",
              !refused.empty() && lb.draft().unconfirmed.empty() && lb.save_refusal({}).empty(), refused);
        lb.undo();
        check("lift: undo un-keeps the last one kept", lb.draft().unconfirmed.size() == 1);
        for (const auto& u : std::set<std::string>(lb.draft().unconfirmed)) lb.confirm(u);
        const std::string werr = tp::save(D, lb.tpl());
        tp::save(D, t.name == "writing-project" ? tp::shell("essay-project", "Writing") : t);
        const auto saved = tp::list(D);
        check("templates: saved through core::own under <data>/templates; listed, sorted, read back the same",
              werr.empty() && saved.size() == 2 && saved[0].name == "essay-project" && saved[1] == lb.tpl() &&
              fs::exists(D + "/templates/writing-project.template") && !fs::exists(D + "/templates/.writing-project.template.tmp"),
              werr);
        check("templates: a template with problems is not written", !tp::save(D, tp::Template{}).empty() &&
              tp::list(D).size() == 2);

        // Drift, at template depth only.
        const auto tw = saved[1];
        const auto dr = tp::drift(tw, tp::folders_of(J));
        std::string said;
        for (const auto& d : dr) said += std::string(d.kind == tp::Drift::Kind::Missing ? "-" : "+") + d.path + " ";
        check("drift: the stray old covers is extra; nothing below template depth is",
              dr.size() == 1 && dr[0] == tp::Drift{tp::Drift::Kind::Extra, "Graphics/old covers"}, said);
        const auto mary = tp::drift(tw, tp::folders_of(H + "/Writing/Mary-project"));
        said.clear();
        for (const auto& d : mary) said += std::string(d.kind == tp::Drift::Kind::Missing ? "-" : "+") + d.path + " ";
        // Mary: no fixes/, no Final/, an extra Notes/, and Research has its own children (free).
        check("drift: missing and extra at the top; Manuscript's own folders and three deep free",
              mary.size() == 3 && std::count(mary.begin(), mary.end(), tp::Drift{tp::Drift::Kind::Missing, "fixes"}) &&
              std::count(mary.begin(), mary.end(), tp::Drift{tp::Drift::Kind::Extra, "Notes"}), said);
        tp::Builder ab(tp::Draft{tw, {}, ""}, tw.name);
        for (const auto& d : mary) ab.adopt(d);
        check("drift: adopted -> the template matches the project; one undo per difference",
              tp::drift(ab.tpl(), tp::folders_of(H + "/Writing/Mary-project")).empty() && ab.edits() == 3 &&
              ab.save_refusal({"writing-project"}).empty());
        check("drift: READ-ONLY -- the projects untouched", tree_facts(H + "/Writing") == writing);
        fs::remove_all(root);
    }

    // ── s010a: the starters -- the creative base + its kinds ─────────────────
    {
        std::cout << "[starters]\n";
        namespace tp = core::templates;
        namespace st = core::starters;
        using R = tp::Role;
        const auto& all = st::all();
        std::set<std::string> names;
        bool clean = true, round = true;
        std::string why;
        for (const auto& s : all) {
            names.insert(s.t.name);
            if (auto ps = tp::problems(s.t); !ps.empty()) { clean = false; why = s.t.name + ": " + ps.front(); }
            if (tp::parse(tp::format(s.t)) != s.t) { round = false; why = s.t.name + " round trip"; }
        }
        check("starters: eleven, the base first, every name its own (s024: + bookbinding)", all.size() == 11 && names.size() == 11 &&
              all.front().t.name == "creative-project" && all.front().t.category == "Creative");
        check("starters: every one passes the template's problems()", clean, why);
        check("starters: every one reads back the same (format -> parse)", round, why);
        const auto& b = all.front().t;
        check("starters: the base -- no fixes/; Assets/ by medium with Fonts; Work + Final + Promo product; Drafts history",
              b.folders.size() == 15 && !tp::find(b, "fixes") && tp::find(b, "Assets/Fonts") &&
              tp::find(b, "Work")->role == R::Product && tp::find(b, "Final")->role == R::Product &&
              tp::find(b, "Promo")->role == R::Product && tp::find(b, "Drafts")->role == R::History &&
              tp::find(b, "backups")->role == R::History);
        bool shares = true;
        for (const auto& s : all)
            for (const auto& f : b.folders) {
                if (s.t.name == "code-project" || s.t.name == "bookbinding") continue;   // not creative kinds
                const auto* g = tp::find(s.t, f.path);
                if (!g || g->role != f.role) { shares = false; why = s.t.name + " lacks " + f.path; }
            }
        check("starters: every kind holds the whole base tree, roles unchanged", shares, why);
        bool free_work = true;
        for (const auto& s : all)
            for (const auto& f : s.t.folders)
                if (f.path.rfind("Work/", 0) == 0) free_work = false;
        std::vector<std::string> proj;
        for (const auto& f : b.folders) proj.push_back(f.path);
        proj.push_back("Work/r003_2026-11-10");
        proj.push_back("Work/r004_2026-11-11");
        std::sort(proj.begin(), proj.end());
        check("starters: Work/ names no children -- revision folders are never drift",
              free_work && tp::drift(b, proj).empty());
        auto rt = [&](const char* n, const char* file) { return tp::route_for(st::find(n)->t, file); };
        check("starters: base routes -- curvz -> Graphics, ttf -> Fonts, scriv -> Work, md -> Notes",
              rt("creative-project", "cover.curvz") == "Assets/Graphics" && rt("creative-project", "Body.TTF") == "Assets/Fonts" &&
              rt("creative-project", "Jasper.scriv") == "Work" && rt("creative-project", "ideas.md") == "Notes");
        check("starters: a kind's route wins over the base's",
              rt("design-project", "logo.curvz") == "Work" && rt("music-project", "kick.wav") == "Assets/Samples" &&
              rt("music-project", "take.mp3") == "Assets/Audio" && rt("3d-project", "beauty.png") == "Assets/Renders" &&
              rt("video-project", "a001.MOV") == "Assets/Footage" && rt("photo-project", "IMG_0001.NEF") == "Assets/Raw" &&
              rt("writing-project", "ch1.odt") == "Work" && rt("cad-project", "bracket.step") == "Assets/Parts");
        bool no_pdf = true;
        for (const auto& s : all)
            if (s.t.name != "bookbinding" &&   // s024: a book's pdf is its print, its epub its final
                (!tp::route_for(s.t, "book.pdf").empty() || !tp::route_for(s.t, "book.epub").empty()))
                no_pdf = false;
        check("starters: pdf / epub route nowhere (bookbinding aside) -- the row asks", no_pdf);
        {
            const auto& c = st::find("code-project")->t;
            check("starters: code-project -- our gtk workspace: backups(+Old) history, docs, fixes, scripts; ~/Develop/gtk",
                  c.category == "Develop/gtk" && c.folders.size() == 5 && tp::find(c, "fixes") &&
                  tp::find(c, "backups/Old")->role == R::History && !tp::find(c, "Assets") &&
                  tp::route_for(c, "s010a_starters.zip") == "backups" && tp::route_for(c, "HANDOFF.md") == "docs" &&
                  tp::route_for(c, "tidy_backup.sh") == "scripts");
        }
        check("starters: find by name; an unknown is none", st::find("cad-project") && !st::find("fixes-project"));

        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_starters_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string D = (root / "data").string();
        tp::Builder ub(tp::Draft{st::find("art-project")->t, {}, ""});
        const bool may = ub.save_refusal({}).empty();
        check("starters: nothing written until Save -- no data folder", !fs::exists(root));
        const auto err = tp::save(D, ub.tpl());
        const auto back = tp::list(D);
        check("starters: Use -> Save writes it through own; it reads back the same",
              may && err.empty() && back.size() == 1 && back.front() == st::find("art-project")->t, err);
        check("starters: Use again over the saved name -> Save refused",
              !tp::Builder(tp::Draft{st::find("art-project")->t, {}, ""}).save_refusal({"art-project"}).empty());
        fs::remove_all(root);
    }

    // ── s008: projects -- found, placed whole, fixed -- on a FIXTURE ────────
    {
        std::cout << "[projects]\n";
        namespace pj = core::projects;
        namespace tp = core::templates;
        namespace dr = latch::door;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_projects_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_projects(home);
        fs::remove_all(home / ".local");
        const std::string H = fs::canonical(home).string();
        const std::string D = core::paths::data_dir(H, nullptr);

        check("projects: the suffix is the declaration; category = where it lives",
              pj::is_project_name("Jasper-project") && !pj::is_project_name("-project") && !pj::is_project_name("project") &&
              pj::category_of("Writing/Jasper-project") == "Writing" && pj::category_of("Develop/gtk/tidy-project") == "Develop" &&
              pj::category_of("Desktop/Essay-project").empty() && pj::category_of("Stray-project").empty());

        const auto before = tree_facts(H);
        const auto found = pj::find(H);
        const auto after = tree_facts(H);
        std::string rels;
        for (const auto& p : found) rels += p.rel + (p.code ? "(code)" : "") + (p.category.empty() ? "(uncat)" : "") + " ";
        check("find: five projects -- never inside one, never in fixes/ or a hidden folder",
              found.size() == 5 && rels.find("nested") == std::string::npos && rels.find("Hidden") == std::string::npos &&
              rels.find("Cached") == std::string::npos, rels);
        const auto by = [&](const std::string& rel) -> const pj::Project* {
            for (const auto& p : found) if (p.rel == rel) return &p;
            return nullptr;
        };
        check("find: tidy-project is code, in Develop; Essay and Stray uncategorized; names without the suffix",
              by("Develop/gtk/tidy-project") && by("Develop/gtk/tidy-project")->code &&
              by("Develop/gtk/tidy-project")->category == "Develop" && by("Desktop/Essay-project") &&
              by("Desktop/Essay-project")->category.empty() && by("Stray-project") && by("Writing/Jasper-project")->name == "Jasper", rels);
        check("find: READ-ONLY -- nothing changed", before == after);

        const std::set<std::string> left = {"Stray-project"};
        check("left: projects.ini round trip; find marks a left one",
              pj::parse_left(pj::format_left(left)) == left && pj::find(H, left).size() == 5 && [&] {
                  for (const auto& p : pj::find(H, left)) if (p.rel == "Stray-project") return p.left;
                  return false; }());

        std::string why;
        const bool r1 = pj::place(*by("Develop/gtk/tidy-project"), H, "Writing", &why).actions.empty() && !why.empty();
        const bool r2 = pj::place(*by("Writing/Jasper-project"), H, "Essays", &why).actions.empty();
        const bool r3 = pj::place(*by("Desktop/Essay-project"), H, "Downloads", &why).actions.empty() &&
                        pj::place(*by("Desktop/Essay-project"), H, ".x", &why).actions.empty();
        check("place: a code project, a placed one, a landing place, a hidden category -- refused", r1 && r2 && r3);

        // s010c: categories two deep; the deepest template wins; code one down.
        {
            namespace tp = core::templates;
            check("category: one or two deep -- Writing, Develop/gtk; never three, hidden, or empty",
                  tp::category_refusal("Writing").empty() && tp::category_refusal("Develop/gtk").empty() &&
                  !tp::category_refusal("a/b/c").empty() && !tp::category_refusal(".x/gtk").empty() &&
                  !tp::category_refusal("").empty());
            tp::Template dev = tp::shell("dev-project", "Develop");
            const auto code = core::starters::find("code-project")->t;
            const std::vector<tp::Template> ts = {dev, code};
            pj::Project loose = *by("Develop/gtk/tidy-project");
            loose.rel = "Develop/foo-project";
            check("template_for: Develop/gtk/tidy-project -> code-project (Develop/gtk) over Develop; "
                  "Develop/foo-project -> Develop",
                  pj::template_for(*by("Develop/gtk/tidy-project"), ts) == &ts[1] &&
                  pj::template_for(loose, ts) == &ts[0] && !pj::template_for(*by("Stray-project"), ts));
            const auto p2 = pj::place(*by("Stray-project"), H, "Art/Old", &why);
            check("place: two deep -> make ~/Art, make ~/Art/Old, move -- parents first",
                  p2.actions.size() == 3 && p2.actions[0].to == H + "/Art" && p2.actions[1].to == H + "/Art/Old" &&
                  p2.actions[2].verb == dr::Verb::Move && p2.actions[2].to == H + "/Art/Old/Stray-project", why);
            check("place: a landing place two deep is still refused",
                  pj::place(*by("Stray-project"), H, "Desktop/x", &why).actions.empty());

            const fs::path r2 = fs::temp_directory_path() / ("tidy_selftest_code_" + std::to_string(::getpid()));
            fs::remove_all(r2);
            fs::create_directories(r2 / "Desktop/Ws-project/ws/.git");
            fs::create_directories(r2 / "Desktop/Make-project/src/lib");
            put_file(r2 / "Desktop/Make-project/src/lib/CMakeLists.txt", "x");    // two down: not code
            fs::create_directories(r2 / "Desktop/Hid-project/.inner/.git");       // hidden one down: not looked into
            fs::create_directories(r2 / "Desktop/Plain-project/notes");
            const auto f2 = pj::find(r2.string());
            auto code_of = [&](const std::string& rel) { for (const auto& p : f2) if (p.rel == rel) return p.code; return false; };
            const pj::Project* ws = nullptr;
            for (const auto& p : f2) if (p.rel == "Desktop/Ws-project") ws = &p;
            check("code: .git one folder down is code (our workspace on the Desktop); two down, hidden, none -- not",
                  f2.size() == 4 && code_of("Desktop/Ws-project") && !code_of("Desktop/Make-project") &&
                  !code_of("Desktop/Hid-project") && !code_of("Desktop/Plain-project"));
            check("place: a workspace found on the Desktop is never moved",
                  ws && pj::place(*ws, r2.string(), "Develop/gtk", &why).actions.empty() &&
                  why.find("code") != std::string::npos, why);
            fs::remove_all(r2);
        }

        latch::door::set_app_name("Tidy");
        dr::Door door(H, D);
        door.load();
        const auto essay_before = tree_facts(H + "/Desktop/Essay-project");
        auto plan = pj::place(*by("Desktop/Essay-project"), H, "Essays", &why);
        check("place: a plan -- make ~/Essays, move the project whole", plan.actions.size() == 2 &&
              plan.actions[0].verb == dr::Verb::MakeDir && plan.actions[1].verb == dr::Verb::Move, why);
        const auto refused_unpreviewed = !door.run(plan).ok && fs::exists(H + "/Desktop/Essay-project");
        plan.previewed = true;
        const auto r = door.run(plan);
        std::map<std::string, std::string> moved;
        for (const auto& [k, v] : tree_facts(H + "/Essays/Essay-project"))
            moved[H + "/Desktop/Essay-project" + k.substr((H + "/Essays/Essay-project").size())] = v;
        // A rename keeps inode, size, mtime; ctime of the moved folder itself may change.
        const auto strip_ctime = [](std::map<std::string, std::string> m) {
            for (auto& [k, v] : m) v = v.substr(0, v.rfind('/'));
            return m;
        };
        check("place: refused unpreviewed; then moved WHOLE -- every file and folder the same inode, size, mtime",
              refused_unpreviewed && r.ok && !fs::exists(H + "/Desktop/Essay-project") &&
              strip_ctime(moved) == strip_ctime(essay_before) && moved.size() == 3,
              std::to_string(moved.size()) + " " + (r.messages.empty() ? "" : r.messages.front()));
        const auto u = door.undo();
        check("place: undone -- back on the Desktop, ~/Essays gone, the same tree",
              u.ok && fs::exists(H + "/Desktop/Essay-project/Research/notes.txt") && !fs::exists(H + "/Essays") &&
              strip_ctime(tree_facts(H + "/Desktop/Essay-project")) == strip_ctime(essay_before));

        // Fix: Mary against a writing template.
        auto t = tp::shell("writing-project", "Writing");
        t.folders.push_back({"Final", tp::Role::Product});
        t.folders.push_back({"Graphics", tp::Role::Input});
        t.folders.push_back({"Graphics/Covers", tp::Role::Input});
        const std::vector<tp::Template> ts = {t};
        const auto* mary = by("Writing/Mary-project");
        check("fix: the template for a project is its category's", pj::template_for(*mary, ts) == &ts[0] &&
              !pj::template_for(*by("Desktop/Essay-project"), ts));
        const auto dft = tp::drift(t, tp::folders_of(mary->path));
        auto fp = pj::fix(*mary, dft);
        std::size_t refused = 0;
        for (const auto& a : fp.actions) refused += !door.refusal(a).empty();
        check("fix: MakeDir for each missing folder (fixes/ and Final/); the door refuses fixes/ -- it is Scott's",
              fp.actions.size() == 2 && refused == 1 && !door.refusal(fp.actions[0]).empty() == (fp.actions[0].to.ends_with("/fixes")),
              std::to_string(fp.actions.size()) + "/" + std::to_string(refused));
        std::erase_if(fp.actions, [&](const dr::Action& a) { return !door.refusal(a).empty(); });
        fp.previewed = true;
        const auto fr = door.run(fp);
        const auto left_drift = tp::drift(t, tp::folders_of(mary->path));
        check("fix: Final/ made; missing now only fixes/ (refused); the extras never moved",
              fr.ok && fs::is_directory(mary->path + "/Final") && left_drift.size() == 4 &&
              std::count(left_drift.begin(), left_drift.end(), tp::Drift{tp::Drift::Kind::Missing, "fixes"}) &&
              fs::is_directory(mary->path + "/Notes"));
        door.undo();
        check("fix: undone -- Final/ gone again", !fs::exists(mary->path + "/Final"));
        check("fix: nothing missing -> an empty plan", pj::fix(*mary, {}).actions.empty());
        fs::remove_all(root);
    }

    // ── s010d: New project -- a template's tree + its .tidy-project id ───────
    {
        std::cout << "[new project]\n";
        namespace dr = latch::door;
        namespace pj = core::projects;
        namespace st = core::starters;
        namespace fc = core::facts;
        check("id: uuid shape; folder_for adds the suffix once",
              pj::is_uuid(fc::new_id()) && !pj::is_uuid("x") && !pj::is_uuid("") &&
              pj::folder_for("Jasper") == "Jasper-project" && pj::folder_for("Jasper-project") == "Jasper-project");
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_newproj_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string();
        fs::create_directories(H);
        const auto& art = st::find("art-project")->t;
        const auto& code = st::find("code-project")->t;
        const std::string id = fc::new_id();
        std::string why;
        const bool bad = pj::create(art, "", H, id, &why).actions.empty() &&
                         pj::create(art, ".hid", H, id, &why).actions.empty() &&
                         pj::create(art, "a/b", H, id, &why).actions.empty() &&
                         pj::create(art, "Ok", H, "not-an-id", &why).actions.empty();
        check("create: no name, hidden, a slash, no id -- refused", bad);
        const auto before = tree_facts(H);
        auto plan = pj::create(art, "Sunrise", H, id, &why);
        check("create: a plan -- ~/Art, the project, 16 folders parents first, then the id",
              plan.actions.size() == 19 && plan.actions[0].to == H + "/Art" &&
              plan.actions[1].to == H + "/Art/Sunrise-project" &&
              plan.actions.back().verb == dr::Verb::Write &&
              plan.actions.back().to == H + "/Art/Sunrise-project/.tidy-project", why);
        check("create: READ-ONLY until run -- nothing made", tree_facts(H) == before);
        latch::door::set_app_name("Tidy");
        dr::Door door(H, D);
        door.load();
        {   // what the preview shows: each step judged in order, earlier MakeDirs counted
            std::vector<std::string> made;
            std::size_t ok_in_order = 0, ok_alone = 0;
            for (const auto& a : plan.actions) {
                ok_alone += door.refusal(a).empty();
                if (door.refusal(a, made).empty()) { ++ok_in_order; if (a.verb == dr::Verb::MakeDir) made.push_back(a.to); }
            }
            check("create: the preview judges in order -- all 19 allowed (alone, only the first would be)",
                  ok_in_order == 19 && ok_alone == 1, std::to_string(ok_in_order) + " / " + std::to_string(ok_alone));
        }
        const bool refused = !door.run(plan).ok && !fs::exists(H + "/Art");
        plan.previewed = true;
        const auto r = door.run(plan);
        const auto found = pj::find(H);
        check("create: refused unpreviewed; run -> the tree on disk, Assets/Brushes too",
              refused && r.ok && fs::is_directory(H + "/Art/Sunrise-project/Assets/Brushes") &&
              fs::is_directory(H + "/Art/Sunrise-project/Work"), r.messages.empty() ? "" : r.messages.front());
        check("create: find() reads the id back from .tidy-project",
              found.size() == 1 && found[0].id == id && pj::read_id(H + "/Art/Sunrise-project") == id &&
              found[0].category == "Art");
        check("create: the same name again -- refused (it exists)",
              pj::create(art, "Sunrise-project", H, fc::new_id(), &why).actions.empty() &&
              why.find("exists") != std::string::npos, why);
        auto c2 = pj::create(code, "kiln", H, fc::new_id(), &why);
        check("create: two-deep category -> ~/Develop, ~/Develop/gtk made first",
              c2.actions.size() >= 3 && c2.actions[0].to == H + "/Develop" && c2.actions[1].to == H + "/Develop/gtk" &&
              c2.actions[2].to == H + "/Develop/gtk/kiln-project", why);
        const auto u = door.undo();
        // Undo of a create puts it in the home's Trash (latch: recoverable),
        // so outside .local/share/Trash the home is as it was.
        auto outside_trash = [&] {
            auto m = tree_facts(H);
            std::erase_if(m, [&](const auto& kv) { return kv.first.rfind(H + "/.local", 0) == 0; });
            return m;
        };
        check("create: undone -- the home as before outside the Trash; ~/Art gone",
              u.ok && outside_trash() == before && !fs::exists(H + "/Art"),
              u.messages.empty() ? "" : u.messages.front());
        fs::remove_all(root);
    }

    // ── s010e: ids for found projects; status in the catalog; search ────────
    {
        std::cout << "[project ids]\n";
        namespace dr = latch::door;
        namespace pj = core::projects;
        namespace ct = core::catalog;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_pids_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string();
        fs::create_directories(H + "/Writing/Jasper-project/Work");
        fs::create_directories(H + "/Develop/gtk/tidy-project/tidy/.git");
        fs::create_directories(H + "/Desktop/Loose-project");
        put_file(H + "/Writing/Odd-project/.tidy-project", "id=garbage\n");      // there, not a uuid: left alone
        int n = 0;
        const std::vector<std::string> fixed = {"11111111-1111-4111-8111-111111111111", "22222222-2222-4222-8222-222222222222",
                                                "33333333-3333-4333-8333-333333333333"};
        const auto gen = [&] { return fixed[static_cast<std::size_t>(n++) % fixed.size()]; };
        const auto found = pj::find(H);
        const auto before = tree_facts(H);
        auto plan = pj::give_ids(found, gen);
        check("give_ids: one Write per project without an id (code too); an odd dot file left alone",
              found.size() == 4 && plan.actions.size() == 3 &&
              std::all_of(plan.actions.begin(), plan.actions.end(), [](const dr::Action& a) {
                  return a.verb == dr::Verb::Write && a.to.ends_with("/.tidy-project"); }), plan.label);
        check("give_ids: READ-ONLY until run", tree_facts(H) == before);
        latch::door::set_app_name("Tidy");
        dr::Door door(H, D);
        door.load();
        plan.previewed = true;
        const auto r = door.run(plan);
        const auto again = pj::find(H);
        std::size_t with = 0;
        for (const auto& p : again) with += pj::is_uuid(p.id);
        check("give_ids: run -> three ids read back; none needed now", r.ok && with == 3 &&
              pj::give_ids(again, gen).actions.empty());

        ct::Catalog cat;
        const bool opened = cat.open(D, true);
        for (const auto& p : again)
            if (!p.id.empty()) cat.seen_project({p.id, p.path, p.name, "", "", "", 100, false});
        const auto rows = cat.projects();
        check("status: seen projects start active, outcome empty", opened && rows.size() == 3 &&
              std::all_of(rows.begin(), rows.end(), [](const ct::ProjectRow& x) { return x.status == "active" && x.outcome.empty(); }));
        const std::string jid = [&] { for (const auto& p : again) if (p.name == "Jasper") return p.id; return std::string(); }();
        const bool set = cat.set_status(jid, "done", "published 2026");
        cat.seen_project({jid, H + "/Elsewhere/Jasper-project", "Jasper", "writing-project", "", "", 200, false});
        const auto j = cat.project(jid);
        check("status: set; a later Look moves the path but keeps status + outcome",
              set && j && j->status == "done" && j->outcome == "published 2026" && j->seen == 200 &&
              j->path == H + "/Elsewhere/Jasper-project" && j->tmpl == "writing-project");
        check("status: an unknown id changes nothing", !cat.set_status("no-such-id", "done", ""));
        check("status: four, stable ids", pj::statuses().size() == 4 && pj::is_status("paused") && !pj::is_status("Done"));
        check("search: every word, any field, any case",
              pj::fits("jas DONE", {"Jasper", "done", "published 2026"}) && pj::fits("", {"x"}) &&
              !pj::fits("jasper paused", {"Jasper", "done"}) && pj::fits("2026", {"Jasper", "done", "published 2026"}));
        // s010g: uses -- a project made from other projects, by id.
        {
            const std::string A = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", B = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
                              C = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
            std::vector<pj::Use> us = {{A, B}, {B, C}};
            std::string why;
            check("uses: may use another; not itself; not twice",
                  pj::can_use(us, A, C, nullptr) && !pj::can_use(us, A, A, &why) && !pj::can_use(us, A, B, nullptr));
            check("uses: no loop, near or far (B uses A; C uses A)",
                  !pj::can_use(us, B, A, &why) && !pj::can_use(us, C, A, nullptr) && why.find("loop") != std::string::npos, why);
            check("uses: uses_of / used_by", pj::uses_of(us, A) == std::vector<std::string>{B} &&
                  pj::used_by(us, C) == std::vector<std::string>{B} && pj::uses_of(us, C).empty());
            const auto before_disk = tree_facts(H);
            const std::string tid = [&] { for (const auto& p : again) if (p.name == "tidy") return p.id; return std::string(); }();
            const bool added = cat.add_use(jid, tid);
            check("uses: catalog add; twice refused; read back", added && !cat.add_use(jid, tid) &&
                  cat.uses().size() == 1 && cat.uses()[0].first == jid && cat.uses()[0].second == tid);
            check("uses: catalog only -- nothing on disk changed", tree_facts(H) == before_disk);
            cat.seen_project({tid, H + "/Develop/gtk/tidy-project", "tidy", "code-project", "", "", 300, false});
            check("uses: a later Look keeps the link", cat.uses().size() == 1);
            check("uses: drop; a second drop changes nothing",
                  cat.drop_use(jid, tid) && !cat.drop_use(jid, tid) && cat.uses().empty());
            ct::Catalog ro;
            check("uses: read-only open reads them; cannot write", ro.open(D, false) && ro.uses().empty() && !ro.add_use(A, B));
        }
        const auto u = door.undo();
        check("give_ids: undone -- no ids on disk again (the odd one untouched)",
              u.ok && pj::give_ids(pj::find(H), gen).actions.size() == 3 &&
              pj::read_id(H + "/Writing/Odd-project").empty() && fs::exists(H + "/Writing/Odd-project/.tidy-project"));
        fs::remove_all(root);
    }

    // ── s010f1: the rescan -- moved by id, gone, back, copies; read-only ──
    {
        std::cout << "[rescan]\n";
        namespace pj = core::projects;
        namespace ct = core::catalog;
        namespace rs = core::rescan;
        namespace st = core::settings;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_rescan_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string();
        const std::string A = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", B = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
        put_file(H + "/Writing/Book-project/.tidy-project", pj::id_text(A));
        put_file(H + "/Art/Cover-project/.tidy-project", pj::id_text(B));
        fs::create_directories(H + "/Art/Plain-project");
        ct::Catalog cat;
        const bool opened = cat.open(D, true);
        const auto before = tree_facts(H);
        auto r = rs::record(pj::find(H), {}, cat, 100);
        check("rescan: first look -- 3 found, 2 ids, 2 new, nothing gone", opened && r.found == 3 && r.with_id == 2 &&
              r.added == 2 && r.moved == 0 && r.gone == 0 && rs::summary(r) == "2 new", rs::summary(r));
        check("rescan: READ-ONLY on the home", tree_facts(H) == before);
        cat.set_status(A, "paused", "waiting on the cover");
        cat.add_use(A, B);
        fs::create_directories(H + "/Books");
        fs::rename(H + "/Writing/Book-project", H + "/Books/Book-project");
        r = rs::record(pj::find(H), {}, cat, 200);
        const auto a = cat.project(A);
        check("rescan: a moved project is followed by id -- status, outcome, uses kept",
              r.moved == 1 && r.added == 0 && a && a->path == H + "/Books/Book-project" && a->status == "paused" &&
              a->outcome == "waiting on the cover" && cat.uses().size() == 1, rs::summary(r));
        fs::rename(H + "/Art/Cover-project", root / "Cover-project");          // out of the home
        r = rs::record(pj::find(H), {}, cat, 300);
        const auto b = cat.project(B);
        check("rescan: not found -> gone, kept (row, uses); counted once",
              r.gone == 1 && b && b->gone && b->seen == 300 && cat.uses().size() == 1 &&
              rs::record(pj::find(H), {}, cat, 400).gone == 0, rs::summary(r));
        fs::rename(root / "Cover-project", H + "/Art/Cover-project");
        r = rs::record(pj::find(H), {}, cat, 500);
        check("rescan: seen again -> back", r.back == 1 && cat.project(B) && !cat.project(B)->gone, rs::summary(r));
        fs::create_directories(H + "/Desktop");
        fs::copy(H + "/Art/Cover-project", H + "/Desktop/Cover-project", fs::copy_options::recursive);
        r = rs::record(pj::find(H), {}, cat, 600);
        check("rescan: a copy (same id twice) -> one row, the copy counted",
              r.twins.size() == 1 && cat.projects().size() == 2 && rs::summary(r).find("cop") != std::string::npos,
              rs::summary(r));
        check("rescan: nothing new -> \"no change\"", rs::summary(rs::Result{}) == "no change");
        check("rescan: the setting -- default 10, 0 = never, clamped, round trip",
              st::Settings{}.rescan_minutes == 10 && st::parse("rescan_minutes=0").rescan_minutes == 0 &&
              st::parse("rescan_minutes=99999").rescan_minutes == st::kRescanMax &&
              st::parse("rescan_minutes=-3").rescan_minutes == 0 &&
              st::parse(st::format(st::parse("rescan_minutes=25\naging_days=7"))).rescan_minutes == 25);
        fs::remove_all(root);
    }

    // ── s010f2: CVS-style id lists -- write, keep, re-find a lost id ──
    {
        std::cout << "[id lists]\n";
        const auto read = [](const std::string& f) { std::ifstream in(f); std::stringstream o; o << in.rdbuf(); return o.str(); };
        namespace fl = core::filelists;
        namespace dr = latch::door;
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_lists_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string();
        const std::string I1 = "11111111-1111-4111-8111-111111111111", I2 = "22222222-2222-4222-8222-222222222222",
                          I3 = "33333333-3333-4333-8333-333333333333";
        put_file(H + "/Documents/Taxes/w2.pdf", "w2 body");
        put_file(H + "/Documents/Taxes/notes.txt", "no id here");
        put_file(H + "/Writing/Jasper-project/Work/ch1.odt", "chapter one");
        put_file(H + "/Pictures/odd\tname.png", "tab");
        xset(H + "/Documents/Taxes/w2.pdf", core::facts::kId, I1);
        xset(H + "/Writing/Jasper-project/Work/ch1.odt", core::facts::kId, I2);
        xset(H + "/Pictures/odd\tname.png", core::facts::kId, I3);

        const std::vector<fl::Line> ls = {{I2, 5, 9, "b c.txt"}, {I1, 7, 8, "a.pdf"}};
        const auto txt = fl::format(ls);
        check("lists: format -> parse (sorted by name); a tab in a name left out; junk skipped",
              fl::parse(txt) == std::vector<fl::Line>{ls[1], ls[0]} &&
              fl::parse(fl::format({{I1, 1, 1, "x\ty"}})).empty() &&
              fl::parse("# h\nnot-an-id\t1\t2\tn\n" + I1 + "\tq\t2\tn\n").empty());
        const auto files = core::scan::walk(H, fl::roots(H));
        const auto want = fl::wanted(files);
        check("lists: wanted -- one per folder holding an id; inside a project too; none where no id",
              want.size() == 2 && want.count(H + "/Documents/Taxes") && want.at(H + "/Documents/Taxes").size() == 1 &&
              want.count(H + "/Writing/Jasper-project/Work") && !want.count(H + "/Pictures"),
              std::to_string(want.size()));
        const auto before = tree_facts(H);
        auto work = fl::plan(fl::refind(files), want);
        check("lists: no list yet -> a door Write per folder, nothing in place, nothing re-found",
              work.plan.actions.size() == 2 && work.replaces.empty() &&
              std::all_of(work.plan.actions.begin(), work.plan.actions.end(), [](const dr::Action& a) {
                  return a.verb == dr::Verb::Write && a.to.ends_with("/.tidy-files"); }), work.label());
        check("lists: READ-ONLY until run", tree_facts(H) == before);
        latch::door::set_app_name("Tidy");
        dr::Door door(H, D);
        door.load();
        work.plan.previewed = true;
        auto r = door.run(work.plan);
        check("lists: run -> on disk; a second look has nothing to do", r.ok &&
              fl::parse(read(H + "/Documents/Taxes/.tidy-files")).size() == 1 &&
              fl::plan({}, fl::wanted(core::scan::walk(H, fl::roots(H)))).empty());

        // ── s010f3: an existing list is rewritten IN PLACE (latch replace_own), no undo ──
        const auto TL = H + "/Documents/Taxes/.tidy-files";
        put_file(H + "/Documents/Taxes/1099.pdf", "1099 body");
        const std::string I4 = "44444444-4444-4444-8444-444444444444";
        xset(H + "/Documents/Taxes/1099.pdf", core::facts::kId, I4);
        work = fl::plan({}, fl::wanted(core::scan::walk(H, fl::roots(H))));
        check("lists (s010f3): a changed folder -> one Replace, no door step (no Trash)",
              work.plan.actions.empty() && work.replaces.size() == 1 && work.replaces[0].path == TL &&
              work.label() == "Id lists: 1 folder", work.label());
        latch::door::set_own_files({});
        check("lists (s010f3): nothing registered -> latch refuses the replace",
              !door.replace_refusal(TL, work.replaces[0].text).empty());
        fl::register_own();
        const auto& own = latch::door::own_files();
        check("lists (s010f3): register_own -> .tidy-files + .tidy-project (+ s012b the two units), their headers",
              own.size() == 4 && own[0].name == ".tidy-files" && own[0].header == fl::kHeader &&
              own[1].name == ".tidy-project" && own[1].header == "id=" && own[2].name == "tidy-scan.timer");
        struct stat s0{}, s1{};
        ::lstat(TL.c_str(), &s0);
        const auto batches = door.batches().size();
        check("lists (s010f3): may be replaced", door.replace_refusal(TL, work.replaces[0].text).empty(),
              door.replace_refusal(TL, work.replaces[0].text));
        const auto why = door.replace_own(TL, work.replaces[0].text);
        ::lstat(TL.c_str(), &s1);
        check("lists (s010f3): replaced in place -- 2 ids, a new inode, no batch (no undo), nothing in the Trash",
              why.empty() && fl::parse(read(TL)).size() == 2 && s0.st_ino != s1.st_ino &&
              door.batches().size() == batches && !fs::exists(H + "/.local/share/Trash/files/.tidy-files"), why);
        check("lists (s010f3): no tmp left beside it", std::none_of(fs::directory_iterator(H + "/Documents/Taxes"),
              fs::directory_iterator(), [](const auto& e) { return e.path().filename().string().ends_with(".new"); }));
        check("lists (s010f3): then up to date", fl::plan({}, fl::wanted(core::scan::walk(H, fl::roots(H)))).empty());

        // A user's own file named .tidy-files (not Tidy's header) -> refused, untouched.
        put_file(H + "/Music/song.ogg", "la");
        xset(H + "/Music/song.ogg", core::facts::kId, "55555555-5555-4555-8555-555555555555");
        put_file(H + "/Music/.tidy-files", "my own notes\n");
        work = fl::plan({}, fl::wanted(core::scan::walk(H, fl::roots(H))));
        check("lists (s010f3): a user's .tidy-files -> a Replace latch refuses; the file untouched",
              work.replaces.size() == 1 && !door.replace_refusal(H + "/Music/.tidy-files", work.replaces[0].text).empty() &&
              !door.replace_own(H + "/Music/.tidy-files", work.replaces[0].text).empty() &&
              read(H + "/Music/.tidy-files") == "my own notes\n");
        fs::remove_all(H + "/Music");

        // ── s010f3: the s010f2 bug -- a line whose file lost its id and changed size is KEPT ──
        fs::remove(H + "/Documents/Taxes/1099.pdf");
        put_file(H + "/Documents/Taxes/1099.pdf", "1099 body, edited by an app that drops xattrs");
        auto scan3 = core::scan::walk(H, fl::roots(H));
        check("lists (s010f3): other size -> not re-found", fl::refind(scan3).empty());
        auto want3 = fl::wanted(scan3);
        const auto& tl = want3[H + "/Documents/Taxes"];
        check("lists (s010f3): its old line carried forward (kept), the id with it",
              tl.size() == 2 && std::any_of(tl.begin(), tl.end(), [&](const fl::Line& l) { return l.id == I4 && l.kept; }));
        work = fl::plan({}, want3);
        check("lists (s010f3): nothing else changed -> no rewrite at all (the line stays on disk)",
              work.empty() && work.kept == 1 && fl::parse(read(TL)).size() == 2, work.label());
        put_file(H + "/Documents/Taxes/roof.pdf", "roof");
        xset(H + "/Documents/Taxes/roof.pdf", core::facts::kId, "66666666-6666-4666-8666-666666666666");
        work = fl::plan({}, fl::wanted(core::scan::walk(H, fl::roots(H))));
        check("lists (s010f3): a rewrite for another reason keeps the lost id's line",
              work.replaces.size() == 1 && work.kept == 1 && door.replace_own(TL, work.replaces[0].text).empty() &&
              fl::parse(read(TL)).size() == 3 && read(TL).find(I4) != std::string::npos &&
              work.replaces[0].reason.find("1 kept") != std::string::npos, work.replaces.empty() ? "" : work.replaces[0].reason);
        fs::copy_file(H + "/Documents/Taxes/w2.pdf", H + "/Documents/w2-copy.pdf");
        xset(H + "/Documents/w2-copy.pdf", core::facts::kId, I4);
        want3 = fl::wanted(core::scan::walk(H, fl::roots(H)));
        check("lists (s010f3): not kept when another file carries that id now",
              std::none_of(want3[H + "/Documents/Taxes"].begin(), want3[H + "/Documents/Taxes"].end(),
                           [](const fl::Line& l) { return l.kept; }));
        fs::remove(H + "/Documents/w2-copy.pdf");
        fs::remove(H + "/Documents/Taxes/1099.pdf");
        want3 = fl::wanted(core::scan::walk(H, fl::roots(H)));
        check("lists (s010f3): the file gone -> its line dropped at the next rewrite",
              want3[H + "/Documents/Taxes"].size() == 2 &&
              std::none_of(want3[H + "/Documents/Taxes"].begin(), want3[H + "/Documents/Taxes"].end(),
                           [&](const fl::Line& l) { return l.id == I4; }));
        work = fl::plan({}, want3);
        if (!work.replaces.empty()) door.replace_own(TL, work.replaces[0].text);

        // The xattr lost (a FAT stick round trip): same name, same size, no tags -> re-found.
        fs::remove(H + "/Documents/Taxes/w2.pdf");
        put_file(H + "/Documents/Taxes/w2.pdf", "w2 body");
        // A copy elsewhere still carrying I2, and ch1 losing its xattr -> NOT re-found (the id is on the copy).
        fs::copy_file(H + "/Writing/Jasper-project/Work/ch1.odt", H + "/Documents/ch1-copy.odt");
        xset(H + "/Documents/ch1-copy.odt", core::facts::kId, I2);
        fs::remove(H + "/Writing/Jasper-project/Work/ch1.odt");
        put_file(H + "/Writing/Jasper-project/Work/ch1.odt", "chapter one");
        // Same name, other size -> not re-found.
        put_file(H + "/Documents/Taxes/notes.txt", "changed, and no id");
        const auto scan2 = core::scan::walk(H, fl::roots(H));
        const auto found = fl::refind(scan2);
        check("lists: re-find -- same name + size gets its id back; a copy's id and other sizes do not",
              found.size() == 1 && found[0].path == H + "/Documents/Taxes/w2.pdf" && found[0].id == I1,
              std::to_string(found.size()));
        const auto st = fl::stale(scan2);
        check("lists (s010f3): stale() counts what Look would do -- the offer's numbers",
              st.any() && st.refound == 1 && st.lists >= 1,
              std::to_string(st.lists) + " lists, " + std::to_string(st.refound) + " re-found");
        work = fl::plan(found, fl::wanted(fl::with_found(scan2, found)));
        check("lists: re-found ids are Tags first; the lists follow", !work.plan.actions.empty() &&
              work.plan.actions[0].verb == dr::Verb::Tag && work.plan.actions[0].content == I1, work.label());
        work.plan.previewed = true;
        r = door.run(work.plan);
        check("lists: run -> w2.pdf carries its old id again", r.ok && core::facts::tags_of(H + "/Documents/Taxes/w2.pdf").id == I1);
        const auto u = door.undo();
        check("lists: undone -- the id off again", u.ok && core::facts::tags_of(H + "/Documents/Taxes/w2.pdf").id.empty());
        check("lists (s010f3): stale() of an up-to-date home is nothing", [&] {
            const auto f = core::scan::walk(H, fl::roots(H));
            const auto w = fl::plan(fl::refind(f), fl::wanted(fl::with_found(f, fl::refind(f))));
            if (!w.plan.actions.empty()) { auto p = w.plan; p.previewed = true; door.run(p); }
            for (const auto& x : w.replaces) door.replace_own(x.path, x.text);
            return !fl::stale(core::scan::walk(H, fl::roots(H))).any();
        }());
        fs::remove_all(root);
    }

    // ── s012a: the trigger -- what is due; the headless look ──
    {
        std::cout << "[due]\n";
        namespace du = core::due;
        namespace rt = core::retention;
        const std::int64_t now = 2000000000, day = 86400;
        const auto mk = [&](const std::string& name, const std::string& level, int born_days, int read_days,
                            int changed_days, int ctime_days, const std::string& purpose = "") {
            core::facts::Facts f;
            f.path = "/h/Documents/" + name;
            f.btime = now - born_days * day; f.has_btime = true;
            f.atime = now - read_days * day; f.mtime = now - changed_days * day; f.ctime = now - ctime_days * day;
            f.tags.retention = level; f.tags.purpose = purpose;
            return f;
        };
        core::purposes::Book book;
        core::purposes::add(book, "Taxes 2025");
        core::purposes::add(book, "House");
        core::purposes::set_closed(book, "taxes-2025", true);
        core::settings::Settings st;
        const std::vector<core::facts::Facts> files = {
            mk("eph-old.png", "ephemeral", 40, 40, 40, 40),       // due: remove
            mk("eph-read.png", "ephemeral", 40, 2, 40, 40),       // read lately: no
            mk("eph-new.png", "ephemeral", 5, 5, 5, 5),           // young: no
            mk("held-8y.pdf", "held", 4000, 4000, 4000, 8 * 365 + 2),   // due: remove
            mk("held-6y.pdf", "held", 4000, 4000, 4000, 6 * 365),       // no
            mk("hist-old.odt", "historical", 900, 400, 400, 400),       // due: archive
            mk("hist-edited.odt", "historical", 900, 400, 10, 10),      // changed lately: no
            mk("w2.pdf", "until-done", 100, 100, 100, 100, "taxes-2025"),   // closed: settle
            mk("roof.pdf", "until-done", 100, 100, 100, 100, "house"),      // open: no
            mk("lost.pdf", "until-done", 100, 100, 100, 100, "gone-purpose"),   // unknown: settle
            mk("bare.pdf", "until-done", 100, 100, 100, 100),                   // none: settle
            mk("und.png", "undecided", 900, 900, 900, 900),
            mk("keep.png", "keep", 9000, 9000, 9000, 9000),
            mk("hands.png", "hands-off", 9000, 9000, 9000, 9000),
            mk("plain.png", "", 9000, 9000, 9000, 9000),
        };
        auto items = du::due(files, book, st, now);
        const auto names = [](const std::vector<du::Item>& v, du::To to) {
            std::string s;
            for (const auto& i : v) if (i.to == to) s += fs::path(i.path).filename().string() + " ";
            return s;
        };
        check("due: Ephemeral born + unread aging_days -> remove; read lately or young: not",
              names(items, du::To::Remove).find("eph-old.png") != std::string::npos &&
              names(items, du::To::Remove).find("eph-read") == std::string::npos &&
              names(items, du::To::Remove).find("eph-new") == std::string::npos, names(items, du::To::Remove));
        check("due: Held past held_years (ctime) -> remove; 6 of 7 years: not",
              names(items, du::To::Remove) == "eph-old.png held-8y.pdf ", names(items, du::To::Remove));
        check("due: Historical unread + unchanged archive_days -> archive; changed lately: not",
              names(items, du::To::Archive) == "hist-old.odt ", names(items, du::To::Archive));
        check("due: Until done -- purpose closed, unknown or none -> settle; open: not",
              names(items, du::To::Settle) == "bare.pdf lost.pdf w2.pdf ", names(items, du::To::Settle));
        check("due: Undecided, Keep, Hands-off, untagged: never due; sorted remove, archive, settle",
              items.size() == 6 && items.front().to == du::To::Remove && items.back().to == du::To::Settle);
        st.held_years = 5;
        st.archive_days = 500;
        items = du::due(files, book, st, now);
        check("due: the settings decide (held 5 years -> 6y due; archive 500 days -> 400 not)",
              names(items, du::To::Remove).find("held-6y.pdf") != std::string::npos &&
              names(items, du::To::Archive).empty(), names(items, du::To::Remove));
        check("settings (s012a): held_years / archive_days -- defaults 7 / 365, round trip, clamped",
              core::settings::Settings{}.held_years == 7 && core::settings::Settings{}.archive_days == 365 &&
              core::settings::parse(core::settings::format(st)) == st &&
              core::settings::parse("held_years=0\narchive_days=99999\n").held_years == 1 &&
              core::settings::parse("held_years=0\narchive_days=99999\n").archive_days == 3650);

        du::Attention a{3, 2, 1, 9, 1, now, 12};
        check("attention: format -> parse; total 16; junk ignored", du::parse(du::format(a)) == a &&
              a.total() == 16 && du::parse("remove=x\nlists=-3\n# c\ncloset=2\n").closet == 2 &&
              du::parse("lists=-3\n").lists == 0);
        check("attention: the line", du::line(a) == "16 need attention: 3 to remove, 2 to archive, 1 to settle, "
                                                    "9 in the Closet, 1 id list" &&
              du::line(du::Attention{}) == "nothing needs attention", du::line(a));

        // The headless look on a fixture: READ-ONLY on the home.
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_due_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string();
        put_file(H + "/Downloads/old.png", "old");
        xset(H + "/Downloads/old.png", core::facts::kRetention, "ephemeral");
        put_file(H + "/Documents/note.txt", "undecided");
        const auto w = du::where(H.c_str(), "/nonexistent", nullptr, nullptr);
        check("where: TIDY_HOME -> a fixture, records inside it; else HOME",
              w.fixture && w.home == fs::canonical(H).string() && w.data_dir.rfind(w.home, 0) == 0 &&
              !du::where("/no/such/dir", "/home/x", nullptr, nullptr).fixture &&
              du::where(nullptr, "/home/x", nullptr, nullptr).home == "/home/x");
        const auto before = tree_facts(H);
        const auto later = static_cast<std::int64_t>(std::time(nullptr)) + 400 * day;   // everything aged
        std::vector<du::Item> got;
        const auto look = du::look(w.home, w.data_dir, "", later, &got);
        check("look: the fixture judged -- 1 to remove; the Closet counted like the Closet",
              look.remove == 1 && got.size() == 1 && got[0].path.ends_with("/Downloads/old.png") &&
              look.closet == core::closet::view(core::birth::report(w.home, core::birth::places(w.home, "")), later,
                                                core::settings::Settings{}).proposed.size(),
              du::line(look));
        check("look: READ-ONLY -- the home's files untouched, no records folder made",
              tree_facts(H) == before && !fs::exists(w.data_dir));
        fs::remove_all(root);
    }

    // ── s012b: the background scan -- the units, on / off / retime, the notice ──
    {
        std::cout << "[background]\n";
        namespace bg = core::background;
        namespace du = core::due;
        namespace dr = latch::door;
        const auto read = [](const std::string& f) { std::ifstream in(f); std::stringstream o; o << in.rdbuf(); return o.str(); };

        // The notice: at N once, again only when it grows; below N resets.
        du::Attention a;
        a.closet = 9;
        auto n = du::notice(a, 0, 10);
        check("notice: below N -> nothing, memory reset", !n.send && n.notified == 0);
        a.remove = 3;                                   // 12
        n = du::notice(a, 0, 10);
        check("notice: reaches N -> sent, remembered", n.send && n.notified == 12 &&
              n.summary == "Tidy: 12 need attention" && n.body == "3 to remove, 9 in the Closet", n.summary + " / " + n.body);
        check("notice: the same again -> quiet; fewer (still >= N) -> quiet, memory kept",
              !du::notice(a, 12, 10).send && du::notice(a, 12, 10).notified == 12 &&
              !du::notice(a, 15, 10).send && du::notice(a, 15, 10).notified == 15);
        a.archive = 2;                                  // 14
        check("notice: grows -> sent again", du::notice(a, 12, 10).send && du::notice(a, 12, 10).notified == 14);
        a = du::Attention{};
        a.closet = 4;
        check("notice: dropped below N -> memory 0 (a later climb notifies)", du::notice(a, 14, 10).notified == 0);
        du::Attention r{1, 2, 3, 4, 5, 6, 7, 8};
        check("attention: notified round trips", du::parse(du::format(r)) == r && du::parse(du::format(r)).notified == 8);
        check("settings (s012b): bg_minutes 60 / notify_at 10 -- round trip, clamped",
              core::settings::Settings{}.bg_minutes == 60 && core::settings::Settings{}.notify_at == 10 &&
              core::settings::parse("bg_minutes=1\nnotify_at=0\n").bg_minutes == 15 &&
              core::settings::parse("bg_minutes=1\nnotify_at=0\n").notify_at == 1 &&
              core::settings::parse(core::settings::format(core::settings::parse("bg_minutes=90\nnotify_at=25\n"))).bg_minutes == 90);

        // The unit texts.
        const auto sv = bg::service_text("/usr/local/bin/tidy", "");
        const auto fx = bg::service_text("/h/my build/tidy", "/tmp/fx");
        check("units: the service runs tidy --scan --notify, quoted; a fixture sets TIDY_HOME",
              sv.rfind(bg::kHeader, 0) == 0 && sv.find("ExecStart=\"/usr/local/bin/tidy\" --scan --notify\n") != std::string::npos &&
              sv.find("TIDY_HOME") == std::string::npos && sv.find("Type=oneshot") != std::string::npos &&
              fx.find("ExecStart=\"/h/my build/tidy\" --scan --notify") != std::string::npos &&
              fx.find("Environment=\"TIDY_HOME=/tmp/fx\"") != std::string::npos);
        check("units: the timer -- every N minutes, its minutes read back; not ours -> 0",
              bg::minutes_of(bg::timer_text(45)) == 45 && bg::timer_text(45).find("WantedBy=timers.target") != std::string::npos &&
              bg::minutes_of("[Timer]\nOnUnitActiveSec=5min\n") == 0);
        check("units: where -- XDG_CONFIG_HOME, else home/.config; a fixture always inside it",
              bg::unit_dir("/home/s", "/cfg", false) == "/cfg/systemd/user" &&
              bg::unit_dir("/home/s", nullptr, false) == "/home/s/.config/systemd/user" &&
              bg::unit_dir("/tmp/fx", "/cfg", true) == "/tmp/fx/.config/systemd/user");

        // On a fixture: turn on through the door, retime in place, turn off, undo.
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_bg_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string();
        fs::create_directories(H + "/.config");
        const auto U = bg::unit_dir(H, nullptr, true);
        dr::Door door(H, D);
        door.load();
        core::filelists::register_own();
        auto w = bg::turn_on(H, U, "/usr/local/bin/tidy", "", 60);
        check("turn on: MakeDir systemd, systemd/user, Write service (names the binary), Write timer; reload + enable --now",
              w.plan.actions.size() == 4 && w.first.empty() && w.plan.actions[2].reason.find("/usr/local/bin/tidy") != std::string::npos && w.plan.actions[0].verb == dr::Verb::MakeDir &&
              w.plan.actions[1].to == U && w.plan.actions[2].to == U + "/tidy-scan.service" &&
              w.plan.actions[3].to == U + "/tidy-scan.timer" && w.replaces.empty() && w.commands.size() == 2 &&
              bg::shown_command(w.commands[1]) == "systemctl --user enable --now tidy-scan.timer",
              std::to_string(w.plan.actions.size()));
        check("turn on: READ-ONLY until run; Off", !fs::exists(U) && !bg::state_of(U).on());
        w.plan.previewed = true;
        auto rr = door.run(w.plan);
        auto s = bg::state_of(U);
        check("turn on: run -> both units, ours, 60 minutes", rr.ok && s.on() && s.minutes == 60,
              rr.messages.empty() ? "" : rr.messages[0]);
        w = bg::retime(U, 90);
        check("retime: the timer rewritten in place (no undo), reload + restart",
              w.plan.actions.empty() && w.replaces.size() == 1 && door.replace_refusal(w.replaces[0].path, w.replaces[0].text).empty() &&
              door.replace_own(w.replaces[0].path, w.replaces[0].text).empty() && bg::state_of(U).minutes == 90 &&
              bg::shown_command(w.commands[1]) == "systemctl --user restart tidy-scan.timer");
        w = bg::turn_on(H, U, "/usr/local/bin/tidy", "", 30);
        check("turn on again: no new folders, both rewritten in place", w.plan.actions.empty() && w.replaces.size() == 2);
        w = bg::turn_off(U);
        check("turn off: disable --now first, Trash both, reload", w.plan.actions.size() == 2 &&
              w.plan.actions[0].verb == dr::Verb::Trash && w.first.size() == 1 &&
              bg::shown_command(w.first[0]) == "systemctl --user disable --now tidy-scan.timer" &&
              w.commands.size() == 1 && bg::shown_command(w.commands[0]) == "systemctl --user daemon-reload");
        w.plan.previewed = true;
        rr = door.run(w.plan);
        s = bg::state_of(U);
        check("turn off: run -> Off, the units in the Trash", rr.ok && !s.service && !s.timer);
        const auto u = door.undo();
        check("turn off undone -> the units back (the 90-minute timer)", u.ok && bg::state_of(U).on() && bg::state_of(U).minutes == 90);
        // A user's own tidy-scan.timer -> not ours: replace refused, untouched.
        put_file(U + "/tidy-scan.timer", "[Timer]\nOnCalendar=daily\n");
        s = bg::state_of(U);
        w = bg::turn_on(H, U, "/usr/local/bin/tidy", "", 60);
        check("a timer that is not Tidy's: not on, the rewrite refused, left untouched",
              !s.on() && !s.ours && !door.replace_refusal(U + "/tidy-scan.timer", bg::timer_text(60)).empty() &&
              read(U + "/tidy-scan.timer") == "[Timer]\nOnCalendar=daily\n");
        fs::remove_all(root);
    }

    // ── s009: purposes -- the learned list; assign / close -- on a FIXTURE ──
    {
        std::cout << "[purposes]\n";
        namespace pu = core::purposes;
        namespace dr = latch::door;
        const std::vector<std::string> landing = {"Downloads", "Desktop", "Documents", "Screenshots"};

        pu::Book b;
        std::string id;
        check("purposes: a title -> its id; no twins", pu::slug_of("  Taxes 2025! ") == "taxes-2025" &&
              pu::add(b, "Taxes 2025", &id).empty() && id == "taxes-2025" && !pu::add(b, "taxes 2025").empty() &&
              pu::add(b, "House").empty() && !pu::add(b, "a|b").empty() && !pu::add(b, "!!").empty());
        check("hints: an empty list guesses nothing", pu::guess(b, "/h/Documents/Taxes/w2.pdf", landing).purpose.empty());

        pu::learn(b, "/h/Documents/Taxes/w2.pdf", "taxes-2025", {}, landing);
        auto g = pu::guess(b, "/h/Documents/Taxes/1099.pdf", landing);
        check("learn: a file's folder becomes a hint -> the next file there is guessed",
              g.purpose == "taxes-2025" && g.because == "folder Taxes" && b.hints.size() == 1, g.because);
        pu::learn(b, "/h/Downloads/f1040.pdf", "taxes-2025", {}, landing);
        check("learn: a landing folder teaches nothing", b.hints.size() == 1 &&
              pu::guess(b, "/h/Downloads/other.pdf", landing).purpose.empty());
        check("teach: a name pattern by hand; names win over folders",
              pu::teach(b, "*1040*", "taxes-2025").empty() && !pu::teach(b, "a/b", "taxes-2025").empty() &&
              !pu::teach(b, "*x*", "nope").empty() &&
              pu::guess(b, "/h/Downloads/f1040-sr.pdf", landing).because == "name *1040*");
        pu::learn(b, "/h/Documents/Taxes/1099.pdf", "taxes-2025", g, landing);
        check("learn: a confirmed guess is a yes (once, not twice)", b.hints[0].yes == 2 && b.hints[0].no == 0,
              std::to_string(b.hints[0].yes));
        g = pu::guess(b, "/h/Documents/Taxes/roof.pdf", landing);
        pu::learn(b, "/h/Documents/Taxes/roof.pdf", "house", g, landing);
        g = pu::guess(b, "/h/Documents/Taxes/roof2.pdf", landing);
        check("learn: a corrected guess is a no, and the folder now hints the chosen one too",
              b.hints[0].no == 1 && b.hints.back().purpose == "house" && b.hints.back().pattern == "Taxes" &&
              g.purpose == "taxes-2025", g.purpose);   // 2 yes 1 no still beats 1 yes
        pu::learn(b, "/h/Documents/Taxes/roof2.pdf", "house", g, landing);
        pu::learn(b, "/h/Documents/Taxes/roof3.pdf", "house", pu::guess(b, "/h/Documents/Taxes/roof3.pdf", landing), landing);
        check("learn: corrected often enough, the guess turns", pu::guess(b, "/h/Documents/Taxes/roof4.pdf", landing).purpose == "house");
        pu::set_closed(b, "house", true);
        check("guess: a closed purpose is never guessed",
              pu::guess(b, "/h/Documents/Taxes/roof5.pdf", landing).purpose != "house");
        check("the book: format -> parse is the same book", pu::parse(pu::format(b)) == b, pu::format(b));
        check("close: retention follows the role",
              pu::retention_at_close("product") == "keep" && pu::retention_at_close("history") == "historical" &&
              pu::retention_at_close("") == "historical" && pu::retention_at_close("input") == "ephemeral" &&
              pu::retention_at_close("incidental") == "ephemeral" &&
              pu::retention_at_close("junk-drawer").empty());

        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_purposes_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const fs::path home = root / "home";
        make_home(home);
        make_birth_extras(home);
        fs::remove_all(home / ".local");
        const std::string H = fs::canonical(home).string();
        const std::string D = core::paths::data_dir(H, nullptr);
        latch::door::set_app_name("Tidy");
        dr::Door door(H, D);
        door.load();
        const auto facts_of = [](const std::vector<std::string>& ps) {
            std::vector<core::facts::Facts> v;
            for (const auto& p : ps) { core::facts::Facts f; if (core::facts::of(p, f)) v.push_back(f); }
            return v;
        };
        const std::string a = H + "/Downloads/f1040.pdf", c = H + "/Documents/Taxes/w2.pdf", k = H + "/Desktop/cover-final.png";
        const auto before = tree_facts(H + "/Documents");
        auto plan = pu::assign(facts_of({a, c}), *b.find("taxes-2025"));
        bool tag_only = !plan.actions.empty();
        for (const auto& x : plan.actions) tag_only = tag_only && x.verb == dr::Verb::Tag;
        const bool refused = !door.run(plan).ok;
        plan.previewed = true;
        const auto r = door.run(plan);
        check("assign: Tag actions only; refused unpreviewed; the purpose on each file",
              tag_only && refused && r.ok && core::facts::tags_of(a).purpose == "taxes-2025" &&
              core::facts::tags_of(c).purpose == "taxes-2025");
        auto strip_ctime = [](std::map<std::string, std::string> m) { for (auto& [x, v] : m) v = v.substr(0, v.rfind('/')); return m; };
        check("assign: bytes, inode, mtime untouched", strip_ctime(tree_facts(H + "/Documents")) == strip_ctime(before));

        // A product and a history file in the purpose too.
        auto roles = tagging_set(facts_of({k}), "taxes-2025", "product");
        roles.previewed = true;
        door.run(roles);
        xset(home / "Documents/Taxes/w2.pdf", core::facts::kRole, "history");   // as an app would mark it
        auto cl = pu::close(facts_of({a, c, k}), *b.find("taxes-2025"));
        cl.previewed = true;
        const auto cr = door.run(cl);
        check("close: f1040 (no role) -> Historical, w2 (history) -> Historical, the cover (product) -> Keep (already: no step)",
              cr.ok && core::facts::tags_of(a).retention == "historical" && core::facts::tags_of(c).retention == "historical" &&
              core::facts::tags_of(k).retention == "keep" && cl.actions.size() == 2, std::to_string(cl.actions.size()));
        const auto u1 = door.undo();
        check("close: undone -- the retentions as they were", u1.ok && core::facts::tags_of(a).retention.empty() &&
              core::facts::tags_of(c).retention.empty());
        door.undo();
        door.undo();
        check("assign: undone -- no purpose left", core::facts::tags_of(a).purpose.empty() && core::facts::tags_of(c).purpose.empty());
        fs::remove_all(root);
    }

    // ── s011: files home -- by type (subfolder required), into a project, "name (2)" ──
    {
        std::cout << "[files home]\n";
        namespace ho = core::homing;
        namespace dr = latch::door;
        const auto read = [](const std::string& f) { std::ifstream in(f); std::stringstream o; o << in.rdbuf(); return o.str(); };

        const auto& d = ho::defaults();
        const auto* png = ho::type_of(d, "Cover FINAL.PNG");
        check("type table: the standard one -- *.png -> Pictures (any case), curvz -> Graphics, pdf -> Documents, none for .xyz",
              png && png->folder == "Pictures" && ho::type_of(d, "a.curvz")->folder == "Graphics" &&
              ho::type_of(d, "f1040.pdf")->folder == "Documents" && ho::type_of(d, "song.flac")->folder == "Music" &&
              !ho::type_of(d, "thing.xyz"));
        ho::TypeRoute r;
        check("type table: a line -> route -> the same line; bad lines refused (path, hidden, project, fixes, too deep)",
              ho::parse_line("  *.kra ->  Graphics/Paint/ ", r) && ho::format_line(r) == "*.kra -> Graphics/Paint" &&
              !ho::parse_line("a/b.png -> Pictures", r) && !ho::parse_line("*.png -> .pics", r) &&
              !ho::parse_line("*.png -> Art-project", r) && !ho::parse_line("*.png -> fixes", r) &&
              !ho::parse_line("*.png -> a/b/c", r) && !ho::parse_line("*.png Pictures", r) && !ho::parse_line("*.png -> ", r));
        core::settings::Settings st;
        auto t0 = ho::table(st);
        st.home_set = true;
        st.home = {"*.kra -> Graphics/Paint", "junk line", "*.png -> Pictures"};
        const auto st2 = core::settings::parse(core::settings::format(st));
        core::settings::Settings none;
        none.home_set = true;
        check("settings: never set -> the standard table; set -> exactly its lines (bad dropped); emptied stays empty",
              t0 == d && st2.home_set && st2.home == st.home && ho::table(st2).size() == 2 &&
              ho::table(st2)[0].folder == "Graphics/Paint" &&
              ho::table(core::settings::parse(core::settings::format(none))).empty() &&
              core::settings::parse(core::settings::format(none)).home_set);
        check("name (2): a.png, x.tar.gz, .bashrc, README",
              ho::numbered("a.png", 2) == "a (2).png" && ho::numbered("x.tar.gz", 2) == "x (2).tar.gz" &&
              ho::numbered(".bashrc", 2) == ".bashrc (2)" && ho::numbered("README", 3) == "README (3)" &&
              ho::numbered("a.b.c", 2) == "a.b (2).c");
        std::string folder, via;
        check("by type: a subfolder is REQUIRED; hidden / project / fixes refused",
              !ho::by_type("Pictures", "  ", &folder).empty() && ho::by_type("Pictures", "Covers/", &folder).empty() &&
              folder == "Pictures/Covers" && !ho::by_type("Pictures", ".x", &folder).empty() &&
              !ho::by_type("Pictures", "Art-project", &folder).empty() && !ho::by_type("Pictures", "fixes", &folder).empty() &&
              !ho::by_type("Pictures", "../Music", &folder).empty());
        core::templates::Template art = core::templates::shell("art-project", "Art");
        art.folders.push_back({"Graphics", core::templates::Role::Input});
        art.routes.push_back({"*.curvz", "Graphics"});
        check("into a project: the template's route; no route -> the row asks; never the top; fixes refused",
              ho::into_project("Art/Sunrise-project", &art, "cover.curvz", "", &folder, &via).empty() &&
              folder == "Art/Sunrise-project/Graphics" && via == "*.curvz" &&
              !ho::into_project("Art/Sunrise-project", &art, "notes.xyz", "", &folder).empty() &&
              ho::into_project("Art/Sunrise-project", &art, "notes.xyz", "docs", &folder).empty() &&
              folder == "Art/Sunrise-project/docs" &&
              !ho::into_project("Art/Sunrise-project", nullptr, "a.png", "", &folder).empty() &&
              !ho::into_project("Art/Sunrise-project", &art, "a.png", "fixes", &folder).empty());

        // A fixture home.
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_home_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string();
        const auto put = [&](const std::string& rel, const std::string& body, const char* level, const char* role = nullptr) {
            put_file(H + "/" + rel, body);
            if (level) xset(H + "/" + rel, core::facts::kRetention, level);
            if (role) xset(H + "/" + rel, core::facts::kRole, role);
        };
        put("Downloads/cover.png", "png one", "keep");
        put("Downloads/scan.pdf", "pdf", "held");
        put("Downloads/cover.curvz", "curvz", "keep");
        put("Downloads/waiting.png", "w", "undecided");
        put("Downloads/gone-soon.png", "e", "ephemeral");
        put("Downloads/mine.png", "h", "hands-off");
        put("Downloads/junk.png", "j", "keep", "junk-drawer");
        put("Downloads/plain.png", "no tags", nullptr);
        put("Downloads/Essay-project/fig.png", "fig", "keep");
        put("Desktop/cover.png", "png two", "keep");
        put("Pictures/Covers/cover.png", "already there", nullptr);
        put("Art/Sunrise-project/Graphics/.keep", "", nullptr);
        fs::create_directories(H + "/Pictures/.hidden");
        fs::create_directories(H + "/Pictures/Old-project");
        fs::create_directories(H + "/Pictures/fixes");
        fs::create_directories(H + "/Pictures/Landscapes");
        const std::string id = "55555555-5555-4555-8555-555555555555";
        xset(H + "/Downloads/cover.png", core::facts::kId, id);

        check("subfolders: Covers + Landscapes; not hidden, a project or fixes",
              ho::subfolders(H, H + "/Pictures") == std::vector<std::string>{"Covers", "Landscapes"});
        {   // s018d: the folders at home's top, less the landing places and Archive
            fs::create_directories(H + "/Archive/2026");
            fs::create_directories(H + "/.config");
            fs::create_directories(H + "/Writing/Jasper-project");
            fs::create_directories(H + "/Top-project");
            std::error_code se;
            fs::create_directory_symlink(H + "/Pictures", H + "/PicLink", se);
            const auto top = ho::home_folders(H, {"Downloads", "Desktop"});
            std::string got;
            for (const auto& n : top) got += n + " ";
            check("home_folders: Art, Pictures, Writing -- not Downloads / Desktop (skipped), Archive, hidden, "
                  "a -project, a symlink", top == std::vector<std::string>{"Art", "Pictures", "Writing"}, got);
            check("home_folders: a folder named there takes the subfolder rule (Pictures + Old -> Pictures/Old; "
                  "empty refused)", [&] {
                      std::string f;
                      return ho::by_type("Pictures", "Old", &f).empty() && f == "Pictures/Old" &&
                             !ho::by_type("Pictures", "", &f).empty();
                  }());
            fs::remove(H + "/PicLink", se);
            fs::remove_all(H + "/Top-project", se);
            fs::remove_all(H + "/Writing", se);
            fs::remove_all(H + "/Archive", se);
            fs::remove_all(H + "/.config", se);
        }
        const auto rep = core::birth::report(H, core::birth::places(H, ""));
        const auto off = ho::offered(rep);
        std::set<std::string> names;
        for (const auto& b : off) names.insert(fs::path(b.file.path).parent_path().filename().string() + "/" +
                                               fs::path(b.file.path).filename().string());
        check("offered: decided files in the landing places -- and s023g every file in Downloads, decided or not; "
              "never hands-off, the Junk Drawer, or inside a project",
              names == std::set<std::string>{"Downloads/cover.png", "Downloads/scan.pdf", "Downloads/cover.curvz",
                                             "Downloads/waiting.png", "Downloads/gone-soon.png", "Downloads/plain.png",
                                             "Desktop/cover.png"},
              std::to_string(names.size()));
        {   // s023g: placing one makes it Keep (an id too); a decided one keeps its level
            ho::Planned kp;
            const std::vector<ho::Pick> picks = {{H + "/Downloads/plain.png", "Pictures/Covers", "test"},
                                                 {H + "/Downloads/gone-soon.png", "Pictures/Covers", "test"},
                                                 {H + "/Downloads/scan.pdf", "Documents/Scans", "test"}};
            ho::keep_sent(kp.plan, picks);
            std::size_t keeps = 0, ids = 0;
            bool scan = false;
            for (const auto& a : kp.plan.actions) {
                keeps += a.attr == core::facts::kRetention && a.content == "keep";
                ids += a.attr == core::facts::kId;
                scan = scan || a.from == H + "/Downloads/scan.pdf";
            }
            check("keep_sent: an untagged and an Ephemeral download -> Keep (+ ids); Held left alone",
                  keeps == 2 && ids == 2 && !scan, std::to_string(keeps) + "/" + std::to_string(ids));
        }

        const std::vector<ho::Pick> picks = {
            {H + "/Downloads/cover.png", "Pictures/Covers", "by type: *.png -> Pictures"},
            {H + "/Desktop/cover.png", "Pictures/Covers", "by type: *.png -> Pictures"},
            {H + "/Downloads/scan.pdf", "Documents/Scans/2026", "by type: *.pdf -> Documents"},
            {H + "/Downloads/cover.curvz", "Art/Sunrise-project/Graphics", "into Sunrise-project: *.curvz -> Graphics"},
        };
        const auto before = tree_facts(H);
        auto pl = ho::plan(H, picks);
        std::vector<std::string> made;
        for (const auto& a : pl.plan.actions) if (a.verb == dr::Verb::MakeDir) made.push_back(a.to.substr(H.size() + 1));
        std::vector<std::string> tos;
        for (const auto& a : pl.plan.actions) if (a.verb == dr::Verb::Move) tos.push_back(a.to.substr(H.size() + 1));
        check("plan: missing levels first, parents first, once (Documents, Scans, 2026); Covers exists",
              made == std::vector<std::string>{"Documents", "Documents/Scans", "Documents/Scans/2026"},
              std::to_string(made.size()));
        check("plan: a taken name -> \"cover (2).png\", and the next one in the same plan -> \"cover (3).png\"; warned",
              tos == std::vector<std::string>{"Pictures/Covers/cover (2).png", "Pictures/Covers/cover (3).png",
                                               "Documents/Scans/2026/scan.pdf", "Art/Sunrise-project/Graphics/cover.curvz"} &&
              pl.renamed.size() == 2 && pl.plan.actions[3].reason.find("is taken there") != std::string::npos,
              tos.empty() ? "" : tos[0]);
        check("plan: READ-ONLY until run", tree_facts(H) == before);
        latch::door::set_app_name("Tidy");
        dr::Door door(H, D);
        door.load();
        const auto unpreviewed = door.run(pl.plan);
        std::vector<std::string> mk;
        std::size_t refused = 0;
        for (const auto& a : pl.plan.actions) {
            refused += !door.refusal(a, mk).empty();
            if (a.verb == dr::Verb::MakeDir) mk.push_back(a.to);
        }
        check("plan: refused unpreviewed; judged in order, nothing refused", !unpreviewed.ok && refused == 0,
              std::to_string(refused));
        struct stat s0{};
        ::stat((H + "/Downloads/cover.png").c_str(), &s0);
        pl.plan.previewed = true;
        const auto run = door.run(pl.plan);
        struct stat s1{};
        ::stat((H + "/Pictures/Covers/cover (2).png").c_str(), &s1);
        check("run: each file home -- moved, not copied (same inode); the one already there untouched",
              run.ok && run.done == pl.plan.actions.size() && s0.st_ino == s1.st_ino &&
              read(H + "/Pictures/Covers/cover (3).png") == "png two" &&
              read(H + "/Pictures/Covers/cover.png") == "already there" &&
              fs::exists(H + "/Documents/Scans/2026/scan.pdf") && fs::exists(H + "/Art/Sunrise-project/Graphics/cover.curvz") &&
              !fs::exists(H + "/Downloads/cover.png") &&
              core::facts::tags_of(H + "/Pictures/Covers/cover (2).png").retention == "keep",
              run.messages.empty() ? "" : run.messages[0]);
        core::catalog::Catalog cat;
        cat.open(D, true);
        core::tagging::record(cat, {H + "/Downloads/cover.png"}, now_s() - 10);   // known before (as a stamp left it)
        const auto moved = core::tagging::moved_paths(door, run.batch);
        core::tagging::record(cat, moved, now_s());
        check("catalog: follows the move (the id's path is the new one)", moved.size() == 4 &&
              cat.by_id(id) && cat.by_id(id)->path == H + "/Pictures/Covers/cover (2).png");
        const auto u = door.undo();
        core::tagging::after_undo(cat, door, run.batch, now_s());
        check("undo: every file back where it was, the new folders gone; the catalog follows back",
              u.ok && read(H + "/Downloads/cover.png") == "png one" && read(H + "/Desktop/cover.png") == "png two" &&
              fs::exists(H + "/Downloads/scan.pdf") && !fs::exists(H + "/Documents") &&
              !fs::exists(H + "/Pictures/Covers/cover (2).png") && cat.by_id(id)->path == H + "/Downloads/cover.png",
              u.messages.empty() ? "" : u.messages[0]);
        fs::remove_all(root);
    }

    // ── s013a: the archive -- ~/Archive/<year>/<its folder>, through the door, on a FIXTURE ──
    {
        std::cout << "[archive]\n";
        const auto read = [](const std::string& f) { std::ifstream in(f); std::stringstream o; o << in.rdbuf(); return o.str(); };
        namespace ar = core::archive;
        namespace sh = core::sha256;
        namespace dr = latch::door;
        check("sha256: the FIPS vectors (\"\", \"abc\", 56 bytes, a million a's)",
              sh::of_text("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" &&
              sh::of_text("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" &&
              sh::of_text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" &&
              sh::of_text(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_arc_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string();
        put_file(H + "/Documents/Letters/letters-2019.odt", "dear sir");
        put_file(H + "/Documents/Letters/old.odt", "older");
        put_file(H + "/notes.txt", "top of home");
        put_file(H + "/Archive/2026/Documents/Letters/old.odt", "already there");
        put_file(H + "/Archive/2025/kept.odt", "in the archive");
        put_file(H + "/Writing/Jasper-project/draft.odt", "a project's");
        const std::string id = "66666666-6666-4666-8666-666666666666";
        xset(H + "/Documents/Letters/letters-2019.odt", core::facts::kId, id);
        xset(H + "/Documents/Letters/letters-2019.odt", core::facts::kRetention, "historical");
        xset(H + "/Documents/Letters/old.odt", core::facts::kRetention, "historical");

        check("folder: ~/Archive/<year>/<its folder>; home's top -> ~/Archive/<year>",
              ar::folder_for(H, H + "/Documents/Letters/a.odt", 2026) == "Archive/2026/Documents/Letters" &&
              ar::folder_for(H, H + "/notes.txt", 2026) == "Archive/2026");
        check("refused: already in ~/Archive; inside a -project; not a file; outside home",
              !ar::refusal(H, H + "/Archive/2025/kept.odt").empty() &&
              !ar::refusal(H, H + "/Writing/Jasper-project/draft.odt").empty() &&
              !ar::refusal(H, H + "/Documents").empty() && !ar::refusal(H, "/etc/hosts").empty() &&
              ar::refusal(H, H + "/Documents/Letters/old.odt").empty());

        // 2026-11-10 noon, local.
        std::tm tm{};
        tm.tm_year = 126; tm.tm_mon = 10; tm.tm_mday = 10; tm.tm_hour = 12; tm.tm_isdst = -1;
        const std::int64_t when = static_cast<std::int64_t>(std::mktime(&tm));
        const auto before = tree_facts(H);
        auto pl = ar::plan(H, {H + "/Documents/Letters/letters-2019.odt", H + "/Documents/Letters/old.odt",
                                H + "/notes.txt", H + "/Writing/Jasper-project/draft.odt"}, when);
        std::size_t tags = 0, ids = 0, dirs = 0;
        std::vector<std::string> tos;
        for (const auto& a : pl.plan.actions) {
            if (a.verb == dr::Verb::Tag) { ++tags; ids += a.attr == core::facts::kId; }
            if (a.verb == dr::Verb::MakeDir) ++dirs;
            if (a.verb == dr::Verb::Move) tos.push_back(a.to.substr(H.size() + 1));
        }
        check("plan: the project's file refused, said; the rest tagged (archived = the day; an id where none)",
              pl.refused.size() == 1 && tags == 6 && ids == 2 && pl.plan.actions[0].verb == dr::Verb::Tag &&
              pl.plan.actions[0].attr == core::facts::kArchived && pl.plan.actions[0].content == "2026-11-10", std::to_string(tags) + " tags");
        check("plan: into ~/Archive/2026/<its folder>; a taken name -> \"old (2).odt\", warned; home's top -> ~/Archive/2026",
              tos == std::vector<std::string>{"Archive/2026/Documents/Letters/letters-2019.odt",
                                               "Archive/2026/Documents/Letters/old (2).odt", "Archive/2026/notes.txt"} &&
              pl.renamed.size() == 1 && dirs == 0, tos.empty() ? "" : tos[0]);
        {   // s023d: archived by hand, a file with no level (or Ephemeral) becomes Historical; Historical stays
            std::size_t hist = 0;
            bool old_untouched = true;
            for (const auto& a : pl.plan.actions)
                if (a.verb == dr::Verb::Tag && a.attr == core::facts::kRetention) {
                    hist += a.content == "historical";
                    old_untouched = old_untouched && a.from != H + "/Documents/Letters/old.odt";
                }
            check("plan: archived by hand -> Historical (notes.txt, no level); the Historical ones untouched",
                  hist == 1 && old_untouched, std::to_string(hist));
        }
        check("plan: READ-ONLY until run", tree_facts(H) == before);

        latch::door::set_app_name("Tidy");
        dr::Door door(H, D);
        door.load();
        check("plan: refused unpreviewed", !door.run(pl.plan).ok);
        struct stat s0{};
        ::stat((H + "/Documents/Letters/letters-2019.odt").c_str(), &s0);
        pl.plan.previewed = true;
        const auto run = door.run(pl.plan);
        struct stat s1{};
        ::stat((H + "/Archive/2026/Documents/Letters/letters-2019.odt").c_str(), &s1);
        const auto t = core::facts::tags_of(H + "/Archive/2026/Documents/Letters/letters-2019.odt");
        check("run: moved, not copied (same inode); id + retention kept; archived tag; the one there untouched",
              run.ok && s0.st_ino == s1.st_ino && t.id == id && t.retention == "historical" && t.archived == "2026-11-10" &&
              read(H + "/Archive/2026/Documents/Letters/old.odt") == "already there" &&
              read(H + "/Archive/2026/Documents/Letters/old (2).odt") == "older" &&
              !fs::exists(H + "/Documents/Letters/letters-2019.odt") && fs::exists(H + "/Archive/2026/notes.txt"),
              run.messages.empty() ? "" : run.messages[0]);

        core::catalog::Catalog cat;
        cat.open(D, true);
        const std::string arc_letters = H + "/Archive/2026/Documents/Letters/letters-2019.odt";
        ::system(("touch -a -d 2024-01-01 '" + arc_letters + "'").c_str());
        core::facts::Facts at0;
        core::facts::of(arc_letters, at0);
        const auto n = ar::record(cat, door, run.batch, when);
        core::facts::Facts at1;
        core::facts::of(arc_letters, at1);
        check("fingerprint: reading the file leaves its read time as it was (O_NOATIME)", at0.atime == at1.atime &&
              at1.atime < now_s() - 86400 * 300, std::to_string(at1.atime));
        const auto a = cat.archived_by_id(id);
        check("catalog: 3 fingerprinted at the archive -- SHA-256 of the bytes, size, where it was, the batch",
              n == 3 && cat.archived().size() == 3 && a && a->path == H + "/Archive/2026/Documents/Letters/letters-2019.odt" &&
              a->origin == H + "/Documents/Letters/letters-2019.odt" && a->sha256 == sh::of_text("dear sir") &&
              a->size == 8 && a->batch == run.batch && a->proved == 0 && cat.by_id(id) &&
              cat.by_id(id)->path == a->path, std::to_string(n));

        check("refused again: already archived (the tag), wherever it sits",
              ar::refusal(H, H + "/Archive/2026/Documents/Letters/letters-2019.odt").find("already") != std::string::npos);
        core::settings::Settings st;
        std::vector<core::facts::Facts> fx(1);
        core::facts::of(H + "/Archive/2026/Documents/Letters/letters-2019.odt", fx[0]);
        const auto later = when + 400LL * 86400;
        fx[0].atime = fx[0].mtime = when - 400LL * 86400;
        auto un = fx[0];
        un.tags.archived.clear();
        check("due: an archived Historical file is not \"to archive\" again (the same file, untagged, is)",
              core::due::due(fx, {}, st, later).empty() && core::due::due({un}, {}, st, later).size() == 1);

        const auto u = door.undo();
        const auto dropped = ar::after_undo(cat, run.batch);
        core::tagging::after_undo(cat, door, run.batch, now_s());
        check("undo: each file back, the archived tag off, the fingerprints dropped; the one there untouched",
              u.ok && read(H + "/Documents/Letters/letters-2019.odt") == "dear sir" &&
              read(H + "/Documents/Letters/old.odt") == "older" && read(H + "/notes.txt") == "top of home" &&
              core::facts::tags_of(H + "/Documents/Letters/letters-2019.odt").archived.empty() &&
              core::facts::tags_of(H + "/Documents/Letters/letters-2019.odt").id == id &&
              dropped == 3 && cat.archived().empty() &&
              read(H + "/Archive/2026/Documents/Letters/old.odt") == "already there",
              u.messages.empty() ? "" : u.messages[0]);
        fs::remove_all(root);
    }

    // ── s013b: backup to media + proof, through the door (latch 0.4 media roots), on a FIXTURE ──
    {
        std::cout << "[backup]\n";
        const auto read = [](const std::string& f) { std::ifstream in(f); std::stringstream o; o << in.rdbuf(); return o.str(); };
        namespace bk = core::backup;
        namespace sh = core::sha256;
        namespace st = core::settings;
        namespace dr = latch::door;

        {   // settings: media lines
            st::Media m;
            const auto s1 = st::parse("media=Big SSD|/run/media/scott/Big SSD/Tidy/\nmedia=Pi|/mnt/pi//arc\n"
                                      "media=Big SSD|/elsewhere\nmedia=relative/path\n");
            check("settings: media lines -- name | folder; cleaned; a name used twice -> the first; a relative folder dropped",
                  s1.media.size() == 2 && s1.media[0] == st::Media{"Big SSD", "/run/media/scott/Big SSD/Tidy"} &&
                  s1.media[1] == st::Media{"Pi", "/mnt/pi/arc"} && st::parse(st::format(s1)) == s1);
            check("settings: a bare folder is named after itself; \"name | folder\" reads back",
                  st::media_of("/run/media/scott/Samsung T7", m) && m.label == "Samsung T7" &&
                  st::media_of(st::media_line({"Pi", "/mnt/pi"}), m) && m == st::Media{"Pi", "/mnt/pi"} &&
                  !st::media_of("Pi | ", m));
        }

        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_bk_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string(), M = (root / "ssd" / "Tidy").string();
        const st::Media media{"SSD", M};
        check("where: ~/Archive/2026/Documents/a.odt -> <media>/2026/Documents/a.odt; outside ~/Archive -> none",
              bk::where_on(media, H, H + "/Archive/2026/Documents/a.odt") == M + "/2026/Documents/a.odt" &&
              bk::where_on(media, H, H + "/Documents/a.odt").empty() && bk::where_on(media, H, H + "/Archive").empty());

        // The archive (as s013a leaves it): files + their fingerprints in the catalog.
        core::catalog::Catalog cat;
        cat.open(D, true);
        const auto arc = [&](const std::string& id, const std::string& rel, const std::string& body,
                             const std::string& fingerprint_of) {
            put_file(H + "/Archive/" + rel, body);
            core::catalog::Archived a;
            a.id = id;
            a.path = H + "/Archive/" + rel;
            a.sha256 = sh::of_text(fingerprint_of);
            a.size = fingerprint_of.size();
            a.when = now_s();
            cat.put_archived(a);
        };
        arc("a", "2026/Documents/Letters/a.odt", "alpha", "alpha");
        arc("b", "2026/notes.txt", "beta", "beta");
        arc("c", "2025/c.odt", "gamma", "gamma");              // already on the disk, the same bytes
        arc("d", "2025/d.odt", "delta", "delta");              // already on the disk, other bytes
        arc("f", "2026/f.odt", "FOXTROT", "foxtrot");          // changed since it was archived
        arc("e", "2026/gone.odt", "echo", "echo");
        fs::remove(H + "/Archive/2026/gone.odt");               // the catalog's, not on disk any more
        put_file(M + "/2025/c.odt", "gamma");
        put_file(M + "/2025/d.odt", "DELTA!");
        fs::create_directories(H + "/.local/share/Trash/files");

        dr::set_media_roots({});
        auto pl = bk::plan(H, media, cat.archived(), cat.copies());
        check("no media registered -> the door refuses every copy there (latch 0.4)",
              !pl.plan.actions.empty() && !dr::Door(H, D).refusal(pl.plan.actions.back(), {}).empty());

        check("problem: missing -> said; on home's own disk (other_disk on) -> \"not mounted?\"; one-disk fixture (off) -> none",
              !bk::problem({"X", (root / "nope").string()}, H, false).empty() &&
              bk::problem(media, H, true).find("mounted") != std::string::npos && bk::problem(media, H, false).empty());

        bk::register_media({media}, false);
        const auto home_before = tree_facts(H), media_before = tree_facts((root / "ssd").string());
        pl = bk::plan(H, media, cat.archived(), cat.copies());
        std::size_t mk = 0, cp = 0;
        bool dirs_first = true, seen_copy = false;
        std::vector<std::string> made;
        for (const auto& a : pl.plan.actions) {
            if (a.verb == dr::Verb::MakeDir) { ++mk; dirs_first &= !seen_copy; made.push_back(a.to.substr(M.size())); }
            if (a.verb == dr::Verb::Copy) { ++cp; seen_copy = true; }
        }
        check("plan: 3 copies, the missing folders top down and first (2025 is there), 2 already there, the gone one said",
              cp == 3 && mk == 3 && dirs_first &&
              made == std::vector<std::string>{"/2026", "/2026/Documents", "/2026/Documents/Letters"} &&
              pl.checks.size() == 2 && pl.skipped.size() == 1 && pl.proved == 0 &&
              pl.plan.label == "Back up: 3 files -> SSD", pl.plan.label);
        check("plan: READ-ONLY until run (home and media)",
              tree_facts(H) == home_before && tree_facts((root / "ssd").string()) == media_before);

        dr::Door door(H, D);
        door.load();
        {
            std::vector<std::string> mm;
            bool all = true;
            for (const auto& a : pl.plan.actions) {
                all &= door.refusal(a, mm).empty();
                if (a.verb == dr::Verb::MakeDir) mm.push_back(a.to);
            }
            check("the door accepts every step, in order", all);
        }
        check("run: refused unpreviewed", !door.run(pl.plan).ok);
        struct stat s0{};
        ::stat((H + "/Archive/2026/Documents/Letters/a.odt").c_str(), &s0);
        pl.plan.previewed = true;
        const auto run = door.run(pl.plan);
        struct stat s1{}, s2{};
        ::stat((H + "/Archive/2026/Documents/Letters/a.odt").c_str(), &s1);
        ::stat((M + "/2026/Documents/Letters/a.odt").c_str(), &s2);
        check("run: copied (a new file on the media; the archive's file untouched); the different one there untouched",
              run.ok && read(M + "/2026/Documents/Letters/a.odt") == "alpha" && read(M + "/2026/notes.txt") == "beta" &&
              s0.st_ino == s1.st_ino && s1.st_ino != s2.st_ino && read(H + "/Archive/2026/Documents/Letters/a.odt") == "alpha" &&
              read(M + "/2025/d.odt") == "DELTA!", run.messages.empty() ? "" : run.messages[0]);

        const auto t0 = now_s();
        const auto proofs = bk::prove(cat, door, run.batch, media, pl.checks, t0);
        std::size_t ok = 0;
        std::string bad;
        for (const auto& p : proofs) { if (p.ok) ++ok; else bad += p.id + ":" + p.why + " "; }
        const auto ca = cat.copies_of("a"), cc = cat.copies_of("c");
        check("prove: read back from the disk -- a, b, and c (already there) proved; d (other bytes) and f (changed "
              "since archived) NOT",
              proofs.size() == 5 && ok == 3 && cat.copies().size() == 3 && cat.copies_of("d").empty() &&
              cat.copies_of("f").empty() && bad.find("d:") != std::string::npos && bad.find("f:") != std::string::npos, bad);
        check("prove: the proof row -- media, where on it, SHA-256, size, when; the copy's batch; already-there = batch 0",
              ca.size() == 1 && ca[0].media == "SSD" && ca[0].path == M + "/2026/Documents/Letters/a.odt" &&
              ca[0].sha256 == sh::of_text("alpha") && ca[0].size == 5 && ca[0].proved == t0 && ca[0].batch == run.batch &&
              cc.size() == 1 && cc[0].batch == 0);
        check("status: \"on SSD <day>\"; none -> \"not on media yet\"",
              bk::status_line(ca) == "on SSD " + core::archive::day_of(t0) && bk::status_line({}) == "not on media yet");

        auto again = bk::plan(H, media, cat.archived(), cat.copies());
        std::size_t cp2 = 0;
        for (const auto& a : again.plan.actions) cp2 += a.verb == dr::Verb::Copy;
        check("again: the 3 proved are left out; d and f are there now -> read back only, never copied over",
              again.proved == 3 && cp2 == 0 && again.checks.size() == 2 && again.skipped.size() == 1);

        {   // a proof whose copy vanished from the disk is no proof: planned again (stat only)
            fs::rename(M + "/2025/c.odt", M + "/2025/c.away");
            const auto p3 = bk::plan(H, media, cat.archived(), cat.copies());
            std::size_t c3 = 0;
            for (const auto& a : p3.plan.actions) c3 += a.verb == dr::Verb::Copy && a.to == M + "/2025/c.odt";
            check("again: a proved copy gone from the disk -> copied again (not counted as proved)",
                  p3.proved == 2 && c3 == 1);
            fs::rename(M + "/2025/c.away", M + "/2025/c.odt");
        }
        const auto trash_before = tree_facts(H + "/.local/share/Trash");
        const auto u = door.undo();
        const auto dropped = bk::after_undo(cat, run.batch);
        const std::string mt = dr::media_trash_dir(dr::media_roots()[0]);
        check("undo: the copies leave the media into ITS trash (not home's); their proofs go; c's (already there) stays",
              u.ok && !fs::exists(M + "/2026/Documents/Letters/a.odt") && !fs::exists(M + "/2026/notes.txt") &&
              fs::exists(mt + "/files") && tree_facts(H + "/.local/share/Trash") == trash_before &&
              dropped == 2 && cat.copies().size() == 1 && cat.copies_of("c").size() == 1 &&
              read(M + "/2025/c.odt") == "gamma" && read(M + "/2025/d.odt") == "DELTA!" &&
              read(H + "/Archive/2026/notes.txt") == "beta", u.messages.empty() ? mt : u.messages[0]);

        // A real other disk: /dev/shm (tmpfs), other_disk ON -- as the app runs.
        struct stat shm{}, tmp{};
        const std::string S = "/dev/shm/tidy_selftest_bk_" + std::to_string(::getpid());
        if (::stat("/dev/shm", &shm) == 0 && ::stat(H.c_str(), &tmp) == 0 && shm.st_dev != tmp.st_dev &&
            ::access("/dev/shm", W_OK) == 0) {
            fs::remove_all(S);
            fs::create_directories(S);
            const st::Media shm_media{"shm", S};
            bk::register_media({shm_media});
            auto p2 = bk::plan(H, shm_media, cat.archived(), cat.copies());
            p2.plan.previewed = true;
            const auto r2 = door.run(p2.plan);
            const auto pr2 = bk::prove(cat, door, r2.batch, shm_media, p2.checks, now_s());
            std::size_t ok2 = 0;
            for (const auto& p : pr2) ok2 += p.ok;
            check("a real other disk (/dev/shm, other_disk on): problem none; 5 copied, 4 proved (f changed), on \"shm\"",
                  bk::problem(shm_media, H).empty() && r2.ok && ok2 == 4 && cat.copies_of("a").size() == 1 &&
                  cat.copies_of("c").size() == 2, r2.messages.empty() ? std::to_string(ok2) : r2.messages[0]);
            fs::remove_all(S);
        } else {
            check("a real other disk: /dev/shm not a separate disk here -- skipped", true);
        }
        dr::set_media_roots({});
        fs::remove_all(root);
    }

    // ── s014: removal -- to the Trash; an archived file only when proved NOW on media, on a FIXTURE ──
    {
        std::cout << "[removal]\n";
        const auto read = [](const std::string& f) { std::ifstream in(f); std::stringstream o; o << in.rdbuf(); return o.str(); };
        namespace rm = core::removal;
        namespace bk = core::backup;
        namespace sh = core::sha256;
        namespace st = core::settings;
        namespace dr = latch::door;

        st::Settings s0;
        check("settings: proofs_needed round trip, default 1, clamped 1..5",
              s0.proofs_needed == 1 && st::parse("proofs_needed=3\n").proofs_needed == 3 &&
              st::parse("proofs_needed=0\n").proofs_needed == 1 && st::parse("proofs_needed=9\n").proofs_needed == 5 &&
              st::parse(st::format(st::parse("proofs_needed=2\n"))).proofs_needed == 2);

        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_rm_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string(), M = (root / "ssd" / "Tidy").string();
        fs::create_directories(M);
        fs::create_directories(H + "/.local/share/Trash/files");
        core::catalog::Catalog cat;
        cat.open(D, true);
        const auto arc = [&](const std::string& id, const std::string& rel, const std::string& body, bool copy,
                             const std::string& media = "SSD") {
            put_file(H + "/Archive/" + rel, body);
            xset(H + "/Archive/" + rel, core::facts::kId, id);
            xset(H + "/Archive/" + rel, core::facts::kRetention, "historical");
            core::catalog::Archived a;
            a.id = id; a.path = H + "/Archive/" + rel; a.sha256 = sh::of_text(body); a.size = body.size(); a.when = now_s();
            cat.put_archived(a);
            if (copy) {
                put_file(M + "/" + rel, body);
                cat.put_copy({id, media, M + "/" + rel, a.sha256, a.size, now_s(), 0});
            }
        };
        arc("11111111-1111-4111-8111-111111111111", "2026/a.odt", "alpha", true);    // proved
        arc("22222222-2222-4222-8222-222222222222", "2026/b.odt", "beta", false);    // not backed up
        arc("33333333-3333-4333-8333-333333333333", "2026/c.odt", "gamma", true, "Gone disk");   // media not in settings
        arc("44444444-4444-4444-8444-444444444444", "2026/d.odt", "delta", true);    // the copy rots on the disk
        arc("55555555-5555-4555-8555-555555555555", "2026/g.odt", "golf", true);     // changed in ~/Archive since
        put_file(M + "/2026/d.odt", "DELTA");
        put_file(H + "/Archive/2026/g.odt", "GOLF");
        put_file(H + "/Downloads/old.iso", "ephemeral");
        xset(H + "/Downloads/old.iso", core::facts::kRetention, "ephemeral");
        put_file(H + "/Documents/keep.odt", "keep");
        xset(H + "/Documents/keep.odt", core::facts::kRetention, "keep");
        put_file(H + "/Archive/2026/stray.odt", "no fingerprint");

        st::Settings S;
        S.media = {{"SSD", M}};
        bk::register_media(S.media, false);
        core::own::write(D, st::kFile, st::format(S));
        auto book = rm::book_of(cat);
        std::vector<std::string> off;
        for (const auto& a : rm::offerable(book, S)) off.push_back(fs::path(a.path).filename().string());
        check("offerable: proved on a media the settings name, still in ~/Archive (no reads) -- a, d, g; not b, not c",
              off == std::vector<std::string>{"a.odt", "d.odt", "g.odt"}, off.empty() ? "" : off[0]);
        st::Settings two = S;
        two.proofs_needed = 2;
        check("offerable: 2 proofs needed -> none (each has one)", rm::offerable(book, two).empty());

        std::vector<core::due::Item> items;
        const auto att = core::due::look(H, D, "", now_s(), &items);
        std::size_t proved_rows = 0;
        for (const auto& it : items) proved_rows += it.to == core::due::To::Remove && it.why.find("proved on SSD") != std::string::npos;
        check("due: the proved archive files are \"to remove\" (3) and counted (old.iso was born today: not due yet)",
              proved_rows == 3 && att.remove == 3, std::to_string(att.remove));

        const auto before_h = tree_facts(H), before_m = tree_facts((root / "ssd").string());
        const std::vector<std::string> paths = {H + "/Archive/2026/a.odt", H + "/Archive/2026/b.odt",
            H + "/Archive/2026/c.odt", H + "/Archive/2026/d.odt", H + "/Archive/2026/g.odt", H + "/Downloads/old.iso",
            H + "/Documents/keep.odt", H + "/Archive/2026/stray.odt"};
        auto pl = rm::plan(H, book, S, paths, false);
        std::map<std::string, std::string> why;
        for (const auto& [p, w] : pl.refused) why[fs::path(p).filename().string()] = w;
        std::vector<std::string> go;
        for (const auto& a : pl.plan.actions) go.push_back(fs::path(a.from).filename().string());
        check("plan: a (read back now) + the Ephemeral one -> the Trash; only Trash steps",
              go == std::vector<std::string>{"a.odt", "old.iso"} && pl.plan.actions[0].verb == dr::Verb::Trash &&
              pl.pending.size() == 1 && pl.pending[0].media == "SSD", go.empty() ? "" : go[0]);
        check("plan: kept, each said -- b not backed up, c's media not in Settings, d's copy differs now, g changed "
              "since archived, keep.odt not Ephemeral/Held, a stray in ~/Archive has no fingerprint",
              why.size() == 6 && why["b.odt"].find("0 of the 1") != std::string::npos &&
              why["c.odt"].find("not in Settings") != std::string::npos &&
              why["d.odt"].find("differs now") != std::string::npos && why["g.odt"].find("changed since") != std::string::npos &&
              why["keep.odt"].find("not Ephemeral") != std::string::npos &&
              why["stray.odt"].find("no fingerprint") != std::string::npos, why["b.odt"]);
        check("plan: READ-ONLY (home and media)", tree_facts(H) == before_h && tree_facts((root / "ssd").string()) == before_m);

        {   // the media unplugged -> not proved now, even with the proof in the catalog
            fs::rename(M, M + ".away");
            const auto p2 = rm::plan(H, book, S, {H + "/Archive/2026/a.odt"}, false);
            check("plan: the media not there now -> kept (\"not there\"), the old proof is not enough",
                  p2.plan.actions.empty() && p2.refused.size() == 1 && p2.refused[0].second.find("not there") != std::string::npos,
                  p2.refused.empty() ? "" : p2.refused[0].second);
            fs::rename(M + ".away", M);
        }
        check("plan: 2 proofs needed -> a kept (\"1 of the 2\")",
              rm::plan(H, book, two, {H + "/Archive/2026/a.odt"}, false).refused[0].second.find("1 of the 2") != std::string::npos);

        dr::Door door(H, D);
        door.load();
        check("run: refused unpreviewed", !door.run(pl.plan).ok);
        pl.plan.previewed = true;
        const auto r = door.run(pl.plan);
        const auto n = rm::record(cat, door, r.batch, pl.pending, now_s());
        const auto gone = cat.removed();
        check("run: a + old.iso in the home Trash (not deleted); the media copy untouched; a's removed row (SSD)",
              r.ok && !fs::exists(H + "/Archive/2026/a.odt") && !fs::exists(H + "/Downloads/old.iso") &&
              read(H + "/.local/share/Trash/files/a.odt") == "alpha" && read(M + "/2026/a.odt") == "alpha" &&
              n == 1 && gone.size() == 1 && gone[0].media == "SSD" && gone[0].batch == r.batch,
              r.messages.empty() ? "" : r.messages[0]);
        book = rm::book_of(cat);
        check("after: a is no longer offered", rm::offerable(book, S).size() == 2);

        const auto u = door.undo();
        const auto dropped = rm::after_undo(cat, r.batch);
        check("undo: both back from the Trash, the removed row dropped, offered again",
              u.ok && read(H + "/Archive/2026/a.odt") == "alpha" && read(H + "/Downloads/old.iso") == "ephemeral" &&
              dropped == 1 && cat.removed().empty() && rm::offerable(rm::book_of(cat), S).size() == 3);
        dr::set_media_roots({});
        fs::remove_all(root);
    }

    // ── s021: the code archive -- git state, the copy, RESTORE.txt, the live folder, build folders; a FIXTURE with real repos ──
    {
        std::cout << "[code]\n";
        namespace cd = core::code;
        namespace pj = core::projects;
        namespace sh = core::sha256;
        namespace st = core::settings;
        namespace dr = latch::door;
        namespace vc = latch::vcs;
        const auto read = [](const std::string& f) { std::ifstream in(f); std::stringstream o; o << in.rdbuf(); return o.str(); };
        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_code_" + std::to_string(::getpid()));
        fs::remove_all(root);
        const std::string H = (root / "home").string(), D = (root / "data").string(), M = (root / "ssd" / "Tidy").string();
        const std::string R = (root / "remotes").string();
        fs::create_directories(M);
        fs::create_directories(R);
        fs::create_directories(H + "/.local/share/Trash/files");
        const auto git = [](const std::string& dir, std::vector<std::string> args) {
            std::vector<std::string> a{"-c", "user.name=Tidy Test", "-c", "user.email=t@example.com", "-c", "init.defaultBranch=main"};
            a.insert(a.end(), args.begin(), args.end());
            return vc::git(dir, a, 30);
        };
        bool git_ok = true;
        // A repo `rel` in project `proj` with files, committed; `remote` = a bare repo it pushes to ("" = none).
        const auto repo = [&](const std::string& dir, const std::map<std::string, std::string>& files, const std::string& remote) {
            for (const auto& [f, body] : files) put_file(dir + "/" + f, body);
            git_ok &= git(dir, {"init", "-q"}).code == 0;
            git_ok &= git(dir, {"add", "-A"}).code == 0;
            git_ok &= git(dir, {"commit", "-qm", "first light"}).code == 0;
            if (!remote.empty()) {
                git_ok &= git(R, {"init", "-q", "--bare", remote + ".git"}).code == 0;
                git_ok &= git(dir, {"remote", "add", "origin", R + "/" + remote + ".git"}).code == 0;
                git_ok &= git(dir, {"push", "-q", "-u", "origin", "HEAD"}).code == 0;
            }
        };
        const std::string A = H + "/Develop/gtk/alpha-project";
        repo(A + "/alpha", {{"CMakeLists.txt", "project(alpha)\n"}, {"src/a.cpp", "int main(){}\n"},
                            {".gitignore", "build/\n*.log\n"}}, "alpha");
        put_file(A + "/alpha/build/CMakeCache.txt", "cache");
        put_file(A + "/alpha/build/obj/a.o", std::string(20000, 'o'));
        put_file(A + "/alpha/notes.log", "ignored by git");
        put_file(A + "/docs/HANDOFF.md", "# handoff\n");
        put_file(A + "/scripts/alpha_backup.sh", "#!/bin/sh\n");
        put_file(A + "/backups/s001.zip", "zip");
        fs::create_directories(A + "/backups/Old");
        put_file(A + "/fixes/try.cpp", "fix");
        put_file(A + "/.cache/clangd/index", "idx");
        put_file(A + "/.tidy-project", "id=aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\n");
        const std::string B = H + "/Develop/gtk/beta-project";
        repo(B + "/beta", {{"CMakeLists.txt", "project(beta)\n"}}, "beta");
        put_file(B + "/beta/more.cpp", "x");
        git(B + "/beta", {"add", "-A"});
        git(B + "/beta", {"commit", "-qm", "not pushed yet"});
        put_file(B + "/.tidy-project", "id=bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb\n");
        const std::string G = H + "/Develop/gtk/gamma-project";
        repo(G + "/gamma", {{"CMakeLists.txt", "project(gamma)\n"}}, "gamma");
        put_file(G + "/gamma/scratch.txt", "untracked");
        put_file(G + "/.tidy-project", "id=cccccccc-cccc-4ccc-8ccc-cccccccccccc\n");
        const std::string E = H + "/Develop/delta-project";
        repo(E, {{"CMakeLists.txt", "project(delta)\n"}}, "");
        put_file(E + "/.tidy-project", "id=dddddddd-dddd-4ddd-8ddd-dddddddddddd\n");
        put_file(H + "/Develop/omega-project/CMakeLists.txt", "project(omega)\n");
        check("fixture: real repos made (git init / commit / a bare remote / push)", git_ok);

        const auto found = pj::find(H);
        const auto infos = cd::look_all(found);
        std::map<std::string, cd::Info> by;
        for (const auto& i : infos) by[i.p.name] = i;
        check("look_all: the five code projects, by name",
              infos.size() == 5 && by.count("alpha") && by.count("beta") && by.count("gamma") && by.count("delta") &&
              by.count("omega"), std::to_string(infos.size()));
        const auto& al = by["alpha"];
        check("look: alpha -- one repo one down (alpha), pushed, its remote, the commit",
              al.repos.size() == 1 && al.repos[0].rel == "alpha" && al.state() == "pushed" && al.problem().empty() &&
              al.repos[0].url == R + "/alpha.git" && al.repos[0].subject == "first light" && al.repos[0].head.size() == 40 &&
              al.repos[0].branch == "main" && al.last_commit > 0, al.problem());
        check("look: alpha's sizes -- the build folder found, build and .git shares inside the total",
              al.builds == std::vector<std::string>{A + "/alpha/build"} && al.build >= 20000 && al.git > 0 &&
              al.total > al.build + al.git);
        check("look: beta -- 1 unpushed (as of the last fetch)",
              by["beta"].state() == "1 unpushed" && by["beta"].problem().find("not pushed") != std::string::npos,
              by["beta"].state());
        check("look: gamma -- an untracked file is an uncommitted change, named",
              by["gamma"].state() == "1 uncommitted change" && by["gamma"].problem().find("scratch.txt") != std::string::npos,
              by["gamma"].problem());
        check("look: delta (the repo at the project's top; Tidy's untracked .tidy-project is not a change) -- no remote",
              by["delta"].repos.size() == 1 && by["delta"].repos[0].rel == "." && by["delta"].state() == "no remote" &&
              by["delta"].problem().find("no remote") != std::string::npos, by["delta"].state());
        check("look: omega (CMakeLists, no .git) -- no git repo",
              by["omega"].state() == "no git repo" && !by["omega"].problem().empty());
        put_file(H + "/Documents/build/x.txt", "a folder named build, no CMake beside it");
        check("is_build: CMakeCache.txt, or \"build\" beside .git / CMakeLists -- not any folder named build",
              cd::is_build(A + "/alpha/build") && !cd::is_build(H + "/Documents/build") && !cd::is_build(A + "/docs"));

        const auto keep = cd::keep_of(al);
        std::set<std::string> kept;
        for (const auto& f : keep.files) kept.insert(f.substr(A.size() + 1));
        const std::set<std::string> want{".tidy-project", "alpha/.gitignore", "alpha/CMakeLists.txt", "alpha/src/a.cpp",
                                         "docs/HANDOFF.md", "scripts/alpha_backup.sh"};
        std::string kl;
        for (const auto& k : kept) kl += k + " ";
        check("keep: docs, scripts, the committed source, the id file -- no .git, build, ignored, backups, fixes, hidden folder",
              kept == want, kl);
        check("keep: backups/, backups/Old/ as empty placeholders; fixes/ only said (the door never makes one)",
              keep.empty == std::vector<std::string>{"backups", "backups/Old"} && keep.remake == std::vector<std::string>{"fixes"});
        std::string left;
        for (const auto& l : keep.left) left += l + " | ";
        check("keep: what is left behind is said (.git, build folders, ignored files, backups/ and fixes/ contents, .cache)",
              left.find(".git (") != std::string::npos && left.find("build folders") != std::string::npos &&
              left.find("files git ignores (1 file") != std::string::npos && left.find("backups/ contents") != std::string::npos &&
              left.find("fixes/ contents") != std::string::npos && left.find("hidden folder .cache") != std::string::npos, left);

        core::catalog::Catalog cat;
        cat.open(D, true);
        for (const auto& [n, i] : by) {
            core::catalog::ProjectRow r;
            r.id = i.p.id; r.path = i.p.path; r.name = n;
            cat.seen_project(r);
        }
        cat.set_status(al.p.id, "done", "");
        cat.set_status(by["beta"].p.id, "done", "");
        cat.set_status(by["delta"].p.id, "done", "");
        const auto now = now_s();
        const int year = std::stoi(core::archive::day_of(now).substr(0, 4));
        const auto before = tree_facts(H);
        std::vector<cd::Info> all;
        for (const auto& i : infos) all.push_back(i);
        auto pl = cd::plan_archive(H, all, cat.projects(), cat.archived(), now);
        std::map<std::string, std::string> why;
        for (const auto& [n, w] : pl.refused) why[n] = w;
        check("archive plan: only alpha -- beta unpushed, gamma not Done, delta no remote, omega no id",
              pl.projects == 1 && why.size() == 4 && why["beta-project"].find("not pushed") != std::string::npos &&
              why["gamma-project"].find("not Done") != std::string::npos &&
              why["delta-project"].find("no remote") != std::string::npos &&
              why["omega-project"].find("no id") != std::string::npos, why["omega-project"]);
        const std::string X = H + "/Archive/" + std::to_string(year) + "/Develop/gtk/alpha-project";
        std::size_t dirs = 0, copies = 0, writes = 0;
        bool order = true;
        int phase = 0;
        for (const auto& a : pl.plan.actions) {
            const int ph = a.verb == dr::Verb::MakeDir ? 0 : a.verb == dr::Verb::Copy ? 1 : 2;
            order &= ph >= phase;
            phase = ph;
            dirs += ph == 0; copies += ph == 1; writes += ph == 2;
        }
        check("archive plan: folders (~/Archive down, parents first), then 6 copies, then RESTORE.txt",
              order && copies == 6 && writes == 1 && pl.plan.actions[0].to == H + "/Archive" &&
              pl.plan.actions.back().to == X + "/RESTORE.txt" && pl.files == 6, std::to_string(dirs));
        const auto& rt = pl.plan.actions.back().content;
        check("RESTORE.txt: the remote, the branch, the commit, the clone line, the placeholders, what was left",
              rt.find(R + "/alpha.git") != std::string::npos && rt.find("branch   main") != std::string::npos &&
              rt.find("git clone " + R + "/alpha.git alpha && git -C alpha checkout " + al.repos[0].head) != std::string::npos &&
              rt.find("backups/ backups/Old/\n") != std::string::npos && rt.find("Make again when you work on it: fixes/") != std::string::npos && rt.find("~/Develop/gtk/alpha-project") != std::string::npos);
        check("archive plan: READ-ONLY", tree_facts(H) == before);

        dr::Door door(H, D);
        door.load();
        check("archive run: refused unpreviewed", !door.run(pl.plan).ok);
        pl.plan.previewed = true;
        const auto ino = [](const std::string& p) { struct stat s{}; return ::lstat(p.c_str(), &s) == 0 ? s.st_ino : 0; };
        const auto a_ino = ino(A + "/alpha/src/a.cpp");
        const auto run = door.run(pl.plan);
        check("archive run: the copy on disk -- source without .git / build, docs, scripts, empty placeholders, RESTORE.txt",
              run.ok && read(X + "/alpha/src/a.cpp") == "int main(){}\n" && read(X + "/docs/HANDOFF.md") == "# handoff\n" &&
              !fs::exists(X + "/alpha/.git") && !fs::exists(X + "/alpha/build") && !fs::exists(X + "/alpha/notes.log") &&
              fs::is_empty(X + "/backups/Old") && !fs::exists(X + "/fixes") && !fs::exists(X + "/backups/s001.zip") &&
              !fs::exists(X + "/.cache") && read(X + "/RESTORE.txt") == rt, run.messages.empty() ? "" : run.messages[0]);
        check("archive run: a COPY -- the live project untouched (same inode, .git and build still there)",
              ino(A + "/alpha/src/a.cpp") == a_ino && fs::exists(A + "/alpha/.git") && fs::exists(A + "/alpha/build/obj/a.o"));
        const auto n = cd::record(cat, door, run.batch, H, now);
        std::size_t match = 0;
        for (const auto& r : cat.archived())
            match += r.sha256 == sh::of_file(r.path) && (r.origin.empty() ? r.path == X + "/RESTORE.txt"
                                                                         : r.origin.rfind(A + "/", 0) == 0);
        check("record: 7 fingerprints (6 copies + RESTORE.txt), the bytes' SHA-256, origin = the live file",
              n == 7 && cat.archived().size() == 7 && match == 7, std::to_string(n));
        const auto arch = cd::archived_of(cat.archived(), A);
        st::Settings S;
        S.media = {{"SSD", M}};
        auto book = core::removal::book_of(cat);
        check("archived_of: the batch, its folder, 7 files; the line says not backed up yet",
              arch.top == X && arch.rows.size() == 7 && arch.batch == run.batch &&
              cd::archived_line(arch, book, S).find("not backed up yet") != std::string::npos,
              cd::archived_line(arch, book, S));
        auto again = cd::plan_archive(H, {al}, cat.projects(), cat.archived(), now);
        check("archive plan: again -> refused, archived already, nothing changed",
              again.plan.actions.empty() && again.refused.size() == 1 &&
              again.refused[0].second.find("archived already") != std::string::npos);

        // The live folder: not before a proof on media read back NOW.
        core::backup::register_media(S.media, false);
        auto tr = cd::plan_trash(H, {al}, book, S, false);
        check("trash plan: not proved -> refused, says how many and what to do",
              tr.plan.actions.empty() && tr.refused.size() == 1 && tr.refused[0].second.find("7 archive files not proved") != std::string::npos &&
              tr.refused[0].second.find("back it up first") != std::string::npos, tr.refused.empty() ? "" : tr.refused[0].second);
        for (const auto& r : cat.archived()) {   // the media copies, as Back up would leave them (proved)
            const std::string to = M + "/" + r.path.substr((H + "/Archive/").size());
            put_file(to, read(r.path));
            cat.put_copy({r.id, "SSD", to, r.sha256, r.size, now, 0});
        }
        book = core::removal::book_of(cat);
        check("archived_line: proved on SSD -- the live folder may go",
              cd::archived_line(arch, book, S).find("proved on SSD") != std::string::npos, cd::archived_line(arch, book, S));
        put_file(A + "/docs/HANDOFF.md", "# handoff, edited after the archive\n");
        tr = cd::plan_trash(H, {cd::look(al.p)}, book, S, false);
        check("trash plan: a live file changed since the archive -> refused, named",
              tr.plan.actions.empty() && tr.refused[0].second.find("changed: docs/HANDOFF.md") != std::string::npos,
              tr.refused[0].second);
        put_file(A + "/docs/HANDOFF.md", "# handoff\n");
        put_file(M + "/" + std::to_string(year) + "/Develop/gtk/alpha-project/alpha/src/a.cpp", "rotted");
        tr = cd::plan_trash(H, {cd::look(al.p)}, book, S, false);
        check("trash plan: a media copy differs now -> refused (read back, not the catalog's word)",
              tr.plan.actions.empty() && tr.refused[0].second.find("differs now") != std::string::npos, tr.refused[0].second);
        put_file(M + "/" + std::to_string(year) + "/Develop/gtk/alpha-project/alpha/src/a.cpp", "int main(){}\n");
        const auto before_t = tree_facts(H);
        tr = cd::plan_trash(H, {cd::look(al.p)}, book, S, false);
        check("trash plan: proved now -> ONE Trash of the live folder, the reason says where; READ-ONLY",
              tr.plan.actions.size() == 1 && tr.plan.actions[0].verb == dr::Verb::Trash && tr.plan.actions[0].from == A &&
              tr.plan.actions[0].reason.find("on SSD") != std::string::npos && tree_facts(H) == before_t,
              tr.refused.empty() ? "" : tr.refused[0].second);
        tr.plan.previewed = true;
        const auto tr_run = door.run(tr.plan);
        check("trash run: the live folder in the Trash whole (its .git too), the archive copy stays",
              tr_run.ok && !fs::exists(A) && fs::exists(H + "/.local/share/Trash/files/alpha-project/alpha/.git") &&
              read(X + "/alpha/src/a.cpp") == "int main(){}\n");
        const auto tr_undo = door.undo();
        check("trash undo: back where it was, .git and build with it", tr_undo.ok && fs::exists(A + "/alpha/.git") &&
              ino(A + "/alpha/src/a.cpp") == a_ino && fs::exists(A + "/alpha/build/CMakeCache.txt"));

        // Clean build folders.
        auto cl = cd::plan_clean({cd::look(al.p), by["beta"]});
        check("clean plan: alpha's build folder to the Trash, its size; beta has none (said)",
              cl.plan.actions.size() == 1 && cl.plan.actions[0].from == A + "/alpha/build" && cl.plan.actions[0].bytes >= 20000 &&
              cl.refused.size() == 1 && cl.refused[0].second == "no build folder" &&
              cl.plan.label.rfind("Clean build folders: alpha-project (", 0) == 0, cl.plan.label);
        cl.plan.previewed = true;
        const auto cl_run = door.run(cl.plan);
        check("clean run: build/ gone, the source and .git stay; look again -> no build share",
              cl_run.ok && !fs::exists(A + "/alpha/build") && fs::exists(A + "/alpha/src/a.cpp") &&
              cd::look(al.p).build == 0 && cd::look(al.p).builds.empty());
        check("clean undo: build/ back", door.undo().ok && read(A + "/alpha/build/CMakeCache.txt") == "cache");

        // The archive undone: the copy goes (to the Trash), the fingerprints with it.
        const auto un = door.undo();
        const auto dropped = core::archive::after_undo(cat, run.batch);
        check("archive undo: the copy and its folders gone from ~/Archive, the 7 fingerprints dropped, live untouched",
              un.ok && !fs::exists(X) && dropped == 7 && cat.archived().empty() && ino(A + "/alpha/src/a.cpp") == a_ino,
              un.messages.empty() ? "" : un.messages[0]);
        dr::set_media_roots({});
        fs::remove_all(root);
    }

    // ── s017a: How to use Tidy -- topics; every page has one; every page a topic names exists ──
    {
        std::cout << "[help]\n";
        namespace hp = core::help;
        std::set<std::string> used, ids;
        bool all_exist = true, whole = true;
        std::string bad;
        for (const auto& t : hp::topics()) {
            whole &= !t.title.empty() && !t.about.empty() && ids.insert(t.id).second;
            if (t.page.empty()) continue;
            used.insert(t.page);
            if (!core::pages::find(t.page)) { all_exist = false; bad = t.page; }
        }
        check("help: topics open on the Introduction; each has a title, words, a unique id",
              !hp::topics().empty() && hp::topics()[0].id == "intro" && whole && hp::find("removal"));
        check("help: every page a topic names exists", all_exist, bad);
        std::string missing;
        for (const auto& id : {"closet", "due", "projects", "templates", "purposes", "home", "archive", "activity"})
            if (!used.count(id)) missing += std::string(missing.empty() ? "" : ", ") + id;
        check("help: every page has a topic", missing.empty(), missing);
        const auto* lv = hp::find("levels");
        bool each = lv && lv->about.size() == core::retention::all().size() + 1;
        for (const auto& i : core::retention::all()) {
            bool found = false;
            if (lv) for (const auto& p : lv->about) found |= p.rfind(i.title + " --", 0) == 0;
            each &= found;
        }
        check("help: Retention levels -- one line per level, by its title", each);
    }


    // ── s024: the bookbinding layout -- a deep template, books by their place ──
    {
        std::cout << "[bookbinding]\n";
        namespace tp = core::templates;
        namespace pj = core::projects;
        namespace dr = latch::door;
        const auto* bs = core::starters::find("bookbinding");
        check("bookbinding: a starter -- ~/Documents/Bookbinding/<Author (Last, First)>/<Title>, by place",
              bs && bs->t.by_place && bs->t.group == "Author (Last, First)" && bs->t.category == "Documents/Bookbinding" &&
              tp::place_depth(bs->t) == 2);
        const auto& bt = bs->t;
        check("bookbinding: the whole Hollow Oak tree, 14 folders, 4 deep (Body/OCR/Scans/Archive)",
              bt.folders.size() == 14 && tp::find(bt, "Body/OCR/Scans/Archive") && tp::find(bt, "Cover/Print") &&
              tp::find(bt, "Body/Layout/Workups") && tp::depth_of(bt) == 4 && tp::problems(bt).empty(),
              tp::problems(bt).empty() ? "" : tp::problems(bt).front());
        check("bookbinding: place + group read back (format -> parse)", tp::parse(tp::format(bt)) == bt, tp::format(bt));
        auto rt = [&](const char* f) { return tp::route_for(bt, f); };
        check("bookbinding: routes by stage -- scans, bundles, odt, print, final, cover",
              rt("A0001.png") == "Body/OCR/Scans/Archive" && rt("Archive.zip") == "Body/OCR/Scans" &&
              rt("The Message in the Hollow Oak.optik") == "Body/OCR/Scans" && rt("X.composix") == "Body/Layout" &&
              rt("X.odt") == "Body/Layout/ODT" && rt("X.quire") == "Body/Print" && rt("X.pdf") == "Body/Print" &&
              rt("X.epub") == "Body/Finals" && rt("Cover_1960.png") == "Cover/Layout/Workups" &&
              rt("The_Message.svg") == "Cover/Layout" && rt("BookPlate.jpg") == "Body/Layout/Images");
        check("bookbinding: a .png neither a scan nor cover* has no route (the row asks); A0001 copy.png is no scan",
              rt("The_Message_in_the_Hollow_Oak.png").empty() && rt("A0001 copy.png").empty());
        {
            tp::Template t = tp::shell("x", "X");
            t.group = "Author";
            tp::Builder b(tp::Draft{tp::shell("x", "X"), {}, ""});
            const bool grp_refused = !b.set_group("Author").empty();
            const bool on = b.set_by_place(true).empty() && b.set_group("Author").empty() && tp::place_depth(b.tpl()) == 2;
            const bool off = b.set_by_place(false).empty() && b.tpl().group.empty() && b.undo() && b.tpl().group == "Author";
            check("bookbinding: a group only by place (problems + builder); off clears it, undo brings it back",
                  !tp::problems(t).empty() && grp_refused && on && off);
        }

        const fs::path root = fs::temp_directory_path() / ("tidy_selftest_book_" + std::to_string(::getpid()));
        fs::remove_all(root);
        fs::create_directories(root / "home");
        fs::create_directories(root / "data");
        const std::string H = fs::canonical(root / "home").string(), D = fs::canonical(root / "data").string();
        pj::apply_places(D);   // no saved templates: the built-in bookbinding's shelf, our bundles
        const auto sh = pj::shelves();
        check("bookbinding: the shelf is there with no template saved; .composix / .quire are packages",
              sh.size() == 1 && sh[0].category == "Documents/Bookbinding" && sh[0].depth == 2 &&
              latch::pkg::is_package_name("A.composix") && latch::pkg::is_package_name("A.QUIRE"));
        pj::Shelf got;
        check("bookbinding: is_whole_at -- the title is a project; the author, the shelf, a part are not",
              pj::is_whole_at("Documents/Bookbinding/Keene, Carolyn/Hollow Oak", &got) && got.tpl == "bookbinding" &&
              !pj::is_whole_at("Documents/Bookbinding/Keene, Carolyn") && !pj::is_whole_at("Documents/Bookbinding") &&
              !pj::is_whole_at("Documents/Bookbinding/Keene, Carolyn/Hollow Oak/Body") &&
              !pj::is_whole_at("Documents/Bookbinding/.hidden/Hollow Oak") && pj::is_whole_at("Writing/Jasper-project"));
        std::string at;
        check("bookbinding: inside_whole -- a file's folder deep in a book names the book",
              pj::inside_whole("Documents/Bookbinding/Keene, Carolyn/Hollow Oak/Body/OCR", &at) &&
              at == "Documents/Bookbinding/Keene, Carolyn/Hollow Oak" && !pj::inside_whole("Documents/Bookbinding/Keene, Carolyn"));

        std::string why;
        const std::string id = core::facts::new_id();
        check("bookbinding: New book needs its author", pj::create(bt, "Hollow Oak", H, id, &why).actions.empty() &&
              why.find("Author") != std::string::npos, why);
        auto plan = pj::create(bt, "The Message in the Hollow Oak", H, id, &why, "Keene, Carolyn");
        const std::string book = H + "/Documents/Bookbinding/Keene, Carolyn/The Message in the Hollow Oak";
        check("bookbinding: a plan -- Documents, Bookbinding, the author, the book (no -project), 14 folders, the id",
              plan.actions.size() == 19 && plan.actions[0].to == H + "/Documents" &&
              plan.actions[1].to == H + "/Documents/Bookbinding" && plan.actions[2].to == H + "/Documents/Bookbinding/Keene, Carolyn" &&
              plan.actions[3].to == book && plan.actions.back().to == book + "/.tidy-project", why);
        latch::door::set_app_name("Tidy");
        dr::Door door(H, D);
        door.load();
        plan.previewed = true;
        const auto r = door.run(plan);
        const auto found = pj::find(H);
        const pj::Project* bp = nullptr;
        for (const auto& p : found) if (p.rel.find("Hollow Oak") != std::string::npos) bp = &p;
        check("bookbinding: run -> the tree on disk; find() knows it by its place, its author, its id",
              r.ok && fs::is_directory(book + "/Body/OCR/Scans/Archive") && fs::is_directory(book + "/Cover/Print") && bp &&
              bp->by_place && bp->name == "The Message in the Hollow Oak" && bp->group == "Keene, Carolyn" &&
              bp->category == "Documents/Bookbinding" && bp->id == id && found.size() == 1);
        check("bookbinding: drift reads as deep as the template -- none on a new book",
              tp::drift(bt, tp::folders_of(book, tp::depth_of(bt))).empty());
        check("bookbinding: a second book with the same title by the same author -- refused (exists)",
              pj::create(bt, "The Message in the Hollow Oak", H, core::facts::new_id(), &why, "Keene, Carolyn").actions.empty() &&
              why.find("exists") != std::string::npos, why);

        // An old book, lumped: loose files at its top, a bundle. Kept whole everywhere.
        const std::string old = H + "/Documents/Bookbinding/Keene, Carolyn/The Hidden Staircase";
        fs::create_directories(old + "/Hidden.composix/assets");
        put_file(old + "/A0001.png", "scan");
        put_file(old + "/Hidden.odt", "odt");
        put_file(old + "/Hidden.composix/document.json", "{}");
        put_file(H + "/Documents/Bookbinding/notes.txt", "loose at the shelf's top");
        const auto loose = core::scan::walk_loose(H, {H + "/Documents"});
        bool in_book = false, shelf_note = false;
        for (const auto& f : loose) {
            in_book |= f.path.find("/Keene, Carolyn/") != std::string::npos;
            shelf_note |= f.path == H + "/Documents/Bookbinding/notes.txt";
        }
        check("bookbinding: the loose walk never goes inside a book (its top files, its bundle)", !in_book && shelf_note);
        check("bookbinding: an old book without -project is found too", pj::find(H).size() == 2);
        check("bookbinding: Adopt refuses a book; Archive refuses a file inside one",
              !core::adopt::refusal(H, "", "Documents/Bookbinding/Keene, Carolyn/The Hidden Staircase", false).empty() &&
              !core::archive::refusal(H, old + "/Hidden.odt").empty());
        fs::remove_all(root);
        pj::set_shelves({});
    }

    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
