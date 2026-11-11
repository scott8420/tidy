#pragma once
#include "core/Projects.hpp"

#include <gtkmm/stack.h>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <latch/Door.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// ProjectsPage (s008) -- every <name>-project, as found. GTK here; the rules
// (find, category, place, fix) are core::projects. Moves go through latch's
// door ONLY: each act builds a plan, the preview shows every step and the
// door's refusals, Do it runs it, Activity undoes it. "Leave it here" is
// written by core::own (<data>/projects.ini) and never asked again.
//
// States: start (hero + Look) | results (to place / placed / left) | preview.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Window; class Label; class ListBox; class Button; }

namespace tidy {

class ProjectsPage : public Gtk::Stack {
public:
    using Changed = std::function<void()>;   // after a run: Activity refreshes
    ProjectsPage(latch::door::Door& door, std::string home, std::string data_dir, bool fixture, Changed changed);

    void refresh_if_shown();                 // after an undo in Activity

private:
    void build_start();
    void build_results();
    void build_preview();

    void on_look();
    void fill();
    void preview(latch::door::Plan plan);
    void on_run();
    void on_leave(const std::string& rel, bool leave);
    std::vector<std::string> categories() const;
    std::string shown(const std::string& path) const;

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    bool m_fixture = false;
    Changed m_changed;

    std::optional<latch::door::Plan> m_plan;

    Gtk::Label*   m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label*   m_status = nullptr;
    Gtk::Label*   m_prev_summary = nullptr;
    Gtk::ListBox* m_prev_list = nullptr;
    Gtk::Label*   m_prev_status = nullptr;
    Gtk::Button*  m_prev_run = nullptr;
};

}  // namespace tidy
