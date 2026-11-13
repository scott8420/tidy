#include "OrganizeWindow.hpp"
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
#include <gtkmm/entry.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>

#include <ctime>
#include <filesystem>

namespace tidy {

namespace dr = latch::door;
namespace og = core::organize;
namespace tp = core::templates;
namespace fs = std::filesystem;

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
std::string n_of(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }
std::string lower_word(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto sp = s.find(' ');
    return sp == std::string::npos ? s : s.substr(0, sp);
}
}  // namespace

OrganizeWindow::OrganizeWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir,
                               SaveCloset save, NewType new_type, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_save(std::move(save)),
      m_new_type(std::move(new_type)), m_changed(std::move(changed)) {
    set_name("organize.window");
    set_title("Organize as...");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(860, 660);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Organize as...", "heading"));

    auto row0 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto pick = Gtk::make_managed<Gtk::Button>("Folder...");
    pick->set_name("organize.pick");
    pick->signal_clicked().connect([this] { on_pick(); });
    row0->append(*pick);
    m_where = text("No folder yet", "dim");
    m_where->set_name("organize.where");
    m_where->set_hexpand(true);
    row0->append(*m_where);
    box->append(*row0);

    auto row1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row1->append(*text("Type"));
    m_type = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({}));
    m_type->set_name("organize.type");
    m_type->set_hexpand(true);
    m_type->property_selected().signal_changed().connect([this] { if (!m_quiet) on_type(); });
    row1->append(*m_type);
    auto nt = Gtk::make_managed<Gtk::Button>("New type...");
    nt->set_name("organize.new_type");
    nt->set_tooltip_text("Make a type in the Templates builder: its folders and what goes in each");
    nt->signal_clicked().connect([this] { set_visible(false); if (m_new_type) m_new_type(); });
    row1->append(*nt);
    box->append(*row1);

    // Everything below needs a type first.
    m_below = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    m_below->set_vexpand(true);
    auto row2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    row2->append(*text("This folder is"));
    m_one = Gtk::make_managed<Gtk::CheckButton>("one project");
    m_one->set_name("organize.one");
    m_many = Gtk::make_managed<Gtk::CheckButton>("a folder of projects");
    m_many->set_name("organize.many");
    m_many->set_group(*m_one);
    m_groupsb = Gtk::make_managed<Gtk::CheckButton>("a folder of groups");
    m_groupsb->set_name("organize.groups");
    m_groupsb->set_group(*m_one);
    m_one->signal_toggled().connect([this] { if (!m_quiet && m_one->get_active()) { m_shape = og::Shape::One; fill(); } });
    m_many->signal_toggled().connect([this] { if (!m_quiet && m_many->get_active()) { m_shape = og::Shape::Many; fill(); } });
    m_groupsb->signal_toggled().connect([this] { if (!m_quiet && m_groupsb->get_active()) { m_shape = og::Shape::Groups; fill(); } });
    row2->append(*m_one);
    row2->append(*m_many);
    row2->append(*m_groupsb);
    m_below->append(*row2);
    m_lay = Gtk::make_managed<Gtk::CheckButton>(
        "Lay out each project too -- its files where the type's tree says; what nothing fits stays at its top");
    m_lay->set_name("organize.lay");
    m_lay->set_active(true);
    m_lay->signal_toggled().connect([this] { replan(); });
    m_below->append(*m_lay);
    m_summary = text("");
    m_summary->set_name("organize.summary");
    m_below->append(*m_summary);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("organize.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    m_below->append(*scroll);
    box->append(*m_below);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("organize.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("organize.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_run->set_name("organize.run");
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

OrganizeWindow::~OrganizeWindow() = default;

void OrganizeWindow::fill_types(const std::string& want) {
    m_types = core::projects::known_templates(m_data_dir);
    auto names = Gtk::StringList::create({});
    names->append("(pick a type first)");
    guint sel = 0;
    for (std::size_t i = 0; i < m_types.size(); ++i) {
        const auto& t = m_types[i];
        const int pd = tp::place_depth(t);
        names->append(t.name + "   ~/" + t.category + (pd == 2 ? "/<" + t.group + ">" : "") +
                      (pd == 0 ? "/<name>-project" : "/<title>"));
        if (t.name == want) sel = static_cast<guint>(i + 1);
    }
    m_quiet = true;
    m_type->set_model(names);
    m_type->set_selected(sel);
    m_quiet = false;
}

void OrganizeWindow::open(const std::string& folder, const std::string& type) {
    const std::string keep = m_t ? m_t->name : "";
    fill_types(type.empty() ? keep : type);
    m_folder = folder;
    present();
    on_type();
    if (m_folder.empty()) on_pick();
}

void OrganizeWindow::on_type() {
    const auto i = m_type->get_selected();
    m_t.reset();
    if (i >= 1 && i - 1 < m_types.size()) m_t = m_types[i - 1];
    m_below->set_sensitive(m_t.has_value());
    if (!m_t) {
        while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
        m_summary->set_text("");
        m_run->set_sensitive(false);
        m_status->set_text(m_types.empty() ? "No types yet -- New type... makes one." : "Pick a type first: what this folder is to become.");
        m_where->set_text(m_folder.empty() ? "No folder yet" : "~" + m_folder.substr(m_home.size()));
        return;
    }
    const bool grp = tp::place_depth(*m_t) == 2;
    m_groupsb->set_visible(grp);
    m_groupsb->set_label("a folder of " + (m_t->group.empty() ? std::string("groups") : lower_word(m_t->group) + "s"));
    m_status->set_text("To ~/" + m_t->category);
    // The shape: guessed for this folder and type.
    m_shape = m_folder.empty() ? og::Shape::One : og::guess(*m_t, m_folder);
    if (!grp && m_shape == og::Shape::Groups) m_shape = og::Shape::Many;
    m_quiet = true;
    (m_shape == og::Shape::One ? m_one : m_shape == og::Shape::Many ? m_many : m_groupsb)->set_active(true);
    m_quiet = false;
    fill();
}

void OrganizeWindow::on_pick() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("The folder to organize");
    dlg->set_modal(true);
    dlg->set_initial_folder(Gio::File::create_for_path(m_folder.empty() ? m_home + "/Documents" : m_folder));
    dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            const auto f = dlg->select_folder_finish(res);
            if (!f || f->get_path().empty()) return;
            m_folder = f->get_path();
            on_type();
        } catch (const Glib::Error&) {}   // cancelled
    });
}

void OrganizeWindow::fill() {
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    m_groups.clear();
    m_where->set_text(m_folder.empty() ? "No folder yet"
                      : "~" + (m_folder.rfind(m_home, 0) == 0 ? m_folder.substr(m_home.size()) : m_folder));
    if (!m_t || m_folder.empty()) { replan(); return; }
    m_groups = og::groups_of(*m_t, m_folder, m_shape);
    std::erase_if(m_groups, [](const og::Group& g) { return g.projects.empty(); });
    const bool grp = tp::place_depth(*m_t) == 2;
    const std::string glabel = m_t->group.empty() ? std::string("Group") : m_t->group;
    for (std::size_t gi = 0; gi < m_groups.size(); ++gi) {
        if (grp) {   // the group's name: one entry for all its projects
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            row->set_margin(4);
            row->append(*text(glabel, "heading"));
            if (!m_groups[gi].dir.empty())
                row->append(*text("(" + fs::path(m_groups[gi].dir).filename().string() + ")", "dim"));
            auto e = Gtk::make_managed<Gtk::Entry>();
            e->set_name("organize.group." + std::to_string(gi));
            e->set_text(m_groups[gi].name);
            e->set_placeholder_text("Keene, Carolyn");
            e->set_hexpand(true);
            e->signal_changed().connect([this, gi, e] { m_groups[gi].name = e->get_text(); replan(); });
            row->append(*e);
            m_list->append(*row);
        }
        for (std::size_t pi = 0; pi < m_groups[gi].projects.size(); ++pi) {
            auto& p = m_groups[gi].projects[pi];
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            row->set_margin(3);
            row->set_margin_start(grp ? 24 : 4);
            auto on = Gtk::make_managed<Gtk::CheckButton>();
            on->set_name("organize.on." + std::to_string(gi) + "." + std::to_string(pi));
            on->set_active(p.on);
            on->signal_toggled().connect([this, gi, pi, on] { m_groups[gi].projects[pi].on = on->get_active(); replan(); });
            row->append(*on);
            auto name = text(fs::path(p.dir).filename().string());
            name->set_size_request(260, -1);
            row->append(*name);
            auto arrow = text("->", "dim");
            arrow->set_wrap(false);
            row->append(*arrow);
            auto e = Gtk::make_managed<Gtk::Entry>();
            e->set_name("organize.title." + std::to_string(gi) + "." + std::to_string(pi));
            e->set_text(p.title);
            e->set_hexpand(true);
            e->signal_changed().connect([this, gi, pi, e] { m_groups[gi].projects[pi].title = e->get_text(); replan(); });
            row->append(*e);
            m_list->append(*row);
        }
    }
    if (m_groups.empty())
        m_list->append(*text("Nothing here to organize as " + m_t->name + " -- no folders of that kind left in it.", "dim"));
    replan();
}

void OrganizeWindow::replan() {
    m_plan.reset();
    m_run->set_sensitive(false);
    if (!m_t || m_groups.empty()) { m_summary->set_text(""); return; }
    if (tp::place_depth(*m_t) == 2)
        for (const auto& g : m_groups)
            if (g.name.empty() && std::any_of(g.projects.begin(), g.projects.end(), [](const og::Project& p) { return p.on; })) {
                m_summary->set_text("Type the " + (m_t->group.empty() ? std::string("group") : m_t->group) +
                                    " -- the names below are guesses; edit any.");
                return;
            }
    auto pl = og::plan(m_home, *m_t, m_groups, m_lay->get_active());
    std::string s = n_of(pl.projects, "project", "projects");
    if (pl.moved) s += ", " + std::to_string(pl.moved) + " moved to ~/" + m_t->category;
    if (pl.in_place) s += ", " + std::to_string(pl.in_place) + " in place already";
    if (pl.laid_out) s += "; " + n_of(pl.laid_out, "file laid out", "files laid out");
    if (!pl.closet.empty()) s += "; loose files left in " + std::to_string(pl.closet.size()) + " folder" +
                                 (pl.closet.size() == 1 ? "" : "s") + " -> the Closet";
    if (!pl.trashed.empty()) s += "; " + std::to_string(pl.trashed.size()) + " left empty -> the Trash";
    if (!pl.refused.empty()) {
        s += " -- not " + std::to_string(pl.refused.size()) + ": ";
        for (std::size_t i = 0; i < pl.refused.size() && i < 3; ++i) s += (i ? "; " : "") + pl.refused[i];
        if (pl.refused.size() > 3) s += "; ...";
    }
    m_summary->set_text(s + ".");
    m_run->set_sensitive(!pl.plan.actions.empty() && !m_busy);
    m_plan = std::move(pl);
}

void OrganizeWindow::on_run() {
    if (!m_plan || m_busy) return;
    auto plan = m_plan->plan;
    plan.previewed = true;   // shown in this window
    const auto closet = m_plan->closet;
    m_busy = true;
    m_run->set_sensitive(false);
    const std::string data = m_data_dir;
    dr::Door* door = &m_door;
    busy::door(*this, m_door, std::move(plan), "Organize",
        [this, closet](const dr::Result& r) {
            m_busy = false;
            std::string extra;
            if (r.ok && !closet.empty() && m_save) {   // loose files left behind: the Closet sees them from now on
                const auto err = m_save(closet);
                extra = err.empty() ? " Loose files left -> the Closet." : " The Closet catch-all was not saved: " + err;
            }
            if (m_changed) m_changed();
            // Look again where things are now: a moved single project, at its new place.
            if (r.ok && m_shape == og::Shape::One && !m_groups.empty() && !m_groups[0].projects.empty() && m_t) {
                const auto to = og::target_of(m_home, *m_t, m_groups[0].name, m_groups[0].projects[0].title);
                if (fs::is_directory(to)) m_folder = to;
            }
            if (fs::is_directory(m_folder)) fill();
            else { m_groups.clear(); while (auto* rw = m_list->get_row_at_index(0)) m_list->remove(*rw); m_summary->set_text(""); }
            m_status->set_text(r.ok ? "Done: " + std::to_string(r.done) + " steps -- one undo in Activity." + extra
                                    : "Not all done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front()));
            if (auto lg = log::get(log::Area::Door)) lg->info("organize: {} done, {} failed, {} to the closet", r.done, r.failed, closet.size());
        },
        [data, door](const dr::Result& r) {   // the catalog follows the files it knows
            core::catalog::Catalog cat;
            if (cat.open(data, true))
                core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
        });
}

}  // namespace tidy
