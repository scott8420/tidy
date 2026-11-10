#include "core/Look.hpp"

#include <algorithm>
#include <set>

namespace tidy::core::look {

std::string_view name(State s) {
    switch (s) {
        case State::Start:   return "start";
        case State::Running: return "running";
        case State::Results: return "results";
    }
    return "start";
}

bool PageStates::begin() {
    if (m_state == State::Running) return false;   // already going: a second click is nothing
    m_state = State::Running;
    return true;
}

bool PageStates::finish() {
    if (m_state != State::Running) return false;   // results only ever follow a run
    m_state = State::Results;
    return true;
}

bool PageStates::cancel() {
    if (m_state != State::Running) return false;
    m_state = State::Start;                        // a stopped run has nothing to show
    return true;
}

bool PageStates::reset() {
    if (m_state == State::Start) return false;
    m_state = State::Start;
    return true;
}

NavEntry section(std::string title) {
    NavEntry e;
    e.section = true;
    e.title = std::move(title);
    return e;
}

NavEntry item(std::string id, std::string icon, std::string title, std::string tint) {
    return NavEntry{false, std::move(id), std::move(icon), std::move(title), std::move(tint)};
}

std::vector<NavEntry> tidy_nav() {
    // The road's pages (docs/PROGRESS.md), one tint each. Icons: Tidy's own
    // symbolics (tidy-<id>-symbolic, from Rouster's set) in the gresource.
    // Colour (the-look.md): each page's emblem is drawn in its own tint.
    return {
        section("TODAY"),
        item("closet",    "tidy-closet-symbolic",      "The Closet",  "emerald"),
        item("due",       "tidy-due-symbolic",         "Due",         "rose"),
        section("ORGANIZE"),
        item("projects",  "tidy-projects-symbolic",    "Projects",    "amber"),
        item("templates", "tidy-templates-symbolic",   "Templates",   "violet"),
        item("purposes",  "tidy-purposes-symbolic",    "Purposes",    "teal"),
        item("home",      "tidy-home-symbolic",        "Files home",  "sky"),
        section("KEEP"),
        item("archive",   "tidy-archive-symbolic",     "Archive",     "indigo"),
        item("activity",  "tidy-activity-symbolic",    "Activity",    "slate"),
    };
}

int first_item(const std::vector<NavEntry>& nav) {
    for (std::size_t i = 0; i < nav.size(); ++i)
        if (!nav[i].section) return static_cast<int>(i);
    return -1;
}

const std::vector<std::string>& tints() {
    static const std::vector<std::string> t = {
        "magenta", "green", "orange", "sky", "rose", "amber",
        "violet", "lime", "teal", "indigo", "slate", "emerald",
    };
    return t;
}

std::vector<std::string> lint(const std::vector<NavEntry>& nav) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    const auto& known = tints();
    for (const auto& e : nav) {
        if (e.section) {
            if (!e.id.empty()) out.push_back("section \"" + e.title + "\" carries an id");
            if (e.title.empty()) out.push_back("a section has no title");
            continue;
        }
        if (e.id.empty()) out.push_back("an item has no id (\"" + e.title + "\")");
        if (e.title.empty()) out.push_back("item " + e.id + " has no title");
        if (!seen.insert(e.id).second) out.push_back("duplicate id " + e.id);
        if (std::find(known.begin(), known.end(), e.tint) == known.end())
            out.push_back("item " + e.id + ": unknown tint \"" + e.tint + "\"");
    }
    if (first_item(nav) < 0) out.push_back("no selectable item");
    return out;
}

}  // namespace tidy::core::look
