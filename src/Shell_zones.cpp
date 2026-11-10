#include "Shell.hpp"
#include "Log.hpp"
#include "Look.hpp"
#include "core/Pages.hpp"

#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stack.h>

// Shell_zones.cpp -- ZONES. Construction of the named regions: the window
// frame and header, the sidebar, the pages, the menu model. The look is CSS
// (resources/look.css, Cairn's stone); widgets here only get their classes.

namespace tidy {

namespace {
Gtk::Label* label(const std::string& text, const char* css, float x = 0.5f) {
    auto l = Gtk::make_managed<Gtk::Label>(text);
    if (css) l->add_css_class(css);
    l->set_xalign(x);
    l->set_wrap(true);
    l->set_justify(x == 0.5f ? Gtk::Justification::CENTER : Gtk::Justification::LEFT);
    return l;
}
Gtk::Image* hero(const std::string& icon, int px) {
    auto i = Gtk::make_managed<Gtk::Image>();
    i->set_from_icon_name(icon);
    i->set_pixel_size(px);
    return i;
}
}  // namespace

void Shell::build_shell() {  // zone: window + header + body
    set_title("Tidy");
    set_default_size(760, 520);
    if (look::enabled()) {
        add_css_class("look");     // every look.css rule hangs off it
        set_default_size(980, 660);
    }

    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    m_logo_button.set_icon_name("tidy-logo-symbolic");
    m_logo_button.set_has_frame(false);
    m_logo_button.set_tooltip_text("About Tidy");
    m_logo_button.set_action_name("win.about");
    header->pack_start(m_logo_button);
    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_menu_model(build_menu());
    header->pack_end(m_menu_button);
    set_titlebar(*header);

    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    if (look::enabled()) {
        m_side.add_css_class("sidebar-pane");
        m_side.set_size_request(220, -1);
        m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
        m_stack.set_transition_duration(180);
        m_body.append(m_side);
    } else {
        m_sidebar.set_stack(m_stack);
        m_sidebar.set_size_request(160, -1);
        m_body.append(m_sidebar);
    }
    m_body.append(m_stack);
    set_child(m_body);
}

void Shell::build_pages() {  // zone: one page per nav item, then the sidebar
    m_nav_model = core::look::tidy_nav();
    for (const auto& e : m_nav_model)
        if (!e.section) build_page(e.id);
    if (look::enabled()) build_nav();   // on_nav needs every stack child first
}

// A page in the spine: its hero (emblem in the page's own tint), the title,
// the promise, and which milestone brings it to life. The orb is there and
// greyed -- the shape of the page to come, not a button that pretends.
void Shell::build_page(const std::string& id) {  // zone: a page's hero
    if (id == "activity") { build_activity(); return; }   // s004: alive
    const auto* p = core::pages::find(id);
    const std::string title = p ? p->title : id;

    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("shell.page." + id);
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero("tidy-hero-" + id, 128));
    start->append(*label(title, "hero-title"));
    if (p) {
        auto sub = label(p->promise, "hero-sub");
        sub->set_max_width_chars(52);
        start->append(*sub);
    }
    auto note = label(page_note(id), "dim");
    note->set_max_width_chars(60);
    note->set_selectable(true);
    start->append(*note);
    auto orb = Gtk::make_managed<Gtk::Button>("Look");
    orb->add_css_class("orb");
    orb->set_halign(Gtk::Align::CENTER);
    orb->set_margin_top(18);
    orb->set_sensitive(false);
    orb->set_tooltip_text(p ? "Comes with " + p->milestone : "Not yet");
    start->append(*orb);

    m_stack.add(*start, id, title);
}

// Activity (s004): the door's batches, newest first, and the way back. Two
// states: start (nothing done yet -- the hero) and results (the list + an act
// bar whose one button undoes the newest batch). Filled by refresh_activity().
void Shell::build_activity() {  // zone: Activity
    const auto* p = core::pages::find("activity");
    m_act_states = Gtk::make_managed<Gtk::Stack>();
    m_act_states->set_name("activity.states");

    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("activity.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero("tidy-hero-activity", 128));
    start->append(*label(p ? p->title : "Activity", "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(page_note("activity"), "dim");
    note->set_name("activity.note");
    note->set_max_width_chars(60);
    note->set_selectable(true);
    start->append(*note);
    m_act_states->add(*start, "start");

    auto results = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    results->set_name("activity.results");
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(12);
    head->append(*hero("tidy-hero-activity", 48));
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label(p ? p->title : "Activity", "hero-title", 0.0f));
    m_act_summary = label("", "dim", 0.0f);
    m_act_summary->set_name("activity.summary");
    m_act_summary->set_selectable(true);
    titles->append(*m_act_summary);
    head->append(*titles);
    results->append(*head);

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_margin_start(32);
    scroll->set_margin_end(32);
    scroll->set_margin_bottom(16);
    m_act_list = Gtk::make_managed<Gtk::ListBox>();
    m_act_list->set_name("activity.list");
    m_act_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_act_list);
    results->append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    bar->add_css_class("act-bar");
    m_act_status = label("", "dim", 0.0f);
    m_act_status->set_name("activity.status");
    m_act_status->set_hexpand(true);
    bar->append(*m_act_status);
    m_act_undo = Gtk::make_managed<Gtk::Button>("Undo");
    m_act_undo->set_name("activity.undo");
    m_act_undo->add_css_class("act");
    m_act_undo->signal_clicked().connect([this] { on_activity_undo(); });
    bar->append(*m_act_undo);
    results->append(*bar);
    m_act_states->add(*results, "results");

    m_stack.add(*m_act_states, "activity", p ? p->title : "Activity");
    refresh_activity();
}

// The look's sidebar (Cairn's build_nav): brand, section headers that are
// never selected, icon rows named "nav.<page>".
void Shell::build_nav() {  // zone: brand + sections + rows
    m_side.append(*label("Tidy", "sidebar-brand", 0.0f));
    m_nav.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_nav.set_vexpand(true);
    for (const auto& e : m_nav_model) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        if (e.section) {
            row->set_selectable(false);
            row->set_activatable(false);
            row->set_can_focus(false);
            row->set_child(*label(e.title, "sidebar-section", 0.0f));
            row->add_css_class("section-row");
        } else {
            row->set_name("nav." + e.id);
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            auto img = Gtk::make_managed<Gtk::Image>();
            img->set_from_icon_name(e.icon);
            img->set_pixel_size(18);
            box->append(*img);
            box->append(*label(e.title, nullptr, 0.0f));
            row->set_child(*box);
        }
        m_nav.append(*row);
    }
    m_nav.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (!row) return;
        const std::string n = row->get_name();
        if (n.rfind("nav.", 0) == 0) on_nav(n.substr(4));
    });
    m_side.append(m_nav);
    const int first = core::look::first_item(m_nav_model);
    if (auto* r = first >= 0 ? m_nav.get_row_at_index(first) : nullptr) m_nav.select_row(*r);
}

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();
    auto diag = Gio::Menu::create();
    diag->append("Keyboard shortcuts", "win.shortcuts");
    diag->append("Dump registry", "win.dump-registry");
    menu->append_section(diag);
    auto app = Gio::Menu::create();
    app->append("About Tidy", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace tidy
