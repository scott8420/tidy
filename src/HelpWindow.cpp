#include "HelpWindow.hpp"
#include "core/Help.hpp"
#include "core/Pages.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

namespace tidy {

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    l->set_max_width_chars(70);
    if (css) l->add_css_class(css);
    return l;
}
}  // namespace

HelpWindow::HelpWindow(Gtk::Window& parent, Go go) {
    set_name("shell.help");
    set_title("How to use Tidy");
    set_transient_for(parent);
    set_hide_on_close(true);
    set_default_size(620, 720);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(22);
    int n = 0;
    for (const auto& sec : core::help::sections()) {
        auto h = text(sec.title, "heading");
        h->set_margin_top(n++ ? 16 : 0);
        box->append(*h);
        if (!sec.intro.empty()) box->append(*text(sec.intro, "dim"));
        for (const auto& st : sec.steps) {
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            row->set_margin_start(10);
            auto t = text("• " + st.text);
            t->set_hexpand(true);
            row->append(*t);
            if (!st.page.empty()) {
                const auto* p = core::pages::find(st.page);
                auto b = Gtk::make_managed<Gtk::Button>(p ? p->title : st.page);
                b->set_name("help.go." + st.page);
                b->set_valign(Gtk::Align::START);
                b->set_tooltip_text("Show this page");
                b->signal_clicked().connect([go, page = st.page] { if (go) go(page); });
                row->append(*b);
            }
            box->append(*row);
        }
    }
    auto lv = text("Retention levels -- what a decided file is", "heading");
    lv->set_margin_top(16);
    box->append(*lv);
    for (const auto& [t, m] : core::help::levels()) {
        auto l = text("• " + t + " -- " + m);
        l->set_margin_start(10);
        box->append(*l);
    }
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_child(*box);
    set_child(*scroll);
}

}  // namespace tidy
