#pragma once
#include <functional>
#include "core/Templates.hpp"

#include <gtkmm/stack.h>

#include <map>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// TemplatesPage (s007) -- the Templates page: the saved templates, and the
// tree builder. GTK here; the rules (names, depth, roles, routes, lift, drift,
// the builder's undo) are core::templates. Writes only through
// core::templates::save -> core::own (<data>/templates/). Touches no user file:
// lift and the drift check READ a project's folders, two deep.
//
// States (a Gtk::Stack, as the Closet):
//   start    -- no templates yet: the hero, New template, Lift from a project
//   list     -- the saved templates: Open, Drift check; New, Lift
//   builder  -- one draft: name, category, folders (role each; a lifted folder
//               carries a Keep check until kept or removed), routes, the drift
//               found; Undo | Back | Drift check | Save
// Undo of a template edit lives here (Scott, s006a), not in Activity.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Popover; }
namespace Gtk { class Window; class Label; class ListBox; class Entry; class Button; class DropDown; class ScrolledWindow; class CheckButton; }

namespace tidy {

class TemplatesPage : public Gtk::Stack {
public:
    TemplatesPage(Gtk::Window& parent, std::string home, std::string data_dir, bool fixture);
    ~TemplatesPage() override;   // s024d: the tree's popover is unparented
    void start_new();   // s024e: Organize as -> New type... -> a new template in the builder
    void refresh();   // s023i: a template saved elsewhere -- the list again (not while the builder is open)

private:
    void build_start();
    void build_list();
    void build_builder();
    void build_starters();                           // s010a: the built-in templates
    void show_starters();

    void show_home();                                // start or list, from what is saved
    void open(core::templates::Builder b);           // -> builder
    void refresh_builder();
    void refresh_head();                             // title lines + Undo / Save
    void on_keep(const std::string& path);           // a lifted folder kept, in place
    void edit(const std::string& err);               // after an edit: status + refresh

    void on_new();
    void on_lift();
    void on_drift_pick();
    void run_drift(const std::string& dir);
    void on_save();
    void on_back();
    void on_undo();
    void pick_folder(const std::string& title, std::function<void(std::string)> then);
    std::string shown(const std::string& path) const;   // ~ for the home

    Gtk::Window& m_parent;
    std::string  m_home, m_data_dir;
    bool         m_fixture = false;

    std::vector<core::templates::Template> m_saved;
    core::templates::Builder m_b;
    std::vector<core::templates::Drift> m_drift;
    std::string m_drift_dir;
    std::map<std::string, std::pair<Gtk::CheckButton*, Gtk::Label*>> m_keep_rows;
    bool m_back_armed = false;                       // Back with unsaved edits: twice

    Gtk::Label*   m_list_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label*   m_list_status = nullptr;

    Gtk::Label*   m_st_summary = nullptr;          // starters (s010a)
    Gtk::ListBox* m_st_list = nullptr;

    Gtk::ScrolledWindow* m_b_scroll = nullptr;   // its place kept across a refresh
    Gtk::Label*    m_b_summary = nullptr;
    Gtk::Entry*    m_b_name = nullptr;
    Gtk::Entry*    m_b_category = nullptr;
    Gtk::CheckButton* m_b_by_place = nullptr;    // s024: projects known by their place (no -project)
    Gtk::Entry*    m_b_group = nullptr;          // s024: the middle level's label (Author (Last, First))
    bool           m_b_quiet = false;            // s024: refresh sets the check without an edit
    // s024d: the template as a tree (Scott: "a template needs to look just like a
    // finder structure. Then in here goes this.") -- a row per folder, its
    // "goes here" chips on it; right-click or "..." for New folder / Rename /
    // This goes here... / Remove; chips drag between folders; a file dropped
    // from Files adds its type there.
    Gtk::ListBox*  m_b_folders = nullptr;
    Gtk::Popover*  m_tree_pop = nullptr;          // one, on the list: rows are rebuilt on every edit
    void fill_tree();
    void tree_menu(Gtk::Widget& at, const std::string& folder);   // folder: a path, kTop or kStays
    void chip_menu(Gtk::Widget& at, const std::string& pattern);
    void tree_ask(Gtk::Widget& at, const std::string& prompt, const std::string& initial,
                  std::function<std::string(const std::string&)> ok);
    void pop_at(Gtk::Widget& at);
    bool drop_on(const std::string& folder, const GValue* v);
    Gtk::Label*    m_b_drift_head = nullptr;
    Gtk::ListBox*  m_b_drift = nullptr;
    Gtk::Label*    m_b_status = nullptr;
    Gtk::Button*   m_b_undo = nullptr;
    Gtk::Button*   m_b_save = nullptr;
};

}  // namespace tidy
