#pragma once
// EbooksWindow (s028) -- loose ebook files -> books on a shelf, one title
// folder each: ~/Entertainment/EBooks/<Genre>/<Last, First>/<Title>/. Tidy
// makes the shelf's TYPE itself (ebook-<genre>, saved in Types for reuse) and
// uses it. Folder... -> Look (core::ebooks::find under Busy: each file's own
// title + author, else its name) -> a row per file, every guess editable
// (genre, author, title; a .pdf starts unticked) -> Preview (the moves, the
// types it will save, refusals) -> Do it: the types saved, then the moves
// through the door -- one undo step in Activity.
#include <gtkmm/window.h>

#include "core/Ebooks.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; class Entry; class Stack; class CheckButton; }

namespace tidy {

class EbooksWindow : public Gtk::Window {
public:
    using Changed = std::function<void()>;
    EbooksWindow(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir, Changed changed);
    ~EbooksWindow() override;

    void open(const std::string& folder);   // abs; "" = pick one

private:
    struct Line {
        core::ebooks::Row row;
        Gtk::CheckButton* pick = nullptr;
        Gtk::Entry *genre = nullptr, *author = nullptr, *title = nullptr;
        Gtk::Label* to = nullptr;
    };
    void on_pick();
    void look();
    void show();
    void read_rows();
    void update();
    void preview();
    void run();
    std::vector<std::string> new_types() const;   // the ebook-<genre> types not saved yet

    Gtk::Window& m_parent;
    latch::door::Door& m_door;
    std::string m_home, m_data_dir, m_folder;
    Changed m_changed;
    std::vector<Line> m_lines;
    std::optional<latch::door::Plan> m_plan;

    Gtk::Stack*   m_states = nullptr;      // "rows" | "preview"
    Gtk::Label*   m_where = nullptr;
    Gtk::Entry*   m_genre = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label*   m_summary = nullptr;
    Gtk::Label*   m_status = nullptr;
    Gtk::Button*  m_go = nullptr;
    Gtk::ListBox* m_prev = nullptr;
    Gtk::Label*   m_prev_summary = nullptr;
    Gtk::Button*  m_run = nullptr;
};

}  // namespace tidy
