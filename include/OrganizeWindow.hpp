#pragma once
// OrganizeWindow (s024e) -- THE one way to put a folder in order (Scott: "a
// simple way to select a folder and say organize this as a [type], then it
// tries its best to organize and move to its location"; "the organization
// template must be set first"). Type first -- nothing below works until one is
// picked (New type... opens the builder). Then: what the folder is (guessed,
// switchable), the names (guessed, editable), "Lay out each project too", the
// summary as the preview -> Do it under Busy, one undo step. Leftovers in a
// container -> the Closet (a catch-all, saved by the app). Rules: core::organize.
#include <gtkmm/window.h>

#include "core/Organize.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; class CheckButton; class DropDown; class Entry; class Box; }

namespace tidy {

class OrganizeWindow : public Gtk::Window {
public:
    using Changed = std::function<void()>;
    using SaveCloset = std::function<std::string(const std::vector<std::string>& rels)>;   // "" = saved
    using NewType = std::function<void()>;                                                 // -> the Templates builder
    OrganizeWindow(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir,
                   SaveCloset save, NewType new_type, Changed changed);
    ~OrganizeWindow() override;

    // A folder (absolute; "" = pick one here), and the type when it is known (a project's own).
    void open(const std::string& folder, const std::string& type = "");

private:
    void fill_types(const std::string& want);
    void on_type();
    void on_pick();
    void fill();        // the groups and rows, from the folder + type + shape
    void replan();
    void on_run();

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    SaveCloset m_save;
    NewType m_new_type;
    Changed m_changed;
    std::string m_folder;
    std::vector<core::templates::Template> m_types;
    std::optional<core::templates::Template> m_t;
    core::organize::Shape m_shape = core::organize::Shape::One;
    std::vector<core::organize::Group> m_groups;
    std::optional<core::organize::Organized> m_plan;
    bool m_busy = false, m_quiet = false;

    Gtk::DropDown* m_type = nullptr;
    Gtk::Label* m_where = nullptr;
    Gtk::Box* m_below = nullptr;            // everything after the type: off until one is picked
    Gtk::CheckButton* m_one = nullptr;
    Gtk::CheckButton* m_many = nullptr;
    Gtk::CheckButton* m_groupsb = nullptr;
    Gtk::CheckButton* m_lay = nullptr;
    Gtk::Label* m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Button* m_run = nullptr;
};

}  // namespace tidy
