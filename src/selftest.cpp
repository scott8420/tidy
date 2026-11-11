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

#include <latch/Door.hpp>
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
    const auto ps = core::birth::places(H, slurp(fs::path(H) / ".config/user-dirs.dirs"));
    std::cout << core::birth::text(core::birth::report(H, ps), H);
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
              is(cb::classify(F("/h/Downloads/a.pdf"), "downloads", "https://gov.example.com/a.pdf"), "download", rt::Level::Undecided));
        check("rules: a downloaded image -> Ephemeral",
              is(cb::classify(F("/h/Downloads/cat.JPG"), "downloads", "https://x.com/cat.jpg"), "image-download", rt::Level::Ephemeral));
        check("rules: a downloaded installer -> Ephemeral",
              is(cb::classify(F("/h/Downloads/tool.AppImage"), "downloads", ""), "installer-download", rt::Level::Ephemeral));
        check("rules: a download no rule knows -> Undecided, its origin named",
              [&] { auto b = cb::classify(F("/h/Downloads/b.pdf"), "downloads", "https://shop.example.com/b.pdf");
                    return is(b, "download", rt::Level::Undecided) && b.why.find("shop.example.com") != std::string::npos; }());
        check("rules: a package is one thing -- never a screenshot, never an installer",
              is(cb::classify(F("/h/Downloads/Screenshot.scriv", "", true), "downloads", ""), "download", rt::Level::Undecided));
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
              has("Downloads/notes.txt", "download", rt::Level::Undecided) &&
              has("Downloads/brochure.pdf", "download", rt::Level::Undecided));
        check("report: Documents -- the loose top only (loose.txt, report.odt, the .scriv whole; not Taxes/w2.pdf)",
              has("Documents/loose.txt", "unknown", rt::Level::Undecided) && find("Documents/report.odt") &&
              find("Documents/Jasper.scriv") && !find("Documents/Taxes/w2.pdf") &&
              !find("Documents/Jasper.scriv/Files/Data/1/content.rtf"));
        check("report: never hidden, a symlink, fixes/, or a folder not a landing place",
              !find("Downloads/.part") && !find("Downloads/report-link.odt") && !find("Documents/fixes/patch.txt") &&
              !find("Pictures/landscape.png"));
        std::string unused;
        for (const auto& r : cb::rules())
            if (!rep.count_rule(r.id)) unused += r.id + " ";
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
        check("closet: proposed = the births with a level (6)",
              names(v.proposed) == std::set<std::string>{"f1040.pdf", "cat.jpg", "fedora-44.rpm",
                                                         "Screenshot From 2026-11-10 09-14-02.png",
                                                         "Screencast From 2026-11-10 09-20-11.webm",
                                                         "Screenshot 2026-11-10.png"},
              std::to_string(v.proposed.size()));
        check("closet: waiting = the undecided with nothing to propose (6, the .scriv whole)",
              names(v.waiting) == std::set<std::string>{"brochure.pdf", "notes.txt", "plan.txt", "loose.txt",
                                                        "report.odt", "Jasper.scriv"},
              std::to_string(v.waiting.size()));
        check("closet: a decided file never visits (cover-final, Keep)", v.decided == 1 && v.junk == 0 &&
              !names(v.proposed).count("cover-final.png") && !names(v.waiting).count("cover-final.png"));
        const auto later = look(now + (cl::kAgingDays + 10) * 86400LL);
        std::size_t aged = 0;
        for (const auto& b : later.proposed) aged += b.rule == "aged-out" && b.level == rt::Level::Ephemeral;
        check("closet: 40 days on, unread -> aging out to Ephemeral; a package waits (never ages as a file)",
              aged == 5 && later.waiting.size() == 1 && names(later.waiting).count("Jasper.scriv"),
              std::to_string(aged) + " aged");

        latch::door::Door door(H, D);
        door.load();
        core::catalog::Catalog cat;
        check("closet: the catalog opens (made by the first write)", cat.open(D, true));

        auto plan = cl::accept(v.proposed);
        bool tags_only = !plan.actions.empty();
        for (const auto& a : plan.actions) tags_only = tags_only && a.verb == latch::door::Verb::Tag;
        check("closet: accept is Tag actions only -- an id + a retention for each of the 6",
              tags_only && plan.actions.size() == 12, std::to_string(plan.actions.size()));
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
        check("closet: ...in the catalog (6)", cat.count() == 6, std::to_string(cat.count()));
        v = look();
        check("closet: ...and they left the Closet (0 proposed, 7 decided)",
              v.proposed.empty() && v.waiting.size() == 6 && v.decided == 7);
        check("closet: accept never takes a decided file back", [&] {
            cb::Born b = cb::classify(fc::Facts{}, "desktop", "");
            fc::of(H + "/Desktop/cover-final.png", b.file);
            b = cb::classify(b.file, "desktop", "");
            return cl::accept({b}).actions.empty();
        }());

        auto jp = cl::junk_drawer(pick(v.waiting, {"notes.txt", "plan.txt"}));
        jp.previewed = true;
        const auto r2 = door.run(jp);
        tg::record(cat, tg::paths_of(door, r2.batch), now);
        const auto jt = fc::tags_of(H + "/Downloads/notes.txt");
        v = look();
        check("closet: the Junk Drawer -- Keep, role junk-drawer; counted, out of the Closet",
              r2.ok && jt.retention == "keep" && jt.role == cl::kJunkDrawer && v.junk == 2 && v.waiting.size() == 4 &&
              !names(v.waiting).count("notes.txt"));

        auto dp = cl::decide(pick(v.waiting, {"brochure.pdf"}), rt::Level::Held);
        dp.previewed = true;
        const auto r3 = door.run(dp);
        tg::record(cat, tg::paths_of(door, r3.batch), now);
        check("closet: decided by hand -- brochure.pdf Held", r3.ok && fc::tags_of(H + "/Downloads/brochure.pdf").retention == "held" &&
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
        while (door.can_undo() && door.undo_label().find("accept 6") == std::string::npos) {
            std::uint64_t b = 0;
            for (auto it = door.batches().rbegin(); it != door.batches().rend(); ++it)
                if (!it->undone) { b = it->id; break; }
            undone += door.undo().ok;
            tg::after_undo(cat, door, b, now);
        }
        v = look();
        check("closet: undo x4 -- back in the Closet as they were (junk, held, seen, historical)",
              undone == 4 && v.junk == 0 && v.waiting.size() == 6 && v.decided == 7 &&
              fc::tags_of(H + "/Downloads/notes.txt").id.empty() &&
              fc::tags_of(H + "/Documents/loose.txt").retention.empty(),
              std::to_string(undone));
        check("closet: ...and the catalog holds only the 6 accepted", cat.count() == 6, std::to_string(cat.count()));

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
              aged == 2 && cl::view(cb::report(H, ps), now + 6 * 86400LL).proposed.empty(), std::to_string(aged));

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
        check("templates: names -- a-z 0-9 '-' only; a folder never hidden, '..', or three deep",
              tp::name_refusal("writing-project").empty() && !tp::name_refusal("Writing").empty() &&
              !tp::name_refusal("").empty() && !tp::name_refusal("-x").empty() &&
              tp::path_refusal("Graphics/Covers").empty() && !tp::path_refusal("a/b/c").empty() &&
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
            check("templates: parse drops a three-deep folder and a bad role",
                  tp::parse("name=x\ncategory=X\nfolder=input a/b/c\nfolder=nope a\nfolder=input a\n").folders.size() == 1);
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
        check("starters: ten, the base first, every name its own", all.size() == 10 && names.size() == 10 &&
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
                if (s.t.name == "code-project") continue;
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
            if (!tp::route_for(s.t, "book.pdf").empty() || !tp::route_for(s.t, "book.epub").empty()) no_pdf = false;
        check("starters: pdf / epub route nowhere -- the row asks", no_pdf);
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
        check("lists (s010f3): register_own -> .tidy-files + .tidy-project, their headers",
              own.size() == 2 && own[0].name == ".tidy-files" && own[0].header == fl::kHeader &&
              own[1].name == ".tidy-project" && own[1].header == "id=");
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
        const auto rep = core::birth::report(H, core::birth::places(H, ""));
        const auto off = ho::offered(rep);
        std::set<std::string> names;
        for (const auto& b : off) names.insert(fs::path(b.file.path).parent_path().filename().string() + "/" +
                                               fs::path(b.file.path).filename().string());
        check("offered: decided files in the landing places -- not undecided, ephemeral, hands-off, the Junk Drawer, "
              "untagged, or inside a project",
              names == std::set<std::string>{"Downloads/cover.png", "Downloads/scan.pdf", "Downloads/cover.curvz", "Desktop/cover.png"},
              std::to_string(names.size()));

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

    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
