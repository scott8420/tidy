#pragma once
// AuthorsWindow (s024c) -- Scott: "just select the author and say bookbinding
// projects". An author's folder (or a folder of authors: each sub-folder one)
// -> a row each: ticked, the author's name (the folder's; "First Last" offered
// flipped), its books. "Lay out each book" (on): its files by the routes, in
// place, before the move. Do it under Busy: the books move whole onto the
// shelf, one undo step. What is left in an old author folder goes to the
// Closet (a catch-all, saved in Settings); left empty, the folder goes to the
// Trash. Rules: core::layout.
#include <gtkmm/window.h>

#include "core/Layout.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; class CheckButton; class Entry; }

namespace tidy {

class AuthorsWindow : public Gtk::Window {
public:
    using Changed = std::function<void()>;
    // Saves the old author folders with loose things left as catch-alls ("" = saved, else why not).
    using SaveCloset = std::function<std::string(const std::vector<std::string>& rels)>;
    AuthorsWindow(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir,
                  SaveCloset save, Changed changed);
    ~AuthorsWindow() override;

    // `folder` is an author's (each = false), or holds authors (each = true); "" = pick one here.
    void open(const std::string& folder, bool each);

private:
    struct Row {
        core::layout::Author a;
        bool on = true;
        std::size_t books = 0;
        Gtk::Entry* name = nullptr;
    };
    void on_pick();
    void fill();
    void replan();
    void on_run();

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    SaveCloset m_save;
    Changed m_changed;
    std::string m_folder;
    std::optional<core::templates::Template> m_t;
    std::vector<Row> m_rows;
    std::optional<core::layout::Authors> m_plan;
    bool m_busy = false;

    Gtk::Label* m_where = nullptr;
    Gtk::CheckButton* m_each = nullptr;
    Gtk::CheckButton* m_lay = nullptr;
    Gtk::Label* m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Button* m_run = nullptr;
};

}  // namespace tidy
