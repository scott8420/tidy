#pragma once
#include "widgets/Widgets.hpp"
#include "core/Look.hpp"

#include <gtkmm/applicationwindow.h>
#include <giomm/menu.h>

#include <memory>
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
//
// s003, the spine: READ-ONLY. No page acts yet; nothing here writes a file.
// The selftest's source gate fails the build's test if any Tidy source calls a
// filesystem write -- every write, from s004 on, goes through latch's door.
// ─────────────────────────────────────────────────────────────────────────────
namespace tidy {

class AboutWindow;
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
    Glib::RefPtr<Gio::Menu> build_menu();          // category: zone: hamburger model

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                           // category: bindings: win.* actions

    // ── handlers ───────────────────────────────────────────────────────────
    void on_nav(const std::string& page);          // category: handler: a sidebar row -> page + tint
    void on_about();                               // category: handler: the About window (hide-on-close)
    void on_shortcuts();                           // category: handler: the keyboard reference
    void on_dump_registry();                       // category: handler: print the live widget tree

    // ── helpers ────────────────────────────────────────────────────────────
    std::string page_note(const std::string& id) const;   // category: helper: the line under a page's promise

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

    // Facts the pages show (resolved, never created, in the spine).
    std::string m_home;
    std::string m_data_dir;

    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
};

}  // namespace tidy
