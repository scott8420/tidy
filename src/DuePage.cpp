#include "DuePage.hpp"
#include "Log.hpp"
#include "core/Archive.hpp"
#include "core/Catalog.hpp"
#include "core/Due.hpp"
#include "core/Own.hpp"
#include "core/Pages.hpp"
#include "core/Removal.hpp"
#include "core/Settings.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>

#include <ctime>
#include <filesystem>

namespace tidy {

namespace du = core::due;
namespace ar = core::archive;
namespace dr = latch::door;

namespace {
constexpr std::size_t kMaxRows = 500;
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
void add_row(Gtk::ListBox* list, Gtk::Widget& child, const std::string& name = "") {
    auto row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_activatable(false);
    if (!name.empty()) row->set_name(name);
    row->set_child(child);
    list->append(*row);
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
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_margin_start(32);
    scroll->set_margin_end(32);
    scroll->set_margin_bottom(16);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("due.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    results->append(*scroll);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_status = label("Check what to archive or remove. Removal goes to the Trash; a file in ~/Archive goes only "
                     "when its copies on media read back the same, now.", "dim", 0.0f);
    m_status->set_name("due.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    m_archive = Gtk::make_managed<Gtk::Button>("Archive checked...");
    m_archive->set_name("due.archive");
    m_archive->set_tooltip_text("The checked files -> ~/Archive/<year>/<their folder> (previewed first)");
    m_archive->signal_clicked().connect([this] { on_archive(); });
    bar->append(*m_archive);
    m_remove = Gtk::make_managed<Gtk::Button>("Remove checked...");
    m_remove->set_name("due.remove");
    m_remove->set_tooltip_text("The checked files -> the Trash (previewed first; archived ones only when proved now)");
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

void DuePage::on_archive() {
    std::vector<std::string> paths;
    for (const auto& [c, p] : m_arc_rows)
        if (c->get_active()) paths.push_back(p);
    if (paths.empty()) { m_status->set_text("Check a file under To archive first."); return; }
    auto planned = ar::plan(m_home, paths, static_cast<std::int64_t>(std::time(nullptr)));

    while (auto* r = m_prev_list->get_row_at_index(0)) m_prev_list->remove(*r);
    const auto row = [&](const std::string& text, const char* css) {
        auto l = label(text, css, 0.0f);
        l->set_margin(4);
        auto r = Gtk::make_managed<Gtk::ListBoxRow>();
        r->set_activatable(false);
        r->set_child(*l);
        m_prev_list->append(*r);
    };
    for (const auto& [p, why] : planned.refused) row(shown(p) + "\nNOT ARCHIVED: " + why, "stop");
    std::vector<std::string> made;
    std::size_t refused = 0, moves = 0, tags = 0, dirs = 0;
    for (const auto& a : planned.plan.actions) {
        const std::string why = m_door.refusal(a, made);   // in order: a Move may use a folder made above it
        refused += !why.empty();
        if (why.empty() && a.verb == dr::Verb::MakeDir) made.push_back(a.to);
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
        row(text, css);
    }
    const auto n = [](std::size_t k, const char* one) {
        return std::to_string(k) + " " + one + (k == 1 ? "" : "s");
    };
    m_prev_summary->set_text(planned.plan.label + "   |   " + n(moves, "move") + ", " + n(dirs, "new folder") + ", " +
                             n(tags, "tag"));
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
}

void DuePage::on_remove() {
    std::vector<std::string> paths;
    for (const auto& [c, p] : m_rem_rows)
        if (c->get_active()) paths.push_back(p);
    if (paths.empty()) { m_status->set_text("Check a file under To remove first."); return; }
    core::removal::Book book;
    {
        core::catalog::Catalog cat;
        if (cat.open(m_data_dir, false)) book = core::removal::book_of(cat);
    }
    const auto st = core::settings::parse(core::own::read(m_data_dir, core::settings::kFile));
    auto planned = core::removal::plan(m_home, book, st, paths);

    while (auto* r = m_prev_list->get_row_at_index(0)) m_prev_list->remove(*r);
    const auto row = [&](const std::string& text, const char* css) {
        auto l = label(text, css, 0.0f);
        l->set_margin(4);
        auto r = Gtk::make_managed<Gtk::ListBoxRow>();
        r->set_activatable(false);
        r->set_child(*l);
        m_prev_list->append(*r);
    };
    for (const auto& [p, why] : planned.refused) row(shown(p) + "\nNOT REMOVED: " + why, "stop");
    std::size_t refused = 0;
    for (const auto& a : planned.plan.actions) {
        const std::string why = m_door.refusal(a, {});
        refused += !why.empty();
        std::string text = "to the Trash   " + shown(a.from) + "\n" + a.reason;
        row(why.empty() ? text : text + "\nREFUSED: " + why, why.empty() ? nullptr : "stop");
    }
    const auto n = planned.plan.actions.size();
    m_prev_summary->set_text(planned.plan.label + "   |   " + std::to_string(n) + " to the Trash" +
                             (planned.refused.empty() ? "" : ", " + std::to_string(planned.refused.size()) + " kept"));
    std::string s = refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen. " : "";
    s += "To the Trash, not deleted -- undo in Activity. An archived file's copies are read back again at Do it; "
         "one that no longer matches stays.";
    m_prev_status->set_text(s);
    m_prev_status->remove_css_class("note");
    m_prev_run->set_sensitive(n > refused);
    m_plan = std::move(planned.plan);
    m_removing = true;
    m_rem_paths = std::move(paths);
    set_visible_child("preview");
}

void DuePage::on_run() {
    if (!m_plan) return;
    auto plan = std::move(*m_plan);
    m_plan.reset();
    {   // drop what the door refuses, judged in order as previewed
        std::vector<std::string> made;
        std::vector<dr::Action> keep;
        for (const auto& a : plan.actions) {
            if (!m_door.refusal(a, made).empty()) continue;
            if (a.verb == dr::Verb::MakeDir) made.push_back(a.to);
            keep.push_back(a);
        }
        plan.actions = std::move(keep);
    }
    if (m_removing) {   // s014: plan again NOW (reads the copies again); only steps shown AND still proved run
        m_removing = false;
        core::removal::Book book;
        {
            core::catalog::Catalog ro;
            if (ro.open(m_data_dir, false)) book = core::removal::book_of(ro);
        }
        const auto st = core::settings::parse(core::own::read(m_data_dir, core::settings::kFile));
        auto now_plan = core::removal::plan(m_home, book, st, m_rem_paths);
        std::vector<dr::Action> keep;
        for (const auto& a : plan.actions)
            for (const auto& b : now_plan.plan.actions)
                if (a.from == b.from) { keep.push_back(a); break; }
        const std::size_t dropped = plan.actions.size() - keep.size();
        plan.actions = std::move(keep);
        if (dropped && !plan.actions.empty())   // the label names what really runs (Activity's undo)
            plan.label = plan.actions.size() == 1 ? "Remove: " + std::filesystem::path(plan.actions[0].from).filename().string() + " -> Trash"
                                                  : "Remove: " + std::to_string(plan.actions.size()) + " files -> Trash";
        std::size_t kept = 0;
        dr::Result r;
        r.ok = true;
        if (!plan.actions.empty()) {
            plan.previewed = true;   // shown in full by on_remove()
            r = m_door.run(plan);
            core::catalog::Catalog cat;
            if (cat.open(m_data_dir, true))
                kept = core::removal::record(cat, m_door, r.batch, now_plan.pending,
                                             static_cast<std::int64_t>(std::time(nullptr)));
        }
        if (m_changed) m_changed();
        fill();
        set_visible_child("results");
        std::string s = plan.actions.empty() ? std::string("Nothing removed")
                                             : (r.ok ? "Done: " : "Partly done: ") + plan.label + " (" +
                                                   std::to_string(r.done) + " to the Trash; " + std::to_string(kept) +
                                                   " from ~/Archive)";
        if (dropped) s += " -- " + std::to_string(dropped) + " kept: no longer proved at Do it";
        if (!r.ok && !r.messages.empty()) s += " -- " + r.messages.front();
        if (!plan.actions.empty()) s += " -- undo in Activity";
        m_status->set_text(s);
        if (auto lg = log::get(log::Area::Door))
            lg->info("remove run '{}': {} done, {} failed; {} archived recorded, {} dropped at Do it", plan.label,
                     r.done, r.failed, kept, dropped);
        return;
    }
    plan.previewed = true;   // shown in full by on_archive() (RULES: preview first)
    const auto r = m_door.run(plan);
    std::size_t kept = 0;
    core::catalog::Catalog cat;
    if (cat.open(m_data_dir, true))
        kept = ar::record(cat, m_door, r.batch, static_cast<std::int64_t>(std::time(nullptr)));
    if (m_changed) m_changed();
    fill();
    set_visible_child("results");
    m_status->set_text(r.ok ? "Done: " + plan.label + " (" + std::to_string(r.done) + " steps; " +
                                  std::to_string(kept) + " fingerprinted) -- undo in Activity"
                            : plan.label + ": " + std::to_string(r.done) + " done, " + std::to_string(r.failed) +
                                  " not -- " + (r.messages.empty() ? std::string() : r.messages.front()));
    if (auto lg = log::get(log::Area::Door))
        lg->info("archive run '{}': {} done, {} failed; {} fingerprinted", plan.label, r.done, r.failed, kept);
}

std::string DuePage::shown(const std::string& path) const {
    if (path.rfind(m_home + "/", 0) == 0) return "~" + path.substr(m_home.size());
    return path;
}

std::string DuePage::look_line() { return m_line; }

void DuePage::refresh_if_shown() {
    if (get_visible_child_name() == "results") fill();
}

void DuePage::fill() {
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    m_arc_rows.clear();
    m_rem_rows.clear();
    std::vector<du::Item> items;
    const auto a = du::look(m_home, m_data_dir, m_user_dirs ? m_user_dirs() : "",
                            static_cast<std::int64_t>(std::time(nullptr)), &items);
    m_line = du::line(a);
    m_summary->set_text(m_line + ".");
    if (auto lg = log::get(log::Area::Shell)) lg->info("due look: {} ({} ms)", m_line, a.ms);

    std::size_t shown_rows = 0;
    bool first = true;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (i == 0 || items[i - 1].to != it.to) {
            const std::size_t n = it.to == du::To::Remove ? a.remove : it.to == du::To::Archive ? a.archive : a.settle;
            auto h = label(std::string(du::to_title(it.to)) + " (" + std::to_string(n) + ")", "heading", 0.0f);
            h->set_margin_top(first ? 6 : 16);
            h->set_margin_start(6);
            first = false;
            add_row(m_list, *h, std::string("due.head.") + du::to_id(it.to));
        }
        if (shown_rows++ >= kMaxRows) continue;
        auto b = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        b->append(*label(std::filesystem::path(it.path).filename().string(), nullptr, 0.0f));
        b->append(*label(shown(std::filesystem::path(it.path).parent_path().string()) + " -- " + it.why, "dim", 0.0f));
        b->set_hexpand(true);
        auto line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        line->set_margin(8);
        if (it.to == du::To::Archive || it.to == du::To::Remove) {
            const bool arc = it.to == du::To::Archive;
            auto c = Gtk::make_managed<Gtk::CheckButton>();
            c->set_active(arc);   // removal: nothing checked until you check it
            c->set_valign(Gtk::Align::CENTER);
            c->set_name(std::string(arc ? "due.arc." : "due.rem.") +
                        std::to_string(arc ? m_arc_rows.size() : m_rem_rows.size()));
            line->append(*c);
            (arc ? m_arc_rows : m_rem_rows).emplace_back(c, it.path);
        }
        line->append(*b);
        add_row(m_list, *line);
    }
    if (m_archive) m_archive->set_sensitive(!m_arc_rows.empty());
    if (m_remove) m_remove->set_sensitive(!m_rem_rows.empty());
    if (a.closet || a.lists) {
        std::string s;
        if (a.closet) s += std::to_string(a.closet) + " in the Closet, proposed -- see The Closet.";
        if (a.lists) s += std::string(s.empty() ? "" : "  ") + std::to_string(a.lists) + " id list change" +
                          (a.lists == 1 ? "" : "s") + " -- main menu, Id lists...";
        auto h = label("Also", "heading", 0.0f);
        h->set_margin_top(first ? 6 : 16);
        h->set_margin_start(6);
        add_row(m_list, *h, "due.head.also");
        auto l = label(s, "dim", 0.0f);
        l->set_margin(8);
        add_row(m_list, *l);
    }
    if (!a.total()) {
        auto l = label("Nothing is due, and nothing else needs attention.", "dim", 0.0f);
        l->set_margin(8);
        add_row(m_list, *l);
    }
}

}  // namespace tidy
