#include "Shell.hpp"
#include "ProjectsPage.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "SettingsWindow.hpp"
#include "core/Own.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Catalog.hpp"
#include "core/Tagging.hpp"
#include "core/Closet.hpp"
#include "core/Retention.hpp"

#include <gtkmm/button.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/stack.h>
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

// Settings (s006a): written by core::own, the one writer of Tidy's own files;
// the Closet re-reads with the new aging days and junk list.
void Shell::on_settings() {  // handler: the Settings window
    if (!m_settings_win)
        m_settings_win = std::make_unique<SettingsWindow>(*this, [this](const core::settings::Settings& s) {
            const std::string err = core::own::write(m_data_dir, core::settings::kFile, core::settings::format(s));
            if (!err.empty()) return err;
            m_settings = s;
            if (m_clo_states && m_clo_states->get_visible_child_name() == "results") fill_closet();
    if (m_projects) m_projects->refresh_if_shown();
            if (auto lg = log::get(log::Area::Shell))
                lg->info("settings saved: aging {} days, {} junk patterns", s.aging_days, s.junk.size());
            return std::string();
        });
    m_settings_win->show(m_settings);
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
    if (m_clo_states && m_clo_states->get_visible_child_name() == "results") fill_closet();
    m_act_status->set_text(r.ok ? "Undone: " + label + " (" + std::to_string(r.done) + " steps)"
                                : "Undo of " + label + ": " + std::to_string(r.done) + " back, " +
                                      std::to_string(r.failed) + " left as they are -- " +
                                      (r.messages.empty() ? std::string() : r.messages.front()));
    if (auto lg = log::get(log::Area::Shell))
        lg->info("undo '{}': {} done, {} failed; catalog {} synced", label, r.done, r.failed, synced);
}

// The birth report (s005): reads the landing places -- statx and xattrs, no
// writes -- and shows what each file's birth proposes. REPORT ONLY.
void Shell::on_closet_look() {  // handler: the Closet's Look / Look again
    fill_closet();
    m_clo_states->set_visible_child("results");
}

// The three choices (s006): each builds a plan from the checked rows and
// shows its preview. None of them runs anything.
void Shell::on_closet_accept() {  // handler
    const auto items = closet_checked();
    if (items.empty()) { m_clo_status->set_text("Check some files first."); return; }
    show_closet_preview(core::closet::accept(items));
}

void Shell::on_closet_decide() {  // handler
    const auto items = closet_checked();
    if (items.empty()) { m_clo_status->set_text("Check some files first."); return; }
    std::vector<core::retention::Level> levels;   // the dropdown's order: all but Undecided
    for (const auto& i : core::retention::all())
        if (i.level != core::retention::Level::Undecided) levels.push_back(i.level);
    const auto k = m_clo_level->get_selected();
    if (k >= levels.size()) return;
    show_closet_preview(core::closet::decide(items, levels[k]));
}

void Shell::on_closet_junk() {  // handler
    const auto items = closet_checked();
    if (items.empty()) { m_clo_status->set_text("Check some files first."); return; }
    show_closet_preview(core::closet::junk_drawer(items));
}

void Shell::on_closet_back() {  // handler: nothing done
    m_clo_plan.reset();
    m_clo_states->set_visible_child("results");
    m_clo_status->set_text("Nothing done.");
}

// The previewed plan, through the door; the catalog follows; Activity shows it.
void Shell::on_closet_run() {  // handler
    if (!m_clo_plan) return;
    auto plan = std::move(*m_clo_plan);
    m_clo_plan.reset();
    plan.previewed = true;   // shown in full by show_closet_preview (RULES: preview first)
    const auto r = m_door->run(plan);
    core::catalog::Catalog cat;
    std::size_t rec = 0;
    if (cat.open(m_data_dir, true))
        rec = core::tagging::record(cat, core::tagging::paths_of(*m_door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
    refresh_activity();
    fill_closet();
    m_clo_states->set_visible_child("results");
    m_clo_status->set_text(r.ok ? "Done: " + plan.label + " (" + std::to_string(r.done) + " steps) -- undo in Activity"
                                : plan.label + ": " + std::to_string(r.done) + " done, " + std::to_string(r.failed) +
                                      " not -- " + (r.messages.empty() ? std::string() : r.messages.front()));
    if (auto lg = log::get(log::Area::Shell))
        lg->info("closet run '{}': {} done, {} failed; catalog {} recorded", plan.label, r.done, r.failed, rec);
}

void Shell::on_dump_registry() {  // handler: print the live widget tree (also callable from gdb)
    registry::dump();
}

}  // namespace tidy
