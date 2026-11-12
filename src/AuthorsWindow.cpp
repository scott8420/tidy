#include "AuthorsWindow.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Catalog.hpp"
#include "core/Projects.hpp"
#include "core/Tagging.hpp"

#include <giomm/file.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>

#include <ctime>
#include <filesystem>

namespace tidy {

namespace dr = latch::door;
namespace ly = core::layout;
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
}  // namespace

AuthorsWindow::AuthorsWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir,
                             SaveCloset save, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_save(std::move(save)),
      m_changed(std::move(changed)) {
    set_name("authors.window");
    set_title("Bookbinding authors");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(820, 620);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Bookbinding authors", "heading"));
    box->append(*text("An author's folder: each folder in it is a book. They move WHOLE to "
                      "~/Documents/Bookbinding/<Author>/<Title>. Loose files left in the author's old folder go to "
                      "the Closet; an old folder left empty goes to the Trash. One undo step in Activity.", "dim"));

    auto row1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto pick = Gtk::make_managed<Gtk::Button>("Folder...");
    pick->set_name("authors.pick");
    pick->signal_clicked().connect([this] { on_pick(); });
    row1->append(*pick);
    m_where = text("No folder yet", "dim");
    m_where->set_name("authors.where");
    m_where->set_hexpand(true);
    row1->append(*m_where);
    m_each = Gtk::make_managed<Gtk::CheckButton>("Each sub-folder is an author");
    m_each->set_name("authors.each");
    m_each->signal_toggled().connect([this] { fill(); });
    row1->append(*m_each);
    box->append(*row1);
    m_lay = Gtk::make_managed<Gtk::CheckButton>(
        "Lay out each book too -- its files by the template's routes; what no route fits stays at its top for Layout...");
    m_lay->set_name("authors.lay");
    m_lay->set_active(true);
    m_lay->signal_toggled().connect([this] { replan(); });
    box->append(*m_lay);

    m_summary = text("");
    m_summary->set_name("authors.summary");
    box->append(*m_summary);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("authors.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    box->append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("authors.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("authors.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_run->set_name("authors.run");
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

AuthorsWindow::~AuthorsWindow() = default;

void AuthorsWindow::open(const std::string& folder, bool each) {
    m_t.reset();
    for (const auto& t : core::projects::known_templates(m_data_dir))
        if (tp::place_depth(t) == 2 && (!m_t || t.name == "bookbinding")) m_t = t;
    m_folder = folder;
    m_each->set_active(each);
    fill();
    m_status->set_text(m_t ? "To ~/" + m_t->category : "No template with an author level.");
    present();
    if (m_folder.empty()) on_pick();
}

void AuthorsWindow::on_pick() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("An author's folder, or a folder of authors");
    dlg->set_modal(true);
    dlg->set_initial_folder(Gio::File::create_for_path(m_folder.empty() ? m_home + "/Documents" : m_folder));
    dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            const auto f = dlg->select_folder_finish(res);
            if (!f || f->get_path().empty()) return;
            m_folder = f->get_path();
            fill();
        } catch (const Glib::Error&) {}   // cancelled
    });
}

void AuthorsWindow::fill() {
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    m_rows.clear();
    m_where->set_text(m_folder.empty() ? "No folder yet"
                      : "~" + (m_folder.rfind(m_home, 0) == 0 ? m_folder.substr(m_home.size()) : m_folder));
    if (m_folder.empty() || !m_t) { replan(); return; }
    for (const auto& d : ly::candidates(m_folder, m_each->get_active())) {
        Row r;
        r.a = {d, ly::author_name(fs::path(d).filename().string())};
        r.books = ly::candidates(d, true).size();
        m_rows.push_back(std::move(r));
    }
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        row->set_margin(4);
        auto on = Gtk::make_managed<Gtk::CheckButton>();
        on->set_name("authors.on." + std::to_string(i));
        on->set_active(m_rows[i].books > 0);
        m_rows[i].on = on->get_active();
        on->signal_toggled().connect([this, i, on] { m_rows[i].on = on->get_active(); replan(); });
        row->append(*on);
        auto left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        left->set_size_request(280, -1);
        left->append(*text(fs::path(m_rows[i].a.dir).filename().string()));
        left->append(*text(n_of(m_rows[i].books, "book", "books"), "dim"));
        row->append(*left);
        auto e = Gtk::make_managed<Gtk::Entry>();
        e->set_name("authors.name." + std::to_string(i));
        e->set_text(m_rows[i].a.name);
        e->set_hexpand(true);
        e->set_tooltip_text("The author as the shelf will name the folder: Last, First");
        e->signal_changed().connect([this, i, e] { m_rows[i].a.name = e->get_text(); replan(); });
        row->append(*e);
        m_rows[i].name = e;
        m_list->append(*row);
    }
    if (m_rows.empty()) m_list->append(*text(m_each->get_active() ? "No sub-folders here." : "Pick a folder.", "dim"));
    replan();
}

void AuthorsWindow::replan() {
    m_plan.reset();
    m_run->set_sensitive(false);
    if (!m_t || m_rows.empty()) { m_summary->set_text(""); return; }
    std::vector<ly::Author> as;
    for (auto& r : m_rows) {
        const auto why = ly::author_refusal(m_home, *m_t, r.a);
        if (r.name) {
            r.name->set_tooltip_text(why.empty() ? "~/" + m_t->category + "/" + r.a.name : "Not this one: " + why);
            if (why.empty() || !r.on) r.name->remove_css_class("error");
            else r.name->add_css_class("error");
        }
        if (r.on) as.push_back(r.a);
    }
    if (as.empty()) { m_summary->set_text("Tick an author."); return; }
    auto pa = ly::plan_authors(m_home, *m_t, as, m_lay->get_active());
    std::string s = n_of(pa.authors, "author", "authors") + ", " + n_of(pa.books, "book", "books") + " to ~/" + m_t->category;
    if (pa.laid_out) s += "; " + n_of(pa.laid_out, "file laid out", "files laid out");
    if (!pa.closet.empty()) s += "; " + std::to_string(pa.closet.size()) + " old folder" + (pa.closet.size() == 1 ? "" : "s") +
                                 " with loose files -> the Closet";
    if (!pa.trashed.empty()) s += "; " + std::to_string(pa.trashed.size()) + " left empty -> the Trash";
    if (!pa.refused.empty()) {
        s += " -- not " + std::to_string(pa.refused.size()) + ": ";
        for (std::size_t i = 0; i < pa.refused.size() && i < 3; ++i) s += (i ? "; " : "") + pa.refused[i];
        if (pa.refused.size() > 3) s += "; ...";
    }
    m_summary->set_text(s + ".");
    m_run->set_sensitive(pa.books > 0 && !m_busy);
    m_plan = std::move(pa);
}

void AuthorsWindow::on_run() {
    if (!m_plan || m_busy) return;
    auto plan = m_plan->plan;
    plan.previewed = true;   // shown in this window
    const auto closet = m_plan->closet;
    m_busy = true;
    m_run->set_sensitive(false);
    const std::string data = m_data_dir;
    dr::Door* door = &m_door;
    busy::door(*this, m_door, std::move(plan), "Bookbinding authors",
        [this, closet](const dr::Result& r) {
            m_busy = false;
            std::string extra;
            if (r.ok && !closet.empty() && m_save) {   // the loose files left behind: the Closet sees them from now on
                const auto err = m_save(closet);
                extra = err.empty() ? " " + std::to_string(closet.size()) + " old folder" + (closet.size() == 1 ? "" : "s") +
                                          " -> the Closet (catch-all in Settings)."
                                    : " The Closet catch-all was not saved: " + err;
            }
            if (m_changed) m_changed();
            fill();
            m_status->set_text(r.ok ? "Done: " + std::to_string(r.done) + " steps -- one undo in Activity." + extra
                                    : "Not all done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front()));
            if (auto lg = log::get(log::Area::Door)) lg->info("authors: {} done, {} failed, {} to the closet", r.done, r.failed, closet.size());
        },
        [data, door](const dr::Result& r) {   // the catalog follows the files it knows
            core::catalog::Catalog cat;
            if (cat.open(data, true))
                core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
        });
}

}  // namespace tidy
