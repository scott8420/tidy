#pragma once
// IdListsWindow (s010f2) -- the CVS-style id lists: Look (scan the home,
// read-only) -> a preview of every step with the door's refusals -> Do it ->
// undo in Activity. s010f3: lists already there are rewritten in place
// (latch replace_own, no undo, a log line). GTK here; the rules are core::filelists.
#include <gtkmm/window.h>

#include "core/FileLists.hpp"

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
    std::optional<core::filelists::Work> m_work;   // shown, not yet run
    Gtk::Label*   m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label*   m_status = nullptr;
    Gtk::Button*  m_run = nullptr;
};

}  // namespace tidy
