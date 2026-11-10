#include "Shell.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"

// Shell_handlers.cpp -- HANDLERS. Slot bodies: what a user action does.

namespace tidy {

void Shell::on_nav(const std::string& page) {  // handler: a sidebar row -> page + tint
    std::string tint;
    for (const auto& e : m_nav_model)
        if (!e.section && e.id == page) tint = e.tint;
    if (tint != m_tint) {
        if (!m_tint.empty()) remove_css_class("tint-" + m_tint);
        m_tint = tint;
        if (!m_tint.empty()) add_css_class("tint-" + m_tint);   // look.css: window.tint-<name>
    }
    m_stack.set_visible_child(page);
    if (auto lg = log::get(log::Area::Shell)) lg->debug("nav -> {} (tint {})", page, m_tint);
}

void Shell::on_about() {  // handler: the About window -- a hide-on-close singleton
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

void Shell::on_shortcuts() {  // handler: the keyboard reference (renders from core::shortcut_registry)
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

void Shell::on_dump_registry() {  // handler: print the live widget tree (also callable from gdb)
    registry::dump();
}

}  // namespace tidy
