#include "core/Guide.hpp"
#include "Shell.hpp"
#include "OrganizeStart.hpp"
#include "OrganizeWindow.hpp"
#include "SeesPage.hpp"
#include "QuarantinePage.hpp"
#include "SettingsPage.hpp"
#include "Log.hpp"
#include "Look.hpp"
#include "core/Pages.hpp"
#include "core/Retention.hpp"
#include "TemplatesPage.hpp"
#include "ProjectsPage.hpp"
#include "PurposesPage.hpp"
#include "HomePage.hpp"
#include "SortPage.hpp"
#include "MapPage.hpp"
#include "ArchivePage.hpp"
#include "DuePage.hpp"

#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include "SelectList.hpp"
#include <gtkmm/dropdown.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/stringlist.h>
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
    // s030b (Scott: "there should always be a way to go back to Easy Steps"):
    // a Steps button, shown on every page but Start itself.
    m_steps_button = Gtk::make_managed<Gtk::Button>();
    m_steps_button->set_name("shell.steps");
    {
        auto b = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto i = Gtk::make_managed<Gtk::Image>();
        i->set_from_icon_name("go-previous-symbolic");
        b->append(*i);
        b->append(*Gtk::make_managed<Gtk::Label>("Steps"));
        m_steps_button->set_child(*b);
    }
    m_steps_button->set_tooltip_text("Back to the steps (Organize \u2192 Start)");
    m_steps_button->signal_clicked().connect([this] { go("organize"); });
    m_steps_button->set_visible(false);
    header->pack_start(*m_steps_button);
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
        // s027: the area's tabs over its pages.
        m_tabs.set_transition_type(Gtk::StackTransitionType::NONE);
        m_tabs.set_vhomogeneous(false);
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        col->set_hexpand(true);
        col->append(m_tabs);
        m_expert = core::guide::expert(m_data_dir);   // s030a: Guided hides the tab rows
        m_tabs.set_visible(m_expert);
        col->append(m_stack);
        m_body.append(*col);
    } else {
        m_sidebar.set_stack(m_stack);
        m_sidebar.set_size_request(160, -1);
        m_body.append(m_sidebar);
        m_body.append(m_stack);
    }
    m_body.set_vexpand(true);

    // s010f3: the rescan's offer, under every page: "3 id lists out of date".
    auto outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    outer->append(m_body);
    m_offer = Gtk::make_managed<Gtk::Revealer>();
    m_offer->set_name("offer.lists");
    m_offer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_UP);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    bar->set_margin_start(16);
    bar->set_margin_end(16);
    bar->set_margin_top(6);
    bar->set_margin_bottom(6);
    m_offer_text = Gtk::make_managed<Gtk::Label>("");
    m_offer_text->set_name("offer.lists.text");
    m_offer_text->set_xalign(0);
    m_offer_text->set_hexpand(true);
    m_offer_text->add_css_class("dim");
    bar->append(*m_offer_text);
    auto open = Gtk::make_managed<Gtk::Button>("Id lists...");
    open->set_name("offer.lists.open");
    open->signal_clicked().connect([this] { m_offer->set_reveal_child(false); on_id_lists(); });
    bar->append(*open);
    auto later = Gtk::make_managed<Gtk::Button>();
    later->set_name("offer.lists.later");
    later->set_icon_name("window-close-symbolic");
    later->set_has_frame(false);
    later->set_tooltip_text("Not now -- offered again when the count changes");
    later->signal_clicked().connect([this] {
        m_offer_dismissed = m_offer_text->get_text();
        m_offer->set_reveal_child(false);
    });
    bar->append(*later);
    m_offer->set_child(*bar);
    outer->append(*m_offer);
    set_child(*outer);
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
    if (id == "organize") {                                // s027: Organize's guided landing
        m_org_start = Gtk::make_managed<OrganizeStart>(*this, *m_door, m_home, m_data_dir,   // s030a: the guided start
            [this](const std::string& page) {
                go(page);
                if (page == "closet" && m_clo_states) {   // s030d: looked at once; emptied -> back to the steps
                    m_closet_from_start = true;
                    m_closet_ran = false;
                    on_closet_look();
                }
            },
            [this](std::vector<std::pair<std::string, std::string>> items) {   // s030c: one after another; done -> Start
                if (items.empty()) return;
                on_organize(items.front().first, items.front().second);   // makes the window
                if (m_organize_win) m_organize_win->open_from_start(std::move(items), [this] { back_to_start(); });
            },
            [this](std::vector<std::string> folders, std::vector<std::string> skip) {   // s030d: folder after folder
                go("sort");
                if (m_sort_page) {
                    m_sort_page->set_on_settled([this](const std::string& rel) {   // s030f
                        const auto why = core::guide::settle(m_data_dir, rel);
                        if (m_org_start) m_org_start->note("~/" + rel + ": nothing to move -- its files stay where they are.");
                        if (auto lg = log::get(log::Area::Shell)) lg->info("start: settled ~/{}{}", rel, why.empty() ? "" : " (not saved: " + why + ")");
                    });
                    m_sort_page->start_queue(std::move(folders), std::move(skip), [this] { back_to_start(); });
                }
            },
            [this] { refresh_activity(); });
        m_org_start->set_on_put([this] { on_put(); });   // s030g
        m_org_start->set_dirs([this] { return user_dirs_text(); });   // s030l: the Closet page's places
        m_stack.add(*m_org_start, id, "Organize");
        return;
    }
    if (id == "sees") {                                    // s027a: What Tidy sees
        m_sees = Gtk::make_managed<SeesPage>(*this, *m_door, m_home, m_data_dir,
            [this](const std::string& page) { go(page); },
            [this](const std::string& dir, const std::string& type) { on_organize(dir, type); },
            [this](const std::string& rel, std::vector<std::string> skip) {
                go("sort");
                if (m_sort_page) m_sort_page->look_in(rel, std::move(skip));
            },
            [this] { refresh_activity(); });
        m_sees->set_on_ebooks([this](const std::string& dir) { on_ebooks(dir); });   // s028
        m_stack.add(*m_sees, id, "What Tidy sees");
        return;
    }
    if (id == "quarantine") {                              // s027: was the Quarantine window (s025b)
        m_quarantine = Gtk::make_managed<QuarantinePage>(*this, *m_door, m_home, m_data_dir, [this] { refresh_activity(); });
        m_stack.add(*m_quarantine, id, "Quarantine");
        return;
    }
    if (id == "settings") {                                // s027: was the Settings window (s006a)
        m_settings_page = Gtk::make_managed<SettingsPage>(*this, m_home,
                                                          [this](const core::settings::Settings& st) { return save_settings(st); });
        m_settings_page->show(m_settings);
        m_stack.add(*m_settings_page, id, "Settings");
        return;
    }
    if (id == "closet") { build_closet(); return; }       // s005: the birth report (read-only)
    if (id == "projects") {                                // s008: found, placed, fixed -- through the door
        const auto* pp = core::pages::find(id);
        m_projects = Gtk::make_managed<ProjectsPage>(*m_door, m_home, m_data_dir, m_fixture, [this] { refresh_activity(); });
        m_projects->set_on_organize([this](const std::string& dir, const std::string& type) { on_organize(dir, type); });   // s024e
        m_stack.add(*m_projects, id, pp ? pp->title : "Projects");
        return;
    }
    if (id == "map") {                                     // s023f: the home map (read-only)
        const auto* pp = core::pages::find(id);
        m_map_page = Gtk::make_managed<MapPage>(m_home, m_fixture, [this] { return user_dirs_text(); },
                                                [this] { return m_settings.catch_all; },
                                                [this](const std::string& rel, bool ca) { on_adopt_folder(rel, ca); });
        m_map_page->set_on_organize([this](const std::string& dir) { on_organize(dir); });   // s024e
        m_map_page->set_on_sees([this](const std::string& dir) {                             // s027a
            go("sees");
            if (m_sees) m_sees->look_at(dir);
        });
        m_stack.add(*m_map_page, id, pp ? pp->title : "Map");
        return;
    }
    if (id == "purposes") {                                // s009: one decision per purpose -- tags through the door
        const auto* pp = core::pages::find(id);
        m_purposes = Gtk::make_managed<PurposesPage>(*m_door, m_home, m_data_dir, m_fixture,
                                                     [this] { return user_dirs_text(); }, [this] { refresh_activity(); });
        m_stack.add(*m_purposes, id, pp ? pp->title : "Purposes");
        return;
    }
    if (id == "due") {                                     // s012a: the trigger's page; s013a: archive through the door
        const auto* pp = core::pages::find(id);
        m_due = Gtk::make_managed<DuePage>(*m_door, m_home, m_data_dir, m_fixture, [this] { return user_dirs_text(); },
                                           [this] { refresh_activity(); });
        m_stack.add(*m_due, id, pp ? pp->title : "Due");
        return;
    }
    if (id == "archive") {                                 // s013b: what is archived, on which media; Back up...
        const auto* pp = core::pages::find(id);
        m_archive = Gtk::make_managed<ArchivePage>(*m_door, m_home, m_data_dir, m_fixture,
                                                   [this] { return m_settings.media; }, [this] { refresh_activity(); });
        m_stack.add(*m_archive, id, pp ? pp->title : "Archive");
        return;
    }
    if (id == "sort") {                                    // s020: the one-step Sort
        const auto* pp = core::pages::find(id);
        m_sort_page = Gtk::make_managed<SortPage>(*m_door, m_home, m_data_dir, m_fixture,
                                                  [this] { return user_dirs_text(); }, [this] { refresh_activity(); });
        m_sort_page->set_on_adopt([this] { on_adopt(); });   // s023
        m_sort_page->set_on_put([this](std::vector<std::string> files) { on_put(std::move(files)); });   // s030g
        m_stack.add(*m_sort_page, id, pp ? pp->title : "Sort");
        return;
    }
    if (id == "home") {                                    // s011: files home -- moves through the door
        const auto* pp = core::pages::find(id);
        m_home_page = Gtk::make_managed<HomePage>(*m_door, m_home, m_data_dir, m_fixture,
                                                  [this] { return user_dirs_text(); }, [this] { refresh_activity(); });
        m_stack.add(*m_home_page, id, pp ? pp->title : "Files home");
        return;
    }
    if (id == "templates") {                               // s007: the tree builder
        const auto* tp = core::pages::find(id);
        auto* tpage = Gtk::make_managed<TemplatesPage>(*this, m_home, m_data_dir, m_fixture);
        m_templates_page = tpage;
        m_stack.add(*tpage, id,
                    tp ? tp->title : "Templates");
        return;
    }
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

// The Closet (s005 report, s006 decides). Start = the hero with a live Look
// orb; results = the Closet (proposed + waiting, checkable) and an act bar
// whose every button builds a plan; preview = that plan's steps, Back or Do it.
// Only "Do it" runs anything, and only through the door.
void Shell::build_closet() {  // zone: The Closet
    const auto* p = core::pages::find("closet");
    const std::string title = p ? p->title : "The Closet";
    m_clo_states = Gtk::make_managed<Gtk::Stack>();
    m_clo_states->set_name("closet.states");

    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("closet.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero("tidy-hero-closet", 128));
    start->append(*label(title, "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(page_note("closet"), "dim");
    note->set_name("closet.note");
    note->set_max_width_chars(60);
    note->set_selectable(true);
    start->append(*note);
    auto orb = Gtk::make_managed<Gtk::Button>("Look");
    orb->set_name("closet.look");
    orb->add_css_class("orb");
    orb->set_halign(Gtk::Align::CENTER);
    orb->set_margin_top(18);
    orb->set_tooltip_text("Sweep the home for loose files and show what each one's birth (or age) says -- nothing is tagged");
    orb->signal_clicked().connect([this] { on_closet_look(); });
    start->append(*orb);
    m_clo_states->add(*start, "start");

    auto results = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    results->set_name("closet.results");
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(12);
    head->append(*hero("tidy-hero-closet", 48));
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label(title, "hero-title", 0.0f));
    m_clo_summary = label("", "dim", 0.0f);
    m_clo_summary->set_name("closet.summary");
    m_clo_summary->set_selectable(true);
    titles->append(*m_clo_summary);
    titles->set_hexpand(true);
    head->append(*titles);
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("closet.again");
    again->set_valign(Gtk::Align::CENTER);
    again->signal_clicked().connect([this] { on_closet_look(); });
    head->append(*again);
    results->append(*head);

    // s018: select like Nautilus (click, Shift, Ctrl, Ctrl+A, drag a box);
    // "N selected  Set to [..]" -- a pick builds the plan and shows its
    // preview; right-click offers the same. Nothing runs from here.
    m_clo_list = Gtk::make_managed<SelectList>("closet.list");
    m_clo_list->set_vexpand(true);
    m_clo_list->set_margin_start(32);
    m_clo_list->set_margin_end(32);
    m_clo_list->set_margin_bottom(16);
    {
        std::vector<std::string> choices{"Its proposal"};
        for (const auto& i : core::retention::all())
            if (i.level != core::retention::Level::Undecided) choices.push_back(i.title);
        choices.push_back("Junk Drawer");
        m_clo_list->set_choices(choices);
    }
    m_clo_list->on_choose([this](std::size_t i) { on_closet_set(i); });
    results->append(*m_clo_list);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_clo_status = label("", "dim", 0.0f);
    m_clo_status->set_name("closet.status");
    m_clo_status->set_hexpand(true);
    bar->append(*m_clo_status);
    bar->append(m_clo_list->setter());
    results->append(*bar);
    m_clo_states->add(*results, "results");

    // The preview (s006): the plan's every step, and any refusal, before it runs.
    auto prev = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    prev->set_name("closet.preview");
    auto ph = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    ph->set_margin_start(32);
    ph->set_margin_end(32);
    ph->set_margin_top(24);
    ph->set_margin_bottom(12);
    ph->append(*hero("tidy-hero-closet", 48));
    auto pt = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    pt->append(*label("Preview", "hero-title", 0.0f));
    m_clo_prev_summary = label("", "dim", 0.0f);
    m_clo_prev_summary->set_name("closet.preview.summary");
    m_clo_prev_summary->set_selectable(true);
    pt->append(*m_clo_prev_summary);
    ph->append(*pt);
    prev->append(*ph);
    auto pscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    pscroll->set_vexpand(true);
    pscroll->set_margin_start(32);
    pscroll->set_margin_end(32);
    pscroll->set_margin_bottom(16);
    m_clo_prev_list = Gtk::make_managed<Gtk::ListBox>();
    m_clo_prev_list->set_name("closet.preview.list");
    m_clo_prev_list->set_selection_mode(Gtk::SelectionMode::NONE);
    pscroll->set_child(*m_clo_prev_list);
    prev->append(*pscroll);
    auto pbar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    pbar->add_css_class("act-bar");
    m_clo_prev_status = label("", "dim", 0.0f);
    m_clo_prev_status->set_name("closet.preview.status");
    m_clo_prev_status->set_hexpand(true);
    pbar->append(*m_clo_prev_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("closet.preview.back");
    back->signal_clicked().connect([this] { on_closet_back(); });
    pbar->append(*back);
    m_clo_prev_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_clo_prev_run->set_name("closet.preview.run");
    m_clo_prev_run->add_css_class("act");
    m_clo_prev_run->signal_clicked().connect([this] { on_closet_run(); });
    pbar->append(*m_clo_prev_run);
    prev->append(*pbar);
    m_clo_states->add(*prev, "preview");

    m_stack.add(*m_clo_states, "closet", title);
}

// The look's sidebar (Cairn's build_nav): brand, section headers that are
// never selected, icon rows named "nav.<page>".
void Shell::build_nav() {  // zone: brand + the four areas + each area's tab row
    m_side.append(*label("Tidy", "sidebar-brand", 0.0f));
    m_nav.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_nav.set_vexpand(true);
    m_areas = core::look::areas(m_nav_model);
    for (const auto& a : m_areas) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name("nav." + a.id);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto img = Gtk::make_managed<Gtk::Image>();
        img->set_from_icon_name(a.icon);
        img->set_pixel_size(18);
        box->append(*img);
        box->append(*label(a.title, nullptr, 0.0f));
        row->set_child(*box);
        m_nav.append(*row);

        // The tab row: one toggle per page, grouped (only one lit).
        auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        bar->set_name("tabs." + a.id);
        bar->set_margin_start(24);
        bar->set_margin_end(24);
        bar->set_margin_top(12);
        bar->set_margin_bottom(2);
        Gtk::ToggleButton* first = nullptr;
        for (const auto& id : a.tabs) {
            std::string title = id;
            for (const auto& e : m_nav_model)
                if (!e.section && e.id == id) title = e.title;
            auto t = Gtk::make_managed<Gtk::ToggleButton>(title);
            t->set_name("tab." + id);
            t->add_css_class("ghost");
            t->add_css_class("small");
            t->add_css_class("mode");
            if (first) t->set_group(*first); else first = t;
            t->signal_toggled().connect([this, t, id] {
                if (t->get_active() && !m_tab_sync) on_nav(id);
            });
            m_tab_buttons[id] = t;
            if (a.tabs.size() > 1) bar->append(*t);   // Settings: one page -- an empty row (a hidden
        }                                              // stack child can't be shown, so it stays)
        m_tabs.add(*bar, a.id);
    }
    m_nav.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (!row) return;
        const std::string n = row->get_name();
        if (n.rfind("nav.", 0) == 0) on_area(n.substr(4));
    });
    m_side.append(m_nav);
    // s027: Tidy opens on Organize's landing.
    for (int i = 0; auto* r = m_nav.get_row_at_index(i); ++i)
        if (std::string(r->get_name()) == "nav.organize") m_nav.select_row(*r);
}

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();
    auto prefs = Gio::Menu::create();
    prefs->append("Put it with\u2026", "win.put");   // s030g: any file, any folder
    prefs->append("Expert mode", "win.expert");   // s030a: every tab, for the nitty-gritty
    prefs->append("Adopt folders\u2026", "win.adopt");   // s023
    prefs->append("Organize as\u2026", "win.organize");   // s024e
    prefs->append("Ebooks from files\u2026", "win.ebooks");   // s028
    prefs->append("Id lists\u2026", "win.id-lists");
    prefs->append("Background scan\u2026", "win.background");
    menu->append_section(prefs);
    auto diag = Gio::Menu::create();
    diag->append("Keyboard shortcuts", "win.shortcuts");
    diag->append("Dump registry", "win.dump-registry");
    menu->append_section(diag);
    auto app = Gio::Menu::create();
    app->append("How to use Tidy", "win.help");
    app->append("About Tidy", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace tidy
