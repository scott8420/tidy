#include "core/Backup.hpp"
#include "Shell.hpp"
#include "AboutWindow.hpp"      // complete types for the unique_ptr dtors
#include "ShortcutsDialog.hpp"
#include "SettingsWindow.hpp"
#include "IdListsWindow.hpp"
#include "BackgroundWindow.hpp"
#include "ProjectsPage.hpp"
#include "core/Own.hpp"
#include "Log.hpp"
#include "core/Paths.hpp"
#include "core/FileLists.hpp"

#include <latch/Door.hpp>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#include <cstdlib>
#include <filesystem>

// Shell.cpp -- GLUE. The ctor and the ordered build_ui() pass; every other
// method body lives in a category file (see the index in Shell.hpp).

namespace tidy {

Shell::Shell()
    : m_logo_button("shell.logo_button"),
      m_menu_button("shell.menu_button"),
      m_body("shell.body", Gtk::Orientation::HORIZONTAL),
      m_side("shell.side", Gtk::Orientation::VERTICAL),
      m_nav("shell.nav"),
      m_sidebar("shell.sidebar"),
      m_stack("shell.stack") {
    set_name("shell.window");
}

Shell::~Shell() = default;

void Shell::build_ui() {
    // latch first: Tidy's name in every refusal and staging name ("Tidy's own
    // records", .tidy-...). Tidy has no root helper, so it hands in no root
    // rules -- every Root step is refused. (REQUIREMENTS §11.7.)
    latch::door::set_app_name("Tidy");
    core::filelists::register_own();   // s010f3: .tidy-files / .tidy-project may be rewritten in place

    // Which home. TIDY_HOME (an absolute folder) is a FIXTURE home -- e.g. one
    // made by `tidy_selftest --fixture DIR` -- and then Tidy's records live
    // inside it (XDG_DATA_HOME ignored), so nothing reaches the real home.
    const char* fixture = std::getenv("TIDY_HOME");
    std::error_code ec;
    if (fixture && fixture[0] == '/' && std::filesystem::is_directory(fixture, ec)) {
        m_home = std::filesystem::weakly_canonical(fixture, ec).string();
        m_fixture = true;
        m_data_dir = core::paths::data_dir(m_home, nullptr);
    } else {
        m_home = Glib::get_home_dir();
        m_data_dir = core::paths::data_dir(m_home, std::getenv("XDG_DATA_HOME"));
    }
    // The door reads its manifest (creates nothing until it runs a plan).
    m_door = std::make_unique<latch::door::Door>(m_home, m_data_dir);
    m_door->load();
    m_settings = core::settings::parse(core::own::read(m_data_dir, core::settings::kFile));   // absent: the defaults
    core::backup::register_media(m_settings.media);   // s013b: the door may copy onto these (latch 0.4)

    build_shell();     // zone
    build_pages();     // zone (+ the look's sidebar over them)
    bind_actions();    // bindings

    // s010f1: once at launch (when the loop is idle), then every N minutes.
    if (m_projects && m_settings.rescan_minutes > 0)
        Glib::signal_idle().connect_once([this] {
            const auto line = m_projects->rescan();
            if (auto lg = log::get(log::Area::Shell)) lg->info("rescan at launch: {}", line);
            offer_lists();
        });
    arm_rescan();

    if (auto lg = log::get(log::Area::Shell))
        lg->info("ready -- latch says '{}'; home {}{}; records at {}; {} batches",
                 latch::door::app_name(), m_home, m_fixture ? " (FIXTURE)" : "", m_data_dir,
                 m_door->batches().size());
}

}  // namespace tidy
