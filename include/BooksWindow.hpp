#pragma once
// BooksWindow (s024b) -- make folders books. A folder (one book) or a folder
// of books (each sub-folder: the Nancy Drew series) -> each gets a title
// (guessed: "02 The Hidden Staircase" -> "The Hidden Staircase"; edit it) and
// the author (one for all; guessed from a "Last, First" parent) -> the plan:
// each moved WHOLE to ~/Documents/Bookbinding/<Author>/<Title>. The window is
// the preview; Do it under Busy, one undo step in Activity. Then each book's
// Layout... (Projects) puts its files in the stages. Rules: core::layout.
#include <gtkmm/window.h>

#include "core/Layout.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; class CheckButton; class Entry; }

namespace tidy {

class BooksWindow : public Gtk::Window {
public:
    using Changed = std::function<void()>;
    BooksWindow(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir, Changed changed);
    ~BooksWindow() override;

    // A folder picked elsewhere (the Map), or "" (pick one here). `each`: its sub-folders are the books.
    void open(const std::string& folder, bool each);

private:
    void on_pick();
    void fill();        // the rows, from the folder
    void replan();      // the plan, from the rows
    void on_run();

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    Changed m_changed;
    std::string m_folder;
    std::optional<core::templates::Template> m_t;
    std::vector<core::layout::Book> m_books;
    std::vector<Gtk::Entry*> m_titles;
    std::optional<core::layout::Made> m_made;
    bool m_busy = false;

    Gtk::Label* m_where = nullptr;
    Gtk::CheckButton* m_each = nullptr;
    Gtk::Entry* m_author = nullptr;
    Gtk::Label* m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Button* m_run = nullptr;
};

}  // namespace tidy
