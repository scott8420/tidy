#include "SettingsWindow.hpp"

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/textview.h>

namespace tidy {

namespace st = core::settings;

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
}  // namespace

SettingsWindow::SettingsWindow(Gtk::Window& parent, SaveFn on_save) : m_save(std::move(on_save)) {
    set_name("settings.window");
    set_title("Tidy Settings");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(460, 440);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);

    box->append(*text("The Closet", "heading"));
    auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    auto pre = text("Propose aging out after");
    pre->set_wrap(false);
    row->append(*pre);
    m_days = Gtk::make_managed<Gtk::SpinButton>(
        Gtk::Adjustment::create(st::kAgingDefault, st::kAgingMin, st::kAgingMax, 1, 10));
    m_days->set_name("settings.aging_days");
    m_days->set_numeric(true);
    row->append(*m_days);
    auto post = text("days unread");
    post->set_wrap(false);
    row->append(*post);
    box->append(*row);
    box->append(*text("An undecided file born and unread this long is proposed as Ephemeral.", "dim"));

    auto prj = text("Projects", "heading");
    prj->set_margin_top(14);
    box->append(*prj);
    auto row2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    auto pre2 = text("Look again every");
    pre2->set_wrap(false);
    row2->append(*pre2);
    m_rescan = Gtk::make_managed<Gtk::SpinButton>(
        Gtk::Adjustment::create(st::kRescanDefault, st::kRescanMin, st::kRescanMax, 1, 10));
    m_rescan->set_name("settings.rescan_minutes");
    m_rescan->set_numeric(true);
    row2->append(*m_rescan);
    auto post2 = text("minutes (0 = never)");
    post2->set_wrap(false);
    row2->append(*post2);
    box->append(*row2);
    box->append(*text("While Tidy runs, and once at launch: finds your projects again, read-only. "
                      "A moved one is followed by its id; one not found is marked gone, never forgotten.", "dim"));

    auto junk = text("Junk list", "heading");
    junk->set_margin_top(14);
    box->append(*junk);
    box->append(*text("File-name patterns, one per line -- *.png, Screenshot*, *.tmp. A file in the "
                      "Closet that matches is proposed for the Junk Drawer: kept, no home. Your "
                      "list wins over Tidy's own birth rules; a decided file is never re-proposed.", "dim"));
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_junk = Gtk::make_managed<Gtk::TextView>();
    m_junk->set_name("settings.junk");
    m_junk->set_monospace(true);
    m_junk->set_top_margin(6);
    m_junk->set_left_margin(6);
    scroll->set_child(*m_junk);
    box->append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->set_margin_top(8);
    m_status = text("");
    m_status->set_name("settings.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*cancel);
    auto save = Gtk::make_managed<Gtk::Button>("Save");
    save->set_name("settings.save");
    save->add_css_class("suggested-action");
    save->signal_clicked().connect([this] { on_save_clicked(); });
    bar->append(*save);
    box->append(*bar);
    set_child(*box);
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::show(const st::Settings& current) {
    m_days->set_value(current.aging_days);
    m_rescan->set_value(current.rescan_minutes);
    std::string lines;
    for (const auto& j : current.junk) lines += j + "\n";
    m_junk->get_buffer()->set_text(lines);
    m_status->set_text("");
    present();
}

void SettingsWindow::on_save_clicked() {
    st::Settings s;
    s.aging_days = m_days->get_value_as_int();
    s.rescan_minutes = m_rescan->get_value_as_int();
    s.junk = st::patterns_of(m_junk->get_buffer()->get_text());
    const std::string err = m_save ? m_save(s) : std::string("nothing to save to");
    if (err.empty()) set_visible(false);
    else m_status->set_text("Not saved: " + err);
}

}  // namespace tidy
