#pragma once
#include <gtkmm/window.h>

#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// HelpWindow (s017, s017a) -- "How to use Tidy": topics on the left, grouped
// (Start here, Every day, Projects, When Tidy says so, The archive, Safety);
// the chosen topic on the right -- what it is, then How to, then a button to
// the page it lives on. Opens on the Introduction. Text: core::help.
// Hide-on-close singleton, non-modal (keep it beside Tidy while you work).
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Box; class ListBox; class ScrolledWindow; }

namespace tidy {

class HelpWindow : public Gtk::Window {
public:
    using Go = std::function<void(const std::string& page)>;
    HelpWindow(Gtk::Window& parent, Go go);
    void show_topic(const std::string& id);

private:
    Go m_go;
    Gtk::ListBox* m_list = nullptr;
    Gtk::ScrolledWindow* m_scroll = nullptr;
    std::vector<std::string> m_row_ids;   // per list row; "" = a group heading
};

}  // namespace tidy
