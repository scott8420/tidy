#include "Shell.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Catalog.hpp"
#include "core/Tagging.hpp"

#include <gtkmm/button.h>
#include <gtkmm/label.h>

#include <ctime>

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

// Undo the newest batch through the door, then bring Tidy's catalog in step
// with what the files carry now (core::tagging::record).
void Shell::on_activity_undo() {  // handler: Activity's act button
    if (!m_door->can_undo()) return;
    std::uint64_t b = 0;
    for (auto it = m_door->batches().rbegin(); it != m_door->batches().rend(); ++it)
        if (!it->undone) { b = it->id; break; }
    const std::string label = m_door->undo_label();
    const auto r = m_door->undo();
    core::catalog::Catalog cat;
    std::size_t synced = 0;
    if (cat.open(m_data_dir, true))
        synced = core::tagging::after_undo(cat, *m_door, b, static_cast<std::int64_t>(std::time(nullptr)));
    refresh_activity();
    m_act_status->set_text(r.ok ? "Undone: " + label + " (" + std::to_string(r.done) + " steps)"
                                : "Undo of " + label + ": " + std::to_string(r.done) + " back, " +
                                      std::to_string(r.failed) + " left as they are -- " +
                                      (r.messages.empty() ? std::string() : r.messages.front()));
    if (auto lg = log::get(log::Area::Shell))
        lg->info("undo '{}': {} done, {} failed; catalog {} synced", label, r.done, r.failed, synced);
}

void Shell::on_dump_registry() {  // handler: print the live widget tree (also callable from gdb)
    registry::dump();
}

}  // namespace tidy
