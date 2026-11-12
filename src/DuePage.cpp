#include "DuePage.hpp"
#include "SelectList.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Archive.hpp"
#include "core/Catalog.hpp"
#include "core/Due.hpp"
#include "core/Own.hpp"
#include "core/Pages.hpp"
#include "core/Removal.hpp"
#include "core/Settings.hpp"

#include <gtkmm/box.h>
#include <gtkmm/window.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>

#include <ctime>
#include <memory>
#include <filesystem>

namespace tidy {

namespace du = core::due;
namespace ar = core::archive;
namespace dr = latch::door;

namespace {
constexpr std::size_t kPreviewRows = 300;   // s023e: the preview lists this many steps, then counts the rest
Gtk::Label* label(const std::string& text, const char* css, float x = 0.5f) {
    auto l = Gtk::make_managed<Gtk::Label>(text);
    if (css) l->add_css_class(css);
    l->set_xalign(x);
    l->set_wrap(true);
    l->set_justify(x == 0.5f ? Gtk::Justification::CENTER : Gtk::Justification::LEFT);
    return l;
}
Gtk::Image* hero(int px) {
    auto i = Gtk::make_managed<Gtk::Image>();
    i->set_from_icon_name("tidy-hero-due");
    i->set_pixel_size(px);
    return i;
}
}  // namespace

DuePage::DuePage(dr::Door& door, std::string home, std::string data_dir, bool fixture, Text user_dirs, Changed changed)
    : m_door(door), m_changed(std::move(changed)), m_home(std::move(home)), m_data_dir(std::move(data_dir)),
      m_fixture(fixture), m_user_dirs(std::move(user_dirs)) {
    set_name("due.states");
    const auto* p = core::pages::find("due");

    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("due.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero(128));
    start->append(*label(p ? p->title : "Due", "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(std::string("Ephemeral files past their days, Held past their years: to remove. Historical "
                                  "files long unread: to archive -- into ~/Archive/<year>/<its folder>, previewed first. "
                                  "Until done whose purpose closed: to settle. Look reads only.") +
                      (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : ""), "dim");
    note->set_max_width_chars(60);
    start->append(*note);
    auto orb = Gtk::make_managed<Gtk::Button>("Look");
    orb->set_name("due.look");
    orb->add_css_class("orb");
    orb->set_halign(Gtk::Align::CENTER);
    orb->set_margin_top(18);
    orb->signal_clicked().connect([this] { fill(); set_visible_child("results"); });
    start->append(*orb);
    add(*start, "start");

    auto results = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    results->set_name("due.results");
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(12);
    head->append(*hero(48));
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label(p ? p->title : "Due", "hero-title", 0.0f));
    m_summary = label("", "dim", 0.0f);
    m_summary->set_name("due.summary");
    titles->append(*m_summary);
    titles->set_hexpand(true);
    head->append(*titles);
    results->append(*head);
    // s023b (Scott: "no multiselect or select all ... can be daunting"): selected like
    // Nautilus -- click, Shift, Ctrl, Ctrl+A, drag a box; right-click or Set to acts.
    m_list = Gtk::make_managed<SelectList>("due.list");
    m_list->set_vexpand(true);
    m_list->set_margin_start(32);
    m_list->set_margin_end(32);
    m_list->set_choices({"Archive...", "Remove (to the Trash)..."});
    m_list->on_choose([this](std::size_t c) { if (c == 0) on_archive(); else on_remove(); });
    results->append(*m_list);
    m_also = label("", "dim", 0.0f);
    m_also->set_name("due.also");
    m_also->set_margin_start(32);
    m_also->set_margin_end(32);
    m_also->set_margin_top(6);
    m_also->set_margin_bottom(10);
    results->append(*m_also);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_status = label("Select what to archive or remove -- click, Shift, Ctrl, drag a box, Ctrl+A.", "dim", 0.0f);
    m_status->set_name("due.status");
    m_status->set_hexpand(true);
    m_status->set_width_chars(26);
    bar->append(*m_status);
    auto& setter = m_list->setter();
    setter.set_valign(Gtk::Align::CENTER);
    // Only "N selected" here: Archive... and Remove... are the buttons (right-click still offers both).
    for (auto* c = setter.get_first_child() ? setter.get_first_child()->get_next_sibling() : nullptr; c;
         c = c->get_next_sibling())
        c->set_visible(false);
    bar->append(setter);
    auto all = Gtk::make_managed<Gtk::Button>("Select all");
    all->set_name("due.all");
    all->set_valign(Gtk::Align::CENTER);
    all->set_tooltip_text("Every row (Ctrl+A)");
    all->signal_clicked().connect([this] { m_list->select_all(); });
    bar->append(*all);
    m_archive = Gtk::make_managed<Gtk::Button>("Archive...");
    m_archive->set_name("due.archive");
    m_archive->set_valign(Gtk::Align::CENTER);
    m_archive->set_tooltip_text("The selected files -> ~/Archive/<year>/<their folder>, fingerprinted (previewed "
                                "first) -- any of them, even ones due to remove; those become Historical");
    m_archive->signal_clicked().connect([this] { on_archive(); });
    bar->append(*m_archive);
    m_remove = Gtk::make_managed<Gtk::Button>("Remove...");
    m_remove->set_name("due.remove");
    m_remove->set_valign(Gtk::Align::CENTER);
    m_remove->set_tooltip_text("The selected files under To remove -> the Trash (previewed first; archived ones only when proved now)");
    m_remove->signal_clicked().connect([this] { on_remove(); });
    bar->append(*m_remove);
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("due.again");
    again->signal_clicked().connect([this] { fill(); });
    bar->append(*again);
    results->append(*bar);
    add(*results, "results");
    build_preview();
    set_visible_child("start");
}

void DuePage::build_preview() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("due.preview");
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(12);
    head->append(*hero(48));
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label("Preview", "hero-title", 0.0f));
    m_prev_summary = label("", "dim", 0.0f);
    m_prev_summary->set_name("due.preview.summary");
    titles->append(*m_prev_summary);
    titles->set_hexpand(true);
    head->append(*titles);
    box->append(*head);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_margin_start(32);
    scroll->set_margin_end(32);
    scroll->set_margin_bottom(16);
    m_prev_list = Gtk::make_managed<Gtk::ListBox>();
    m_prev_list->set_name("due.preview.list");
    m_prev_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_prev_list);
    box->append(*scroll);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_prev_status = label("", "dim", 0.0f);
    m_prev_status->set_name("due.preview.status");
    m_prev_status->set_hexpand(true);
    bar->append(*m_prev_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("due.preview.back");
    back->signal_clicked().connect([this] { m_plan.reset(); m_removing = false; set_visible_child("results"); });
    bar->append(*back);
    m_prev_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_prev_run->set_name("due.preview.run");
    m_prev_run->add_css_class("act");
    m_prev_run->signal_clicked().connect([this] { on_run(); });
    bar->append(*m_prev_run);
    box->append(*bar);
    add(*box, "preview");
}

// s023e: the plan (an archive reads tags; a removal reads every copy back --
// SHA-256) and the door's refusals run off the window's thread under Busy; the
// preview lists the first kPreviewRows steps and counts the rest.
void DuePage::preview_rows(const std::vector<std::pair<std::string, const char*>>& rows) {
    while (auto* r = m_prev_list->get_row_at_index(0)) m_prev_list->remove(*r);
    std::size_t n = 0;
    for (const auto& [text, css] : rows) {
        auto l = label(n == kPreviewRows ? "... and " + std::to_string(rows.size() - kPreviewRows) +
                                               " more -- all of them are in the plan"
                                         : text,
                       n == kPreviewRows ? "dim" : css, 0.0f);
        l->set_margin(4);
        auto r = Gtk::make_managed<Gtk::ListBoxRow>();
        r->set_activatable(false);
        r->set_child(*l);
        m_prev_list->append(*r);
        if (n++ == kPreviewRows) break;
    }
}

void DuePage::on_archive() {
    // s023d (Scott): any due file may be archived -- To remove and To settle too
    // (core::archive makes a non-Historical one Historical, so it is not due again).
    std::vector<std::string> paths;
    for (auto i : m_list->selected())
        if (i < m_items.size()) paths.push_back(m_items[i].path);
    if (paths.empty()) { m_status->set_text("Select the files to archive first."); return; }
    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    if (!win || m_busy) return;
    struct Out {
        ar::Planned planned;
        std::vector<std::string> why;
    };
    auto out = std::make_shared<Out>();
    const std::string home = m_home;
    const dr::Door* door = &m_door;
    m_busy = true;
    busy::start(*win, "Archive -- preparing", "Planning " + std::to_string(paths.size()) + " files",
        [out, home, paths, door] {
            out->planned = ar::plan(home, paths, static_cast<std::int64_t>(std::time(nullptr)));
            std::vector<std::string> made;
            for (const auto& a : out->planned.plan.actions) {   // in order: a Move may use a folder made above it
                out->why.push_back(door->refusal(a, made));
                if (out->why.back().empty() && a.verb == dr::Verb::MakeDir) made.push_back(a.to);
            }
        },
        [this, out] {
            m_busy = false;
            auto& planned = out->planned;
            std::vector<std::pair<std::string, const char*>> rows;
            for (const auto& [p, why] : planned.refused) rows.emplace_back(shown(p) + "\nNOT ARCHIVED: " + why, "stop");
            std::size_t refused = 0, moves = 0, tags = 0, dirs = 0;
            for (std::size_t i = 0; i < planned.plan.actions.size(); ++i) {
                const auto& a = planned.plan.actions[i];
                const std::string& why = out->why[i];
                refused += !why.empty();
                std::string text;
                const char* css = nullptr;
                if (a.verb == dr::Verb::Tag) {
                    ++tags;
                    text = "tag   " + shown(a.from) + "   " + a.attr.substr(10) + " = " + a.content;
                    css = "dim";
                } else if (a.verb == dr::Verb::MakeDir) {
                    ++dirs;
                    text = "new folder   " + shown(a.to);
                    css = "dim";
                } else {
                    ++moves;
                    text = shown(a.from) + "   ->   " + shown(a.to) + "\n" + a.reason;
                    if (a.reason.find("is taken there") != std::string::npos) css = "note";
                }
                if (!why.empty()) { text += "\nREFUSED: " + why; css = "stop"; }
                rows.emplace_back(text, css);
            }
            preview_rows(rows);
            const auto n = [](std::size_t k, const char* one) {
                return std::to_string(k) + " " + one + (k == 1 ? "" : "s");
            };
            m_prev_summary->set_text(planned.plan.label + "   |   " + n(moves, "move") + ", " + n(dirs, "new folder") +
                                     ", " + n(tags, "tag"));
            std::string st = refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen. "
                                     : std::string();
            if (!planned.renamed.empty())
                st += std::to_string(planned.renamed.size()) + " name" + (planned.renamed.size() == 1 ? " is" : "s are") +
                      " taken there -- renamed with (2), (3)... Nothing is overwritten. ";
            st += "After Do it each file's fingerprint (SHA-256) goes into the catalog. Every step can be undone from Activity.";
            m_prev_status->set_text(st);
            m_prev_status->remove_css_class("note");
            if (!planned.renamed.empty()) m_prev_status->add_css_class("note");
            m_prev_run->set_sensitive(moves > refused);
            m_plan = std::move(planned.plan);
            m_removing = false;
            set_visible_child("preview");
        });
}

void DuePage::on_remove() {
    std::size_t others = 0;
    auto paths = selected_paths(du::To::Remove, &others);
    if (paths.empty()) { m_status->set_text("Select a file under To remove first."); return; }
    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    if (!win || m_busy) return;
    struct Out {
        core::removal::Planned planned;
        std::vector<std::string> why;
    };
    auto out = std::make_shared<Out>();
    const std::string home = m_home, data = m_data_dir;
    const dr::Door* door = &m_door;
    m_busy = true;
    busy::start(*win, "Remove -- reading back", "Checking " + std::to_string(paths.size()) +
                                                    " files (an archived one: every copy read back, SHA-256)",
        [out, home, data, paths, door] {
            core::removal::Book book;
            {
                core::catalog::Catalog cat;
                if (cat.open(data, false)) book = core::removal::book_of(cat);
            }
            const auto st = core::settings::parse(core::own::read(data, core::settings::kFile));
            out->planned = core::removal::plan(home, book, st, paths);
            for (const auto& a : out->planned.plan.actions) out->why.push_back(door->refusal(a, {}));
        },
        [this, out, paths, others] {
            m_busy = false;
            auto& planned = out->planned;
            std::vector<std::pair<std::string, const char*>> rows;
            for (const auto& [p, why] : planned.refused) rows.emplace_back(shown(p) + "\nNOT REMOVED: " + why, "stop");
            std::size_t refused = 0;
            for (std::size_t i = 0; i < planned.plan.actions.size(); ++i) {
                const auto& a = planned.plan.actions[i];
                const std::string& why = out->why[i];
                refused += !why.empty();
                std::string text = "to the Trash   " + shown(a.from) + "\n" + a.reason;
                rows.emplace_back(why.empty() ? text : text + "\nREFUSED: " + why, why.empty() ? nullptr : "stop");
            }
            preview_rows(rows);
            const auto n = planned.plan.actions.size();
            m_prev_summary->set_text(planned.plan.label + "   |   " + std::to_string(n) + " to the Trash" +
                                     (planned.refused.empty() ? "" : ", " + std::to_string(planned.refused.size()) + " kept"));
            std::string s = refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen. " : "";
            if (others) s += std::to_string(others) + " selected not under To remove -- left out. ";
            s += "To the Trash, not deleted -- undo in Activity. An archived file's copies are read back again at Do it; "
                 "one that no longer matches stays.";
            m_prev_status->set_text(s);
            m_prev_status->remove_css_class("note");
            m_prev_run->set_sensitive(n > refused);
            m_plan = std::move(planned.plan);
            m_removing = true;
            m_rem_paths = paths;
            set_visible_child("preview");
        });
}

void DuePage::on_run() {
    if (!m_plan || m_busy) return;
    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    if (!win) return;
    auto plan = std::move(*m_plan);
    m_plan.reset();
    plan.previewed = true;   // shown in full by on_archive() / on_remove() (RULES: preview first)
    const bool removing = m_removing;
    m_removing = false;
    struct Out {
        std::size_t dropped = 0, kept = 0;
        std::string label;
        bool any = false;
        std::vector<core::catalog::Removed> pending;
    };
    auto out = std::make_shared<Out>();
    out->label = plan.label;
    const std::string home = m_home, data = m_data_dir;
    const auto paths = m_rem_paths;
    dr::Door* door = &m_door;
    m_busy = true;
    // s023e: all of it off the window's thread -- the refusals, the removal's read
    // back AGAIN (only steps shown AND still proved run), the run, the catalog.
    busy::door(*win, m_door, std::move(plan), removing ? "Remove" : "Archive",
        [this, out, removing](const dr::Result& r) {
            m_busy = false;
            if (m_changed) m_changed();
            fill();
            set_visible_child("results");
            if (removing) {
                std::string s = !out->any ? std::string("Nothing removed")
                                          : (r.ok ? "Done: " : "Partly done: ") + out->label + " (" +
                                                std::to_string(r.done) + " to the Trash; " + std::to_string(out->kept) +
                                                " from ~/Archive)";
                if (out->dropped) s += " -- " + std::to_string(out->dropped) + " kept: no longer proved at Do it";
                if (!r.ok && !r.messages.empty()) s += " -- " + r.messages.front();
                if (out->any) s += " -- undo in Activity";
                m_status->set_text(s);
                if (auto lg = log::get(log::Area::Door))
                    lg->info("remove run '{}': {} done, {} failed; {} archived recorded, {} dropped at Do it", out->label,
                             r.done, r.failed, out->kept, out->dropped);
                return;
            }
            m_status->set_text(r.ok ? "Done: " + out->label + " (" + std::to_string(r.done) + " steps; " +
                                          std::to_string(out->kept) + " fingerprinted) -- undo in Activity"
                                    : out->label + ": " + std::to_string(r.done) + " done, " + std::to_string(r.failed) +
                                          " not -- " + (r.messages.empty() ? std::string() : r.messages.front()));
            if (auto lg = log::get(log::Area::Door))
                lg->info("archive run '{}': {} done, {} failed; {} fingerprinted", out->label, r.done, r.failed, out->kept);
        },
        [data, door, out, removing](const dr::Result& r) {
            if (!out->any) return;
            core::catalog::Catalog cat;
            if (!cat.open(data, true)) return;
            const auto now = static_cast<std::int64_t>(std::time(nullptr));
            out->kept = removing ? core::removal::record(cat, *door, r.batch, out->pending, now)
                                 : ar::record(cat, *door, r.batch, now);
        },
        [home, data, paths, door, out, removing](dr::Plan& p) {
            {   // drop what the door refuses, judged in order as previewed
                std::vector<std::string> made;
                std::vector<dr::Action> keep;
                for (const auto& a : p.actions) {
                    if (!door->refusal(a, made).empty()) continue;
                    if (a.verb == dr::Verb::MakeDir) made.push_back(a.to);
                    keep.push_back(a);
                }
                p.actions = std::move(keep);
            }
            if (removing) {   // s014: plan again NOW (reads the copies again)
                core::removal::Book book;
                {
                    core::catalog::Catalog ro;
                    if (ro.open(data, false)) book = core::removal::book_of(ro);
                }
                const auto st = core::settings::parse(core::own::read(data, core::settings::kFile));
                auto now_plan = core::removal::plan(home, book, st, paths);
                out->pending = now_plan.pending;
                std::vector<dr::Action> keep;
                for (const auto& a : p.actions)
                    for (const auto& b : now_plan.plan.actions)
                        if (a.from == b.from) { keep.push_back(a); break; }
                out->dropped = p.actions.size() - keep.size();
                p.actions = std::move(keep);
                if (out->dropped && !p.actions.empty())   // the label names what really runs (Activity's undo)
                    p.label = p.actions.size() == 1 ? "Remove: " + std::filesystem::path(p.actions[0].from).filename().string() + " -> Trash"
                                                    : "Remove: " + std::to_string(p.actions.size()) + " files -> Trash";
            }
            out->any = !p.actions.empty();
            out->label = p.label;
        });
}

std::string DuePage::shown(const std::string& path) const {
    if (path == m_home) return "~";   // s019: a file at home's top
    if (path.rfind(m_home + "/", 0) == 0) return "~" + path.substr(m_home.size());
    return path;
}

std::string DuePage::look_line() { return m_line; }

void DuePage::refresh_if_shown() {
    if (get_visible_child_name() == "results") fill();
}

std::vector<std::string> DuePage::selected_paths(du::To to, std::size_t* others) const {
    std::vector<std::string> out;
    for (auto i : m_list->selected()) {
        if (i >= m_items.size()) continue;
        if (m_items[i].to == to) out.push_back(m_items[i].path);
        else if (others) ++*others;
    }
    return out;
}

void DuePage::fill() {
    std::vector<du::Item> items;
    const auto a = du::look(m_home, m_data_dir, m_user_dirs ? m_user_dirs() : "",
                            static_cast<std::int64_t>(std::time(nullptr)), &items);
    m_line = du::line(a);
    m_summary->set_text(m_line + ".");
    if (auto lg = log::get(log::Area::Shell)) lg->info("due look: {} ({} ms)", m_line, a.ms);

    m_items = std::move(items);
    std::vector<SelectList::Item> rows;
    std::vector<std::size_t> pre;   // To archive comes up selected (it was checked before s023b); To remove never
    std::size_t n_rem = 0;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const auto& it = m_items[i];
        SelectList::Item r;
        if (i == 0 || m_items[i - 1].to != it.to) {
            const std::size_t n = it.to == du::To::Remove ? a.remove : it.to == du::To::Archive ? a.archive : a.settle;
            r.header = std::string(du::to_title(it.to)) + "  (" + std::to_string(n) + ")";
            if (it.to == du::To::Remove) r.header_sub = "To the Trash -- select, then Remove... (or Archive... to keep it). Nothing is selected for you.";
            else if (it.to == du::To::Archive) r.header_sub = "Into ~/Archive/<year>/<its folder>, fingerprinted -- selected; Archive...";
            else r.header_sub = "Close or reopen its purpose on the Purposes page -- nothing to do here.";
        }
        r.title = std::filesystem::path(it.path).filename().string();
        r.line = shown(std::filesystem::path(it.path).parent_path().string()) + "   ·   " + it.why;
        r.tooltip = it.path;
        if (it.to == du::To::Archive) pre.push_back(i);
        n_rem += it.to == du::To::Remove;
        rows.push_back(std::move(r));
    }
    m_list->set_items(rows);
    m_list->select(pre);
    if (m_archive) m_archive->set_sensitive(!m_items.empty());   // s023d: any due file may be archived
    if (m_remove) m_remove->set_sensitive(n_rem > 0);
    std::string s;
    if (a.closet) s += std::to_string(a.closet) + " in the Closet, proposed -- see The Closet.";
    if (a.lists) s += std::string(s.empty() ? "" : "  ") + std::to_string(a.lists) + " id list change" +
                      (a.lists == 1 ? "" : "s") + " -- main menu, Id lists...";
    if (!a.total()) s = "Nothing is due, and nothing else needs attention.";
    m_also->set_text(s.empty() ? "" : "Also: " + s);
    m_also->set_visible(!s.empty());
}

}  // namespace tidy
