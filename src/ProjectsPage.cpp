#include "ProjectsPage.hpp"
#include "CodeView.hpp"
#include "Log.hpp"
#include "core/Catalog.hpp"
#include "core/Facts.hpp"
#include "core/Own.hpp"
#include "core/Rescan.hpp"
#include "core/Pages.hpp"
#include "core/Starters.hpp"
#include "core/Templates.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/entry.h>
#include <gtkmm/grid.h>
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

namespace pj = core::projects;
namespace tp = core::templates;
namespace dr = latch::door;

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
    i->set_from_icon_name("tidy-hero-projects");
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
    (*summary)->set_selectable(true);
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
// Each step's refusal judged IN ORDER (s010d): a MakeDir earlier in the plan
// counts as made for the steps after it -- a new project's folders sit inside
// one the plan makes first. Asked one by one against today's disk, every
// child of a new folder read as "does not exist" and was dropped.
std::vector<std::string> refusals(const dr::Door& door, const dr::Plan& plan) {
    std::vector<std::string> out, made;
    for (const auto& a : plan.actions) {
        out.push_back(door.refusal(a, made));
        if (out.back().empty() && a.verb == dr::Verb::MakeDir) made.push_back(a.to);
    }
    return out;
}
}  // namespace

ProjectsPage::ProjectsPage(dr::Door& door, std::string home, std::string data_dir, bool fixture, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_fixture(fixture),
      m_changed(std::move(changed)) {
    set_name("projects.states");
    build_start();
    build_results();
    build_preview();
    build_new();
    m_code = Gtk::make_managed<CodeView>(m_door, m_home, m_data_dir, m_fixture, m_changed,
                                         [this] { on_look(); });
    add(*m_code, "code");
    set_visible_child("start");
}

void ProjectsPage::show_code() {
    set_visible_child("code");
    if (!m_code->looked()) m_code->fill();
}

std::string ProjectsPage::shown(const std::string& path) const {
    if (path == m_home) return "~";
    if (path.rfind(m_home + "/", 0) == 0) return "~" + path.substr(m_home.size());
    return path;
}

void ProjectsPage::build_start() {
    const auto* p = core::pages::find("projects");
    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("projects.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero(128));
    start->append(*label(p ? p->title : "Projects", "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(std::string("Look finds every folder named <name>-project, four deep in your home. Where it lives is its ") +
                      "category. One on the Desktop, in Downloads or loose in the home is asked about once: place it, "
                      "or leave it. Each placed one is checked against its category's template. Every move is a "
                      "preview first, and undoable from Activity." +
                      (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : ""), "dim");
    note->set_name("projects.note");
    note->set_max_width_chars(60);
    note->set_selectable(true);
    start->append(*note);
    auto orb = Gtk::make_managed<Gtk::Button>("Look");
    orb->set_name("projects.look");
    orb->add_css_class("orb");
    orb->set_halign(Gtk::Align::CENTER);
    orb->set_margin_top(18);
    orb->set_tooltip_text("Find the projects -- nothing is moved");
    orb->signal_clicked().connect([this] { on_look(); });
    start->append(*orb);
    auto nw = Gtk::make_managed<Gtk::Button>("New project...");
    nw->set_name("projects.start.new");
    nw->set_halign(Gtk::Align::CENTER);
    nw->set_margin_top(10);
    nw->set_tooltip_text("A template's tree under a name you give, with its id -- previewed first");
    nw->signal_clicked().connect([this] { show_new(); });
    start->append(*nw);
    auto code = Gtk::make_managed<Gtk::Button>("Code projects...");
    code->set_name("projects.start.code");
    code->set_halign(Gtk::Align::CENTER);
    code->set_tooltip_text("Each code project: pushed?, sizes (build, .git), status -- archive the done ones, clean builds");
    code->signal_clicked().connect([this] { show_code(); });
    start->append(*code);
    add(*start, "start");
}

void ProjectsPage::build_results() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("projects.results");
    auto head = head_box(&m_summary, "Projects", "projects.summary");
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("projects.again");
    again->set_valign(Gtk::Align::CENTER);
    again->signal_clicked().connect([this] { on_look(); });
    m_search = Gtk::make_managed<Gtk::Entry>();
    m_search->set_name("projects.search");
    m_search->set_placeholder_text("Search: name, status, outcome...");
    m_search->set_valign(Gtk::Align::CENTER);
    m_search->set_width_chars(26);
    m_search->signal_changed().connect([this] { render(); });
    head->append(*m_search);
    head->append(*again);
    box->append(*head);
    m_list = listbox("projects.list");
    box->append(*scrolled(*m_list));
    auto bar = bar_box();
    m_status = label("Place or leave what is uncategorized; fix what drifted. Every move is previewed first.", "dim", 0.0f);
    m_status->set_name("projects.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    m_undo_status = Gtk::make_managed<Gtk::Button>("Undo");
    m_undo_status->set_name("projects.undo_status");
    m_undo_status->set_sensitive(false);
    m_undo_status->signal_clicked().connect([this] { on_undo_status(); });
    bar->append(*m_undo_status);
    m_give_ids = Gtk::make_managed<Gtk::Button>("Give ids...");
    m_give_ids->set_name("projects.give_ids");
    m_give_ids->set_tooltip_text("A .tidy-project with a new id in each project that has none -- previewed first");
    m_give_ids->signal_clicked().connect([this] {
        auto plan = pj::give_ids(m_found, [] { return core::facts::new_id(); });
        if (!plan.actions.empty()) preview(std::move(plan));
    });
    bar->append(*m_give_ids);
    auto nw = Gtk::make_managed<Gtk::Button>("New project...");
    nw->set_name("projects.new");
    nw->signal_clicked().connect([this] { show_new(); });
    bar->append(*nw);
    auto code = Gtk::make_managed<Gtk::Button>("Code projects...");
    code->set_name("projects.code");
    code->set_tooltip_text("Each code project: pushed?, sizes (build, .git), status -- archive the done ones, clean builds");
    code->signal_clicked().connect([this] { show_code(); });
    bar->append(*code);
    box->append(*bar);
    add(*box, "results");
}

void ProjectsPage::build_preview() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("projects.preview");
    box->append(*head_box(&m_prev_summary, "Preview", "projects.preview.summary"));
    m_prev_list = listbox("projects.preview.list");
    box->append(*scrolled(*m_prev_list));
    auto bar = bar_box();
    m_prev_status = label("", "dim", 0.0f);
    m_prev_status->set_name("projects.preview.status");
    m_prev_status->set_hexpand(true);
    bar->append(*m_prev_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("projects.preview.back");
    back->signal_clicked().connect([this] {
        const bool fresh = m_plan && m_plan->label.rfind("New project: ", 0) == 0;
        m_plan.reset();
        set_visible_child(fresh ? "new" : "results");
    });
    bar->append(*back);
    m_prev_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_prev_run->set_name("projects.preview.run");
    m_prev_run->add_css_class("act");
    m_prev_run->signal_clicked().connect([this] { on_run(); });
    bar->append(*m_prev_run);
    box->append(*bar);
    add(*box, "preview");
}

void ProjectsPage::build_new() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("projects.new.page");
    Gtk::Label* sum = nullptr;
    box->append(*head_box(&sum, "New project", "projects.new.summary"));
    sum->set_text("A template's folders under a name you give. Tidy gives it an id (.tidy-project inside).");
    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(10);
    grid->set_column_spacing(12);
    grid->set_margin(24);
    grid->attach(*label("Template", nullptr, 0.0f), 0, 0);
    m_new_tpl = Gtk::make_managed<Gtk::DropDown>();
    m_new_tpl->set_name("projects.new.template");
    m_new_tpl->set_hexpand(true);
    const auto where = [this] {
        const auto i = m_new_tpl->get_selected();
        const std::string n = m_new_name->get_text();
        const tp::Template* t = i < m_new_templates.size() ? &m_new_templates[i] : nullptr;
        const int pd = t ? tp::place_depth(*t) : 0;
        // s024: a by-place template asks its group (the author) and adds no suffix.
        m_new_group_label->set_visible(pd == 2);
        m_new_group->set_visible(pd == 2);
        if (pd == 2) m_new_group_label->set_text(t->group);
        m_new_name->set_placeholder_text(pd > 0 ? "The Message in the Hollow Oak" : "Jasper  (-project is added)");
        const std::string g = m_new_group->get_text();
        const std::string folder = pd > 0 ? n : pj::folder_for(n);
        m_new_where->set_text(t && !n.empty() && (pd < 2 || !g.empty())
            ? "~/" + t->category + "/" + (pd == 2 ? g + "/" : "") + folder + "  --  " +
                  std::to_string(t->folders.size()) + " folders"
            : "");
    };
    m_new_tpl->property_selected().signal_changed().connect(where);
    m_new_refresh = where;
    grid->attach(*m_new_tpl, 1, 0);
    m_new_group_label = label("Author", nullptr, 0.0f);
    m_new_group_label->set_visible(false);
    grid->attach(*m_new_group_label, 0, 1);
    m_new_group = Gtk::make_managed<Gtk::Entry>();
    m_new_group->set_name("projects.new.group");
    m_new_group->set_placeholder_text("Keene, Carolyn");
    m_new_group->set_visible(false);
    m_new_group->signal_changed().connect(where);
    m_new_group->signal_activate().connect([this] { m_new_name->grab_focus(); });
    grid->attach(*m_new_group, 1, 1);
    grid->attach(*label("Name", nullptr, 0.0f), 0, 2);
    m_new_name = Gtk::make_managed<Gtk::Entry>();
    m_new_name->set_name("projects.new.name");
    m_new_name->set_placeholder_text("Jasper  (-project is added)");
    m_new_name->signal_changed().connect(where);
    m_new_name->signal_activate().connect([this] { on_new_preview(); });
    grid->attach(*m_new_name, 1, 2);
    m_new_where = label("", "dim", 0.0f);
    m_new_where->set_name("projects.new.where");
    grid->attach(*m_new_where, 1, 3);
    box->append(*grid);
    auto spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_vexpand(true);
    box->append(*spacer);
    auto bar = bar_box();
    m_new_status = label("Then save into it from your app.", "dim", 0.0f);
    m_new_status->set_name("projects.new.status");
    m_new_status->set_hexpand(true);
    bar->append(*m_new_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("projects.new.back");
    back->signal_clicked().connect([this] { set_visible_child(m_list->get_first_child() ? "results" : "start"); });
    bar->append(*back);
    auto pv = Gtk::make_managed<Gtk::Button>("Preview...");
    pv->set_name("projects.new.preview");
    pv->add_css_class("act");
    pv->signal_clicked().connect([this] { on_new_preview(); });
    bar->append(*pv);
    box->append(*bar);
    add(*box, "new");
}

void ProjectsPage::new_from(const std::string& name) {
    show_new();
    for (std::size_t i = 0; i < m_new_templates.size(); ++i)
        if (m_new_templates[i].name == name) m_new_tpl->set_selected(static_cast<guint>(i));
    if (m_new_group->get_visible()) m_new_group->grab_focus();
    else m_new_name->grab_focus();
}

void ProjectsPage::show_new() {
    m_new_templates = tp::list(m_data_dir);
    // s024: the built-in bookbinding is offered until a template of that name is saved.
    std::size_t builtin = m_new_templates.size();
    if (const auto* b = core::starters::find("bookbinding");
        b && std::none_of(m_new_templates.begin(), m_new_templates.end(), [&](const tp::Template& t) { return t.name == b->t.name; })) {
        m_new_templates.push_back(b->t);
    }
    auto names = Gtk::StringList::create({});
    for (std::size_t k = 0; k < m_new_templates.size(); ++k) {
        const auto& t = m_new_templates[k];
        names->append(t.name + "  (~/" + t.category + (tp::place_depth(t) == 2 ? "/<" + t.group + ">" : "") + ")" +
                      (k >= builtin ? "  -- built in" : ""));
    }
    m_new_tpl->set_model(names);
    m_new_name->set_text("");
    m_new_group->set_text("");
    m_new_where->set_text("");
    if (m_new_refresh) m_new_refresh();
    m_new_status->set_text(m_new_templates.empty()
        ? "No templates saved yet -- Templates -> Starters... -> Use -> Save, then come back."
        : "Then save into it from your app.");
    set_visible_child("new");
    if (m_new_group->get_visible()) m_new_group->grab_focus();   // s024: a book -- the author first
    else m_new_name->grab_focus();
}

void ProjectsPage::on_new_preview() {
    const auto i = m_new_tpl->get_selected();
    if (i >= m_new_templates.size()) { m_new_status->set_text("Pick a template first."); return; }
    std::string why;
    auto plan = pj::create(m_new_templates[i], m_new_name->get_text(), m_home, core::facts::new_id(), &why,
                           m_new_group->get_text());
    if (plan.actions.empty()) { m_new_status->set_text("Not possible: " + why); return; }
    preview(std::move(plan));
}

// The categories to offer: each template's, then the home's top folders --
// not hidden, not a landing place, not a project.
std::vector<std::string> ProjectsPage::categories() const {
    std::vector<std::string> out;
    const auto add = [&](const std::string& c) {
        if (!c.empty() && std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    };
    for (const auto& t : tp::list(m_data_dir)) add(t.category);
    std::vector<std::string> tops;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(m_home, ec), end; !ec && it != end; it.increment(ec)) {
        const auto fn = it->path().filename().string();
        if (fn.empty() || fn[0] == '.' || fn == "Desktop" || fn == "Downloads" || pj::is_project_name(fn)) continue;
        if (it->is_directory(ec) && !it->is_symlink(ec)) tops.push_back(fn);
    }
    std::sort(tops.begin(), tops.end());
    for (const auto& t : tops) add(t);
    return out;
}

void ProjectsPage::on_look() {
    fill();
    set_visible_child("results");
}

void ProjectsPage::refresh_if_shown() {
    if (get_visible_child_name() == "results") fill();
    if (get_visible_child_name() == "code") m_code->refresh_if_shown();
}

void ProjectsPage::fill() {
    look();
    render();
}

// Find, then tell the catalog (s010f1): seen / moved by id / back / gone.
// Read-only on the home; the catalog is opened for writing only if there is
// an id to record or a catalog already holding projects.
core::rescan::Result ProjectsPage::look() {
    const auto left = pj::parse_left(core::own::read(m_data_dir, pj::kFile));
    m_found = pj::find(m_home, left);
    core::rescan::Result r;
    r.found = m_found.size();
    const bool any_id = std::any_of(m_found.begin(), m_found.end(), [](const pj::Project& p) { return !p.id.empty(); });
    std::error_code ec;
    core::catalog::Catalog cat;
    if ((any_id || std::filesystem::exists(core::catalog::Catalog::path_in(m_data_dir), ec)) && cat.open(m_data_dir, true))
        r = core::rescan::record(m_found, pj::known_templates(m_data_dir), cat, static_cast<std::int64_t>(std::time(nullptr)));
    for (const auto& t : r.twins)
        if (auto lg = log::get(log::Area::Io)) lg->warn("projects: {} carries an id already seen -- a copy?", t);
    return r;
}

std::string ProjectsPage::rescan() {
    const auto r = look();
    const auto line = core::rescan::summary(r);
    if (get_visible_child_name() == "results") {
        render();
        if (line != "no change") {
            char hm[8];
            const auto t = std::time(nullptr);
            std::strftime(hm, sizeof hm, "%H:%M", std::localtime(&t));
            m_status->set_text(std::string("Looked again at ") + hm + ": " + line);
        }
    }
    return line;
}

void ProjectsPage::status_widgets(Gtk::Box* b, const pj::Project& p) {
    if (p.id.empty()) {
        auto l = Gtk::make_managed<Gtk::Label>("no id");
        l->add_css_class("dim");
        l->set_valign(Gtk::Align::CENTER);
        l->set_tooltip_text("Give ids... writes one (.tidy-project)");
        b->append(*l);
        return;
    }
    core::catalog::Catalog cat;
    core::catalog::ProjectRow row;
    if (cat.open(m_data_dir, false))
        if (auto r = cat.project(p.id)) row = *r;
    auto out = Gtk::make_managed<Gtk::Entry>();
    out->set_name("projects.outcome." + p.rel);
    out->set_placeholder_text("outcome...");
    out->set_width_chars(16);
    out->set_valign(Gtk::Align::CENTER);
    out->set_text(row.outcome);
    out->set_tooltip_text("What came of it -- Enter saves");
    auto model = Gtk::StringList::create({});
    guint at = 0;
    for (guint i = 0; i < pj::statuses().size(); ++i) {
        model->append(pj::statuses()[i]);
        if (pj::statuses()[i] == row.status) at = i;
    }
    auto dd = Gtk::make_managed<Gtk::DropDown>(model);
    dd->set_name("projects.status." + p.rel);
    dd->set_valign(Gtk::Align::CENTER);
    dd->set_selected(at);
    dd->set_tooltip_text("Status of the work (Tidy's catalog; id " + p.id.substr(0, 8) + ")");
    dd->property_selected().signal_changed().connect([this, dd, out, id = p.id] {
        const auto i = dd->get_selected();
        if (i < pj::statuses().size()) set_status(id, pj::statuses()[i], out->get_text());
    });
    out->signal_activate().connect([this, dd, out, id = p.id] {
        const auto i = dd->get_selected();
        if (i < pj::statuses().size()) set_status(id, pj::statuses()[i], out->get_text());
    });
    b->append(*out);
    b->append(*dd);
}

void ProjectsPage::set_status(const std::string& id, const std::string& status, const std::string& outcome) {
    core::catalog::Catalog cat;
    if (!cat.open(m_data_dir, true)) { m_status->set_text("The catalog could not be opened."); return; }
    const auto was = cat.project(id);
    if (!was) { m_status->set_text("Not in the catalog yet -- Look again."); return; }
    if (was->status == status && was->outcome == outcome) return;
    if (!cat.set_status(id, status, outcome)) { m_status->set_text("Not saved."); return; }
    m_status_undo.push_back({Was::Kind::Status, id, was->name, was->status, was->outcome, "", ""});
    undo_label();
    m_status->set_text(was->name + "-project: " + status + (outcome.empty() ? "" : " -- " + outcome));
    if (auto lg = log::get(log::Area::Io)) lg->info("project {} status {} -> {}", id, was->status, status);
}

namespace {
std::vector<pj::Use> uses_in(const core::catalog::Catalog& cat) {
    std::vector<pj::Use> out;
    for (const auto& [a, b] : cat.uses()) out.push_back({a, b});
    return out;
}
std::string name_in(const core::catalog::Catalog& cat, const std::string& id) {
    const auto r = cat.project(id);
    return r ? r->name + "-project" + (r->gone ? " (gone)" : "") : "(unknown " + id.substr(0, 8) + ")";
}
}  // namespace

void ProjectsPage::set_use(const std::string& project, const std::string& used, bool add) {
    core::catalog::Catalog cat;
    if (!cat.open(m_data_dir, true)) { m_status->set_text("The catalog could not be opened."); return; }
    const auto a = cat.project(project);
    if (!a) { m_status->set_text("Not in the catalog yet -- Look again."); return; }
    const std::string bn = name_in(cat, used);
    if (add) {
        std::string why;
        if (!pj::can_use(uses_in(cat), project, used, &why)) { m_status->set_text("Not used: " + why); return; }
        if (!cat.add_use(project, used)) { m_status->set_text("Not saved."); return; }
    } else if (!cat.drop_use(project, used)) { m_status->set_text("Not saved."); return; }
    m_status_undo.push_back({add ? Was::Kind::UseAdded : Was::Kind::UseDropped, project, a->name, "", "", used, bn});
    undo_label();
    m_status->set_text(a->name + "-project " + (add ? "uses " : "no longer uses ") + bn);
    if (auto lg = log::get(log::Area::Io)) lg->info("project {} {} {}", project, add ? "uses" : "drops", used);
    render();
}

void ProjectsPage::undo_label() {
    m_undo_status->set_sensitive(!m_status_undo.empty());
    if (m_status_undo.empty()) { m_undo_status->set_label("Undo"); m_undo_status->set_tooltip_text(""); return; }
    const auto& w = m_status_undo.back();
    m_undo_status->set_label(w.kind == Was::Kind::Status ? "Undo status" : "Undo use");
    m_undo_status->set_tooltip_text(
        w.kind == Was::Kind::Status ? w.name + "-project back to " + w.status :
        w.kind == Was::Kind::UseAdded ? w.name + "-project stops using " + w.used_name
                                      : w.name + "-project uses " + w.used_name + " again");
}

void ProjectsPage::on_undo_status() {
    if (m_status_undo.empty()) return;
    const auto w = m_status_undo.back();
    m_status_undo.pop_back();
    core::catalog::Catalog cat;
    const bool open = cat.open(m_data_dir, true);
    if (w.kind == Was::Kind::Status) {
        if (open && cat.set_status(w.id, w.status, w.outcome))
            m_status->set_text("Undone: " + w.name + "-project back to " + w.status);
    } else if (w.kind == Was::Kind::UseAdded) {
        if (open && cat.drop_use(w.id, w.used))
            m_status->set_text("Undone: " + w.name + "-project no longer uses " + w.used_name);
    } else if (open && cat.add_use(w.id, w.used)) {
        m_status->set_text("Undone: " + w.name + "-project uses " + w.used_name + " again");
    }
    undo_label();
    render();
}

void ProjectsPage::uses_row(const pj::Project& p) {
    if (p.id.empty()) return;
    core::catalog::Catalog cat;
    std::vector<pj::Use> uses;
    std::vector<core::catalog::ProjectRow> rows;
    if (cat.open(m_data_dir, false)) { uses = uses_in(cat); rows = cat.projects(); }
    const auto mine = pj::uses_of(uses, p.id);
    const auto by = pj::used_by(uses, p.id);

    auto b = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    b->set_margin_start(32);
    b->set_margin_end(8);
    b->set_margin_bottom(6);
    auto row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_activatable(false);
    row->set_child(*b);
    m_list->append(*row);

    auto head = Gtk::make_managed<Gtk::Label>(mine.empty() ? "uses nothing" : "uses");
    head->add_css_class("dim");
    b->append(*head);
    for (const auto& u : mine) {
        const std::string n = cat.is_open() ? name_in(cat, u) : u.substr(0, 8);
        auto x = Gtk::make_managed<Gtk::Button>(n + "  \u2715");
        x->add_css_class("flat");
        x->set_name("projects.unuse." + p.rel + "." + n);
        x->set_tooltip_text("Stop using it -- Tidy's catalog only; nothing on disk changes");
        x->signal_clicked().connect([this, id = p.id, u] { set_use(id, u, false); });
        b->append(*x);
    }
    if (!by.empty()) {
        std::string t = "-- used by ";
        for (std::size_t i = 0; i < by.size(); ++i) t += (i ? ", " : "") + (cat.is_open() ? name_in(cat, by[i]) : by[i].substr(0, 8));
        auto l = Gtk::make_managed<Gtk::Label>(t);
        l->add_css_class("dim");
        l->set_name("projects.usedby." + p.rel);
        b->append(*l);
    }
    // What it may use: every other project in the catalog, not gone, no loop.
    std::vector<std::string> ids;
    auto model = Gtk::StringList::create({});
    for (const auto& r : rows) {
        if (r.gone || !pj::can_use(uses, p.id, r.id, nullptr)) continue;
        ids.push_back(r.id);
        model->append(r.name + "-project");
    }
    auto spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    b->append(*spacer);
    if (ids.empty()) return;
    auto dd = Gtk::make_managed<Gtk::DropDown>(model);
    dd->set_name("projects.use." + p.rel);
    dd->set_tooltip_text("A project this one is made with (a cover, a font, a score...)");
    b->append(*dd);
    auto add = Gtk::make_managed<Gtk::Button>("Use");
    add->set_name("projects.use_add." + p.rel);
    add->set_tooltip_text("Record it in Tidy's catalog -- nothing on disk changes; Undo use takes it back");
    add->signal_clicked().connect([this, dd, ids, id = p.id] {
        const auto i = dd->get_selected();
        if (i < ids.size()) set_use(id, ids[i], true);
    });
    b->append(*add);
}

void ProjectsPage::render() {
    const std::string q = m_search ? std::string(m_search->get_text()) : std::string();
    const auto ts = pj::known_templates(m_data_dir);
    std::vector<pj::Project> found;
    {
        core::catalog::Catalog cat;
        const bool open = cat.open(m_data_dir, false);
        for (const auto& p : m_found) {
            std::vector<std::string> fields = {p.name, p.rel, p.category, p.group, p.code ? "code" : ""};
            if (const auto* t = pj::template_for(p, ts)) fields.push_back(t->name);
            if (open && !p.id.empty())
                if (auto r = cat.project(p.id)) { fields.push_back(r->status); fields.push_back(r->outcome); }
            if (p.id.empty()) fields.push_back("no id");
            else if (open) {
                const auto uses = uses_in(cat);
                for (const auto& u : pj::uses_of(uses, p.id)) fields.push_back("uses " + name_in(cat, u));
                for (const auto& u : pj::used_by(uses, p.id)) fields.push_back("used by " + name_in(cat, u));
            }
            if (pj::fits(q, fields)) found.push_back(p);
        }
    }
    const std::size_t no_id = std::count_if(m_found.begin(), m_found.end(), [](const pj::Project& p) { return p.id.empty(); });
    m_give_ids->set_visible(no_id > 0);
    m_give_ids->set_label("Give ids (" + std::to_string(no_id) + ")...");
    const auto cats = categories();
    clear(m_list);

    const auto section = [&](const std::string& text) {
        auto l = label(text, "heading", 0.0f);
        l->set_margin_top(12);
        l->set_margin_start(4);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*l);
        m_list->append(*row);
    };
    const auto row_box = [&] {
        auto b = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        b->set_margin(8);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*b);
        m_list->append(*row);
        return b;
    };
    const auto texts = [&](Gtk::Box* b, const std::string& top, const std::string& under) {
        auto t = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        t->set_hexpand(true);
        t->append(*label(top, nullptr, 0.0f));
        t->append(*label(under, "dim", 0.0f));
        b->append(*t);
    };

    std::size_t n_place = 0, n_placed = 0, n_left = 0;
    for (const auto& p : found) (p.category.empty() ? (p.left ? n_left : n_place) : n_placed)++;

    if (n_place) {
        section("To place -- uncategorized, asked once");
        for (const auto& p : found) {
            if (!p.category.empty() || p.left) continue;
            auto b = row_box();
            texts(b, p.name + "-project", "in " + shown(std::filesystem::path(p.path).parent_path().string()) +
                                              (p.code ? " -- code: stays where it is" : ""));
            status_widgets(b, p);
            uses_row(p);
            if (p.code) continue;
            auto model = Gtk::StringList::create({});
            for (const auto& c : cats) model->append(c);
            auto dd = Gtk::make_managed<Gtk::DropDown>(model);
            dd->set_name("projects.category." + p.rel);
            dd->set_tooltip_text("The category -- the home folder it will live in");
            b->append(*dd);
            auto place = Gtk::make_managed<Gtk::Button>("Place...");
            place->set_name("projects.place." + p.rel);
            place->set_sensitive(!cats.empty());
            place->signal_clicked().connect([this, p, dd, cats] {
                const auto i = dd->get_selected();
                if (i >= cats.size()) return;
                std::string why;
                auto plan = pj::place(p, m_home, cats[i], &why);
                if (plan.actions.empty()) { m_status->set_text(why); return; }
                preview(std::move(plan));
            });
            b->append(*place);
            auto leave = Gtk::make_managed<Gtk::Button>("Leave it here");
            leave->set_name("projects.leave." + p.rel);
            leave->set_tooltip_text("Not asked again (remembered in Tidy's records)");
            leave->signal_clicked().connect([this, r = p.rel] { on_leave(r, true); });
            b->append(*leave);
        }
    }
    if (n_placed) {
        section("Placed -- category is where it lives");
        for (const auto& p : found) {
            if (p.category.empty()) continue;
            auto b = row_box();
            const auto* t = pj::template_for(p, ts);
            std::string under = "~/" + p.category + (p.code ? " -- code" : "");
            std::vector<tp::Drift> d;
            std::size_t missing = 0;
            if (!t) under += " -- no template for " + p.category;
            else {
                d = tp::drift(*t, tp::folders_of(p.path, tp::depth_of(*t)));
                for (const auto& x : d) missing += x.kind == tp::Drift::Kind::Missing;
                const std::size_t extra = d.size() - missing;
                if (d.empty()) under += " -- matches " + t->name;
                else {
                    under += " -- " + t->name + ": ";
                    for (const auto& x : d)
                        under += std::string(x.kind == tp::Drift::Kind::Missing ? "-" : "+") + x.path + "  ";
                    under += "(" + std::to_string(missing) + " missing, " + std::to_string(extra) + " extra)";
                }
            }
            texts(b, shown(p.path), under);
            status_widgets(b, p);
            uses_row(p);
            // Offer Fix only when the door would make something (fixes/ is always refused: Scott's).
            std::size_t doable = 0;
            if (missing)
                for (const auto& w : refusals(m_door, pj::fix(p, d))) doable += w.empty();
            if (doable) {
                auto fix = Gtk::make_managed<Gtk::Button>("Fix...");
                fix->set_name("projects.fix." + p.rel);
                fix->set_tooltip_text("Make the folders the template names and it lacks -- extras are never moved");
                fix->signal_clicked().connect([this, p, d] { preview(pj::fix(p, d)); });
                b->append(*fix);
            }
        }
    }
    if (n_left) {
        section("Left where they are -- your answer");
        for (const auto& p : found) {
            if (!p.left) continue;
            auto b = row_box();
            texts(b, p.name + "-project", "in " + shown(std::filesystem::path(p.path).parent_path().string()));
            status_widgets(b, p);
            uses_row(p);
            auto again = Gtk::make_managed<Gtk::Button>("Ask again");
            again->set_name("projects.ask." + p.rel);
            again->signal_clicked().connect([this, r = p.rel] { on_leave(r, false); });
            b->append(*again);
        }
    }
    // s010f1: in the catalog, not found at the last look -- kept, never deleted.
    std::size_t n_gone = 0;
    {
        core::catalog::Catalog cat;
        if (cat.open(m_data_dir, false)) {
            std::vector<core::catalog::ProjectRow> gone;
            for (const auto& r : cat.projects())
                if (r.gone && pj::fits(q, {r.name, r.path, r.status, r.outcome, "gone"})) gone.push_back(r);
            n_gone = gone.size();
            if (!gone.empty()) {
                section("Gone -- not found at the last look; kept, and back when seen again");
                for (const auto& r : gone) {
                    auto b = row_box();
                    char day[16] = "";
                    const std::time_t t = r.seen;
                    std::strftime(day, sizeof day, "%Y-%m-%d", std::localtime(&t));
                    texts(b, r.name + "-project", "last at " + shown(r.path) + " -- missed " + day + " -- " + r.status +
                                                      (r.outcome.empty() ? "" : ": " + r.outcome));
                }
            }
        }
    }
    m_summary->set_text(m_found.empty() ? std::string("No -project folders found, four deep in ") + shown(m_home) :
                        found.empty() ? "Nothing fits \"" + q + "\" (" + std::to_string(m_found.size()) + " projects)" :
                        (found.size() < m_found.size() ? std::to_string(found.size()) + " of " + std::to_string(m_found.size())
                                                       : std::to_string(found.size())) +
                        " project" + (m_found.size() == 1 ? "" : "s") + ": " +
                        std::to_string(n_place) + " to place, " + std::to_string(n_placed) + " placed, " +
                        std::to_string(n_left) + " left" + (n_gone ? ", " + std::to_string(n_gone) + " gone" : ""));
    if (auto lg = log::get(log::Area::Io)) lg->info("projects look: {} found ({} to place, {} placed, {} left)",
                                                    found.size(), n_place, n_placed, n_left);
}

void ProjectsPage::on_leave(const std::string& rel, bool leave) {
    auto left = pj::parse_left(core::own::read(m_data_dir, pj::kFile));
    if (leave) left.insert(rel); else left.erase(rel);
    const auto err = core::own::write(m_data_dir, pj::kFile, pj::format_left(left));
    fill();
    m_status->set_text(!err.empty() ? "Not remembered: " + err :
                       leave ? "Left where it is -- not asked again. \"Ask again\" undoes it."
                             : "It will be asked about again.");
}

void ProjectsPage::preview(dr::Plan plan) {
    clear(m_prev_list);
    std::size_t refused = 0;
    const auto whys = refusals(m_door, plan);
    for (std::size_t k = 0; k < plan.actions.size(); ++k) {
        const auto& a = plan.actions[k];
        const std::string& why = whys[k];
        refused += !why.empty();
        std::string text = a.verb == dr::Verb::Move ? "move  " + shown(a.from) + "  ->  " + shown(a.to) + "   (whole)"
                                                    : std::string(dr::verb_name(a.verb)) + "  " + shown(a.to);
        if (!a.reason.empty()) text += "     -- " + a.reason;
        if (!why.empty()) text += "     -- REFUSED: " + why;
        auto l = label(text, why.empty() ? nullptr : "dim", 0.0f);
        l->set_margin(4);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*l);
        m_prev_list->append(*row);
    }
    m_prev_summary->set_text(plan.label + "   |   " + std::to_string(plan.actions.size()) + " step" +
                             (plan.actions.size() == 1 ? "" : "s"));
    m_prev_status->set_text(refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen"
                                    : "Every step can be undone from Activity.");
    m_prev_run->set_sensitive(plan.actions.size() > refused);
    m_plan = std::move(plan);
    set_visible_child("preview");
}

void ProjectsPage::on_run() {
    if (!m_plan) return;
    auto plan = std::move(*m_plan);
    m_plan.reset();
    {   // drop what the preview showed as refused, judged in order as it was shown
        const auto whys = refusals(m_door, plan);
        std::vector<dr::Action> keep;
        for (std::size_t k = 0; k < plan.actions.size(); ++k)
            if (whys[k].empty()) keep.push_back(plan.actions[k]);
        plan.actions = std::move(keep);
    }
    if (plan.label.rfind("Fix: ", 0) == 0) {   // the label names what will happen, refusals dropped
        const auto n = plan.actions.size();
        plan.label = plan.label.substr(0, plan.label.find(" -- ")) + " -- " + std::to_string(n) + " folder" +
                     (n == 1 ? "" : "s") + " made";
    }
    plan.previewed = true;   // shown in full by preview() (RULES: preview first)
    const auto r = m_door.run(plan);
    if (m_changed) m_changed();
    fill();
    set_visible_child("results");
    m_status->set_text(r.ok ? "Done: " + plan.label + " (" + std::to_string(r.done) + " steps) -- undo in Activity"
                            : plan.label + ": " + std::to_string(r.done) + " done, " + std::to_string(r.failed) +
                                  " not -- " + (r.messages.empty() ? std::string() : r.messages.front()));
    if (auto lg = log::get(log::Area::Door))
        lg->info("projects run '{}': {} done, {} failed", plan.label, r.done, r.failed);
}

}  // namespace tidy
