#pragma once
// core::look -- THE LOOK stone's GTK-free half (Cairn s001, lifted from Rouster).
//
// The look itself is CSS (resources/look.css) and is the visual channel --
// Scott's eyes. What can be tested headless lives here:
//
//   PageStates -- the start / running / results machine every module page in
//                 Rouster repeats: the hero + orb, the big spinner + live
//                 number + Stop, the headline + cards. One rule set, so a
//                 page can't land in "results" without having run, or be
//                 cancelled into "results".
//   NavEntry   -- the custom sidebar as plain data: section headers (never
//                 selectable) and items (page id, icon, title, tint).
//   tints()    -- the tint names look.css defines (`window.tint-<name>`).
//                 The selftest reads look.css and proves every tint here has
//                 its gradient, its orb and its act button -- and that every
//                 nav item's tint is one of them.
//
// Opt-in: an app that wants the look says so in ONE place (look::enabled() in
// include/Look.hpp). A plain app (Plunk-style) never loads look.css and keeps
// the stock StackSidebar. docs/the-look.md.
#include <string>
#include <string_view>
#include <vector>

namespace tidy::core::look {

// ── the page-state machine ───────────────────────────────────────────────────
enum class State { Start, Running, Results };

// The stack child names the Shell uses -- one spelling, here.
std::string_view name(State s);

class PageStates {
public:
    State state() const { return m_state; }
    std::string_view child() const { return name(m_state); }

    // Each returns true when the state changed (the caller repaints only then).
    bool begin();    // Start / Results -> Running   ("Scan", "Scan again")
    bool finish();   // Running -> Results           (the worker is done)
    bool cancel();   // Running -> Start             (Stop: nothing to show)
    bool reset();    // anything -> Start            (back to the hero)

private:
    State m_state = State::Start;
};

// ── the sidebar as data ──────────────────────────────────────────────────────
struct NavEntry {
    bool        section = false;   // a header row: not selectable, keyboard skips it
    std::string id;                // page id == stack child name (items only)
    std::string icon;              // symbolic icon name (items only)
    std::string title;             // the row's words ("PATTERNS" for a section)
    std::string tint;              // window.tint-<tint> while this page shows
};

NavEntry section(std::string title);
NavEntry item(std::string id, std::string icon, std::string title, std::string tint);

// Tidy's sidebar: the road's pages.
std::vector<NavEntry> tidy_nav();

// The first selectable entry's index (the page shown at start), or -1.
int first_item(const std::vector<NavEntry>& nav);

// The tints look.css defines, in its order.
const std::vector<std::string>& tints();

// Problems with a nav list: duplicate ids, empty ids/titles, unknown tints, a
// section carrying an id. Empty = clean. (The selftest prints them.)
std::vector<std::string> lint(const std::vector<NavEntry>& nav);

}  // namespace tidy::core::look
