#pragma once
// LayoutWindow (s024a) -- put ONE old book in the layout (Scott: "just do your
// best"). Look (under Busy, read-only) -> rows: what no route fits FIRST
// (asked -- "leave it" until you pick), then each proposal, files of one
// folder sent by one route folded into one row (220 scans = one line). Every
// row has a folder drop-down: change any guess. The window is the preview:
// new folders, moves, the folders left empty -> the Trash. Do it under Busy,
// one undo step in Activity. Rules: core::layout.
#include <gtkmm/window.h>

#include "core/Layout.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; }

namespace tidy {

class LayoutWindow : public Gtk::Window {
public:
    using Changed = std::function<void()>;
    LayoutWindow(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir, Changed changed);
    ~LayoutWindow() override;

    // A book (absolute) and its template: present, Look at once.
    void open(const std::string& book, const core::templates::Template& t);

private:
    struct Group {
        std::vector<std::size_t> rows;   // indexes into m_look.rows
        std::string label;               // "A0001.png .. A0220.png (220 files)"
        std::string why;
        bool asked = false;
    };
    void on_look();
    void show();
    void replan();
    void on_run();

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    Changed m_changed;
    std::string m_book;
    core::templates::Template m_t;
    core::layout::Look m_look;
    std::vector<Group> m_groups;
    std::optional<core::layout::Planned> m_plan;
    bool m_busy = false;

    Gtk::Label* m_title = nullptr;
    Gtk::Label* m_summary = nullptr;
    Gtk::Label* m_extra = nullptr;       // new folders + the Trash
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Button* m_run = nullptr;
};

}  // namespace tidy
