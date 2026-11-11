#include "DuePage.hpp"
#include "Log.hpp"
#include "core/Due.hpp"
#include "core/Pages.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>

#include <ctime>
#include <filesystem>

namespace tidy {

namespace du = core::due;

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

DuePage::DuePage(std::string home, std::string data_dir, bool fixture, Text user_dirs)
    : m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_fixture(fixture), m_user_dirs(std::move(user_dirs)) {
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
                                  "files long unread: to archive. Until done whose purpose closed: to settle. "
                                  "Look reads only; nothing here moves a file.") +
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
    auto st = label("Read-only -- archive and remove come in later milestones.", "dim", 0.0f);
    st->set_hexpand(true);
    bar->append(*st);
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("due.again");
    again->signal_clicked().connect([this] { fill(); });
    bar->append(*again);
    results->append(*bar);
    add(*results, "results");
    set_visible_child("start");
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
        b->set_margin(8);
        b->append(*label(std::filesystem::path(it.path).filename().string(), nullptr, 0.0f));
        b->append(*label(shown(std::filesystem::path(it.path).parent_path().string()) + " -- " + it.why, "dim", 0.0f));
        add_row(m_list, *b);
    }
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
