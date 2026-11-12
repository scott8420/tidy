#include "HomePage.hpp"
#include "SelectList.hpp"
#include "Log.hpp"
#include "core/Archive.hpp"
#include "core/Catalog.hpp"
#include "core/Own.hpp"
#include "core/Pages.hpp"
#include "core/Retention.hpp"
#include "core/Settings.hpp"
#include "core/Tagging.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>

#include <algorithm>
#include <ctime>
#include <filesystem>

namespace tidy {

namespace ho = core::homing;
namespace pj = core::projects;
namespace dr = latch::door;
namespace fs = std::filesystem;

namespace {
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
    i->set_from_icon_name("tidy-hero-home");
    i->set_pixel_size(px);
    return i;
}
Gtk::Box* head_box(Gtk::Label** summary, const std::string& title, const std::string& name) {
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(12);
    head->append(*hero(48));
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label(title, "hero-title", 0.0f));
    *summary = label("", "dim", 0.0f);
    (*summary)->set_name(name);
    titles->append(**summary);
    titles->set_hexpand(true);
    head->append(*titles);
    return head;
}
Gtk::ScrolledWindow* scrolled(Gtk::Widget& child) {
    auto s = Gtk::make_managed<Gtk::ScrolledWindow>();
    s->set_vexpand(true);
    s->set_margin_start(32);
    s->set_margin_end(32);
    s->set_margin_bottom(16);
    s->set_child(child);
    return s;
}
Gtk::ListBox* listbox(const std::string& name) {
    auto l = Gtk::make_managed<Gtk::ListBox>();
    l->set_name(name);
    l->set_selection_mode(Gtk::SelectionMode::NONE);
    return l;
}
void clear(Gtk::ListBox* l) {
    while (auto* row = l->get_row_at_index(0)) l->remove(*row);
}
Gtk::Box* bar_box() {
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    return bar;
}
}  // namespace

HomePage::HomePage(dr::Door& door, std::string home, std::string data_dir, bool fixture, Text user_dirs, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_fixture(fixture),
      m_user_dirs(std::move(user_dirs)), m_changed(std::move(changed)) {
    set_name("home.states");
    build_start();
    build_results();
    build_preview();
    set_visible_child("start");
}

std::string HomePage::shown(const std::string& path) const {
    if (path == m_home) return "~";
    if (path.rfind(m_home + "/", 0) == 0) return "~" + path.substr(m_home.size());
    return path;
}

void HomePage::build_start() {
    const auto* p = core::pages::find("home");
    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("home.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero(128));
    start->append(*label(p ? p->title : "Files home", "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(std::string("Decided files still sitting loose -- at home's top, at a top folder's top, in Downloads or the Desktop. ") +
                      "By type, a file goes into a subfolder of its home folder (Pictures/Covers -- never loose in "
                      "Pictures); into a project, the project's template picks the folder. A name already taken "
                      "there gets \"(2)\", and the preview says so. Tidy remembers where you send each kind "
                      "(cover*.png -> Pictures/Covers) and proposes it next time. Every move undoable from Activity." +
                      (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : ""), "dim");
    note->set_name("home.note");
    note->set_max_width_chars(60);
    start->append(*note);
    auto orb = Gtk::make_managed<Gtk::Button>("Look");
    orb->set_name("home.look");
    orb->add_css_class("orb");
    orb->set_halign(Gtk::Align::CENTER);
    orb->set_margin_top(18);
    orb->signal_clicked().connect([this] { fill(); set_visible_child("results"); });
    start->append(*orb);
    add(*start, "start");
}

void HomePage::build_results() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("home.results");
    auto head = head_box(&m_summary, "Files home", "home.summary");
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("home.again");
    again->set_valign(Gtk::Align::CENTER);
    again->signal_clicked().connect([this] { fill(); });
    head->append(*again);
    box->append(*head);
    // s018c: select like Nautilus; one destination per selection.
    m_list = Gtk::make_managed<SelectList>("home.list");
    m_list->set_vexpand(true);
    m_list->set_margin_start(32);
    m_list->set_margin_end(32);
    m_list->set_margin_bottom(16);
    m_list->on_choose([this](std::size_t i) { on_set(i); });
    m_list->on_changed([this](std::size_t) { on_selection(); });
    box->append(*m_list);
    auto bar = bar_box();
    m_status = label("", "dim", 0.0f);
    m_status->set_name("home.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    // "N selected   Folder [....] [there: v]   Set to [..]"
    auto& setter = m_list->setter();
    m_folder = Gtk::make_managed<Gtk::Entry>();
    m_folder->set_name("home.folder");
    m_folder->set_width_chars(16);
    m_folder->set_placeholder_text("subfolder / folder");
    m_folder->set_tooltip_text("By type: the subfolder (required) -- Covers -> ~/Pictures/Covers.\n"
                               "Into a project: a folder in it; empty = the template's route for each file.");
    m_there = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({"there:"}));
    m_there->set_name("home.there");
    m_there->set_tooltip_text("Subfolders already in the selected files' type folders -- pick one to fill the box");
    m_there->property_selected().signal_changed().connect([this] {
        const auto s = m_there->get_selected();
        if (s == 0 || s > m_there_names.size()) return;
        m_folder->set_text(m_there_names[s - 1]);
        m_there->set_selected(0);   // a menu, not a state: the box holds the choice
    });
    auto fl = label("Folder", nullptr, 0.0f);
    fl->set_wrap(false);
    auto* count = setter.get_first_child();
    setter.insert_child_after(*fl, *count);
    setter.insert_child_after(*m_folder, *fl);
    setter.insert_child_after(*m_there, *m_folder);
    // s019: at a narrow window the status wrapped to a word per line and the
    // controls stretched to its height -- give it room, keep them their size.
    m_status->set_width_chars(26);
    setter.set_valign(Gtk::Align::CENTER);
    bar->append(setter);
    box->append(*bar);
    add(*box, "results");
}

void HomePage::build_preview() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("home.preview");
    box->append(*head_box(&m_prev_summary, "Preview", "home.preview.summary"));
    m_prev_list = listbox("home.preview.list");
    box->append(*scrolled(*m_prev_list));
    auto bar = bar_box();
    m_prev_status = label("", "dim", 0.0f);
    m_prev_status->set_name("home.preview.status");
    m_prev_status->set_hexpand(true);
    bar->append(*m_prev_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("home.preview.back");
    back->signal_clicked().connect([this] { m_plan.reset(); set_visible_child("results"); m_status->set_text("Nothing done."); });
    bar->append(*back);
    m_prev_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_prev_run->set_name("home.preview.run");
    m_prev_run->add_css_class("act");
    m_prev_run->signal_clicked().connect([this] { on_run(); });
    bar->append(*m_prev_run);
    box->append(*bar);
    add(*box, "preview");
}

void HomePage::refresh_if_shown() {
    if (get_visible_child_name() == "results") fill();
}

// The "there:" list: the subfolders already in the selected files' type
// folders (by type is where a subfolder is required). Picking one fills the box.
void HomePage::on_selection() {
    std::vector<std::string> names, seen;
    for (auto i : m_list->selected()) {
        if (i >= m_rows.size() || m_rows[i].type_folder.empty()) continue;
        const auto& tf = m_rows[i].type_folder;
        if (std::find(seen.begin(), seen.end(), tf) != seen.end()) continue;
        seen.push_back(tf);
        for (const auto& n : ho::subfolders(m_home, m_home + "/" + tf))
            if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    if (names == m_there_names) return;
    m_there_names = names;
    auto m = Gtk::StringList::create({names.empty() ? "(none there)" : "there:"});
    for (const auto& n : names) m->append(n);
    m_there->set_model(m);
    m_there->set_selected(0);
    m_there->set_sensitive(!names.empty());
}

namespace { constexpr const char* kLooking = "Looking through the home..."; }

// s019 the home sweep: every decided file loose anywhere in the home (home's
// top, a top folder's top, a landing place) -- walked off the window's thread.
void HomePage::fill() {
    m_status->set_text(kLooking);
    if (m_rows.empty()) m_summary->set_text("Looking...");
    const std::string home = m_home, data = m_data_dir, dirs = m_user_dirs ? m_user_dirs() : "";
    m_look.start(
        [home, data, dirs] {
            Look l;
            const auto st = core::settings::parse(core::own::read(data, core::settings::kFile));
            l.table = ho::table(st);
            l.projects = pj::find(home, pj::parse_left(core::own::read(data, pj::kFile)));
            std::sort(l.projects.begin(), l.projects.end(), [](const auto& a, const auto& b) { return a.rel < b.rel; });
            l.templates = core::templates::list(data);
            const auto places = core::birth::places(home, dirs);
            l.offered = ho::offered(core::birth::report(home, core::birth::sweep(home, dirs, st.catch_all)));   // s023: catch-alls too
            // s018d (Scott: "the std folders too"): By type | Into ~/<each top folder> |
            // Archive (fingerprinted) | Into <each project>.
            std::vector<std::string> skip;
            for (const auto& pl : places)
                if (pl.id == "downloads" || pl.id == "desktop") skip.push_back(fs::path(pl.path).filename().string());
            l.tops = ho::home_folders(home, skip);
            l.book = core::routes::parse(core::own::read(data, core::routes::kFile));   // s022
            return l;
        },
        [this](Look l) { show(std::move(l)); });
}

void HomePage::show(Look look) {
    m_table = std::move(look.table);
    m_projects = std::move(look.projects);
    m_templates = std::move(look.templates);
    const auto offered = std::move(look.offered);
    m_book = std::move(look.book);
    m_rows.clear();
    std::size_t typed = 0, learned = 0;
    std::vector<std::size_t> proposed;
    std::vector<SelectList::Item> items;
    for (const auto& b : offered) {
        Row r;
        r.born = b;
        const auto name = fs::path(b.file.path).filename().string();
        if (const auto* tr = ho::type_of(m_table, name)) { r.type_folder = tr->folder; r.type_pattern = tr->pattern; ++typed; }
        r.learned = core::routes::propose(m_book, m_home, b);   // s022
        core::retention::Level lv{};
        const std::string level = core::retention::parse(b.file.tags.retention, lv) ? core::retention::title_of(lv) : b.file.tags.retention;
        SelectList::Item it;
        it.title = name;
        it.line = shown(fs::path(b.file.path).parent_path().string()) + "   -- " + level +
                  (r.type_folder.empty() ? "   -- no type for it (Settings: the type table) -- into a project"
                                         : "   -- by type: ~/" + r.type_folder);
        if (!r.learned.folder.empty()) {   // s022: the proposal leads the line; the row starts selected
            it.title += "   -> ~/" + r.learned.folder;
            it.line += "   -- learned: " + r.learned.because + " (" + std::to_string(r.learned.times) +
                       (r.learned.times == 1 ? " time)" : " times)");
            proposed.push_back(m_rows.size());
            ++learned;
        }
        items.push_back(std::move(it));
        m_rows.push_back(r);
    }
    m_list->set_items(items);
    m_tops = std::move(look.tops);
    m_offer = learned ? 1 : 0;
    std::vector<std::string> choices;
    if (m_offer) choices.push_back("Its proposal  (learned)");
    choices.push_back("By type");
    for (const auto& t : m_tops) choices.push_back("Into ~/" + t);
    choices.push_back("Archive  (~/Archive/<year>, fingerprinted)");
    for (const auto& p : m_projects) choices.push_back("Into " + fs::path(p.path).filename().string());
    m_list->set_choices(choices);
    m_list->select(proposed);   // s022: the learned ones start selected (Purposes' guesses do too)
    m_there_names = {"\x01"};   // force the refill
    on_selection();
    m_summary->set_text(std::to_string(offered.size()) + " decided file" + (offered.size() == 1 ? "" : "s") +
                        " loose in the home" +
                        "   |   " + std::to_string(typed) + " with a type   |   " + std::to_string(m_projects.size()) +
                        " project" + (m_projects.size() == 1 ? "" : "s") +
                        (learned ? "   |   " + std::to_string(learned) + " with a learned home" : std::string()));
    if (m_status->get_text() != kLooking) {}   // s019: a "Done: ..." set while it looked stays
    else if (offered.empty())
        m_status->set_text("Nothing to send home: a file is offered once it carries a decision (the Closet) and still "
                           "sits loose -- at home's top, at a top folder's top, or in a landing place.");
    else
        m_status->set_text(learned ? "The files with a learned home are selected -- Set to: Its proposal. Or select "
                                     "files, name a folder, then Set to. Every move is previewed first."
                                   : "Select files, name a folder, then Set to. Every move is previewed first.");
}

// Set to (s018c): every selected file -> one destination. By type: each to its
// own type folder, the box's subfolder (required); a file with no type is left
// out (said). Into a project: the box's folder, or each file's route.
void HomePage::on_set(std::size_t choice) {
    const auto sel = m_list->selected();
    if (sel.empty()) { m_status->set_text("Select some files first."); return; }
    m_teach.clear();
    if (m_offer && choice == 0) { propose(sel); return; }   // s022: each to its learned home
    choice -= m_offer;
    const std::size_t T = m_tops.size();
    if (choice > T + 1 + m_projects.size()) return;
    if (choice == T + 1) { archive(sel); return; }   // s018d: the archive, as Due does it
    const std::string box = m_folder->get_text();
    std::vector<ho::Pick> picks;
    std::vector<Teach> teach;
    std::size_t untyped = 0;
    for (auto i : sel) {
        if (i >= m_rows.size()) continue;
        const auto& r = m_rows[i];
        const auto name = fs::path(r.born.file.path).filename().string();
        std::string folder, why, route, reason;
        if (choice == 0) {
            if (r.type_folder.empty()) { ++untyped; continue; }
            why = ho::by_type(r.type_folder, box, &folder);
            reason = "by type: " + r.type_pattern + " -> ~/" + r.type_folder;
        } else if (choice <= T) {   // s018d: a folder at home's top -- the subfolder rule as by type
            why = ho::by_type(m_tops[choice - 1], box, &folder);
            reason = "into ~/" + m_tops[choice - 1] + ": the folder you named";
        } else {
            const auto& p = m_projects[choice - T - 2];
            const auto* tm = pj::template_for(p, m_templates);
            why = ho::into_project(p.rel, tm, name, box, &folder, &route);
            // The box was empty or named the route's own folder: say the route, not "you named".
            std::string routed, via;
            if (why.empty() && route.empty() && ho::into_project(p.rel, tm, name, "", &routed, &via).empty() && routed == folder)
                route = via;
            reason = "into " + fs::path(p.path).filename().string() + ": " +
                     (route.empty() ? "the folder you named" : route + " -> " + folder.substr(p.rel.size() + 1));
        }
        if (!why.empty()) { m_status->set_text(name + ": " + why); return; }
        picks.push_back({r.born.file.path, folder, reason});
        teach.push_back({r.born, folder, r.learned});
    }
    if (picks.empty()) {
        m_status->set_text(untyped ? "No type for " + std::string(untyped == 1 ? "that file" : "those files") +
                                         " (Settings: the type table) -- Set to a project instead."
                                   : "Select some files first.");
        return;
    }
    auto planned = ho::plan(m_home, picks);
    if (untyped) planned.plan.label += " (" + std::to_string(untyped) + " without a type left out)";

    m_archiving = false;
    m_teach = std::move(teach);
    show_preview(std::move(planned.plan), planned.renamed, {}, false);
}

// s022: Its proposal -- each selected file to where you sent its kind before.
// One without a learned home is left out (said in the label).
void HomePage::propose(const std::vector<std::size_t>& sel) {
    std::vector<ho::Pick> picks;
    std::vector<Teach> teach;
    std::size_t none = 0;
    for (auto i : sel) {
        if (i >= m_rows.size()) continue;
        const auto& r = m_rows[i];
        if (r.learned.folder.empty()) { ++none; continue; }
        picks.push_back({r.born.file.path, r.learned.folder,
                         "learned: " + r.learned.because + " -> ~/" + r.learned.folder + " (" +
                             std::to_string(r.learned.times) + (r.learned.times == 1 ? " time)" : " times)")});
        teach.push_back({r.born, r.learned.folder, r.learned});
    }
    if (picks.empty()) {
        m_status->set_text("No learned home for " + std::string(none == 1 ? "that file" : "those files") +
                           " yet -- name a folder and Set to; Tidy remembers it.");
        return;
    }
    auto planned = ho::plan(m_home, picks);
    planned.plan.label = "Files home: " + std::to_string(picks.size()) + (picks.size() == 1 ? " file" : " files") +
                         " to their learned homes";
    if (none) planned.plan.label += " (" + std::to_string(none) + " without one left out)";
    m_archiving = false;
    m_teach = std::move(teach);
    show_preview(std::move(planned.plan), planned.renamed, {}, false);
}

// s018d: Archive -- the selected files into ~/Archive/<year>/<their folder>,
// tagged archived, through core::archive (Due's path): fingerprinted at Do it.
void HomePage::archive(const std::vector<std::size_t>& sel) {
    std::vector<std::string> paths;
    for (auto i : sel) if (i < m_rows.size()) paths.push_back(m_rows[i].born.file.path);
    auto planned = core::archive::plan(m_home, paths, static_cast<std::int64_t>(std::time(nullptr)));
    m_archiving = true;
    show_preview(std::move(planned.plan), planned.renamed, planned.refused, true);
}

// The plan's every step (tags, new folders, moves) and each refusal, before it runs.
void HomePage::show_preview(dr::Plan plan, const std::vector<std::string>& renamed,
                            const std::vector<std::pair<std::string, std::string>>& not_planned, bool archiving) {
    clear(m_prev_list);
    const auto row = [&](const std::string& text, const char* css) {
        auto l = label(text, css, 0.0f);
        l->set_margin(4);
        auto r = Gtk::make_managed<Gtk::ListBoxRow>();
        r->set_activatable(false);
        r->set_child(*l);
        m_prev_list->append(*r);
    };
    for (const auto& [p, why] : not_planned) row(shown(p) + "\nNOT ARCHIVED: " + why, "stop");
    std::vector<std::string> made;
    std::size_t refused = 0, moves = 0, dirs = 0, tags = 0;
    for (const auto& a : plan.actions) {
        const std::string why = m_door.refusal(a, made);   // in order: a Move may use a folder made above it
        refused += !why.empty();
        if (why.empty() && a.verb == dr::Verb::MakeDir) made.push_back(a.to);
        std::string text;
        const char* css = nullptr;
        if (a.verb == dr::Verb::Tag) {
            ++tags;
            const std::string key = a.attr.rfind(dr::kTagPrefix, 0) == 0 ? a.attr.substr(std::string(dr::kTagPrefix).size()) : a.attr;
            text = "tag   " + shown(a.from) + "   " + key + " = " + (key == "id" ? std::string("a new id") : a.content);
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
    const auto n = [](std::size_t k, const char* one) { return std::to_string(k) + " " + one + (k == 1 ? "" : "s"); };
    m_prev_summary->set_text(plan.label + "   |   " + n(moves, "move") + ", " + n(dirs, "new folder") +
                             (tags ? ", " + n(tags, "tag") : std::string()));
    std::string st = refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen. "
                             : std::string();
    if (!renamed.empty())
        st += std::to_string(renamed.size()) + " name" + (renamed.size() == 1 ? " is" : "s are") +
              " taken there -- renamed with (2), (3)... Nothing is overwritten. ";
    if (archiving) st += "After Do it each file's fingerprint (SHA-256) goes into the catalog. ";
    st += "Every step can be undone from Activity.";
    m_prev_status->set_text(st);
    m_prev_status->remove_css_class("note");
    if (!renamed.empty()) m_prev_status->add_css_class("note");
    m_prev_run->set_sensitive(moves > 0 && plan.actions.size() > refused);
    m_plan = std::move(plan);
    set_visible_child("preview");
}

void HomePage::on_run() {
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
    plan.previewed = true;   // shown in full by on_set() (RULES: preview first)
    const auto r = m_door.run(plan);
    std::size_t synced = 0, prints = 0;
    core::catalog::Catalog cat;
    if (cat.open(m_data_dir, true)) {   // the catalog follows each tagged file to its new path
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        synced = core::tagging::record(cat, core::tagging::moved_paths(m_door, r.batch), now);
        if (m_archiving) prints = core::archive::record(cat, m_door, r.batch, now);   // s018d: read back AT the archive
    }
    const bool archived = m_archiving;
    m_archiving = false;
    if (r.done > 0 && !m_teach.empty()) {   // s022: each file that moved teaches its route
        auto book = core::routes::parse(core::own::read(m_data_dir, core::routes::kFile));
        std::size_t taught = 0;
        for (const auto& t : m_teach) {
            std::error_code ec;
            if (fs::exists(t.born.file.path, ec)) continue;   // refused / not moved: nothing learned
            core::routes::learn(book, t.born, t.folder, t.proposed);
            ++taught;
        }
        if (taught) core::own::write(m_data_dir, core::routes::kFile, core::routes::format(book));
    }
    m_teach.clear();
    if (m_changed) m_changed();
    fill();
    set_visible_child("results");
    m_status->set_text(r.ok ? "Done: " + plan.label + " (" + std::to_string(r.done) + (r.done == 1 ? " step" : " steps") +
                                  (archived ? "; " + std::to_string(prints) + " fingerprinted" : std::string()) +
                                  ") -- undo in Activity"
                            : plan.label + ": " + std::to_string(r.done) + " done, " + std::to_string(r.failed) +
                                  " not -- " + (r.messages.empty() ? std::string() : r.messages.front()));
    if (auto lg = log::get(log::Area::Door))
        lg->info("home run '{}': {} done, {} failed; catalog {} followed", plan.label, r.done, r.failed, synced);
}

}  // namespace tidy
