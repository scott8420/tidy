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


    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
