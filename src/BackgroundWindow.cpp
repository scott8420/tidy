#include "BackgroundWindow.hpp"
#include "Log.hpp"
#include "core/Due.hpp"
#include "core/Own.hpp"

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <glibmm/spawn.h>

#include <ctime>
#include <filesystem>

namespace tidy {

namespace bg = core::background;
namespace dr = latch::door;
namespace st = core::settings;

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
std::string self_exe() {
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec).string();
    const std::string del = " (deleted)";
    if (p.size() > del.size() && p.compare(p.size() - del.size(), del.size(), del) == 0) p.resize(p.size() - del.size());
    return p.empty() ? "tidy" : p;
}
}  // namespace

BackgroundWindow::BackgroundWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir,
                                   std::string unit_dir, bool fixture, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_unit_dir(std::move(unit_dir)),
      m_fixture(fixture), m_changed(std::move(changed)) {
    set_name("background.window");
    set_title("Background scan");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(720, 560);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Background scan", "heading"));
    box->append(*text("A systemd user timer runs \"tidy --scan --notify\" whether Tidy is open or not. The scan only "
                      "reads; it counts what needs attention (due, the Closet, id lists) for the tile. When the count "
                      "reaches the number below you get one notification -- again only when it grows. Clicking it "
                      "opens Tidy.", "dim"));
    m_state = text("");
    m_state->set_name("background.state");
    box->append(*m_state);
    m_last = text("", "dim");
    m_last->set_name("background.last");
    box->append(*m_last);
    const auto spin_row = [&](const char* pre, Gtk::SpinButton*& spin, const char* name, int def, int lo, int hi,
                              const char* post) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto a = text(pre);
        a->set_wrap(false);
        row->append(*a);
        spin = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(def, lo, hi, 1, 10));
        spin->set_name(name);
        spin->set_numeric(true);
        row->append(*spin);
        auto b = text(post);
        b->set_wrap(false);
        row->append(*b);
        box->append(*row);
    };
    spin_row("Look every", m_minutes, "background.minutes", st::kBgDefault, st::kBgMin, st::kBgMax, "minutes");
    spin_row("Notify at", m_notify, "background.notify_at", st::kNotifyDefault, st::kNotifyMin, st::kNotifyMax,
             "files needing attention");

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("background.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    box->append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("background.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    const auto button = [&](const char* label, const char* name, auto fn) {
        auto b = Gtk::make_managed<Gtk::Button>(label);
        b->set_name(name);
        b->signal_clicked().connect(fn);
        bar->append(*b);
        return b;
    };
    m_on = button("Turn on…", "background.on", [this] {
        if (!save()) return;
        preview(bg::turn_on(m_home, m_unit_dir, self_exe(), m_fixture ? m_home : "", m_settings.bg_minutes));
    });
    m_off = button("Turn off…", "background.off", [this] { preview(bg::turn_off(m_unit_dir)); });
    button("Save", "background.save", [this] {
        if (!save()) return;
        const auto s = bg::state_of(m_unit_dir);
        if (s.on() && s.minutes != m_settings.bg_minutes) { preview(bg::retime(m_unit_dir, m_settings.bg_minutes)); return; }
        m_status->set_text("Saved.");
    });
    button("Close", "background.close", [this] { set_visible(false); });
    m_run = button("Do it", "background.run", [this] { on_run(); });
    m_run->add_css_class("suggested-action");
    box->append(*bar);
    set_child(*box);
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType) {
        if (key != GDK_KEY_Escape) return false;
        set_visible(false);
        return true;
    }, false);
    add_controller(keys);
}

BackgroundWindow::~BackgroundWindow() = default;

void BackgroundWindow::open(const st::Settings& current) {
    m_settings = current;
    m_minutes->set_value(current.bg_minutes);
    m_notify->set_value(current.notify_at);
    m_work.reset();
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    m_status->set_text("");
    m_run->set_sensitive(false);       // nothing shown yet
    refresh();
    present();
}

void BackgroundWindow::refresh() {
    const auto s = bg::state_of(m_unit_dir);
    std::string line;
    if (s.on()) {
        line = "On: every " + std::to_string(s.minutes) + " minutes (" + bg::kTimer + ")";
        if (m_fixture) {
            line += " -- a FIXTURE home: systemctl is never asked";
        } else {
            bool ok = false;
            const auto said = run_command({"systemctl", "--user", "is-enabled", bg::kTimer}, &ok);
            line += ok ? " -- enabled" : " -- NOT enabled (" + said + "): Turn on again";
        }
    } else if (s.service || s.timer) {
        line = s.ours ? "Half there (one unit missing) -- Turn on writes it, Turn off clears it."
                      : "A " + std::string(bg::kTimer) + " / .service that is not Tidy's is there -- left alone.";
    } else {
        line = "Off.";
    }
    m_state->set_text(line);
    m_on->set_sensitive(!s.on() && (s.ours || (!s.service && !s.timer)));
    m_off->set_sensitive(s.service || s.timer ? s.ours || s.on() : false);

    bool found = false;
    const auto a = core::due::parse(core::own::read(m_data_dir, core::due::kFile, &found));
    if (!found || !a.when) { m_last->set_text("No look yet."); return; }
    char when[32];
    const auto t = static_cast<std::time_t>(a.when);
    std::strftime(when, sizeof when, "%a %H:%M", std::localtime(&t));
    m_last->set_text(std::string("Last look: ") + when + " -- " + core::due::line(a) + " (" + std::to_string(a.ms) + " ms)");
}

bool BackgroundWindow::save() {
    m_settings.bg_minutes = m_minutes->get_value_as_int();
    m_settings.notify_at = m_notify->get_value_as_int();
    if (const auto err = core::own::write(m_data_dir, st::kFile, st::format(m_settings)); !err.empty()) {
        m_status->set_text("Not saved: " + err);
        return false;
    }
    if (m_changed) m_changed(m_settings);
    return true;
}

void BackgroundWindow::preview(bg::Work work) {
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    const auto row = [this](const std::string& what, const std::string& why) {
        auto l = text(what + (why.empty() ? "" : "   REFUSED: " + why), why.empty() ? nullptr : "dim");
        l->set_margin(6);
        m_list->append(*l);
    };
    const auto shown = [this](const std::string& p) { return p.rfind(m_home, 0) == 0 ? "~" + p.substr(m_home.size()) : p; };
    const auto commands = [&](const std::vector<std::vector<std::string>>& cs) {
        for (const auto& c : cs)
            row((m_fixture ? "Would run (a fixture: not run): " : "Run: ") + bg::shown_command(c), "");
    };
    std::size_t refused = 0;
    commands(work.first);
    std::vector<std::string> made, freed;
    for (const auto& a : work.plan.actions) {
        const auto why = m_door.refusal(a, made, freed);
        if (why.empty() && a.verb == dr::Verb::MakeDir) made.push_back(a.to);
        refused += !why.empty();
        row(a.verb == dr::Verb::MakeDir ? "Make folder " + shown(a.to)
            : a.verb == dr::Verb::Trash  ? "Trash " + shown(a.from) + " -- " + a.reason
                                         : "Write " + shown(a.to) + " -- " + a.reason, why);
    }
    for (const auto& x : work.replaces) {
        const auto why = m_door.replace_refusal(x.path, x.text);
        refused += !why.empty();
        row("Rewrite " + shown(x.path) + " -- " + x.reason + "  (in place, no undo)", why);
    }
    commands(work.commands);
    m_run->set_sensitive(refused == 0);
    m_status->set_text(refused ? std::to_string(refused) + " refused -- nothing will be done." : "Shown -- Do it to go ahead.");
    m_work = refused ? std::nullopt : std::optional<bg::Work>(std::move(work));
}

std::string BackgroundWindow::run_command(const std::vector<std::string>& argv, bool* ok) {
    std::string out, err;
    int status = -1;
    *ok = false;
    try {
        Glib::spawn_sync("", argv, Glib::SpawnFlags::SEARCH_PATH, {}, &out, &err, &status);
        *ok = status == 0;
    } catch (const Glib::Error& e) {
        err = e.what();
    }
    std::string said = out + err;
    while (!said.empty() && (said.back() == '\n' || said.back() == ' ')) said.pop_back();
    return said;
}

void BackgroundWindow::on_run() {
    if (!m_work) return;
    const auto work = *m_work;
    m_work.reset();
    auto lg = log::get(log::Area::Io);
    std::string fail;
    const auto commands = [&](const std::vector<std::vector<std::string>>& cs) {
        for (const auto& c : cs) {
            if (!fail.empty()) return;
            const auto line = bg::shown_command(c);
            if (m_fixture) { if (lg) lg->info("background (fixture, not run): {}", line); continue; }
            bool ok = false;
            const auto said = run_command(c, &ok);
            if (lg) lg->info("background: {} -> {}{}", line, ok ? "ok" : "FAILED", said.empty() ? "" : ": " + said);
            if (!ok) fail = line + (said.empty() ? "" : ": " + said);
        }
    };
    commands(work.first);
    if (fail.empty() && !work.plan.actions.empty()) {
        auto plan = work.plan;
        plan.previewed = true;                 // shown in this window's list
        const auto r = m_door.run(plan);
        if (!r.ok) fail = r.messages.empty() ? std::string("see the log") : r.messages.front();
    }
    if (fail.empty())
        for (const auto& x : work.replaces) {
            const auto why = m_door.replace_own(x.path, x.text);
            if (lg) lg->info("background: {} rewritten in place (no undo){}", x.path, why.empty() ? "" : " -- NOT: " + why);
            if (!why.empty()) { fail = why; break; }
        }
    commands(work.commands);
    if (m_changed) m_changed(m_settings);
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    refresh();
    m_run->set_sensitive(false);
    m_status->set_text(fail.empty() ? "Done" + std::string(work.plan.actions.empty() ? "." : " -- the files undo in Activity.")
                                    : "Stopped: " + fail);
}

}  // namespace tidy
