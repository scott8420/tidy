#include "HomePage.hpp"
#include "Log.hpp"
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
constexpr std::size_t kMaxRows = 300;
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
Gtk::Box* append_row(Gtk::ListBox* list, int margin = 8) {
    auto b = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    b->set_margin(margin);
    auto row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_activatable(false);
    row->set_child(*b);
    list->append(*row);
    return b;
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
    auto note = label(std::string("Decided files still sitting in Downloads, the Desktop and the other landing places. ") +
                      "By type, a file goes into a subfolder of its home folder (Pictures/Covers -- never loose in "
                      "Pictures); into a project, the project's template picks the folder. A name already taken "
                      "there gets \"(2)\", and the preview says so. Every move undoable from Activity." +
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
    m_list = listbox("home.list");
    box->append(*scrolled(*m_list));
    auto bar = bar_box();
    m_status = label("Pick where each file goes, check it, then Go home.", "dim", 0.0f);
    m_status->set_name("home.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto go = Gtk::make_managed<Gtk::Button>("Go home...");
    go->set_name("home.go");
    go->add_css_class("act");
    go->set_tooltip_text("The checked files -> the folder beside each (previewed first)");
    go->signal_clicked().connect([this] { on_go(); });
    bar->append(*go);
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
    back->signal_clicked().connect([this] { m_plan.reset(); set_visible_child("results"); });
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

int HomePage::target_of(const Row& r) const {
    const auto i = static_cast<int>(r.to->get_selected());
    if (i <= 0) return -1;
    if (!r.type_folder.empty()) {
        if (i == 1) return -2;
        return i - 2;
    }
    return i - 1;
}

// The row's destination changed: refill what is there already, and the folder
// (by type: empty -- the subfolder is the user's to pick or name; a project:
// its template's route, if any).
void HomePage::on_to_changed(std::size_t idx) {
    auto& r = m_rows[idx];
    const int t = target_of(r);
    std::string dir, fill_with, hint = "a folder";
    if (t == -2) {
        dir = m_home + "/" + r.type_folder;
        hint = "a subfolder (required)";
    } else if (t >= 0) {
        const auto& p = m_projects[static_cast<std::size_t>(t)];
        dir = p.path;
        const auto* tm = pj::template_for(p, m_templates);
        std::string folder;
        if (ho::into_project(p.rel, tm, fs::path(r.born.file.path).filename().string(), "", &folder).empty())
            fill_with = folder.substr(p.rel.size() + 1);
        hint = "a folder in it";
    }
    r.existing_names = dir.empty() ? std::vector<std::string>{} : ho::subfolders(m_home, dir);
    if (t >= 0) {   // a project: the template's folders too, even if not made yet
        const auto& p = m_projects[static_cast<std::size_t>(t)];
        if (const auto* tm = pj::template_for(p, m_templates))
            for (const auto& f : tm->folders)
                if (std::find(r.existing_names.begin(), r.existing_names.end(), f.path) == r.existing_names.end() &&
                    ho::into_project(p.rel, tm, "x", f.path, nullptr).empty())
                    r.existing_names.push_back(f.path);
        std::sort(r.existing_names.begin(), r.existing_names.end());
    }
    auto m = Gtk::StringList::create({r.existing_names.empty() ? "(none yet)" : "there:"});
    for (const auto& n : r.existing_names) m->append(n);
    r.existing->set_model(m);
    r.existing->set_selected(0);
    r.existing->set_sensitive(!r.existing_names.empty());
    r.folder->set_text(fill_with);
    r.folder->set_placeholder_text(hint);
    r.folder->set_sensitive(t != -1);
}

void HomePage::fill() {
    m_table = ho::table(core::settings::parse(core::own::read(m_data_dir, core::settings::kFile)));
    m_projects = pj::find(m_home, pj::parse_left(core::own::read(m_data_dir, pj::kFile)));
    std::sort(m_projects.begin(), m_projects.end(), [](const auto& a, const auto& b) { return a.rel < b.rel; });
    m_templates = core::templates::list(m_data_dir);
    const auto places = core::birth::places(m_home, m_user_dirs ? m_user_dirs() : "");
    const auto offered = ho::offered(core::birth::report(m_home, places));
    clear(m_list);
    m_rows.clear();
    m_rows.reserve(std::min(offered.size(), kMaxRows));

    std::size_t typed = 0;
    for (const auto& b : offered) {
        if (m_rows.size() >= kMaxRows) break;
        Row r;
        r.born = b;
        const auto name = fs::path(b.file.path).filename().string();
        if (const auto* tr = ho::type_of(m_table, name)) { r.type_folder = tr->folder; r.type_pattern = tr->pattern; ++typed; }

        auto box = append_row(m_list, 4);
        r.check = Gtk::make_managed<Gtk::CheckButton>();
        r.check->set_name("home.file." + b.file.path);
        r.check->set_valign(Gtk::Align::CENTER);
        box->append(*r.check);
        auto t = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        t->set_hexpand(true);
        t->append(*label(name, nullptr, 0.0f));
        core::retention::Level lv{};
        const std::string level = core::retention::parse(b.file.tags.retention, lv) ? core::retention::title_of(lv) : b.file.tags.retention;
        t->append(*label(shown(fs::path(b.file.path).parent_path().string()) + "   -- " + level +
                         (r.type_folder.empty() ? "   -- no type for it (Settings: the type table)" : ""),
                         "dim", 0.0f));
        box->append(*t);

        auto to = Gtk::StringList::create({"--"});
        if (!r.type_folder.empty()) to->append("by type: ~/" + r.type_folder);
        for (const auto& p : m_projects) to->append("into " + fs::path(p.path).filename().string());
        r.to = Gtk::make_managed<Gtk::DropDown>(to);
        r.to->set_name("home.to." + b.file.path);
        r.to->set_valign(Gtk::Align::CENTER);
        r.to->set_size_request(240, -1);   // columns line up row to row
        box->append(*r.to);
        r.folder = Gtk::make_managed<Gtk::Entry>();
        r.folder->set_name("home.folder." + b.file.path);
        r.folder->set_width_chars(16);
        r.folder->set_valign(Gtk::Align::CENTER);
        box->append(*r.folder);
        r.existing = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({"(none yet)"}));
        r.existing->set_name("home.existing." + b.file.path);
        r.existing->set_valign(Gtk::Align::CENTER);
        r.existing->set_size_request(130, -1);
        r.existing->set_tooltip_text("Folders already there -- pick one to fill the box");
        box->append(*r.existing);
        m_rows.push_back(r);
    }
    // Signals after the vector stops moving: rows are found by index.
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        auto& r = m_rows[i];
        r.to->property_selected().signal_changed().connect([this, i] { on_to_changed(i); });
        r.existing->property_selected().signal_changed().connect([this, i] {
            auto& x = m_rows[i];
            const auto s = x.existing->get_selected();
            if (s == 0 || s > x.existing_names.size()) return;
            x.folder->set_text(x.existing_names[s - 1]);
        });
        r.folder->signal_changed().connect([this, i] {
            auto& x = m_rows[i];
            if (!x.folder->get_text().empty() && target_of(x) != -1) x.check->set_active(true);
        });
        if (!r.type_folder.empty()) r.to->set_selected(1);   // by type, first; the subfolder is still to pick
        else on_to_changed(i);
    }
    m_summary->set_text(std::to_string(offered.size()) + " decided file" + (offered.size() == 1 ? "" : "s") +
                        " in the landing places" + (offered.size() > kMaxRows ? " (first " + std::to_string(kMaxRows) + " shown)" : "") +
                        "   |   " + std::to_string(typed) + " with a type   |   " + std::to_string(m_projects.size()) +
                        " project" + (m_projects.size() == 1 ? "" : "s"));
    if (offered.empty())
        m_status->set_text("Nothing to send home: a file is offered once it carries a decision (the Closet) and still "
                           "sits in a landing place.");
    else
        m_status->set_text("Pick where each file goes, check it, then Go home.");
}

void HomePage::on_go() {
    std::vector<ho::Pick> picks;
    for (const auto& r : m_rows) {
        if (!r.check->get_active()) continue;
        const auto name = fs::path(r.born.file.path).filename().string();
        const int t = target_of(r);
        std::string folder, why, route;
        if (t == -1) why = "pick where it goes";
        else if (t == -2) why = ho::by_type(r.type_folder, r.folder->get_text(), &folder);
        else {
            const auto& p = m_projects[static_cast<std::size_t>(t)];
            const auto* tm = pj::template_for(p, m_templates);
            why = ho::into_project(p.rel, tm, name, r.folder->get_text(), &folder, &route);
            // The box was filled from the route and left as it was: say the route, not "you named".
            std::string routed, via;
            if (why.empty() && route.empty() && ho::into_project(p.rel, tm, name, "", &routed, &via).empty() && routed == folder)
                route = via;
        }
        if (!why.empty()) { m_status->set_text(name + ": " + why); return; }
        std::string reason;
        if (t == -2) reason = "by type: " + r.type_pattern + " -> ~/" + r.type_folder;
        else {
            const auto& p = m_projects[static_cast<std::size_t>(t)];
            reason = "into " + fs::path(p.path).filename().string() + ": " +
                     (route.empty() ? "the folder you named" : route + " -> " + folder.substr(p.rel.size() + 1));
        }
        picks.push_back({r.born.file.path, folder, reason});
    }
    if (picks.empty()) { m_status->set_text("Check a file and pick where it goes first."); return; }
    auto planned = ho::plan(m_home, picks);

    clear(m_prev_list);
    std::vector<std::string> made;
    std::size_t refused = 0, moves = 0;
    for (const auto& a : planned.plan.actions) {
        const std::string why = m_door.refusal(a, made);   // in order: a Move may use a folder made above it
        refused += !why.empty();
        if (why.empty() && a.verb == dr::Verb::MakeDir) made.push_back(a.to);
        std::string text;
        const char* css = nullptr;
        if (a.verb == dr::Verb::MakeDir) {
            text = "new folder   " + shown(a.to);
            css = "dim";
        } else {
            ++moves;
            text = shown(a.from) + "   ->   " + shown(a.to) + "\n" + a.reason;
            if (a.reason.find("is taken there") != std::string::npos) css = "note";
        }
        if (!why.empty()) { text += "\nREFUSED: " + why; css = "stop"; }
        auto l = label(text, css, 0.0f);
        l->set_margin(4);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*l);
        m_prev_list->append(*row);
    }
    m_prev_summary->set_text(planned.plan.label + "   |   " + std::to_string(moves) + " move" + (moves == 1 ? "" : "s") +
                             ", " + std::to_string(planned.plan.actions.size() - moves) + " new folder" +
                             (planned.plan.actions.size() - moves == 1 ? "" : "s"));
    std::string st = refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen. "
                             : std::string();
    if (!planned.renamed.empty())
        st += std::to_string(planned.renamed.size()) + " name" + (planned.renamed.size() == 1 ? " is" : "s are") +
              " taken there -- renamed with (2), (3)... Nothing is overwritten. ";
    st += "Every step can be undone from Activity.";
    m_prev_status->set_text(st);
    m_prev_status->remove_css_class("note");
    if (!planned.renamed.empty()) m_prev_status->add_css_class("note");
    m_prev_run->set_sensitive(planned.plan.actions.size() > refused);
    m_plan = std::move(planned.plan);
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
    plan.previewed = true;   // shown in full by on_go() (RULES: preview first)
    const auto r = m_door.run(plan);
    std::size_t synced = 0;
    core::catalog::Catalog cat;
    if (cat.open(m_data_dir, true))   // the catalog follows each tagged file to its new path
        synced = core::tagging::record(cat, core::tagging::moved_paths(m_door, r.batch),
                                       static_cast<std::int64_t>(std::time(nullptr)));
    if (m_changed) m_changed();
    fill();
    set_visible_child("results");
    m_status->set_text(r.ok ? "Done: " + plan.label + " (" + std::to_string(r.done) + " steps) -- undo in Activity"
                            : plan.label + ": " + std::to_string(r.done) + " done, " + std::to_string(r.failed) +
                                  " not -- " + (r.messages.empty() ? std::string() : r.messages.front()));
    if (auto lg = log::get(log::Area::Door))
        lg->info("home run '{}': {} done, {} failed; catalog {} followed", plan.label, r.done, r.failed, synced);
}

}  // namespace tidy
