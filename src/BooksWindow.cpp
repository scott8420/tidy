#include "BooksWindow.hpp"
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
}  // namespace

BooksWindow::BooksWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_changed(std::move(changed)) {
    set_name("books.window");
    set_title("Make books");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(820, 620);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Make books", "heading"));
    box->append(*text("Each folder moves WHOLE to ~/Documents/Bookbinding/<Author>/<Title> -- from then on it is a book: "
                      "kept whole, and its row on the Projects page has Layout... to put its files in the stages. "
                      "Nothing inside is touched here. One undo step in Activity.", "dim"));

    auto row1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto pick = Gtk::make_managed<Gtk::Button>("Folder...");
    pick->set_name("books.pick");
    pick->signal_clicked().connect([this] { on_pick(); });
    row1->append(*pick);
    m_where = text("No folder yet", "dim");
    m_where->set_name("books.where");
    m_where->set_hexpand(true);
    row1->append(*m_where);
    m_each = Gtk::make_managed<Gtk::CheckButton>("Each sub-folder is a book (a series)");
    m_each->set_name("books.each");
    m_each->signal_toggled().connect([this] { fill(); });
    row1->append(*m_each);
    box->append(*row1);

    auto row2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row2->append(*text("Author (Last, First)"));
    m_author = Gtk::make_managed<Gtk::Entry>();
    m_author->set_name("books.author");
    m_author->set_placeholder_text("Keene, Carolyn");
    m_author->set_hexpand(true);
    m_author->signal_changed().connect([this] { replan(); });
    row2->append(*m_author);
    box->append(*row2);

    m_summary = text("");
    m_summary->set_name("books.summary");
    box->append(*m_summary);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("books.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    box->append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("books.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("books.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_run->set_name("books.run");
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

BooksWindow::~BooksWindow() = default;

void BooksWindow::open(const std::string& folder, bool each) {
    m_t.reset();
    for (const auto& t : core::projects::known_templates(m_data_dir))   // the bookbinding (yours, or built in)
        if (tp::place_depth(t) > 0 && (!m_t || t.name == "bookbinding")) m_t = t;
    m_folder = folder;
    m_author->set_text("");     // guessed again from the new folder
    m_each->set_active(each);
    fill();
    m_status->set_text(m_t ? "To ~/" + m_t->category : "No template known by its place.");
    present();
    if (m_folder.empty()) on_pick();
    else m_author->grab_focus();
}

void BooksWindow::on_pick() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("A book's folder, or a folder of books (a series)");
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

void BooksWindow::fill() {
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    m_books.clear();
    m_titles.clear();
    m_where->set_text(m_folder.empty() ? "No folder yet"
                      : "~" + (m_folder.rfind(m_home, 0) == 0 ? m_folder.substr(m_home.size()) : m_folder));
    if (m_folder.empty() || !m_t) { replan(); return; }
    const bool each = m_each->get_active();
    for (const auto& d : ly::candidates(m_folder, each))
        m_books.push_back({d, ly::guess_title(fs::path(d).filename().string())});
    // The author: a "Last, First" parent of the books (the series' own name is no author).
    if (std::string(m_author->get_text()).empty() && !m_books.empty())
        m_author->set_text(ly::guess_author(m_books.front().dir));
    for (std::size_t i = 0; i < m_books.size(); ++i) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        row->set_margin(4);
        auto left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        left->set_size_request(300, -1);
        left->append(*text(fs::path(m_books[i].dir).filename().string()));
        row->append(*left);
        auto arrow = text("->", "dim");
        arrow->set_wrap(false);
        row->append(*arrow);
        auto e = Gtk::make_managed<Gtk::Entry>();
        e->set_name("books.title." + std::to_string(i));
        e->set_text(m_books[i].title);
        e->set_hexpand(true);
        e->signal_changed().connect([this, i, e] { m_books[i].title = e->get_text(); replan(); });
        row->append(*e);
        m_titles.push_back(e);
        m_list->append(*row);
    }
    if (m_books.empty()) m_list->append(*text(each ? "No sub-folders here." : "Pick a folder.", "dim"));
    replan();
}

void BooksWindow::replan() {
    m_made.reset();
    m_run->set_sensitive(false);
    if (!m_t || m_books.empty()) { m_summary->set_text(""); return; }
    const std::string author = m_author->get_text();
    if (author.empty() && tp::place_depth(*m_t) == 2) {   // nothing to judge yet: ask, don't paint every row red
        for (auto* e : m_titles) { e->remove_css_class("error"); e->set_tooltip_text(""); }
        m_summary->set_text(std::to_string(m_books.size()) + (m_books.size() == 1 ? " folder" : " folders") +
                            " -- type the author (Last, First); the titles below are guesses, edit any.");
        return;
    }
    auto made = ly::make_books(m_home, *m_t, author, m_books);
    // Each row says why it can't be made (the title's tooltip), or where it goes.
    for (std::size_t i = 0; i < m_books.size() && i < m_titles.size(); ++i) {
        const auto why = ly::book_refusal(m_home, *m_t, author, m_books[i]);
        m_titles[i]->set_tooltip_text(why.empty() ? "~/" + m_t->category + "/" + author + "/" + m_books[i].title : "Not this one: " + why);
        if (why.empty()) m_titles[i]->remove_css_class("error");
        else m_titles[i]->add_css_class("error");
    }
    std::string s = std::to_string(made.books) + (made.books == 1 ? " book" : " books") + " to ~/" + m_t->category +
                    (author.empty() ? "" : "/" + author);
    if (!made.refused.empty()) {
        s += " -- not " + std::to_string(made.refused.size()) + ": ";
        for (std::size_t i = 0; i < made.refused.size() && i < 3; ++i) s += (i ? "; " : "") + made.refused[i];
        if (made.refused.size() > 3) s += "; ...";
    }
    m_summary->set_text(s + ".");
    m_run->set_sensitive(made.books > 0 && !m_busy);
    m_made = std::move(made);
}

void BooksWindow::on_run() {
    if (!m_made || m_busy) return;
    auto plan = m_made->plan;
    plan.previewed = true;   // shown in this window
    m_busy = true;
    m_run->set_sensitive(false);
    const std::string data = m_data_dir;
    dr::Door* door = &m_door;
    busy::door(*this, m_door, std::move(plan), "Make books",
        [this](const dr::Result& r) {
            m_busy = false;
            if (m_changed) m_changed();
            fill();
            m_status->set_text(r.ok ? "Done: " + std::to_string(r.done) + " steps -- Projects lists them; each has Layout... "
                                      "Undo in Activity."
                                    : "Not all done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front()));
            if (auto lg = log::get(log::Area::Door)) lg->info("make books: {} done, {} failed", r.done, r.failed);
        },
        [data, door](const dr::Result& r) {   // the catalog follows the files it knows
            core::catalog::Catalog cat;
            if (cat.open(data, true))
                core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
        });
}

}  // namespace tidy
