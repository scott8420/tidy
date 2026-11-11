#pragma once
#include "OffThread.hpp"
#include "core/Birth.hpp"
#include "core/Homing.hpp"
#include "core/Projects.hpp"
#include "core/Routes.hpp"
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
// s018c: the files are a SelectList (select like Nautilus); ONE destination per
// selection (Scott, 2026-11-11): "Folder [..]  Set to [By type | Into <project>]"
// -> the preview at once. The folder box: by type, the subfolder (required);
// into a project, a folder in it ("" = the template's route).
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Label; class ListBox; class Button; class Entry; class DropDown; class CheckButton; }
namespace tidy { class SelectList; }

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
        core::routes::Proposal learned;        // s022: where you sent its kind before ("" = none)
    };

    void build_start();
    void build_results();
    void build_preview();
    void fill();                           // s019: the sweep off the window's thread -> show()
    struct Look {
        std::vector<core::homing::TypeRoute> table;
        std::vector<core::projects::Project> projects;
        std::vector<core::templates::Template> templates;
        std::vector<core::birth::Born> offered;
        std::vector<std::string> tops;
        core::routes::Book book;           // s022
    };
    void show(Look look);
    void on_set(std::size_t choice);       // [Its proposal] | by type | ~/top x T | Archive | each project
    void on_selection();                   // the "there:" list follows the selection
    void propose(const std::vector<std::size_t>& sel);   // s022: Set to Its proposal
    void archive(const std::vector<std::size_t>& sel);   // s018d: Set to Archive -> core::archive's plan
    void show_preview(latch::door::Plan plan, const std::vector<std::string>& renamed,
                      const std::vector<std::pair<std::string, std::string>>& not_planned, bool archiving);
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
    bool m_archiving = false;                 // s018d: the plan shown is core::archive's (record at Do it)
    std::vector<std::string> m_tops;          // s018d: home's top folders, as in Set to after "By type"
    OffThread<Look> m_look;                   // s019
    core::routes::Book m_book;                // s022: the learned routes, as read at the last look
    std::size_t m_offer = 0;                  // s022: 1 when Set to opens with "Its proposal"
    struct Teach { core::birth::Born born; std::string folder; core::routes::Proposal proposed; };
    std::vector<Teach> m_teach;               // s022: learned at Do it, for the files that moved

    Gtk::Label*    m_summary = nullptr;
    SelectList*    m_list = nullptr;          // s018c
    Gtk::Entry*    m_folder = nullptr;        // the subfolder / the folder in the project
    Gtk::DropDown* m_there = nullptr;         // folders already there -> fills m_folder
    std::vector<std::string> m_there_names;
    Gtk::Label*    m_status = nullptr;
    Gtk::Label*    m_prev_summary = nullptr;
    Gtk::ListBox*  m_prev_list = nullptr;
    Gtk::Label*    m_prev_status = nullptr;
    Gtk::Button*   m_prev_run = nullptr;
};

}  // namespace tidy
