#pragma once
#include "widgets/Widgets.hpp"
#include "core/Look.hpp"
#include "core/Birth.hpp"
#include "core/Settings.hpp"

#include <gtkmm/applicationwindow.h>
#include <gtkmm/label.h>
#include <gtkmm/revealer.h>
#include <giomm/menu.h>

#include <latch/Door.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Shell -- Tidy's window. A SPLIT CLASS across TUs by category of code (Cairn;
// CANON "the header is the index"): every method's trailing tag names the
// file its body lives in.
//
//   Zones    -- construction: window, header, sidebar, pages, menu.  Shell_zones.cpp
//   Bindings -- what's wired to what: win.* actions.                 Shell_bindings.cpp
//   Handlers -- slot bodies (on_*).                                  Shell_handlers.cpp
//   Helpers  -- shared orchestration.                                Shell_helpers.cpp
//   Glue     -- the ctor + build_ui().                               Shell.cpp
//
// Finding things
//   "Where is the sidebar built?"      -> Shell_zones.cpp    (build_nav; the
//                                         StackSidebar when the look is off)
//   "What tints the window per page?"  -> Shell_handlers.cpp (on_nav)
//   "Where is a page built?"           -> Shell_zones.cpp    (build_page, from
//                                         core::pages -- the spine's heroes)
//   "Where does latch get Tidy's name?"-> Shell.cpp          (build_ui)
//   "Where is Activity built / undone?"-> Shell_zones.cpp    (build_activity),
//                                         Shell_handlers.cpp (on_activity_undo),
//                                         Shell_helpers.cpp  (refresh_activity)
//   "Which home?"                      -> Shell.cpp          (TIDY_HOME = a fixture)
//   "Where is the birth report shown?" -> Shell_zones.cpp    (build_closet),
//                                         Shell_handlers.cpp (on_closet_look),
//                                         Shell_helpers.cpp  (fill_closet) -- s005
//   "Where are the settings?"         -> Shell_handlers.cpp (on_settings), SettingsWindow,
//                                         core::settings; written by core::own (s006a)
//   "Where are projects found / moved?"-> ProjectsPage (core::projects; plans through
//                                         m_door, previewed; undone in Activity, s008)
//   "Where are purposes given / closed?"-> PurposesPage (core::purposes: the learned hints;
//                                         tag plans through m_door; the book by core::own, s009)
//   "Where are templates built?"      -> TemplatesPage (its own class; core::templates;
//                                         written by core::own, s007)
//   "Where does the Closet decide?"    -> Shell_handlers.cpp (on_closet_accept /
//                                         _decide / _junk -> show_closet_preview ->
//                                         on_closet_run): core::closet plans,
//                                         previewed, through the door (s006)
//
// s004: every change to a user's file goes through latch's door (m_door); the
// selftest's source gate fails the build's test if any Tidy source calls a
// filesystem or xattr write, or SQLite outside core/Catalog.cpp.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class CheckButton; class DropDown; }

namespace tidy {

class AboutWindow;
class ProjectsPage;
class IdListsWindow;
class BackgroundWindow;
class PurposesPage;
class HomePage;
class DuePage;
class SettingsWindow;
class ShortcutsDialog;

class Shell : public Gtk::ApplicationWindow {
public:
    Shell();
    ~Shell() override;

    void build_ui();                               // category: glue

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void build_shell();                            // category: zone: window + header + body
    void build_nav();                              // category: zone: the look's sidebar -- brand, sections, rows
    void build_pages();                            // category: zone: one page per nav item
    void build_page(const std::string& id);        // category: zone: a page's hero (spine: start state only)
    void build_activity();                         // category: zone: Activity -- the door's batches + Undo (s004)
    void build_closet();                           // category: zone: The Closet -- the birth report (s005, read-only)
    Glib::RefPtr<Gio::Menu> build_menu();          // category: zone: hamburger model

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                           // category: bindings: win.* actions

    // ── handlers ───────────────────────────────────────────────────────────
    void on_nav(const std::string& page);          // category: handler: a sidebar row -> page + tint
    void on_about();                               // category: handler: the About window (hide-on-close)
    void on_settings();                            // category: handler: the Settings window (s006a)
    void arm_rescan();
                                                   // handler: the loose background rescan (s010f1)
    void on_id_lists();                            // handler: the Id lists window (s010f2)
    void on_background();                          // handler: the Background scan window (s012b)
    void offer_lists();                            // handler: s010f3 -- count stale id lists -> the offer bar
    void on_shortcuts();                           // category: handler: the keyboard reference
    void on_dump_registry();                       // category: handler: print the live widget tree
    void on_activity_undo();                       // category: handler: undo the newest batch, through the door
    void on_closet_look();                         // category: handler: run the birth report (reads only)
    void on_closet_accept();                       // category: handler: checked -> their proposals (a plan, previewed)
    void on_closet_decide();                       // category: handler: checked -> the chosen level (previewed)
    void on_closet_junk();                         // category: handler: checked -> the Junk Drawer (previewed)
    void on_closet_run();                          // category: handler: the previewed plan, through the door
    void on_closet_back();                         // category: handler: preview -> the list, nothing done

    // ── helpers ────────────────────────────────────────────────────────────
    std::string page_note(const std::string& id) const;   // category: helper: the line under a page's promise
    void refresh_activity();                       // category: helper: Activity from the door's manifest
    void fill_closet();                            // category: helper: the birth report -> the Closet's list
    std::string user_dirs_text() const;            // category: helper: ~/.config/user-dirs.dirs, read ("" if none)
    std::vector<core::birth::Born> closet_checked() const;   // category: helper: the checked rows
    void show_closet_preview(latch::door::Plan plan);  // category: helper: the plan's steps + refusals -> preview state

    // Titlebar: brand logo (start) + hamburger (end).
    widgets::Button     m_logo_button;
    widgets::MenuButton m_menu_button;

    // Body: sidebar | stack.
    widgets::Box          m_body;      // horizontal
    widgets::Box          m_side;      // the look's sidebar pane (vertical)
    widgets::ListBox      m_nav;
    widgets::StackSidebar m_sidebar;   // the look off: GTK's own
    widgets::Stack        m_stack;
    std::vector<core::look::NavEntry> m_nav_model;
    std::string           m_tint;      // the tint on the window now ("" = none)

    // Facts the pages show.
    std::string m_home;
    std::string m_data_dir;
    bool        m_fixture = false;     // TIDY_HOME: a fixture home, not the real one

    // s004: the one door (Tidy's manifest in m_data_dir), and Activity's widgets.
    std::unique_ptr<latch::door::Door> m_door;
    Gtk::Stack*   m_act_states = nullptr;   // "start" (nothing yet) | "results"
    Gtk::ListBox* m_act_list = nullptr;
    Gtk::Label*   m_act_summary = nullptr;
    Gtk::Button*  m_act_undo = nullptr;
    Gtk::Label*   m_act_status = nullptr;

    // s005: The Closet's birth report (read-only).
    Gtk::Stack*   m_clo_states = nullptr;   // "start" | "results"
    Gtk::ListBox* m_clo_list = nullptr;
    Gtk::Label*   m_clo_summary = nullptr;
    Gtk::Label*   m_clo_status = nullptr;
    // s006: the Closet decides -- checked rows, a level to decide, the preview.
    std::vector<std::pair<Gtk::CheckButton*, core::birth::Born>> m_clo_rows;
    Gtk::DropDown* m_clo_level = nullptr;
    Gtk::Label*    m_clo_prev_summary = nullptr;
    Gtk::ListBox*  m_clo_prev_list = nullptr;
    Gtk::Label*    m_clo_prev_status = nullptr;
    Gtk::Button*   m_clo_prev_run = nullptr;
    std::optional<latch::door::Plan> m_clo_plan;   // shown, not yet run

    ProjectsPage* m_projects = nullptr;           // s008 (managed by the stack)
    PurposesPage* m_purposes = nullptr;           // s009 (managed by the stack)
    HomePage*     m_home_page = nullptr;
    DuePage*      m_due = nullptr;                // s012a the trigger (managed by the stack)          // s011 files home (managed by the stack)

    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<SettingsWindow>  m_settings_win;
    std::unique_ptr<IdListsWindow>   m_idlists_win;   // s010f2
    std::unique_ptr<BackgroundWindow> m_background_win;   // s012b
    core::settings::Settings         m_settings;   // read at build_ui; written only by core::own
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
    sigc::connection                 m_rescan;     // s010f1: every rescan_minutes; 0 = none
    // s010f3: the rescan offers "N id lists out of date" -> Id lists...
    Gtk::Revealer* m_offer = nullptr;
    Gtk::Label*    m_offer_text = nullptr;
    std::string    m_offer_dismissed;              // the line last dismissed -- shown again when it changes
};

}  // namespace tidy
