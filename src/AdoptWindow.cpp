#include "AdoptWindow.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Catalog.hpp"
#include "core/Own.hpp"
#include "core/Protection.hpp"
#include "core/Routes.hpp"
#include "core/Tagging.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>
#include <giomm/file.h>

#include <ctime>
#include <memory>

namespace tidy {

namespace dr = latch::door;
namespace ad = core::adopt;
namespace rt = core::retention;

namespace {
constexpr std::size_t kShownPerFolder = 200;   // the preview lists this many files per folder, then counts the rest

Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}
}  // namespace

AdoptWindow::AdoptWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir,
                         std::function<std::string()> user_dirs, Save save, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_user_dirs(std::move(user_dirs)),
      m_save(std::move(save)), m_changed(std::move(changed)) {
    set_name("adopt.window");
    set_title("Adopt folders");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(760, 620);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Adopt folders", "heading"));
    box->append(*text("Folders you organized by hand. In place: every file is cataloged where it is -- an id, and "
                      "a level if it has none (Keep; Historical when untouched for years, so Due offers to archive "
                      "it). Nothing moves, and Tidy learns from what is there (cover*.png kept in ~/Pictures/Covers "
                      "-> proposed there next time). Catch-all: a folder of leftovers -- its files, at any depth, "
                      "are offered to Sort, Files home and the Closet like loose ones. Never hidden files, "
                      "-projects, repos, Hands off or Samson.", "dim"));

    auto fscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    fscroll->set_has_frame(true);
    fscroll->set_min_content_height(120);
    fscroll->set_max_content_height(200);
    fscroll->set_propagate_natural_height(true);
    m_folders = Gtk::make_managed<Gtk::ListBox>();
    m_folders->set_name("adopt.folders");
    m_folders->set_selection_mode(Gtk::SelectionMode::NONE);
    fscroll->set_child(*m_folders);
    box->append(*fscroll);
    auto fbar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto add = Gtk::make_managed<Gtk::Button>("Add folder...");
    add->set_name("adopt.add");
    add->signal_clicked().connect([this] { on_add(); });
    fbar->append(*add);
    auto look = Gtk::make_managed<Gtk::Button>("Look");
    look->set_name("adopt.look");
    look->signal_clicked().connect([this] { on_look(); });
    fbar->append(*look);
    box->append(*fbar);

    m_summary = text("");
    m_summary->set_name("adopt.summary");
    box->append(*m_summary);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("adopt.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    box->append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("adopt.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("adopt.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_run->set_name("adopt.run");
    m_run->add_css_class("suggested-action");
    m_run->set_sensitive(false);
    m_run->signal_clicked().connect([this] { on_run(); });
    bar->append(*m_run);
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

AdoptWindow::~AdoptWindow() = default;

void AdoptWindow::open(const core::settings::Settings& current) {
    m_current = current;
    m_picks.clear();
    for (const auto& r : current.adopted) m_picks.push_back({r, false});
    for (const auto& r : current.catch_all) m_picks.push_back({r, true});
    build_folders();
    stale();
    m_status->set_text("");
    present();
    if (!m_picks.empty()) on_look();   // the remembered folders: what is new in them
}

void AdoptWindow::build_folders() {
    while (auto* r = m_folders->get_row_at_index(0)) m_folders->remove(*r);
    if (m_picks.empty()) {
        auto l = text("No folders yet -- Add folder... (Documents/Writing, Pictures, a Misc folder...)", "dim");
        l->set_margin(8);
        m_folders->append(*l);
        return;
    }
    for (std::size_t i = 0; i < m_picks.size(); ++i) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->set_margin(4);
        row->set_margin_start(8);
        auto name = text("~/" + m_picks[i].rel);
        name->set_hexpand(true);
        row->append(*name);
        auto ca = Gtk::make_managed<Gtk::CheckButton>("Catch-all");
        ca->set_name("adopt.catchall." + std::to_string(i));
        ca->set_tooltip_text("Its files are leftovers: offer them to Sort like loose files, instead of cataloging them here");
        ca->set_active(m_picks[i].catch_all);
        ca->signal_toggled().connect([this, i, ca] {
            if (i < m_picks.size()) m_picks[i].catch_all = ca->get_active();
            stale();
        });
        row->append(*ca);
        auto forget = Gtk::make_managed<Gtk::Button>();
        forget->set_name("adopt.forget." + std::to_string(i));
        forget->set_icon_name("window-close-symbolic");
        forget->set_has_frame(false);
        forget->set_tooltip_text("Forget this folder (its files keep their ids and levels)");
        forget->signal_clicked().connect([this, i] {
            if (i < m_picks.size()) m_picks.erase(m_picks.begin() + static_cast<long>(i));
            build_folders();
            stale();
        });
        row->append(*forget);
        m_folders->append(*row);
    }
}

void AdoptWindow::stale() {
    m_result.reset();
    m_plan.reset();
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    m_summary->set_text(m_picks.empty() ? "" : "Look to see what adopting does -- nothing is changed until Do it.");
    m_run->set_sensitive(false);
}

void AdoptWindow::on_add() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("A folder you organized");
    dlg->set_modal(true);
    dlg->set_initial_folder(Gio::File::create_for_path(m_home));
    dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            const auto f = dlg->select_folder_finish(res);
            if (!f || f->get_path().empty()) return;
            std::string rel;
            if (const auto why = core::protection::rel_of(m_home, f->get_path(), rel); !why.empty()) {
                m_status->set_text("Not added: " + why);
                return;
            }
            for (const auto& p : m_picks)
                if (p.rel == rel) { m_status->set_text("~/" + rel + " is listed already"); return; }
            m_picks.push_back({rel, false});
            m_status->set_text("");
            build_folders();
            on_look();
        } catch (const Glib::Error&) {}   // cancelled
    });
}

void AdoptWindow::on_look() {
    if (m_picks.empty()) { stale(); return; }
    m_summary->set_text("Looking...");
    m_run->set_sensitive(false);
    const std::string home = m_home, dirs = m_user_dirs ? m_user_dirs() : "";
    const int old_years = m_current.old_years;
    const auto picks = m_picks;
    if (m_busy) return;
    m_busy = true;
    std::string where;
    for (const auto& p : picks) where += (where.empty() ? "~/" : ", ~/") + p.rel;
    auto out = std::make_shared<Result>();
    const dr::Door* door = &m_door;   // read only (refusal); the Busy window is modal -- nothing runs the door meanwhile
    busy::start(*this, "Adopt folders -- looking", "Reading " + where,
        [home, dirs, old_years, picks, out, door] {
            Result& r = *out;
            const auto now = static_cast<std::int64_t>(std::time(nullptr));
            for (const auto& p : picks) {
                r.refused.push_back(ad::refusal(home, dirs, p.rel, p.catch_all));
                r.looks.push_back(r.refused.back().empty() ? ad::look(home, dirs, p.rel, p.catch_all, now, old_years)
                                                           : ad::Look{p.rel, p.catch_all, {}, 0, 0, 0, {}});
            }
            std::vector<ad::Look> ok;
            for (std::size_t i = 0; i < r.looks.size(); ++i)
                if (r.refused[i].empty()) ok.push_back(r.looks[i]);
            std::vector<std::string> made, freed;
            for (const auto& a : ad::plan(ok).actions) r.refused_steps += !door->refusal(a, made, freed).empty();
        },
        [this, out] { m_busy = false; show(std::move(*out)); });
}

void AdoptWindow::show(Result r) {
    while (auto* x = m_list->get_row_at_index(0)) m_list->remove(*x);
    const auto line = [this](const std::string& s, const char* css = nullptr, int indent = 6) {
        auto l = text(s, css);
        l->set_margin(3);
        l->set_margin_start(indent);
        m_list->append(*l);
    };
    std::vector<ad::Look> ok;
    std::size_t files = 0, offered = 0, bad = 0;
    const auto book = core::routes::parse(core::own::read(m_data_dir, core::routes::kFile));
    const auto is_new = [&book](const ad::Learned& rr) {   // the book lacks it, or knows it from fewer files
        for (const auto& x : book.routes)
            if (x.kind == core::routes::Route::Kind::Name && x.pattern == rr.pattern && x.folder == rr.folder)
                return x.yes < rr.count;
        return true;
    };
    for (std::size_t i = 0; i < r.looks.size(); ++i) {
        const auto& l = r.looks[i];
        if (!r.refused[i].empty()) {
            ++bad;
            line("~/" + l.rel + "   REFUSED: " + r.refused[i], "dim");
            continue;
        }
        ok.push_back(l);
        if (l.catch_all) {
            offered += l.offered;
            line("~/" + l.rel + " -- catch-all: " + plural(l.offered, "file", "files") +
                 " offered to Sort, Files home and the Closet like loose ones. Nothing tagged now.", "heading");
            continue;
        }
        files += l.items.size();
        std::string head = "~/" + l.rel + " -- in place: " + plural(l.items.size(), "file", "files") + " cataloged";
        std::string parts;
        for (const auto lv : {rt::Level::Keep, rt::Level::Historical})
            if (const auto n = l.count(lv)) parts += (parts.empty() ? "" : ", ") + std::to_string(n) + " " + rt::title_of(lv);
        if (const auto other = l.items.size() - l.count(rt::Level::Keep) - l.count(rt::Level::Historical))
            parts += (parts.empty() ? "" : ", ") + std::to_string(other) + " keep their own level";
        if (!parts.empty()) head += " (" + parts + ")";
        if (l.known) head += "; " + std::to_string(l.known) + " known already";
        if (l.loose) head += "; " + std::to_string(l.loose) + " loose at a top -- left to Sort";
        line(head, "heading");
        for (const auto& rr : l.routes)
            line((is_new(rr) ? "learns: " : "known route: ") + rr.pattern + " -> ~/" + rr.folder + " (" +
                     plural(static_cast<std::size_t>(rr.count), "file", "files") + " there)", is_new(rr) ? nullptr : "dim", 18);
        std::size_t shown = 0;
        for (const auto& it : l.items) {
            if (shown++ == kShownPerFolder) { line("... and " + std::to_string(l.items.size() - kShownPerFolder) + " more", "dim", 18); break; }
            const std::string p = it.file.path.substr(m_home.size() + 1);
            line("~/" + p + "   " + (it.give_id ? "id + " : "") + it.why, "dim", 18);
        }
    }
    auto copy = book;
    const std::size_t routes = ad::learn(copy, ok);   // only what would change the book
    auto plan = ad::plan(ok);
    const std::size_t refused_steps = r.refused_steps;
    // What Do it saves: the picks that look fine (a refused one is dropped).
    std::vector<std::string> adopted, catch_all;
    for (std::size_t i = 0; i < r.looks.size(); ++i)
        if (r.refused[i].empty()) (r.looks[i].catch_all ? catch_all : adopted).push_back(r.looks[i].rel);
    const bool settings_change = adopted != m_current.adopted || catch_all != m_current.catch_all;
    std::string s = plural(files, "file", "files") + " to catalog in place";
    if (offered) s += ", " + plural(offered, "file", "files") + " offered to Sort";
    if (routes) s += ", " + plural(routes, "route", "routes") + " to learn";
    if (bad) s += "; " + plural(bad, "folder", "folders") + " refused (dropped at Do it)";
    if (refused_steps) s += "; " + std::to_string(refused_steps) + " of the door's steps refused";
    if (!files && !routes && !settings_change) s += ". Nothing to do.";
    else s += ". The tags are one step to undo in Activity.";
    m_summary->set_text(s);
    m_run->set_sensitive((files && refused_steps < plan.actions.size()) || routes || settings_change);
    if (auto lg = log::get(log::Area::Io))
        lg->info("adopt look: {} folders, {} files, {} offered, {} routes, {} refused folders, {} refused steps",
                 r.looks.size(), files, offered, routes, bad, refused_steps);
    m_plan = plan.actions.empty() ? std::nullopt : std::optional<dr::Plan>(std::move(plan));
    m_result = std::move(r);
}

void AdoptWindow::on_run() {
    if (!m_result || m_busy) return;
    const auto result = *m_result;
    std::vector<ad::Look> ok;
    std::vector<std::string> adopted, catch_all;
    for (std::size_t i = 0; i < result.looks.size(); ++i)
        if (result.refused[i].empty()) {
            ok.push_back(result.looks[i]);
            (result.looks[i].catch_all ? catch_all : adopted).push_back(result.looks[i].rel);
        }
    // What happens once the tags are on (or straight away when there are none):
    // the routes, the folders, the look again -- on the window's thread.
    auto finish = [this, ok, adopted, catch_all](std::size_t done, std::string fail) {
        std::size_t learned = 0;
        if (fail.empty()) {   // what it teaches -- core::own; not undone by Activity (as Files home's routes)
            auto book = core::routes::parse(core::own::read(m_data_dir, core::routes::kFile));
            learned = ad::learn(book, ok);
            if (learned)
                if (const auto e = core::own::write(m_data_dir, core::routes::kFile, core::routes::format(book)); !e.empty())
                    fail = "routes not saved: " + e;
        }
        if (fail.empty() && (adopted != m_current.adopted || catch_all != m_current.catch_all)) {
            auto s = m_current;
            s.adopted = adopted;
            s.catch_all = catch_all;
            if (const auto e = m_save ? m_save(s) : std::string("no settings writer"); !e.empty()) fail = "folders not saved: " + e;
            else m_current = s;
        }
        if (auto lg = log::get(log::Area::Io))
            lg->info("adopt run: {} steps, {} routes learned, {} in place, {} catch-all{}", done, learned, adopted.size(),
                     catch_all.size(), fail.empty() ? "" : " -- " + fail);
        if (m_changed) m_changed();
        m_picks.clear();
        for (const auto& a : adopted) m_picks.push_back({a, false});
        for (const auto& c : catch_all) m_picks.push_back({c, true});
        build_folders();
        on_look();
        if (!fail.empty()) { m_status->set_text("Not done: " + fail); return; }
        std::string s = "Done: " + plural(done, "tag", "tags");
        if (done) s += " (one step to undo in Activity)";
        if (learned) s += "; " + plural(learned, "route", "routes") + " learned";
        s += "; the folders are remembered.";
        m_status->set_text(s);
    };
    if (!m_plan) { finish(0, ""); return; }
    auto plan = *m_plan;
    plan.previewed = true;   // shown in this window's list
    m_busy = true;
    m_run->set_sensitive(false);
    const std::string data = m_data_dir;
    dr::Door* door = &m_door;
    busy::door(*this, m_door, std::move(plan), "Adopt folders -- cataloging",
        [this, finish](const dr::Result& r) {
            m_busy = false;
            finish(r.done, r.ok ? std::string() : (r.messages.empty() ? std::string("see the log") : r.messages.front()));
        },
        [data, door](const dr::Result& r) {   // off the thread too: the catalog records every file
            core::catalog::Catalog cat;
            if (r.ok && cat.open(data, true))
                core::tagging::record(cat, core::tagging::paths_of(*door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
        });
}

}  // namespace tidy
