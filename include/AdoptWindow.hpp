#pragma once
// AdoptWindow (s023) -- adopt folders you organized by hand. Pick folders; each
// is "in place" (cataloged where it is: an id + a level, routes learned from
// what is there) or a "catch-all" (its files offered to Sort / Files home / the
// Closet like loose ones). Look (off the window's thread, read-only) -> the
// preview -> Do it: one door batch of tags (undo in Activity), the routes book
// and settings.ini written by core::own. GTK here; the rules are core::adopt.
#include <gtkmm/window.h>

#include "core/Adopt.hpp"
#include "core/Settings.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; }

namespace tidy {

class AdoptWindow : public Gtk::Window {
public:
    using Changed = std::function<void()>;                                        // after a run: Activity refreshes
    using Save = std::function<std::string(const core::settings::Settings&)>;    // "" = saved
    AdoptWindow(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir,
                std::function<std::string()> user_dirs, Save save, Changed changed);
    ~AdoptWindow() override;

    void open(const core::settings::Settings& current);   // present; the folders as saved

private:
    struct Pick {
        std::string rel;
        bool catch_all = false;
    };
    struct Result {
        std::vector<core::adopt::Look> looks;
        std::vector<std::string> refused;   // per pick: "" or why
        std::size_t refused_steps = 0;      // the door's refusals of the plan (s023a: worked out off the thread)
    };
    void build_folders();
    void on_add();
    void on_look();
    void show(Result r);
    void on_run();
    void stale();                            // the picks changed: the preview is gone

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    std::function<std::string()> m_user_dirs;
    Save m_save;
    Changed m_changed;
    core::settings::Settings m_current;
    std::vector<Pick> m_picks;
    bool m_busy = false;                     // s023a: a look or a run is in its Busy window
    std::optional<Result> m_result;          // shown, not yet run
    std::optional<latch::door::Plan> m_plan;

    Gtk::ListBox* m_folders = nullptr;
    Gtk::Label*   m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label*   m_status = nullptr;
    Gtk::Button*  m_run = nullptr;
};

}  // namespace tidy
