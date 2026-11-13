#include "core/Backup.hpp"
#include "Shell.hpp"
#include "core/Protection.hpp"
#include "AboutWindow.hpp"
#include "HelpWindow.hpp"      // complete types for the unique_ptr dtors
#include "ShortcutsDialog.hpp"
#include "SettingsWindow.hpp"
#include "IdListsWindow.hpp"
#include "QuarantineWindow.hpp"
#include "AdoptWindow.hpp"
#include "OrganizeWindow.hpp"
#include "BackgroundWindow.hpp"
#include "ProjectsPage.hpp"
#include "core/Own.hpp"
#include "Log.hpp"
#include "core/Paths.hpp"
#include "core/FileLists.hpp"
#include "core/Scan.hpp"
#include "Busy.hpp"
#include <chrono>

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
    core::protection::apply(m_home, core::protection::config_home(m_home), m_settings);   // s019a
    core::projects::apply_places(m_data_dir, m_home);   // s024: shelves (books by their place) + .composix/.quire bundles; s025b the Quarantine

    build_shell();     // zone
    build_pages();     // zone (+ the look's sidebar over them)
    bind_actions();    // bindings

    // s010f1: once at launch (when the loop is idle), then every N minutes.
    // s025b3 (Scott: "open the app then create a bkgnd thread to scan and
    // show a progress dlg"): the window first, then the walks under Busy.
    if (m_projects && m_settings.rescan_minutes > 0)
        Glib::signal_timeout().connect_once([this] { launch_look(); }, 150);   // the window is drawn first
    arm_rescan();

    if (auto lg = log::get(log::Area::Shell))
        lg->info("ready -- latch says '{}'; home {}{}; records at {}; {} batches",
                 latch::door::app_name(), m_home, m_fixture ? " (FIXTURE)" : "", m_data_dir,
                 m_door->batches().size());
}

void Shell::launch_look() {
    struct Out {
        ProjectsPage::Gathered g;
        core::filelists::Stale st;
        long ms_projects = 0, ms_lists = 0;
    };
    auto out = std::make_shared<Out>();
    const std::string home = m_home, data = m_data_dir;
    busy::start(*this, "Tidy -- looking over your home", "Finding your projects, then checking the id lists",
        [out, home, data] {
            const auto t0 = std::chrono::steady_clock::now();
            out->g = ProjectsPage::gather(home, data);
            const auto t1 = std::chrono::steady_clock::now();
            namespace fl = core::filelists;
            out->st = fl::stale(core::scan::walk(home, fl::roots(home)));
            const auto t2 = std::chrono::steady_clock::now();
            out->ms_projects = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
            out->ms_lists = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
        },
        [this, out] {
            const auto line = m_projects->take(std::move(out->g));
            if (auto lg = log::get(log::Area::Shell)) lg->info("rescan at launch: {} ({} ms)", line, out->ms_projects);
            show_offer(out->st.lists, out->st.refound, out->ms_lists);
        });
}

}  // namespace tidy
