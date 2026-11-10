#include "Shell.hpp"
#include "AboutWindow.hpp"      // complete types for the unique_ptr dtors
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "core/Paths.hpp"

#include <latch/Door.hpp>
#include <glibmm/miscutils.h>

#include <cstdlib>

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

    // Where Tidy's records will live. RESOLVED, not created: the spine writes
    // nothing (RULES: read-only until the s004 door is crc'd).
    m_home = Glib::get_home_dir();
    m_data_dir = core::paths::data_dir(m_home, std::getenv("XDG_DATA_HOME"));

    build_shell();     // zone
    build_pages();     // zone (+ the look's sidebar over them)
    bind_actions();    // bindings

    if (auto lg = log::get(log::Area::Shell))
        lg->info("ready -- latch says '{}'; records at {} (not created)",
                 latch::door::app_name(), m_data_dir);
}

}  // namespace tidy
