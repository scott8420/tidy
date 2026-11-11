#include "Shell.hpp"

// Shell_bindings.cpp -- BINDINGS. What's wired to what: the win.* actions.
// Accelerators bind in App::on_startup from core::shortcut_registry(), the
// same list the keyboard reference renders, so the two can't drift.

namespace tidy {

void Shell::bind_actions() {  // bindings: win.* actions
    add_action("help",          sigc::mem_fun(*this, &Shell::on_help));   // s017
    add_action("about",         sigc::mem_fun(*this, &Shell::on_about));
    add_action("settings",      sigc::mem_fun(*this, &Shell::on_settings));
    add_action("id-lists",      sigc::mem_fun(*this, &Shell::on_id_lists));
    add_action("background",    sigc::mem_fun(*this, &Shell::on_background));
    add_action("shortcuts",     sigc::mem_fun(*this, &Shell::on_shortcuts));
    add_action("dump-registry", sigc::mem_fun(*this, &Shell::on_dump_registry));
    add_action("quit",          [this]() { close(); });
}

}  // namespace tidy
