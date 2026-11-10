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
//
//   ./build/tidy_selftest

#include "core/Look.hpp"
#include "core/Pages.hpp"
#include "core/Paths.hpp"
#include "core/Shortcuts.hpp"

#include <latch/Door.hpp>
#include <latch/Protect.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>   // getpid: a fixture dir per run

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
    };
    return f;
}

std::vector<std::string> gate_hits(const std::string& file, const std::string& text) {
    std::vector<std::string> hits;
    const std::string code = code_only(text);
    for (const auto& f : forbidden())
        if (std::regex_search(code, f.re)) hits.push_back(file + ": " + f.name);
    return hits;
}
}  // namespace

int main() {
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

    // ── The source gate: the spine writes NOTHING ────────────────────────────
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
        check("gate: no Tidy source writes a file (" + std::to_string(files) + " files read)",
              files > 10 && hits.empty(), hits);
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
