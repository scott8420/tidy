#pragma once
#include "core/Settings.hpp"

#include <gtkmm/window.h>

#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// SettingsWindow (s006a) -- what the user sets: the Closet's aging days, and
// the junk list (file-name patterns, one per line: *.png, Screenshot*).
// Hide-on-close singleton, as AboutWindow. show(current) fills it; Save hands
// the new settings to `on_save`, which writes them (core::own, Shell) and says
// "" or why not. GTK here; the rules (parse, clamp, patterns) are core::settings.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class SpinButton; class TextView; class Label; }

namespace tidy {

class SettingsWindow : public Gtk::Window {
public:
    using SaveFn = std::function<std::string(const core::settings::Settings&)>;
    SettingsWindow(Gtk::Window& parent, SaveFn on_save);
    ~SettingsWindow() override;

    void show(const core::settings::Settings& current);

private:
    void on_save_clicked();

    SaveFn           m_save;
    Gtk::SpinButton* m_days = nullptr;
    Gtk::TextView*   m_junk = nullptr;
    Gtk::Label*      m_status = nullptr;
};

}  // namespace tidy
