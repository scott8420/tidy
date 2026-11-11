#pragma once
#include <gtkmm/box.h>
#include <giomm/liststore.h>

#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// SelectList (s018) -- a file list you select like Nautilus: click, Shift+click
// a range, Ctrl+click to toggle, Ctrl+A, drag a box (rubber band). A
// Gtk::ListView over a MultiSelection; rows are plain lines (no checkboxes).
//
// The setter (setter(), placed by the page in its act bar) reads
// "5 selected   Set to [ ... ]": picking a choice calls on_choose(i) at once --
// no Set button -- and the dropdown goes back to "choose...". Right-click on
// the rows offers the same choices as a menu. The page's on_choose builds a
// plan and shows its preview: nothing moves unseen.
//
// A row may carry a section heading (header) drawn above it, inside the row
// (gtkmm 4.10 has no ListView section headers).
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class ListView; class MultiSelection; class Label; class DropDown; class PopoverMenu; }

namespace tidy {

class SelectList : public Gtk::Box {
public:
    struct Item {
        std::string header;       // a section heading above this row ("" = none)
        std::string header_sub;   // its dim line
        std::string title;        // the name
        std::string line;         // the dim line: where, why, ...
        std::string tooltip;
    };
    using Choose = std::function<void(std::size_t choice)>;
    using Changed = std::function<void(std::size_t selected)>;

    explicit SelectList(const std::string& name);
    ~SelectList() override;

    void set_items(const std::vector<Item>& items);
    void set_choices(const std::vector<std::string>& choices);   // "Set to" entries, in order
    void on_choose(Choose f) { m_choose = std::move(f); }
    void on_changed(Changed f) { m_changed = std::move(f); }

    void select(const std::vector<std::size_t>& rows);   // exactly these
    void select_all();
    std::vector<std::size_t> selected() const;            // row indexes, in order
    std::size_t size() const;

    Gtk::Box& setter() { return *m_setter; }               // "N selected  Set to [..]" -- the page places it

    // For a drive / the selftest: what picking choice i would do.
    void choose(std::size_t i);

private:
    void selection_changed();
    void popup_at(double x, double y);

    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_store;
    Glib::RefPtr<Gtk::MultiSelection> m_sel;
    Gtk::ListView* m_view = nullptr;
    Gtk::Box* m_setter = nullptr;
    Gtk::Label* m_count = nullptr;
    Gtk::DropDown* m_to = nullptr;
    Gtk::PopoverMenu* m_menu = nullptr;
    std::vector<std::string> m_choices;
    bool m_resetting = false;
    Choose m_choose;
    Changed m_changed;
};

}  // namespace tidy
