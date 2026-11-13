#include "TemplatesPage.hpp"
#include "Log.hpp"
#include "core/Pages.hpp"
#include "core/Starters.hpp"
#include "core/Projects.hpp"

#include <algorithm>

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/entry.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>
#include <gdkmm/contentprovider.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/droptarget.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/popover.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/window.h>
#include <giomm/file.h>
#include <glibmm/main.h>

namespace tidy {

namespace tp = core::templates;

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
    i->set_from_icon_name("tidy-hero-templates");
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
Gtk::DropDown* role_dropdown(tp::Role r) {
    auto names = Gtk::StringList::create({});
    for (auto x : tp::roles()) names->append(tp::title_of(x));
    auto dd = Gtk::make_managed<Gtk::DropDown>(names);
    dd->set_size_request(120, -1);
    for (std::size_t i = 0; i < tp::roles().size(); ++i)
        if (tp::roles()[i] == r) dd->set_selected(static_cast<guint>(i));
    std::string tip;
    for (auto x : tp::roles()) tip += std::string(tip.empty() ? "" : "\n") + tp::title_of(x) + ": " + tp::meaning_of(x);
    dd->set_tooltip_text(tip);
    return dd;
}
void clear(Gtk::ListBox* l) {
    while (auto* row = l->get_row_at_index(0)) l->remove(*row);
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
}  // namespace

TemplatesPage::TemplatesPage(Gtk::Window& parent, std::string home, std::string data_dir, bool fixture)
    : m_parent(parent), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_fixture(fixture) {
    set_name("templates.states");
    build_start();
    build_list();
    build_builder();
    build_starters();
    show_home();
}

std::string TemplatesPage::shown(const std::string& path) const {
    if (path == m_home) return "~";
    if (path.rfind(m_home + "/", 0) == 0) return "~" + path.substr(m_home.size());
    return path;
}

// ── Zones ──────────────────────────────────────────────────────────────────
void TemplatesPage::build_start() {
    const auto* p = core::pages::find("templates");
    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("templates.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero(128));
    start->append(*label(p ? p->title : "Templates", "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(std::string("No templates yet. Start from a built-in starter (the creative base and its kinds), the common shell "
                      "(backups, docs, fixes, scripts), or ") +
                      "lift one from a project you have -- a proposal you go through folder by folder, never "
                      "copied as-is. Saved in " + shown(m_data_dir) + "/templates." +
                      (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : ""), "dim");
    note->set_name("templates.note");
    note->set_max_width_chars(60);
    note->set_selectable(true);
    start->append(*note);
    auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    row->set_halign(Gtk::Align::CENTER);
    row->set_margin_top(18);
    auto sts = Gtk::make_managed<Gtk::Button>("Starters...");
    sts->set_name("templates.start.starters");
    sts->set_valign(Gtk::Align::CENTER);
    sts->set_tooltip_text("Built-in templates for creative work -- open one, change what you like, Save");
    sts->signal_clicked().connect([this] { show_starters(); });
    row->append(*sts);
    auto lift = Gtk::make_managed<Gtk::Button>("Lift from a project...");
    lift->set_name("templates.start.lift");
    lift->set_valign(Gtk::Align::CENTER);
    lift->set_tooltip_text("Read a project's folders, two deep, as a proposal -- nothing in it is touched");
    lift->signal_clicked().connect([this] { on_lift(); });
    row->append(*lift);
    auto orb = Gtk::make_managed<Gtk::Button>("New");
    orb->set_name("templates.start.new");
    orb->add_css_class("orb");
    orb->set_tooltip_text("A new template from the common shell");
    orb->signal_clicked().connect([this] { on_new(); });
    row->append(*orb);
    start->append(*row);
    add(*start, "start");
}

void TemplatesPage::build_list() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("templates.list.page");
    box->append(*head_box(&m_list_summary, "Templates", "templates.list.summary"));
    m_list = listbox("templates.list");
    box->append(*scrolled(*m_list));
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_list_status = label("Open one to edit it, or check a project against it.", "dim", 0.0f);
    m_list_status->set_name("templates.list.status");
    m_list_status->set_hexpand(true);
    bar->append(*m_list_status);
    auto sts = Gtk::make_managed<Gtk::Button>("Starters...");
    sts->set_name("templates.list.starters");
    sts->signal_clicked().connect([this] { show_starters(); });
    bar->append(*sts);
    auto lift = Gtk::make_managed<Gtk::Button>("Lift from a project...");
    lift->set_name("templates.list.lift");
    lift->signal_clicked().connect([this] { on_lift(); });
    bar->append(*lift);
    auto nw = Gtk::make_managed<Gtk::Button>("New template");
    nw->set_name("templates.list.new");
    nw->add_css_class("act");
    nw->signal_clicked().connect([this] { on_new(); });
    bar->append(*nw);
    box->append(*bar);
    add(*box, "list");
}

void TemplatesPage::build_builder() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("templates.builder");
    box->append(*head_box(&m_b_summary, "Template builder", "templates.builder.summary"));

    auto body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    body->set_margin_end(12);

    const auto field = [&](const char* text, Gtk::Entry** e, const std::string& name, const char* tip, auto commit) {
        auto r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto l = label(text, nullptr, 0.0f);
        l->set_size_request(90, -1);
        r->append(*l);
        *e = Gtk::make_managed<Gtk::Entry>();
        (*e)->set_name(name);
        (*e)->set_hexpand(true);
        (*e)->set_tooltip_text(tip);
        (*e)->signal_activate().connect(commit);
        r->append(**e);
        body->append(*r);
    };
    field("Name", &m_b_name, "templates.builder.name", "a-z, 0-9 and '-' (writing-project). Enter sets it.",
          [this] { if (std::string(m_b_name->get_text()) != m_b.tpl().name) edit(m_b.set_name(m_b_name->get_text())); });
    field("Category", &m_b_category, "templates.builder.category",
          "The home folder its projects live in (Writing, Develop). Enter sets it.",
          [this] { if (std::string(m_b_category->get_text()) != m_b.tpl().category) edit(m_b.set_category(m_b_category->get_text())); });

    {   // s024: by place -- ~/<category>/<group>/<Name>, no -project (Bookbinding)
        auto r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto l = label("", nullptr, 0.0f);
        l->set_size_request(90, -1);
        r->append(*l);
        m_b_by_place = Gtk::make_managed<Gtk::CheckButton>("Known by its place (no -project)");
        m_b_by_place->set_name("templates.builder.by_place");
        m_b_by_place->set_tooltip_text("Each folder at ~/<category>/<group>/<Name> IS a project -- kept whole, no -project suffix");
        m_b_by_place->signal_toggled().connect([this] {
            if (!m_b_quiet) edit(m_b.set_by_place(m_b_by_place->get_active()));
        });
        r->append(*m_b_by_place);
        m_b_group = Gtk::make_managed<Gtk::Entry>();
        m_b_group->set_name("templates.builder.group");
        m_b_group->set_placeholder_text("Group level, e.g. Author (Last, First) -- empty = none");
        m_b_group->set_tooltip_text("A folder level between the category and each project (the author). Enter sets it.");
        m_b_group->set_hexpand(true);
        m_b_group->signal_activate().connect([this] {
            if (std::string(m_b_group->get_text()) != m_b.tpl().group) edit(m_b.set_group(m_b_group->get_text()));
        });
        r->append(*m_b_group);
        body->append(*r);
    }

    // s024d: the tree -- folders and what goes in each, like Files.
    body->append(*label("The tree -- each folder and what goes in it", "heading", 0.0f));
    body->append(*label("Right-click a folder (or its \u22ef) for New folder, Rename, This goes here..., Remove. Drag a rule "
                        "to another folder. Drop a file from Files on a folder: its type goes there. A file no rule fits "
                        "is asked when you organize.", "dim", 0.0f));
    m_b_folders = listbox("templates.builder.folders");
    m_b_folders->add_css_class("boxed-list");
    body->append(*m_b_folders);
    m_tree_pop = Gtk::make_managed<Gtk::Popover>();
    m_tree_pop->set_has_arrow(false);
    m_tree_pop->set_parent(*m_b_folders);

    m_b_drift_head = label("", "heading", 0.0f);
    m_b_drift_head->set_name("templates.builder.drift_head");
    m_b_drift_head->set_selectable(true);
    body->append(*m_b_drift_head);
    m_b_drift = listbox("templates.builder.drift");
    m_b_drift->add_css_class("boxed-list");
    body->append(*m_b_drift);

    m_b_scroll = scrolled(*body);
    box->append(*m_b_scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_b_status = label("", "dim", 0.0f);
    m_b_status->set_name("templates.builder.status");
    m_b_status->set_hexpand(true);
    bar->append(*m_b_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("templates.builder.back");
    back->signal_clicked().connect([this] { on_back(); });
    bar->append(*back);
    auto drift = Gtk::make_managed<Gtk::Button>("Drift check...");
    drift->set_name("templates.builder.drift_check");
    drift->set_tooltip_text("Compare a project to this template, two deep -- the project is only read");
    drift->signal_clicked().connect([this] { on_drift_pick(); });
    bar->append(*drift);
    m_b_undo = Gtk::make_managed<Gtk::Button>("Undo");
    m_b_undo->set_name("templates.builder.undo");
    m_b_undo->signal_clicked().connect([this] { on_undo(); });
    bar->append(*m_b_undo);
    m_b_save = Gtk::make_managed<Gtk::Button>("Save");
    m_b_save->set_name("templates.builder.save");
    m_b_save->add_css_class("act");
    m_b_save->signal_clicked().connect([this] { on_save(); });
    bar->append(*m_b_save);
    box->append(*bar);
    add(*box, "builder");
}

void TemplatesPage::build_starters() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("templates.starters.page");
    box->append(*head_box(&m_st_summary, "Starters", "templates.starters.summary"));
    m_st_list = listbox("templates.starters");
    box->append(*scrolled(*m_st_list));
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    auto note = label("Use opens one in the builder. Nothing is written until you Save.", "dim", 0.0f);
    note->set_hexpand(true);
    bar->append(*note);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("templates.starters.back");
    back->signal_clicked().connect([this] { show_home(); });
    bar->append(*back);
    box->append(*bar);
    add(*box, "starters");
}

// ── States ─────────────────────────────────────────────────────────────────
void TemplatesPage::show_starters() {
    m_saved = tp::list(m_data_dir);
    clear(m_st_list);
    for (const auto& s : core::starters::all()) {
        const auto& t = s.t;
        const bool have = std::any_of(m_saved.begin(), m_saved.end(), [&](const tp::Template& x) { return x.name == t.name; });
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        row->set_margin(8);
        auto text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        text->set_hexpand(true);
        text->append(*label(t.name + (have ? "  (saved)" : ""), nullptr, 0.0f));
        text->append(*label(s.about + " -- ~/" + t.category + ", " + std::to_string(t.folders.size()) + " folders, " +
                            std::to_string(t.routes.size()) + " routes", "dim", 0.0f));
        row->append(*text);
        auto use = Gtk::make_managed<Gtk::Button>("Use...");
        use->set_name("templates.starters.use." + t.name);
        use->set_tooltip_text(have ? "Saved already -- rename it in the builder to keep a second one"
                                   : "Open in the builder; Save writes it");
        use->signal_clicked().connect([this, t, have] {
            open(tp::Builder(tp::Draft{t, {}, ""}));
            m_b_status->set_text(have
                ? "The starter " + t.name + " -- that name is saved already. Rename it to keep this one too."
                : "The starter " + t.name + ". Change what you like; Save writes it. Nothing is written until then.");
        });
        row->append(*use);
        m_st_list->append(*row);
    }
    m_st_summary->set_text(std::to_string(core::starters::all().size()) + " built in -- the creative base, its kinds, and code");
    set_visible_child("starters");
}

void TemplatesPage::refresh() {
    const auto c = get_visible_child_name();
    if (c == "start" || c == "list") show_home();
}

void TemplatesPage::show_home() {
    m_saved = tp::list(m_data_dir);
    if (m_saved.empty()) { set_visible_child("start"); return; }
    clear(m_list);
    for (const auto& t : m_saved) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        row->set_margin(8);
        auto text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        text->set_hexpand(true);
        text->append(*label(t.name, nullptr, 0.0f));
        text->append(*label("~/" + t.category + " -- " + std::to_string(t.folders.size()) + (t.folders.size() == 1 ? " folder, " : " folders, ") +
                            std::to_string(t.routes.size()) + (t.routes.size() == 1 ? " route" : " routes"), "dim", 0.0f));
        row->append(*text);
        auto drift = Gtk::make_managed<Gtk::Button>("Drift check...");
        drift->set_name("templates.list.drift." + t.name);
        drift->signal_clicked().connect([this, t] {
            open(tp::Builder(tp::Draft{t, {}, ""}, t.name));
            on_drift_pick();
        });
        row->append(*drift);
        auto op = Gtk::make_managed<Gtk::Button>("Open");
        op->set_name("templates.list.open." + t.name);
        op->signal_clicked().connect([this, t] { open(tp::Builder(tp::Draft{t, {}, ""}, t.name)); });
        row->append(*op);
        m_list->append(*row);
    }
    m_list_summary->set_text(std::to_string(m_saved.size()) + " template" + (m_saved.size() == 1 ? "" : "s") +
                             " in " + shown(m_data_dir) + "/templates");
    set_visible_child("list");
}

void TemplatesPage::open(tp::Builder b) {
    m_b = std::move(b);
    m_b_scroll->get_vadjustment()->set_value(0);
    m_drift.clear();
    m_drift_dir.clear();
    m_back_armed = false;
    m_b_status->set_text(m_b.draft().unconfirmed.empty()
        ? "Every edit can be undone here until you leave the builder. Save writes the template."
        : "A proposal: Keep each lifted folder you want, Remove the rest. Save waits until every one is decided.");
    refresh_builder();
    set_visible_child("builder");
}

void TemplatesPage::edit(const std::string& err) {
    m_back_armed = false;
    if (!err.empty()) m_b_status->set_text(err);
    else m_b_status->set_text(m_b.edits() == 1 ? "1 edit -- Undo takes it back." :
                              std::to_string(m_b.edits()) + " edits -- Undo takes back the last.");
    refresh_builder();
}

// The title lines and the act bar -- what every edit changes, rows aside.
void TemplatesPage::refresh_head() {
    const auto& d = m_b.draft();
    const auto& t = d.t;
    std::string sum = t.name.empty() ? "(no name yet)" : t.name;
    sum += m_b.saved_as().empty() ? " -- new" : (m_b.saved_as() == t.name ? " -- saved" : " -- renamed from " + m_b.saved_as());
    if (!d.origin.empty()) sum += "\nLifted from " + shown(d.origin) + (d.unconfirmed.empty()
        ? std::string(" -- every lifted folder decided")
        : " -- a proposal, " + std::to_string(d.unconfirmed.size()) + " folder" +
          (d.unconfirmed.size() == 1 ? "" : "s") + " still to keep or remove");
    m_b_summary->set_text(sum);
    m_b_undo->set_sensitive(m_b.can_undo());
    std::vector<std::string> names;
    for (const auto& s : m_saved) names.push_back(s.name);
    const auto why = m_b.save_refusal(names);
    m_b_save->set_sensitive(why.empty());
    m_b_save->set_tooltip_text(why.empty() ? "Write it to " + shown(m_data_dir) + "/" + tp::rel_of(t.name) : why);
}

// Keep: the row changes in place (the check stays, ticked and settled) -- no
// rebuild under the pointer. Undo rebuilds and gives the check back.
void TemplatesPage::on_keep(const std::string& path) {
    const auto err = m_b.confirm(path);
    m_back_armed = false;
    if (!err.empty()) { m_b_status->set_text(err); return; }
    for (auto& [p, w] : m_keep_rows)
        if (!m_b.draft().unconfirmed.count(p) && w.first->get_sensitive()) {
            w.first->set_active(true);
            w.first->set_sensitive(false);
            w.second->remove_css_class("dim");
        }
    m_b_status->set_text(std::to_string(m_b.edits()) + " edits -- Undo takes back the last.");
    refresh_head();
}

void TemplatesPage::refresh_builder() {
    // Rows are rebuilt; the reader's place in the list is not lost.
    const double at = m_b_scroll->get_vadjustment()->get_value();
    Glib::signal_idle().connect_once([this, at] { m_b_scroll->get_vadjustment()->set_value(at); });
    refresh_head();
    const auto& d = m_b.draft();
    const auto& t = d.t;
    m_b_name->set_text(t.name);
    m_b_category->set_text(t.category);
    m_b_quiet = true;
    m_b_by_place->set_active(t.by_place);
    m_b_quiet = false;
    m_b_group->set_text(t.group);
    m_b_group->set_sensitive(t.by_place);

    fill_tree();   // s024d

    // The drift found -- against the draft as it is now.
    clear(m_b_drift);
    if (m_drift_dir.empty()) {
        m_b_drift_head->set_visible(false);
        m_b_drift->set_visible(false);
    } else {
        m_drift = tp::drift(t, tp::folders_of(m_drift_dir, tp::depth_of(t)));
        m_b_drift_head->set_visible(true);
        m_b_drift_head->set_text("Drift -- " + shown(m_drift_dir) + ": " +
            (m_drift.empty() ? std::string("matches the template, two deep") :
             std::to_string(m_drift.size()) + " difference" + (m_drift.size() == 1 ? "" : "s") +
             ". Adopt one into the template here; fixing the project itself comes with Projects (s008)."));
        m_b_drift->set_visible(!m_drift.empty());
        for (const auto& dr : m_drift) {
            const bool extra = dr.kind == tp::Drift::Kind::Extra;
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            row->set_margin(6);
            auto l = label((extra ? "+ " : "- ") + dr.path + "/   " +
                           (extra ? "in the project, not in the template" : "in the template, not in the project"),
                           nullptr, 0.0f);
            l->set_hexpand(true);
            row->append(*l);
            auto ad = Gtk::make_managed<Gtk::Button>(extra ? "Adopt (add)" : "Adopt (drop)");
            ad->set_name("templates.builder.adopt." + dr.path);
            ad->set_tooltip_text(extra ? "Add it to the template" : "Drop it from the template");
            ad->signal_clicked().connect([this, dr] { Glib::signal_idle().connect_once([this, dr] { edit(m_b.adopt(dr)); }); });
            row->append(*ad);
            m_b_drift->append(*row);
        }
    }

}

TemplatesPage::~TemplatesPage() {
    if (m_tree_pop) m_tree_pop->unparent();
}

void TemplatesPage::start_new() { on_new(); }   // s024e

// ── s024d: the tree ────────────────────────────────────────────────────────
namespace {
std::string readable(std::string s) {   // A[0-9][0-9][0-9][0-9].png -> A####.png
    for (std::size_t at; (at = s.find("[0-9]")) != std::string::npos;) s.replace(at, 5, "#");
    return s;
}
}  // namespace

void TemplatesPage::pop_at(Gtk::Widget& at) {
    double x = 0, y = 0;
    if (!at.translate_coordinates(*m_b_folders, 0, 0, x, y)) { x = 0; y = 0; }
    Gdk::Rectangle r{static_cast<int>(x), static_cast<int>(y), at.get_width(), at.get_height()};
    m_tree_pop->set_pointing_to(r);
    m_tree_pop->popup();
}

void TemplatesPage::tree_ask(Gtk::Widget& at, const std::string& prompt, const std::string& initial,
                             std::function<std::string(const std::string&)> ok) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->set_margin(8);
    box->append(*label(prompt, nullptr, 0.0f));
    auto e = Gtk::make_managed<Gtk::Entry>();
    e->set_name("templates.tree.ask");
    e->set_text(initial);
    e->set_width_chars(28);
    box->append(*e);
    auto why = label("", "dim", 0.0f);
    box->append(*why);
    const auto go = [this, e, why, ok] {
        const auto err = ok(e->get_text());
        if (!err.empty()) { why->set_text(err); return; }
        m_tree_pop->popdown();
    };
    e->signal_activate().connect(go);
    auto b = Gtk::make_managed<Gtk::Button>("OK");
    b->set_name("templates.tree.ok");
    b->signal_clicked().connect(go);
    box->append(*b);
    m_tree_pop->set_child(*box);
    pop_at(at);
    e->grab_focus();
}

void TemplatesPage::tree_menu(Gtk::Widget& at, const std::string& folder) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    box->set_margin(4);
    const bool real = folder != tp::kTop && folder != tp::kStays;
    Gtk::Widget* anchor = &at;
    const auto item = [&](const std::string& text, const std::string& name, std::function<void()> f) {
        auto b = Gtk::make_managed<Gtk::Button>(text);
        b->set_name(name);
        b->set_has_frame(false);
        b->signal_clicked().connect([f = std::move(f)] { f(); });
        box->append(*b);
    };
    const std::string where = folder == tp::kTop ? "the project's top" : folder == tp::kStays ? "stays where it is" : folder + "/";
    if (folder != tp::kStays)
        item("New folder" + std::string(real ? " inside..." : "..."), "templates.tree.new", [this, anchor, folder, real] {
            tree_ask(*anchor, "New folder in " + (real ? folder + "/" : std::string("the top")), "", [this, folder, real](const std::string& n) {
                const auto err = m_b.add_folder(real ? folder + "/" + n : n, tp::guess_role(n.empty() ? "x" : n));
                if (err.empty()) Glib::signal_idle().connect_once([this] { edit(""); });
                return err;
            });
        });
    if (real)
        item("Rename...", "templates.tree.rename", [this, anchor, folder] {
            const auto slash = folder.rfind('/');
            tree_ask(*anchor, "Rename " + folder + "/", slash == std::string::npos ? folder : folder.substr(slash + 1),
                     [this, folder](const std::string& n) {
                         const auto err = m_b.rename_folder(folder, n);
                         if (err.empty()) Glib::signal_idle().connect_once([this] { edit(""); });
                         return err;
                     });
        });
    item("This goes here...", "templates.tree.goes", [this, anchor, folder, where] {
        tree_ask(*anchor, "What goes in " + where + "?  (*.odt   *cover*.png   A0001.png)", "", [this, folder](const std::string& raw) {
            std::string p = raw;
            while (!p.empty() && p.back() == ' ') p.pop_back();
            while (!p.empty() && p.front() == ' ') p.erase(0, 1);
            // A sample name (no wildcard) reads as its pattern: "A0001.png" -> the run, "x.odt" -> *.odt.
            if (!p.empty() && p.find_first_of("*?[/") == std::string::npos && p.find('.') != std::string::npos)
                p = tp::pattern_for_file(p);
            std::string err;
            const auto& rs = m_b.tpl().routes;
            if (std::any_of(rs.begin(), rs.end(), [&](const tp::Route& r) { return r.pattern == p; })) err = m_b.move_route(p, folder);
            else err = m_b.add_route(p, folder);
            if (err.empty()) Glib::signal_idle().connect_once([this] { edit(""); });
            return err;
        });
    });
    if (real)
        item("Remove " + folder + "/", "templates.tree.remove", [this, folder] {
            m_tree_pop->popdown();
            Glib::signal_idle().connect_once([this, folder] { edit(m_b.remove_folder(folder)); });
        });
    m_tree_pop->set_child(*box);
    pop_at(at);
}

void TemplatesPage::chip_menu(Gtk::Widget& at, const std::string& pattern) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    box->set_margin(4);
    box->append(*label(readable(pattern) + " -- drag it to another folder, or:", "dim", 0.0f));
    auto rm = Gtk::make_managed<Gtk::Button>("Remove this rule");
    rm->set_name("templates.tree.chip_remove");
    rm->set_has_frame(false);
    rm->signal_clicked().connect([this, pattern] {
        m_tree_pop->popdown();
        Glib::signal_idle().connect_once([this, pattern] { edit(m_b.remove_route(pattern)); });
    });
    box->append(*rm);
    m_tree_pop->set_child(*box);
    pop_at(at);
}

bool TemplatesPage::drop_on(const std::string& folder, const GValue* v) {
    if (!v) return false;
    std::vector<std::string> pats;
    if (G_VALUE_HOLDS_STRING(v)) {
        const char* s = g_value_get_string(v);
        const std::string t = s ? s : "";
        if (t.rfind("tidy-route:", 0) != 0) return false;
        const std::string pat = t.substr(11);
        Glib::signal_idle().connect_once([this, pat, folder] { edit(m_b.move_route(pat, folder)); });
        return true;
    }
    if (G_VALUE_HOLDS(v, GDK_TYPE_FILE_LIST)) {
        for (auto* l = static_cast<GSList*>(g_value_get_boxed(v)); l; l = l->next) {
            char* base = g_file_get_basename(G_FILE(l->data));
            if (base) { pats.push_back(tp::pattern_for_file(base)); g_free(base); }
        }
        if (pats.empty()) return false;
        Glib::signal_idle().connect_once([this, pats, folder] {
            std::string err;
            for (const auto& p : pats) {
                const auto& rs = m_b.tpl().routes;
                const bool has = std::any_of(rs.begin(), rs.end(), [&](const tp::Route& r) { return r.pattern == p; });
                const auto e = has ? m_b.move_route(p, folder) : m_b.add_route(p, folder);
                if (!e.empty() && err.empty()) err = e;
            }
            edit(err);
        });
        return true;
    }
    return false;
}

void TemplatesPage::fill_tree() {
    const auto& d = m_b.draft();
    const auto& t = d.t;
    m_keep_rows.clear();
    if (m_tree_pop) m_tree_pop->popdown();
    clear(m_b_folders);
    // The rows: the project's top, each folder (tree order), then "stays where it is".
    struct R { std::string key, name; int depth; };
    std::vector<R> rows;
    rows.push_back({tp::kTop, t.name.empty() ? std::string("(the project)") : t.name + "  (the project's top)", 0});
    for (const auto& f : t.folders) {
        const auto slash = f.path.rfind('/');
        rows.push_back({f.path, slash == std::string::npos ? f.path : f.path.substr(slash + 1),
                        1 + static_cast<int>(std::count(f.path.begin(), f.path.end(), '/'))});
    }
    rows.push_back({tp::kStays, "(stays where it is)", 0});
    for (const auto& r : rows) {
        const bool real = r.key != tp::kTop && r.key != tp::kStays;
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->set_name("templates.tree.row." + r.key);
        row->set_margin(4);
        row->set_margin_start(8 + r.depth * 22);
        if (real && d.unconfirmed.count(r.key)) {
            auto keep = Gtk::make_managed<Gtk::CheckButton>("Keep");
            keep->set_name("templates.builder.keep." + r.key);
            keep->set_tooltip_text("Lifted from the project: keep it in the template?");
            const std::string p = r.key;
            keep->signal_toggled().connect([this, keep, p] { if (keep->get_active() && keep->get_sensitive()) on_keep(p); });
            row->append(*keep);
        }
        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(real ? "folder-symbolic" : r.key == tp::kTop ? "go-top-symbolic" : "action-unavailable-symbolic");
        row->append(*icon);
        auto name = label(real ? r.name + "/" : r.name, real ? nullptr : "dim", 0.0f);
        name->set_wrap(false);
        name->set_tooltip_text(real ? r.key : r.key == tp::kTop ? "Files that go to the project's top" : "Files that stay where they are found");
        if (real && d.unconfirmed.count(r.key)) name->add_css_class("dim");
        if (auto* k = dynamic_cast<Gtk::CheckButton*>(row->get_first_child()); k && real) m_keep_rows[r.key] = {k, name};
        row->append(*name);
        // Its rules, as chips.
        auto chips = Gtk::make_managed<Gtk::FlowBox>();   // wraps: the row never pushes the page wider
        chips->set_hexpand(true);
        chips->set_selection_mode(Gtk::SelectionMode::NONE);
        chips->set_max_children_per_line(12);
        chips->set_column_spacing(4);
        chips->set_row_spacing(2);
        chips->set_valign(Gtk::Align::CENTER);
        for (const auto& rt : t.routes) {
            if (rt.folder != r.key) continue;
            auto chip = Gtk::make_managed<Gtk::Button>("← " + readable(rt.pattern));
            chip->set_name("templates.tree.chip." + rt.pattern);
            chip->add_css_class("flat");
            chip->set_tooltip_text(rt.pattern + " goes here -- drag it to another folder; click to remove");
            const std::string pat = rt.pattern;
            chip->signal_clicked().connect([this, chip, pat] { chip_menu(*chip, pat); });
            auto drag = Gtk::DragSource::create();
            drag->set_actions(Gdk::DragAction::MOVE);
            drag->signal_prepare().connect([pat](double, double) {
                Glib::Value<Glib::ustring> v;
                v.init(v.value_type());
                v.set("tidy-route:" + pat);
                return Gdk::ContentProvider::create(v);
            }, false);
            chip->add_controller(drag);
            chips->append(*chip);
        }
        row->append(*chips);
        if (real) {
            const auto* f = tp::find(t, r.key);
            auto dd = role_dropdown(f ? f->role : tp::Role::Input);
            dd->set_name("templates.builder.role." + r.key);
            dd->set_tooltip_text("What happens at close: product kept, input as you say, history archived");
            const std::string p = r.key;
            dd->property_selected().signal_changed().connect([this, dd, p] {
                const auto i = dd->get_selected();
                if (i < tp::roles().size()) Glib::signal_idle().connect_once([this, p, i] { edit(m_b.set_role(p, tp::roles()[i])); });
            });
            row->append(*dd);
        }
        auto more = Gtk::make_managed<Gtk::Button>("⋯");
        more->set_name("templates.tree.more." + r.key);
        more->add_css_class("flat");
        more->set_tooltip_text("New folder, Rename, This goes here..., Remove");
        const std::string key = r.key;
        more->signal_clicked().connect([this, more, key] { tree_menu(*more, key); });
        row->append(*more);
        // Right-click anywhere on the row: the same menu.
        auto rc = Gtk::GestureClick::create();
        rc->set_button(3);
        rc->signal_pressed().connect([this, row, key](int, double, double) { tree_menu(*row, key); });
        row->add_controller(rc);
        // A drop: a rule dragged here, or files from Files (their types).
        auto drop = Gtk::DropTarget::create(G_TYPE_INVALID, Gdk::DragAction::MOVE | Gdk::DragAction::COPY);
        drop->set_gtypes({G_TYPE_STRING, GDK_TYPE_FILE_LIST});
        drop->signal_drop().connect([this, key](const Glib::ValueBase& v, double, double) {
            return drop_on(key, v.gobj());
        }, false);
        row->add_controller(drop);
        m_b_folders->append(*row);
    }
}

// ── Handlers ───────────────────────────────────────────────────────────────
void TemplatesPage::on_new() {
    m_saved = tp::list(m_data_dir);
    open(tp::Builder(tp::Draft{tp::shell("", ""), {}, ""}));
    m_b_status->set_text("The common shell to start. Name it, give it a category, add the kind's own folders.");
}

void TemplatesPage::pick_folder(const std::string& title, std::function<void(std::string)> then) {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title(title);
    dlg->set_modal(true);
    dlg->set_initial_folder(Gio::File::create_for_path(m_home));
    dlg->select_folder(m_parent, [dlg, then](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            if (auto f = dlg->select_folder_finish(res)) then(f->get_path());
        } catch (const Glib::Error&) {
            // dismissed: nothing
        }
    });
}

void TemplatesPage::on_lift() {
    pick_folder("Lift a template from a project", [this](std::string dir) {
        if (dir == m_home || dir.rfind(m_home + "/", 0) != 0) {
            set_visible_child(m_saved.empty() ? "start" : "list");
            if (m_list_status) m_list_status->set_text("Lift takes a project inside your home, not the home itself.");
            return;
        }
        auto d = tp::proposal(dir);
        if (auto lg = log::get(log::Area::Io)) lg->info("lift {}: {} folders proposed", dir, d.t.folders.size());
        m_saved = tp::list(m_data_dir);
        open(tp::Builder(std::move(d)));
    });
}

void TemplatesPage::on_drift_pick() {
    pick_folder("Check a project against " + (m_b.tpl().name.empty() ? std::string("the template") : m_b.tpl().name),
                [this](std::string dir) { run_drift(dir); });
}

void TemplatesPage::run_drift(const std::string& dir) {
    m_drift_dir = dir;
    refresh_builder();
    m_b_status->set_text(m_drift.empty() ? shown(dir) + " matches the template." :
                         "Adopt what belongs; the rest is the project's drift (left as it is for now).");
    if (auto lg = log::get(log::Area::Io)) lg->info("drift {} vs {}: {}", dir, m_b.tpl().name, m_drift.size());
}

void TemplatesPage::on_undo() {
    m_back_armed = false;
    m_b_status->set_text(m_b.undo() ? "Undone." : "Nothing to undo.");
    refresh_builder();
}

void TemplatesPage::on_save() {
    // A typed name or category not yet set with Enter counts.
    if (std::string(m_b_name->get_text()) != m_b.tpl().name) {
        if (auto e = m_b.set_name(m_b_name->get_text()); !e.empty()) { edit(e); return; }
    }
    if (std::string(m_b_category->get_text()) != m_b.tpl().category) {
        if (auto e = m_b.set_category(m_b_category->get_text()); !e.empty()) { edit(e); return; }
    }
    if (std::string(m_b_group->get_text()) != m_b.tpl().group) {   // s024: a typed group not yet set with Enter counts
        if (auto e = m_b.set_group(m_b_group->get_text()); !e.empty()) { edit(e); return; }
    }
    std::vector<std::string> names;
    for (const auto& s : m_saved) names.push_back(s.name);
    if (auto why = m_b.save_refusal(names); !why.empty()) { m_b_status->set_text(why); refresh_builder(); return; }
    const auto err = tp::save(m_data_dir, m_b.tpl());
    if (err.empty()) core::projects::apply_places(m_data_dir);   // s024: a by-place template's shelf, now
    if (!err.empty()) { m_b_status->set_text("Not saved: " + err); return; }
    if (auto lg = log::get(log::Area::Io)) lg->info("saved {} ({} folders, {} routes)", m_b.tpl().name,
                                                  m_b.tpl().folders.size(), m_b.tpl().routes.size());
    m_b.mark_saved();
    m_saved = tp::list(m_data_dir);
    refresh_builder();
    m_b_status->set_text("Saved " + shown(m_data_dir) + "/" + tp::rel_of(m_b.tpl().name) +
                         ". Undo still works here; Save again to write it back.");
}

void TemplatesPage::on_back() {
    const bool unsaved = m_b.can_undo() && !(m_b.saved_as() == m_b.tpl().name && [&] {
        for (const auto& s : m_saved) if (s.name == m_b.tpl().name) return s == m_b.tpl();
        return false;
    }());
    if (unsaved && !m_back_armed) {
        m_back_armed = true;
        m_b_status->set_text("Unsaved edits. Back again drops them; Save keeps them.");
        return;
    }
    show_home();
}

}  // namespace tidy
