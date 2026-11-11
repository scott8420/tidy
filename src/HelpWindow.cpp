#include "HelpWindow.hpp"
#include "core/Help.hpp"
#include "core/Pages.hpp"

#include <glibmm/markup.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>

namespace tidy {

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr, int width = 60) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    l->set_max_width_chars(width);
    l->set_selectable(css == nullptr);
    if (css) l->add_css_class(css);
    return l;
}
}  // namespace

HelpWindow::HelpWindow(Gtk::Window& parent, Go go) : m_go(std::move(go)) {
    set_name("shell.help");
    set_title("How to use Tidy");
    set_transient_for(parent);
    set_hide_on_close(true);
    set_default_size(820, 600);

    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("help.topics");
    m_list->set_selection_mode(Gtk::SelectionMode::SINGLE);
    std::string group;
    for (const auto& t : core::help::topics()) {
        if (t.group != group) {
            group = t.group;
            auto h = text("", "dim", 24);
            h->set_markup("<span size='small' weight='bold' alpha='60%'>" +
                          Glib::Markup::escape_text(group).raw() + "</span>");
            h->set_margin_start(12);
            h->set_margin_top(m_row_ids.empty() ? 8 : 16);
            h->set_margin_bottom(4);
            auto r = Gtk::make_managed<Gtk::ListBoxRow>();
            r->set_selectable(false);
            r->set_activatable(false);
            r->set_child(*h);
            m_list->append(*r);
            m_row_ids.push_back("");
        }
        auto l = text(t.title, nullptr, 24);
        l->set_selectable(false);
        l->set_margin_start(20);
        l->set_margin_end(12);
        l->set_margin_top(6);
        l->set_margin_bottom(6);
        auto r = Gtk::make_managed<Gtk::ListBoxRow>();
        r->set_name("help.topic." + t.id);
        r->set_child(*l);
        m_list->append(*r);
        m_row_ids.push_back(t.id);
    }
    m_list->signal_row_selected().connect([this](Gtk::ListBoxRow* r) {
        if (!r) return;
        const auto i = static_cast<std::size_t>(r->get_index());
        if (i < m_row_ids.size() && !m_row_ids[i].empty()) show_topic(m_row_ids[i]);
    });
    auto left = Gtk::make_managed<Gtk::ScrolledWindow>();
    left->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    left->set_child(*m_list);
    left->set_size_request(220, -1);

    m_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    m_scroll->set_name("help.page");
    m_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll->set_hexpand(true);

    auto paned = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);
    paned->set_start_child(*left);
    paned->set_end_child(*m_scroll);
    paned->set_resize_start_child(false);
    paned->set_shrink_start_child(false);
    paned->set_position(230);
    set_child(*paned);

    if (auto* r = m_list->get_row_at_index(1)) m_list->select_row(*r);   // the Introduction
}

void HelpWindow::show_topic(const std::string& id) {
    const auto* t = core::help::find(id);
    if (!t) return;
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_margin(26);
    auto title = text("", "hero-title");
    title->set_markup("<span size='x-large' weight='bold'>" + Glib::Markup::escape_text(t->title).raw() + "</span>");
    title->set_margin_bottom(4);
    box->append(*title);
    for (const auto& p : t->about) box->append(*text(p));
    if (!t->steps.empty()) {
        auto h = text("", "heading");
        h->set_markup("<b>How to</b>");
        h->set_margin_top(10);
        box->append(*h);
        int n = 0;
        for (const auto& s : t->steps) {
            auto l = text(std::to_string(++n) + ".  " + s);
            l->set_margin_start(8);
            box->append(*l);
        }
    }
    if (!t->page.empty()) {
        const auto* p = core::pages::find(t->page);
        auto b = Gtk::make_managed<Gtk::Button>("Show " + (p ? p->title : t->page));
        b->set_name("help.go." + t->page);
        b->set_halign(Gtk::Align::START);
        b->set_margin_top(14);
        b->signal_clicked().connect([this, page = t->page] { if (m_go) m_go(page); });
        box->append(*b);
    }
    m_scroll->set_child(*box);
    m_scroll->get_vadjustment()->set_value(0);
}

}  // namespace tidy
