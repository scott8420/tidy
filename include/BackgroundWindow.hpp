#pragma once
// BackgroundWindow (s012b) -- the background scan: a systemd user timer that
// runs `tidy --scan --notify` every N minutes; a notification at N files
// needing attention. Turn on / Turn off / Save show their steps first (the
// door's, the in-place rewrite, the systemctl lines), then Do it. On a
// FIXTURE home systemctl is never run -- the window says what it would run.
// GTK here; the rules are core::background and core::due.
#include "core/Background.hpp"
#include "core/Settings.hpp"

#include <gtkmm/window.h>

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <string>

namespace Gtk { class Label; class ListBox; class Button; class SpinButton; }

namespace tidy {

class BackgroundWindow : public Gtk::Window {
public:
    using Changed = std::function<void(const core::settings::Settings&)>;   // settings saved / a run: Activity refreshes
    BackgroundWindow(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir,
                     std::string unit_dir, bool fixture, Changed changed);
    ~BackgroundWindow() override;

    void open(const core::settings::Settings& current);

private:
    void refresh();                                   // state + last look
    bool save();                                      // N + minutes -> settings.ini (core::own)
    void preview(core::background::Work work);
    void on_run();
    std::string run_command(const std::vector<std::string>& argv, bool* ok);   // "" or what it said

    latch::door::Door& m_door;
    std::string m_home, m_data_dir, m_unit_dir;
    bool m_fixture = false;
    Changed m_changed;
    core::settings::Settings m_settings;
    std::optional<core::background::Work> m_work;   // shown, not yet run
    Gtk::Label*      m_state = nullptr;
    Gtk::Label*      m_last = nullptr;
    Gtk::SpinButton* m_minutes = nullptr;
    Gtk::SpinButton* m_notify = nullptr;
    Gtk::ListBox*    m_list = nullptr;
    Gtk::Label*      m_status = nullptr;
    Gtk::Button*     m_on = nullptr;
    Gtk::Button*     m_off = nullptr;
    Gtk::Button*     m_run = nullptr;
};

}  // namespace tidy
