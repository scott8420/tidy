#pragma once
// ConformWindow (s023h) -- put many project folders in order with one
// template: pick the template and a folder (itself, or each sub-folder as a
// project -- ~/Documents/EBooks and its 30 books), Look (under Busy, read-only)
// -> one preview grouped by folder: the moves, what is in place, what no route
// fits (listed; it stays) -> Do it under Busy, one undo step in Activity.
// Optionally each folder renamed "<name>-project" last. Rules: core::conform.
#include <gtkmm/window.h>

#include "core/Conform.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; class DropDown; class CheckButton; }

namespace tidy {

class ConformWindow : public Gtk::Window {
public:
    using Changed = std::function<void()>;
    ConformWindow(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir, Changed changed);
    ~ConformWindow() override;

    void open(const std::string& folder = "");   // present; a folder picked elsewhere (the Map), or the last one
    // s023i: straight from a template made by a scan -- that template, that folder, Look at once.
    void open_with(const std::string& folder, const std::string& tpl, bool each);

private:
    void fill_templates();
    void on_pick();
    void on_look();
    void show(std::vector<core::conform::Project> ps, std::vector<std::string> refused);
    void on_run();
    void stale();

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    Changed m_changed;
    std::vector<core::templates::Template> m_templates;
    std::string m_folder;                       // absolute
    bool m_busy = false;
    std::vector<core::conform::Project> m_projects;
    std::optional<core::conform::Planned> m_plan;

    Gtk::DropDown* m_tpl = nullptr;
    Gtk::Label* m_where = nullptr;
    Gtk::CheckButton* m_each = nullptr;
    Gtk::CheckButton* m_rename = nullptr;
    Gtk::Label* m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Button* m_run = nullptr;
};

}  // namespace tidy
