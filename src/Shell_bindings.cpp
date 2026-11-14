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
    add_action("quarantine",    sigc::mem_fun(*this, &Shell::on_quarantine));   // s025b
    add_action("adopt",         sigc::mem_fun(*this, &Shell::on_adopt));   // s023
    add_action("organize",      [this] { on_organize(); });                // s024e
    add_action("ebooks",        [this] { on_ebooks(); });                  // s028
    add_action("background",    sigc::mem_fun(*this, &Shell::on_background));
    add_action("shortcuts",     sigc::mem_fun(*this, &Shell::on_shortcuts));
    add_action("dump-registry", sigc::mem_fun(*this, &Shell::on_dump_registry));
    add_action("quit",          [this]() { close(); });
    // s030a: Expert mode -- a check in the menu, kept in Tidy's own mode.txt.
    m_expert_act = add_action_bool("expert", [this] { set_expert(!m_expert); }, m_expert);
    add_action("put", [this] { on_put(); });   // s030g
}

}  // namespace tidy
