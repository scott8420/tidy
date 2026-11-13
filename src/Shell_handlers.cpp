#include "Shell.hpp"
#include "core/Protection.hpp"
#include "core/Archive.hpp"
#include "ArchivePage.hpp"
#include "HelpWindow.hpp"
#include "DuePage.hpp"
#include "core/Backup.hpp"
#include "core/Removal.hpp"
#include "ProjectsPage.hpp"
#include "PurposesPage.hpp"
#include "HomePage.hpp"
#include "SortPage.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "SettingsPage.hpp"
#include "IdListsWindow.hpp"
#include "QuarantinePage.hpp"
#include "OrganizeStart.hpp"
#include "AdoptWindow.hpp"
#include "OrganizeWindow.hpp"
#include "TemplatesPage.hpp"
#include "Busy.hpp"

#include <memory>
#include "BackgroundWindow.hpp"
#include "core/Background.hpp"
#include "core/FileLists.hpp"
#include "core/Scan.hpp"

#include <glibmm/main.h>
#include <chrono>
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

void Shell::on_nav(const std::string& page) {  // handler: a page shown -> its tab lit + tint
    std::string tint;
    for (const auto& e : m_nav_model)
        if (!e.section && e.id == page) tint = e.tint;
    if (tint != m_tint) {
        if (!m_tint.empty()) remove_css_class("tint-" + m_tint);
        m_tint = tint;
        if (!m_tint.empty()) add_css_class("tint-" + m_tint);   // look.css: window.tint-<name>
    }
    const bool from_elsewhere = std::string(m_stack.get_visible_child_name()) != page;
    m_stack.set_visible_child(page);
    // s027: the area's tab row follows, and remembers this page.
    if (const auto area = core::look::area_of(m_areas, page); !area.empty()) {
        m_area_page[area] = page;
        if (std::string(m_tabs.get_visible_child_name()) != area) m_tabs.set_visible_child(area);
        if (auto it = m_tab_buttons.find(page); it != m_tab_buttons.end() && !it->second->get_active()) {
            m_tab_sync = true;
            it->second->set_active(true);
            m_tab_sync = false;
        }
    }
    // s027: pages that read when shown.
    if (page == "organize" && m_org_start) m_org_start->refresh();
    if (page == "quarantine" && m_quarantine) m_quarantine->open();   // looks the first time only
    if (page == "settings" && m_settings_page && from_elsewhere) m_settings_page->show(m_settings);
    if (auto lg = log::get(log::Area::Shell)) lg->debug("nav -> {} (tint {})", page, m_tint);
}

void Shell::on_area(const std::string& area) {  // handler: s027 -- a sidebar row -> its last page
    for (const auto& a : m_areas) {
        if (a.id != area || a.tabs.empty()) continue;
        const auto it = m_area_page.find(area);
        on_nav(it != m_area_page.end() ? it->second : a.tabs.front());
        return;
    }
}

void Shell::on_help() {  // handler: How to use Tidy -- non-modal, beside the window (s017)
    if (!m_help) m_help = std::make_unique<HelpWindow>(*this, [this](const std::string& page) { go(page); });
    m_help->present();
}

void Shell::on_about() {  // handler: the About window -- a hide-on-close singleton
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

// Settings (s006a): written by core::own, the one writer of Tidy's own files;
// the Closet re-reads with the new aging days and junk list.
void Shell::on_settings() {  // handler: s027 -- the Settings area (was a window)
    go("settings");
}

// What Settings' Save does: written by core::own, then everyone re-reads.
std::string Shell::save_settings(const core::settings::Settings& s) {  // glue
    const std::string err = core::own::write(m_data_dir, core::settings::kFile, core::settings::format(s));
    if (!err.empty()) return err;
    m_settings = s;
    core::backup::register_media(m_settings.media);   // s013b
    core::protection::apply(m_home, core::protection::config_home(m_home), m_settings);   // s019a
    if (m_archive) m_archive->refresh_if_shown();
    arm_rescan();
    if (m_clo_states && m_clo_states->get_visible_child_name() == "results") fill_closet();
    if (m_projects) m_projects->refresh_if_shown();
    if (m_purposes) m_purposes->refresh_if_shown();
    if (m_home_page) m_home_page->refresh_if_shown();
    if (m_sort_page) m_sort_page->refresh_if_shown();   // s020
    if (m_projects) m_projects->refresh_if_shown();     // s021: the code view's archived lines
    if (auto lg = log::get(log::Area::Shell))
        lg->info("settings saved: aging {} days, {} junk patterns, rescan every {} min", s.aging_days,
                 s.junk.size(), s.rescan_minutes);
    return std::string();
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
    // s023a: a big batch (an adopt of thousands) undoes off the window's thread, under Busy's bar.
    struct Out { latch::door::Result r; std::size_t synced = 0; };
    auto out = std::make_shared<Out>();
    m_act_undo->set_sensitive(false);
    latch::door::Door* door = m_door.get();
    const std::string data = m_data_dir;
    busy::start(*this, "Undo", "Undoing \u201c" + label + "\u201d",
        [door, data, b, out] {
            out->r = door->undo();
            core::catalog::Catalog cat;
            if (cat.open(data, true)) {
                out->synced = core::tagging::after_undo(cat, *door, b, static_cast<std::int64_t>(std::time(nullptr)));
                out->synced += core::archive::after_undo(cat, b);   // s013a: an archive undone leaves no fingerprint behind
                out->synced += core::backup::after_undo(cat, b);    // s013b: copies undone -> their proofs go
                out->synced += core::removal::after_undo(cat, b);   // s014: back from the Trash -> not removed
            }
        },
        [this, out, label] { after_activity_undo(label, out->r, out->synced); });
}

void Shell::after_activity_undo(const std::string& label, const latch::door::Result& r, std::size_t synced) {  // helper
    refresh_activity();
    if (m_due) m_due->refresh_if_shown();
    if (m_archive) m_archive->refresh_if_shown();
    if (m_clo_states && m_clo_states->get_visible_child_name() == "results") fill_closet();
    if (m_home_page) m_home_page->refresh_if_shown();
    if (m_sort_page) m_sort_page->refresh_if_shown();   // s020
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

// Set to (s018; the three choices of s006): the selected rows -> a plan ->
// its preview. Choice 0 = each one's proposal; then the levels but Undecided;
// last the Junk Drawer. None of them runs anything.
void Shell::on_closet_set(std::size_t choice) {  // handler
    const auto items = closet_selected();
    if (items.empty()) { m_clo_status->set_text("Select some files first."); return; }
    std::vector<core::retention::Level> levels;   // the setter's order: all but Undecided
    for (const auto& i : core::retention::all())
        if (i.level != core::retention::Level::Undecided) levels.push_back(i.level);
    if (choice == 0) show_closet_preview(core::closet::accept(items));
    else if (choice <= levels.size()) show_closet_preview(core::closet::decide(items, levels[choice - 1]));
    else show_closet_preview(core::closet::junk_drawer(items));
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

// s010f1: the loose background rescan ("a file monitor of a loose kind",
// Scott). Projects only, READ-ONLY on the home: find, record ids in the
// catalog (moves followed by id, gone marked). Every rescan_minutes while
// Tidy runs; 0 = never. On the main loop -- find() is four folders deep;
// its time is logged, and it moves to a thread if that ever shows.
void Shell::arm_rescan() {  // handler
    m_rescan.disconnect();
    if (!m_projects || m_settings.rescan_minutes <= 0) return;
    m_rescan = Glib::signal_timeout().connect_seconds([this] {
        const auto t0 = std::chrono::steady_clock::now();
        const auto line = m_projects->rescan();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (auto lg = log::get(log::Area::Shell)) lg->info("rescan: {} ({} ms)", line, ms);
        offer_lists();
        return true;
    }, static_cast<unsigned>(m_settings.rescan_minutes) * 60u);
}

// s010f2: the id lists -- its own window: Look, preview, Do it; undo in Activity.
void Shell::on_id_lists() {  // handler
    if (!m_idlists_win)
        m_idlists_win = std::make_unique<IdListsWindow>(*this, *m_door, m_home, m_data_dir, [this] { refresh_activity(); offer_lists(); });
    m_idlists_win->open();
}

// s025b: the Quarantine -- answers to the suspect scan; writes only Tidy's own list.
void Shell::on_quarantine() {  // handler: s027 -- Review's Quarantine tab (was a window)
    go("quarantine");
}

// s023: adopt folders -- the tags through the door (undo in Activity); the
// picked folders saved in settings.ini as Settings saves them.
void Shell::on_adopt() {  // handler
    if (!m_adopt_win)
        m_adopt_win = std::make_unique<AdoptWindow>(
            *this, *m_door, m_home, m_data_dir, [this] { return user_dirs_text(); },
            [this](const core::settings::Settings& s) {
                const std::string err = core::own::write(m_data_dir, core::settings::kFile, core::settings::format(s));
                if (!err.empty()) return err;
                m_settings = s;
                if (m_clo_states && m_clo_states->get_visible_child_name() == "results") fill_closet();
                if (m_home_page) m_home_page->refresh_if_shown();
                if (m_sort_page) m_sort_page->refresh_if_shown();
                if (auto lg = log::get(log::Area::Shell))
                    lg->info("adopted folders saved: {} in place, {} catch-all", s.adopted.size(), s.catch_all.size());
                return std::string();
            },
            [this] { refresh_activity(); });
    m_adopt_win->open(m_settings);
}

// s024e: Organize as... -- one folder + a type: laid out and moved where the type lives;
// loose leftovers -> the Closet (catch-alls in settings.ini, Adopt's way).
void Shell::on_organize(const std::string& folder, const std::string& type) {  // handler
    if (!m_organize_win)
        m_organize_win = std::make_unique<OrganizeWindow>(*this, *m_door, m_home, m_data_dir,
            [this](const std::vector<std::string>& rels) {
                auto s = m_settings;
                for (const auto& r : rels)
                    if (std::find(s.catch_all.begin(), s.catch_all.end(), r) == s.catch_all.end()) s.catch_all.push_back(r);
                const std::string err = core::own::write(m_data_dir, core::settings::kFile, core::settings::format(s));
                if (!err.empty()) return err;
                m_settings = s;
                if (m_clo_states && m_clo_states->get_visible_child_name() == "results") fill_closet();
                if (auto lg = log::get(log::Area::Shell)) lg->info("organize: {} catch-alls added for the Closet", rels.size());
                return std::string();
            },
            [this] { on_new_type(); },
            [this] {
                refresh_activity();
                if (m_projects) m_projects->refresh_if_shown();
            });
    m_organize_win->open(folder, type);
}

// s024e: "the organization template must be set first" -- a new type in the builder.
void Shell::on_new_type() {  // handler
    go("templates");   // the sidebar follows
    if (m_templates_page) m_templates_page->start_new();
}

// s023f: the Map's right-click -> the Adopt window, with that folder added.
void Shell::on_adopt_folder(const std::string& rel, bool catch_all) {  // handler
    on_adopt();
    if (m_adopt_win) m_adopt_win->add(rel, catch_all);
}

}  // namespace tidy


namespace tidy {

// s010f3: Scott -- the background rescan offers out-of-date lists. READ-ONLY:
// the same scan as the window's Look, counted. On the main loop like the
// rescan; the ms are logged (a thread if it ever shows on a real home).
void Shell::offer_lists() {  // handler
    if (!m_offer) return;
    const auto t0 = std::chrono::steady_clock::now();
    namespace fl = core::filelists;
    const auto st = fl::stale(core::scan::walk(m_home, fl::roots(m_home)));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    show_offer(st.lists, st.refound, static_cast<long>(ms));
}

void Shell::show_offer(std::size_t lists, std::size_t refound, long ms) {  // handler
    if (!m_offer) return;
    struct { std::size_t lists, refound; } st{lists, refound};
    std::string line;
    if (st.lists) line = std::to_string(st.lists) + " id list" + (st.lists == 1 ? "" : "s") + " out of date";
    if (st.refound) line += (line.empty() ? "" : ", ") + std::to_string(st.refound) + " id" +
                            (st.refound == 1 ? "" : "s") + " to give back";
    if (auto lg = log::get(log::Area::Shell)) lg->info("id lists: {} ({} ms)", line.empty() ? "up to date" : line, ms);
    m_offer_text->set_text(line);
    m_offer->set_reveal_child(!line.empty() && line != m_offer_dismissed);
}

}  // namespace tidy

namespace tidy {

// s012b: the background scan -- a systemd user timer; its own window.
void Shell::on_background() {  // handler
    if (!m_background_win)
        m_background_win = std::make_unique<BackgroundWindow>(
            *this, *m_door, m_home, m_data_dir,
            core::background::unit_dir(m_home, std::getenv("XDG_CONFIG_HOME"), m_fixture), m_fixture,
            [this](const core::settings::Settings& s) {
                m_settings = s;
                core::protection::apply(m_home, core::protection::config_home(m_home), m_settings);   // s019a
                refresh_activity();
            });
    m_background_win->open(m_settings);
}

}  // namespace tidy

namespace tidy {

// s012c: the tile's requests, through App::on_command_line.
void Shell::go(const std::string& page) {  // glue
    const auto area = core::look::area_of(m_areas, page);   // s027: the page's area row
    if (!area.empty()) m_area_page[area] = page;             // so selecting the row lands here
    bool selected = false;
    for (int i = 0; auto* r = m_nav.get_row_at_index(i); ++i)
        if (!area.empty() && std::string(r->get_name()) == "nav." + area) {
            if (m_nav.get_selected_row() == r) on_nav(page);
            else m_nav.select_row(*r);
            selected = true;
        }
    if (!selected) on_nav(page);       // the look off: GTK's own sidebar follows the stack
    present();
}

void Shell::new_project_from(const std::string& name) {  // glue
    go("projects");
    if (m_projects) m_projects->new_from(name);
}

void Shell::open_background() {  // glue
    present();
    on_background();
}

}  // namespace tidy
