#include "ConformWindow.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Catalog.hpp"
#include "core/Projects.hpp"
#include "core/Tagging.hpp"

#include <giomm/file.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>

#include <ctime>
#include <filesystem>
#include <memory>

namespace tidy {

namespace dr = latch::door;
namespace cf = core::conform;
namespace tp = core::templates;

namespace {
constexpr std::size_t kShown = 400;   // preview lines, then a count
Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
std::string n_of(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }
// s023i: a numbered run's pattern read as A####.png, not A[0-9][0-9][0-9][0-9].png.
std::string readable(std::string s) {
    for (std::size_t at; (at = s.find("[0-9]")) != std::string::npos;) s.replace(at, 5, "#");
    return s;
}
}  // namespace

ConformWindow::ConformWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_changed(std::move(changed)) {
    set_name("conform.window");
    set_title("Conform folders");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(780, 640);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Conform folders", "heading"));
    box->append(*text("A template's routes put every file of each folder in its place -- top to bottom, the first that "
                      "fits decides (Templates: \"*.odt -> Originals\", \"Archive/*.png -> stays where it is\", "
                      "\"*.optik -> the project's top\"). A file already under its folder is left; one no route fits "
                      "stays and is listed. Nothing leaves its folder. One preview, one undo step in Activity.", "dim"));

    auto row1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row1->append(*text("Template"));
    m_tpl = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({}));
    m_tpl->set_name("conform.template");
    m_tpl->property_selected().signal_changed().connect([this] { stale(); });
    row1->append(*m_tpl);
    box->append(*row1);

    auto row2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto pick = Gtk::make_managed<Gtk::Button>("Folder...");
    pick->set_name("conform.pick");
    pick->signal_clicked().connect([this] { on_pick(); });
    row2->append(*pick);
    m_where = text("No folder yet", "dim");
    m_where->set_name("conform.where");
    m_where->set_hexpand(true);
    row2->append(*m_where);
    box->append(*row2);

    auto row3 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
    m_each = Gtk::make_managed<Gtk::CheckButton>("Each sub-folder is a project");
    m_each->set_name("conform.each");
    m_each->set_active(true);
    m_each->signal_toggled().connect([this] { stale(); });
    row3->append(*m_each);
    m_rename = Gtk::make_managed<Gtk::CheckButton>("Rename each to <name>-project (kept whole from then on)");
    m_rename->set_name("conform.rename");
    m_rename->signal_toggled().connect([this] { stale(); });
    row3->append(*m_rename);
    auto look = Gtk::make_managed<Gtk::Button>("Look");
    look->set_name("conform.look");
    look->signal_clicked().connect([this] { on_look(); });
    row3->append(*look);
    box->append(*row3);

    m_summary = text("");
    m_summary->set_name("conform.summary");
    box->append(*m_summary);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("conform.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    box->append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("conform.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("conform.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_run->set_name("conform.run");
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

ConformWindow::~ConformWindow() = default;

void ConformWindow::fill_templates() {
    m_templates = tp::list(m_data_dir);
    auto names = Gtk::StringList::create({});
    for (const auto& t : m_templates)
        names->append(t.name + "   (" + std::to_string(t.routes.size()) + (t.routes.size() == 1 ? " route)" : " routes)"));
    m_tpl->set_model(names);
}

void ConformWindow::open(const std::string& folder) {
    fill_templates();
    if (!folder.empty()) m_folder = folder;
    m_where->set_text(m_folder.empty() ? "No folder yet" : "~" + m_folder.substr(m_home.size()));
    stale();
    m_status->set_text(m_templates.empty() ? "No templates yet -- Templates -> Lift from a project... (your best book "
                                             "folder), add its routes, Save; then come back."
                                           : "");
    present();
    if (!folder.empty() && !m_templates.empty()) on_look();   // from the Map: show what it would do
}

void ConformWindow::open_with(const std::string& folder, const std::string& tpl, bool each) {
    fill_templates();
    for (std::size_t i = 0; i < m_templates.size(); ++i)
        if (m_templates[i].name == tpl) m_tpl->set_selected(static_cast<unsigned>(i));
    m_each->set_active(each);
    m_folder = folder;
    m_where->set_text("~" + (m_folder.rfind(m_home, 0) == 0 ? m_folder.substr(m_home.size()) : m_folder));
    stale();
    m_status->set_text("");
    present();
    on_look();
}

void ConformWindow::stale() {
    m_plan.reset();
    m_projects.clear();
    if (m_list)
        while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    if (m_summary) m_summary->set_text(m_folder.empty() ? "" : "Look to see what conforming does -- nothing changes until Do it.");
    if (m_run) m_run->set_sensitive(false);
}

void ConformWindow::on_pick() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("The folder of projects (e.g. ~/Documents/EBooks)");
    dlg->set_modal(true);
    dlg->set_initial_folder(Gio::File::create_for_path(m_folder.empty() ? m_home : m_folder));
    dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            const auto f = dlg->select_folder_finish(res);
            if (!f || f->get_path().empty()) return;
            m_folder = f->get_path();
            m_where->set_text("~" + (m_folder.rfind(m_home, 0) == 0 ? m_folder.substr(m_home.size()) : m_folder));
            stale();
            on_look();
        } catch (const Glib::Error&) {}   // cancelled
    });
}

void ConformWindow::on_look() {
    if (m_busy) return;
    const auto i = m_tpl->get_selected();
    if (i >= m_templates.size()) { m_status->set_text("Pick a template first."); return; }
    if (m_folder.empty()) { m_status->set_text("Pick a folder first."); return; }
    if (const auto why = cf::refusal(m_home, m_folder); !why.empty()) { m_status->set_text("Not that folder: " + why); return; }
    m_status->set_text("");
    const auto t = m_templates[i];
    const std::string folder = m_folder, home = m_home;
    const bool each = m_each->get_active();
    struct Out { std::vector<cf::Project> ps; std::vector<std::string> refused; };
    auto out = std::make_shared<Out>();
    m_busy = true;
    busy::start(*this, "Conform -- looking", "Reading every file in each folder (nothing is changed)",
        [out, t, folder, home, each] {
            for (const auto& d : cf::folders(folder, each)) {
                if (auto why = cf::refusal(home, d); !why.empty()) { out->refused.push_back(d + " -- " + why); continue; }
                out->ps.push_back(cf::look(t, d));
            }
        },
        [this, out] { m_busy = false; show(std::move(out->ps), std::move(out->refused)); });
}

void ConformWindow::show(std::vector<cf::Project> ps, std::vector<std::string> refused) {
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    std::size_t shown = 0, moves = 0, unmatched = 0, in_place = 0, stays = 0;
    const auto line = [&](const std::string& s, const char* css, int indent) {
        if (shown++ >= kShown) return;
        auto l = text(s, css);
        l->set_margin(2);
        l->set_margin_start(indent);
        m_list->append(*l);
    };
    for (const auto& r : refused) line(r, "dim", 6);
    for (const auto& p : ps) {
        const std::string name = std::filesystem::path(p.dir).filename().string();
        moves += p.moves.size();
        unmatched += p.unmatched.size();
        in_place += p.in_place;
        stays += p.stays;
        std::string head = name + " -- " + n_of(p.moves.size(), "move", "moves");
        if (p.in_place) head += ", " + std::to_string(p.in_place) + " in place";
        if (p.stays) head += ", " + std::to_string(p.stays) + " stay (a route says so)";
        if (!p.unmatched.empty()) head += ", " + std::to_string(p.unmatched.size()) + " no route fits";
        if (p.whole) head += ", " + std::to_string(p.whole) + " kept whole inside";
        line(head, "heading", 6);
        for (const auto& m : p.moves) {
            const std::string rel = m.from.substr(p.dir.size() + 1);
            line(rel + "   ->   " + (m.folder.empty() ? std::string("(the top)") : m.folder + "/") + "      " + readable(m.why), nullptr, 20);
        }
        for (const auto& u : p.unmatched) line("no route fits: " + u + " -- stays", "dim", 20);
    }
    if (shown > kShown) {
        auto l = text("... and " + std::to_string(shown - kShown) + " more lines -- all of them are in the plan", "dim");
        l->set_margin(4);
        m_list->append(*l);
    }
    auto pl = cf::plan(m_home, ps, m_rename->get_active());
    std::string s = n_of(ps.size(), "folder", "folders") + ": " + n_of(moves, "file to move", "files to move") + ", " +
                    std::to_string(in_place) + " in place, " + std::to_string(stays) + " staying by a route";
    if (unmatched) s += ", " + std::to_string(unmatched) + " no route fits (add a route in Templates, Look again)";
    if (!pl.renamed.empty()) s += "; " + std::to_string(pl.renamed.size()) + " names taken -> \"(2)\"";
    if (!pl.not_renamed.empty()) s += "; " + std::to_string(pl.not_renamed.size()) + " not renamed";
    s += ".";
    m_summary->set_text(s);
    if (auto lg = log::get(log::Area::Io))
        lg->info("conform look: {} folders, {} moves, {} in place, {} stay, {} unmatched", ps.size(), moves, in_place, stays, unmatched);
    m_projects = std::move(ps);
    m_run->set_sensitive(!pl.plan.actions.empty());
    m_plan = std::move(pl);
}

void ConformWindow::on_run() {
    if (!m_plan || m_busy) return;
    auto plan = m_plan->plan;
    plan.previewed = true;   // shown in this window's list
    m_busy = true;
    m_run->set_sensitive(false);
    const std::string data = m_data_dir;
    dr::Door* door = &m_door;
    busy::door(*this, m_door, std::move(plan), "Conform",
        [this](const dr::Result& r) {
            m_busy = false;
            if (m_changed) m_changed();
            std::string folder = m_folder;
            if (m_rename->get_active() && !m_each->get_active() && r.ok &&
                !core::projects::is_project_name(std::filesystem::path(folder).filename().string()))
                folder += "-project";   // the one folder was renamed: look at it where it is now
            m_folder = folder;
            on_look();
            m_status->set_text(r.ok ? "Done: " + std::to_string(r.done) + " steps -- one step to undo in Activity."
                                    : "Not all done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front()));
            if (auto lg = log::get(log::Area::Door)) lg->info("conform run: {} done, {} failed", r.done, r.failed);
        },
        [data, door](const dr::Result& r) {   // the catalog follows the files it knows
            core::catalog::Catalog cat;
            if (cat.open(data, true))
                core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
        });
}

}  // namespace tidy
