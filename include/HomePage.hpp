#pragma once
#include "core/Birth.hpp"
#include "core/Homing.hpp"
#include "core/Projects.hpp"
#include "core/Templates.hpp"

#include <gtkmm/stack.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <latch/Door.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// HomePage (s011 on the road) -- Files home: each decided file in the landing
// places to its folder by type (a SUBFOLDER of it: Pictures/Covers), or into
// the project it serves (the template's route picks the folder). GTK here; the
// rules are core::homing. Moves go through latch's door only, previewed; a
// taken name is warned and substituted "name (2)". Undo in Activity.
//
// States: start (hero + Look) | results (one row per file) | preview.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Label; class ListBox; class Button; class Entry; class DropDown; class CheckButton; }

namespace tidy {

class HomePage : public Gtk::Stack {
public:
    using Changed = std::function<void()>;
    using Text = std::function<std::string()>;
    HomePage(latch::door::Door& door, std::string home, std::string data_dir, bool fixture,
             Text user_dirs, Changed changed);
    void refresh_if_shown();

private:
    struct Row {
        core::birth::Born born;
        std::string type_folder;               // "Pictures"; "" = no type for it
        std::string type_pattern;              // "*.png"
        Gtk::CheckButton* check = nullptr;
        Gtk::DropDown* to = nullptr;           // "--" | "by type: ~/Pictures" | each project
        Gtk::Entry* folder = nullptr;          // the subfolder (by type) / the folder in the project
        Gtk::DropDown* existing = nullptr;     // what is there already -> fills `folder`
        std::vector<std::string> existing_names;
    };

    void build_start();
    void build_results();
    void build_preview();
    void fill();
    void on_to_changed(std::size_t row);
    // What the row's "to" dropdown means: -1 none, -2 by type, else an index into m_projects.
    int target_of(const Row& r) const;
    void on_go();
    void on_run();
    std::string shown(const std::string& path) const;

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    bool m_fixture = false;
    Text m_user_dirs;
    Changed m_changed;

    std::vector<core::homing::TypeRoute> m_table;
    std::vector<core::projects::Project> m_projects;
    std::vector<core::templates::Template> m_templates;
    std::vector<Row> m_rows;
    std::optional<latch::door::Plan> m_plan;

    Gtk::Label*    m_summary = nullptr;
    Gtk::ListBox*  m_list = nullptr;
    Gtk::Label*    m_status = nullptr;
    Gtk::Label*    m_prev_summary = nullptr;
    Gtk::ListBox*  m_prev_list = nullptr;
    Gtk::Label*    m_prev_status = nullptr;
    Gtk::Button*   m_prev_run = nullptr;
};

}  // namespace tidy
