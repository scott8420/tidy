#include "ArchivePage.hpp"
#include "Log.hpp"
#include "core/Archive.hpp"
#include "core/Catalog.hpp"
#include "core/Pages.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>

#include <ctime>
#include <filesystem>
#include <map>

namespace tidy {

namespace bk = core::backup;
namespace ca = core::catalog;
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
    i->set_from_icon_name("tidy-hero-archive");
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
void set_css(Gtk::Label* l, const char* css) {
    for (const char* c : {"ok", "note", "stop", "dim"}) l->remove_css_class(c);
    if (css) l->add_css_class(css);
}
std::string n_of(std::size_t k, const char* one, const char* many = nullptr) {
    return std::to_string(k) + " " + (k == 1 ? one : many ? many : std::string(one) + "s");
}
}  // namespace

ArchivePage::ArchivePage(dr::Door& door, std::string home, std::string data_dir, bool fixture, Media media,
                         Changed changed)
    : m_door(door), m_changed(std::move(changed)), m_home(std::move(home)), m_data_dir(std::move(data_dir)),
      m_fixture(fixture), m_media(std::move(media)) {
    set_name("archive.states");
    const auto* p = core::pages::find("archive");

    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("archive.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero(128));
    start->append(*label(p ? p->title : "Archive", "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(std::string("What is in ~/Archive, and on which backup disk each file is proved: copied, "
                                  "read back from the disk, its SHA-256 the same as when it was archived. Pick "
                                  "the disks in Settings -> Backup media. Look reads only.") +
                          (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : ""), "dim");
    note->set_max_width_chars(60);
    start->append(*note);
    auto orb = Gtk::make_managed<Gtk::Button>("Look");
    orb->set_name("archive.look");
    orb->add_css_class("orb");
    orb->set_halign(Gtk::Align::CENTER);
    orb->set_margin_top(18);
    orb->signal_clicked().connect([this] { fill(); set_visible_child("results"); });
    start->append(*orb);
    add(*start, "start");

    auto results = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    results->set_name("archive.results");
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(12);
    head->append(*hero(48));
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label(p ? p->title : "Archive", "hero-title", 0.0f));
    m_summary = label("", "dim", 0.0f);
    m_summary->set_name("archive.summary");
    titles->append(*m_summary);
    titles->set_hexpand(true);
    head->append(*titles);
    results->append(*head);

    // The media bar: which disk, is it there, Back up...
    auto mbar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    mbar->set_margin_start(32);
    mbar->set_margin_end(32);
    mbar->set_margin_bottom(10);
    auto to = label("Back up to", nullptr, 0.0f);
    to->set_wrap(false);
    mbar->append(*to);
    m_pick_names = Gtk::StringList::create({});
    m_pick = Gtk::make_managed<Gtk::DropDown>(m_pick_names);
    m_pick->set_name("archive.media");
    m_pick->property_selected().signal_changed().connect([this] { if (!m_filling) fill_media(); });
    mbar->append(*m_pick);
    m_pick_state = label("", "dim", 0.0f);
    m_pick_state->set_name("archive.media.state");
    m_pick_state->set_hexpand(true);
    m_pick_state->set_selectable(true);
    mbar->append(*m_pick_state);
    m_backup = Gtk::make_managed<Gtk::Button>("Back up...");
    m_backup->set_name("archive.backup");
    m_backup->set_tooltip_text("Copy what is not yet proved there, read it back, compare (previewed first)");
    m_backup->signal_clicked().connect([this] { on_backup(); });
    mbar->append(*m_backup);
    results->append(*mbar);

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_margin_start(32);
    scroll->set_margin_end(32);
    scroll->set_margin_bottom(16);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("archive.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    results->append(*scroll);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_status = label("Removal (s014) will need one proof on a disk. Degoo is by hand, through Samson -- never proof.",
                     "dim", 0.0f);
    m_status->set_name("archive.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("archive.again");
    again->signal_clicked().connect([this] { fill(); });
    bar->append(*again);
    results->append(*bar);
    add(*results, "results");
    build_preview();
    set_visible_child("start");
}

void ArchivePage::build_preview() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("archive.preview");
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(12);
    head->append(*hero(48));
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label("Preview", "hero-title", 0.0f));
    m_prev_summary = label("", "dim", 0.0f);
    m_prev_summary->set_name("archive.preview.summary");
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
    m_prev_list->set_name("archive.preview.list");
    m_prev_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_prev_list);
    box->append(*scroll);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_prev_status = label("", "dim", 0.0f);
    m_prev_status->set_name("archive.preview.status");
    m_prev_status->set_hexpand(true);
    bar->append(*m_prev_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("archive.preview.back");
    back->signal_clicked().connect([this] {
        m_plan.reset();
        m_checks.clear();
        set_visible_child("results");
    });
    bar->append(*back);
    m_prev_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_prev_run->set_name("archive.preview.run");
    m_prev_run->add_css_class("act");
    m_prev_run->signal_clicked().connect([this] { on_run(); });
    bar->append(*m_prev_run);
    box->append(*bar);
    add(*box, "preview");
}

std::optional<core::settings::Media> ArchivePage::chosen() const {
    const auto i = m_pick ? m_pick->get_selected() : GTK_INVALID_LIST_POSITION;
    if (i == GTK_INVALID_LIST_POSITION || i >= m_shown_media.size()) return std::nullopt;
    return m_shown_media[i];
}

void ArchivePage::fill_media() {
    const auto m = chosen();
    if (!m) {
        m_pick_state->set_text("No backup media yet -- Settings -> Backup media.");
        set_css(m_pick_state, "note");
        m_backup->set_sensitive(false);
        return;
    }
    const auto why = bk::problem(*m, m_home);
    m_pick_state->set_text(why.empty() ? m->path : why);
    set_css(m_pick_state, why.empty() ? "dim" : "stop");
    m_backup->set_sensitive(why.empty());
}

void ArchivePage::on_backup() {
    const auto m = chosen();
    if (!m) return;
    ca::Catalog cat;
    std::vector<ca::Archived> arch;
    std::vector<ca::Copy> copies;
    if (cat.open(m_data_dir, false)) {
        arch = cat.archived();
        copies = cat.copies();
    }
    auto planned = bk::plan(m_home, *m, arch, copies);

    while (auto* r = m_prev_list->get_row_at_index(0)) m_prev_list->remove(*r);
    const auto row = [&](const std::string& text, const char* css) {
        auto l = label(text, css, 0.0f);
        l->set_margin(4);
        auto r = Gtk::make_managed<Gtk::ListBoxRow>();
        r->set_activatable(false);
        r->set_child(*l);
        m_prev_list->append(*r);
    };
    for (const auto& [p, why] : planned.skipped) row(shown(p) + "\nNOT BACKED UP: " + why, "stop");
    std::vector<std::string> made;
    std::size_t refused = 0, copies_n = 0, dirs = 0;
    for (const auto& a : planned.plan.actions) {
        const std::string why = m_door.refusal(a, made);
        refused += !why.empty();
        if (why.empty() && a.verb == dr::Verb::MakeDir) made.push_back(a.to);
        std::string text;
        const char* css = nullptr;
        if (a.verb == dr::Verb::MakeDir) {
            ++dirs;
            text = "new folder   " + a.to;
            css = "dim";
        } else {
            ++copies_n;
            text = shown(a.from) + "   ->   " + a.to + "\n" + a.reason;
        }
        if (!why.empty()) { text += "\nREFUSED: " + why; css = "stop"; }
        row(text, css);
    }
    for (const auto& c : planned.checks)
        row(shown(c.from) + "\nalready there: " + c.to + " -- read back and compared, not copied", "note");

    m_prev_summary->set_text(planned.plan.label + "   |   " + n_of(copies_n, "copy", "copies") + ", " +
                             n_of(dirs, "new folder") + ", " + std::to_string(planned.checks.size()) +
                             " already there" +
                             (planned.proved ? ", " + std::to_string(planned.proved) + " proved before" : ""));
    std::string st = refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen. "
                             : std::string();
    if (!copies_n && planned.checks.empty())
        st += "Nothing to back up: every archived file is proved on " + m->label + " already.";
    else
        st += "Nothing on " + m->label + " is overwritten. After Do it each copy is read back from the disk and its "
              "SHA-256 compared with the archive's; only a match is kept as proof. Undo in Activity puts the copies "
              "in " + m->label + "'s own trash.";
    m_prev_status->set_text(st);
    m_prev_run->set_sensitive(copies_n > refused || !planned.checks.empty());
    m_plan = std::move(planned.plan);
    m_checks = std::move(planned.checks);
    m_plan_media = *m;
    set_visible_child("preview");
}

void ArchivePage::on_run() {
    if (!m_plan || !m_plan_media) return;
    auto plan = std::move(*m_plan);
    const auto checks = std::move(m_checks);
    const auto media = *m_plan_media;
    m_plan.reset();
    m_checks.clear();
    m_plan_media.reset();
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
    dr::Result r;
    r.ok = true;
    if (!plan.actions.empty()) {
        plan.previewed = true;   // shown in full by on_backup() (RULES: preview first)
        r = m_door.run(plan);
    }
    std::vector<bk::Proof> proofs;
    ca::Catalog cat;
    if (cat.open(m_data_dir, true))
        proofs = bk::prove(cat, m_door, r.batch, media, checks, static_cast<std::int64_t>(std::time(nullptr)));
    std::size_t ok = 0;
    std::string bad;
    for (const auto& p : proofs) {
        if (p.ok) { ++ok; continue; }
        bad += "\n  " + p.path + ": " + p.why;
        if (p.was_there) bad += " -- not Tidy's copy; Tidy never overwrites it. Move it aside on the disk, then Back up again.";
    }
    const std::size_t nbad = proofs.size() - ok;
    if (m_changed) m_changed();
    fill();
    set_visible_child("results");
    std::string st = plan.actions.empty() ? std::string("Read back on ") + media.label + ": "
                                          : (r.ok ? "Done: " : "Partly done: ") + plan.label + " (" +
                                                std::to_string(r.done) + " steps" +
                                                (r.failed ? ", " + std::to_string(r.failed) + " failed" : "") + "): ";
    st += std::to_string(ok) + " proved on " + media.label;
    if (nbad) st += "; " + std::to_string(nbad) + " did NOT match -- not proved:" + bad;
    if (!r.ok && !r.messages.empty()) st += "\n" + r.messages.front();
    if (!plan.actions.empty()) st += "\nUndo in Activity.";
    m_status->set_text(st);
    set_css(m_status, nbad || !r.ok ? "stop" : "ok");
    if (auto lg = log::get(log::Area::Door))
        lg->info("backup run '{}' -> {}: {} done, {} failed; {} proved, {} not", plan.label, media.path, r.done,
                 r.failed, ok, nbad);
}

std::string ArchivePage::shown(const std::string& path) const {
    if (path.rfind(m_home + "/", 0) == 0) return "~" + path.substr(m_home.size());
    return path;
}

void ArchivePage::refresh_if_shown() {
    if (get_visible_child_name() == "results") fill();
}

void ArchivePage::fill() {
    // The media dropdown: keep the pick by label across refreshes.
    m_filling = true;
    const auto before = chosen();
    m_shown_media = m_media ? m_media() : std::vector<core::settings::Media>{};
    std::vector<Glib::ustring> names;
    for (const auto& m : m_shown_media) names.push_back(m.label);
    m_pick_names->splice(0, m_pick_names->get_n_items(), names);
    guint sel = 0;
    for (guint i = 0; i < m_shown_media.size(); ++i)
        if (before && m_shown_media[i].label == before->label) sel = i;
    if (!m_shown_media.empty()) m_pick->set_selected(sel);
    m_filling = false;
    fill_media();

    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    ca::Catalog cat;
    std::vector<ca::Archived> arch;
    std::map<std::string, std::vector<ca::Copy>> by_id;
    if (cat.open(m_data_dir, false)) {
        arch = cat.archived();
        for (auto& c : cat.copies()) by_id[c.id].push_back(std::move(c));
    }
    std::map<std::string, std::size_t> per_media;
    std::size_t none = 0, rows = 0;
    for (const auto& a : arch) {
        const auto it = by_id.find(a.id);
        const std::vector<ca::Copy> mine = it == by_id.end() ? std::vector<ca::Copy>{} : it->second;
        if (mine.empty()) ++none;
        for (const auto& c : mine) ++per_media[c.media];
        if (rows++ >= kMaxRows) continue;
        auto b = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        b->set_margin(8);
        b->append(*label(std::filesystem::path(a.path).filename().string(), nullptr, 0.0f));
        b->append(*label(shown(std::filesystem::path(a.path).parent_path().string()) + " -- archived " +
                             core::archive::day_of(a.when), "dim", 0.0f));
        b->append(*label(bk::status_line(mine), mine.empty() ? "note" : "ok", 0.0f));
        add_row(m_list, *b, "archive.row." + std::to_string(rows - 1));
    }
    std::string s = n_of(arch.size(), "file") + " archived";
    for (const auto& [m, k] : per_media) s += "; " + std::to_string(k) + " on " + m;
    if (none) s += "; " + std::to_string(none) + " not on media yet";
    m_summary->set_text(s + ".");
    if (arch.empty()) {
        auto l = label("Nothing archived yet -- Due -> Look -> To archive.", "dim", 0.0f);
        l->set_margin(8);
        add_row(m_list, *l);
    }
    if (auto lg = log::get(log::Area::Shell))
        lg->info("archive look: {} archived, {} not on media", arch.size(), none);
}

}  // namespace tidy
