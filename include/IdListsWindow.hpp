#pragma once
// IdListsWindow (s010f2) -- the CVS-style id lists: Look (scan the home,
// read-only) -> a preview of every step with the door's refusals -> Do it ->
// undo in Activity. GTK here; the rules are core::filelists.
#include <gtkmm/window.h>

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <string>

namespace Gtk { class Label; class ListBox; class Button; }

namespace tidy {

class IdListsWindow : public Gtk::Window {
public:
    using Changed = std::function<void()>;   // after a run: Activity refreshes
    IdListsWindow(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir, Changed changed);
    ~IdListsWindow() override;

    void open();                             // present + Look

private:
    void on_look();
    void on_run();

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    Changed m_changed;
    std::optional<latch::door::Plan> m_plan;
    Gtk::Label*   m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label*   m_status = nullptr;
    Gtk::Button*  m_run = nullptr;
};

}  // namespace tidy
