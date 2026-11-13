#include "EbooksWindow.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Catalog.hpp"
#include "core/Projects.hpp"
#include "core/Tagging.hpp"
#include "core/Templates.hpp"

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
#include <gtkmm/stack.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <memory>
#include <set>

namespace fs = std::filesystem;

namespace tidy {

namespace eb = core::ebooks;
namespace dr = latch::door;

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr, float x = 0.0f) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(x);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
Gtk::Entry* entry(const std::string& v, const std::string& hint, int chars) {
    auto e = Gtk::make_managed<Gtk::Entry>();
    e->set_text(v);
    e->set_placeholder_text(hint);
    e->set_width_chars(chars);
    return e;
}
}  // namespace

EbooksWindow::EbooksWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir, Changed changed)
    : m_parent(parent), m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_changed(std::move(changed)) {
    set_name("ebooks.window");
    set_title("Ebooks from files");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(1000, 640);

    m_states = Gtk::make_managed<Gtk::Stack>();

    // ── rows ────────────────────────────────────────────────────────────────
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Ebooks from files", "heading"));
    box->append(*text("Each loose ebook gets its own title folder on a shelf: ~/Entertainment/EBooks/<Genre>/<Last, First>/"
                      "<Title>/. Tidy makes the shelf's type itself (ebook-fiction ...) and saves it in Types, so the "
                      "next books go the same way. Author and title come from inside the file (EPUB, PDF) or its name -- "
                      "check each guess; a PDF starts unticked (it may not be a book).", "dim"));
    auto top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto pick = Gtk::make_managed<Gtk::Button>("Folder…");
    pick->set_name("ebooks.pick");
    pick->signal_clicked().connect([this] { on_pick(); });
    top->append(*pick);
    m_where = text("(no folder yet)");
    m_where->set_name("ebooks.where");
    m_where->set_hexpand(true);
    top->append(*m_where);
    top->append(*text("Genre"));
    m_genre = entry("Fiction", "Fiction", 14);
    m_genre->set_name("ebooks.genre");
    top->append(*m_genre);
    auto all = Gtk::make_managed<Gtk::Button>("For all");
    all->set_name("ebooks.genre.all");
    all->set_tooltip_text("Set every row's genre to this one");
    all->signal_clicked().connect([this] {
        for (auto& l : m_lines) l.genre->set_text(m_genre->get_text());
        update();
    });
    top->append(*all);
    box->append(*top);

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("ebooks.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    box->append(*scroll);
    m_summary = text("");
    m_summary->set_name("ebooks.summary");
    box->append(*m_summary);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("ebooks.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_go = Gtk::make_managed<Gtk::Button>("Preview…");
    m_go->set_name("ebooks.preview");
    m_go->add_css_class("suggested-action");
    m_go->set_sensitive(false);
    m_go->signal_clicked().connect([this] { preview(); });
    bar->append(*m_go);
    box->append(*bar);
    m_states->add(*box, "rows");

    // ── preview ─────────────────────────────────────────────────────────────
    auto pbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    pbox->set_margin(20);
    pbox->append(*text("Preview -- nothing has moved yet", "heading"));
    m_prev_summary = text("");
    m_prev_summary->set_name("ebooks.preview.summary");
    pbox->append(*m_prev_summary);
    auto pscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    pscroll->set_vexpand(true);
    pscroll->set_has_frame(true);
    m_prev = Gtk::make_managed<Gtk::ListBox>();
    m_prev->set_name("ebooks.preview.list");
    m_prev->set_selection_mode(Gtk::SelectionMode::NONE);
    pscroll->set_child(*m_prev);
    pbox->append(*pscroll);
    auto pbar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto spacer = text("");
    spacer->set_hexpand(true);
    pbar->append(*spacer);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("ebooks.preview.back");
    back->signal_clicked().connect([this] { m_plan.reset(); m_states->set_visible_child("rows"); m_status->set_text("Nothing done."); });
    pbar->append(*back);
    m_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_run->set_name("ebooks.preview.run");
    m_run->add_css_class("suggested-action");
    m_run->signal_clicked().connect([this] { run(); });
    pbar->append(*m_run);
    pbox->append(*pbar);
    m_states->add(*pbox, "preview");

    set_child(*m_states);
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType) {
        if (key != GDK_KEY_Escape) return false;
        set_visible(false);
        return true;
    }, false);
    add_controller(keys);
}

EbooksWindow::~EbooksWindow() = default;

void EbooksWindow::open(const std::string& folder) {
    present();
    m_states->set_visible_child("rows");
    m_plan.reset();
    if (folder.empty()) { if (m_folder.empty()) on_pick(); return; }
    m_folder = folder;
    look();
}

void EbooksWindow::on_pick() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("A folder with loose ebooks");
    dlg->set_modal(true);
    dlg->set_initial_folder(Gio::File::create_for_path(m_folder.empty() ? m_home : m_folder));
    dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            const auto f = dlg->select_folder_finish(res);
            if (!f || f->get_path().empty()) return;
            const std::string p = f->get_path();
            if (p != m_home && p.rfind(m_home + "/", 0) != 0) { m_status->set_text("Only folders in your home."); return; }
            m_folder = p;
            look();
        } catch (const Glib::Error&) {}
    });
}

void EbooksWindow::look() {
    m_where->set_text(m_folder == m_home ? "~" : "~/" + m_folder.substr(m_home.size() + 1));
    const std::string home = m_home, dir = m_folder, genre = m_genre->get_text();
    auto out = std::make_shared<std::vector<eb::Row>>();
    busy::start(*this, "Ebooks -- looking", "Reading each ebook's own title and author (nothing moves)",
                [out, home, dir, genre] { *out = eb::find(home, dir, genre.empty() ? "Fiction" : genre); },
                [this, out] {
                    m_lines.clear();
                    for (auto& r : *out) m_lines.push_back({std::move(r)});
                    show();
                    if (auto lg = log::get(log::Area::Shell)) lg->info("ebooks: {} found under {}", m_lines.size(), m_folder);
                });
}

void EbooksWindow::show() {
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    for (auto& l : m_lines) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        row->set_margin(6);
        auto line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        l.pick = Gtk::make_managed<Gtk::CheckButton>();
        l.pick->set_active(l.row.pick);
        l.pick->signal_toggled().connect([this] { update(); });
        line->append(*l.pick);
        auto name = text(fs::path(l.row.path).filename().string());
        name->set_hexpand(true);
        name->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        name->set_wrap(false);
        name->set_tooltip_text(l.row.path + "\nguessed from " + (l.row.from.empty() ? "nothing" : l.row.from));
        line->append(*name);
        l.genre = entry(l.row.genre, "Genre", 10);
        l.author = entry(l.row.author, "Last, First", 20);
        l.title = entry(l.row.title, "Title", 28);
        for (auto* e : {l.genre, l.author, l.title}) {
            e->signal_changed().connect([this] { update(); });
            line->append(*e);
        }
        row->append(*line);
        l.to = text("", "dim");
        l.to->set_margin_start(30);
        row->append(*l.to);
        m_list->append(*row);
    }
    if (m_lines.empty()) {
        auto none = text("No loose ebooks here (books already on a shelf, inside projects or Hands off are left out).", "dim");
        none->set_margin(10);
        m_list->append(*none);
    }
    update();
    m_status->set_text("Check the guesses -- then Preview.");
}

void EbooksWindow::read_rows() {
    for (auto& l : m_lines) {
        l.row.pick = l.pick->get_active();
        l.row.genre = l.genre->get_text();
        l.row.author = l.author->get_text();
        l.row.title = l.title->get_text();
    }
}

std::vector<std::string> EbooksWindow::new_types() const {
    std::set<std::string> saved;
    for (const auto& t : core::templates::list(m_data_dir)) saved.insert(t.name);
    std::set<std::string> want;
    for (const auto& l : m_lines)
        if (l.row.pick && eb::row_problem(l.row).empty() && !saved.count(eb::type_name(l.row.genre))) want.insert(l.row.genre);
    std::vector<std::string> out;
    std::set<std::string> names;
    for (const auto& g : want)
        if (names.insert(eb::type_name(g)).second) out.push_back(g);
    return out;
}

void EbooksWindow::update() {
    read_rows();
    std::size_t picked = 0, ready = 0;
    for (auto& l : m_lines) {
        const std::string why = eb::row_problem(l.row);
        if (l.row.pick) ++picked;
        if (l.row.pick && why.empty()) ++ready;
        l.to->set_text(!l.row.pick ? "left where it is"
                       : why.empty() ? "-> ~/" + eb::target_of(l.row) + "/   (" + eb::type_name(l.row.genre) + ")"
                                     : "can't go yet: " + why);
    }
    std::string types;
    for (const auto& g : new_types()) types += (types.empty() ? "" : ", ") + eb::type_name(g);
    m_summary->set_text(std::to_string(m_lines.size()) + " ebooks found, " + std::to_string(picked) + " ticked, " +
                        std::to_string(ready) + " ready to go" +
                        (types.empty() ? "." : ". New types Tidy will make and save: " + types + "."));
    m_go->set_sensitive(ready > 0);
}

void EbooksWindow::preview() {
    read_rows();
    std::vector<eb::Row> rows;
    for (const auto& l : m_lines) rows.push_back(l.row);
    auto planned = eb::plan(m_home, rows);
    auto plan = std::move(planned.plan);
    plan.label = "Ebooks from files";
    while (auto* r = m_prev->get_row_at_index(0)) m_prev->remove(*r);
    const auto rel = [&](const std::string& a) { return a.rfind(m_home + "/", 0) == 0 ? "~/" + a.substr(m_home.size() + 1) : a; };
    std::vector<std::string> made;
    std::size_t moves = 0, refused = 0;
    for (const auto& g : new_types()) {
        const auto t = eb::type_for(g);
        auto l = text("save the type " + t.name + "   (~/" + t.category + "/<" + t.group + ">/<Title>) -- in Types for next time");
        l->set_margin(4);
        m_prev->append(*l);
    }
    for (const auto& a : plan.actions) {
        const std::string why = m_door.refusal(a, made);
        std::string line;
        if (a.verb == dr::Verb::MakeDir) { made.push_back(a.to); line = "make " + rel(a.to); }
        else if (a.verb == dr::Verb::Move) { ++moves; line = "move " + rel(a.from) + "\n    -> " + rel(a.to); }
        else continue;
        if (!why.empty()) { ++refused; line += "\n    REFUSED: " + why; }
        auto l = text(line);
        l->set_margin(4);
        m_prev->append(*l);
    }
    for (const auto& r : planned.renamed) m_prev->append(*text("  taken: " + r, "dim"));
    m_prev_summary->set_text(std::to_string(moves) + " books to their title folders" +
                             (refused ? ", " + std::to_string(refused) + " refused (they stay)" : "") +
                             ". One undo step in Activity.");
    m_run->set_sensitive(moves > refused);
    plan.previewed = true;
    m_plan = std::move(plan);
    m_states->set_visible_child("preview");
}

void EbooksWindow::run() {
    if (!m_plan) return;
    // The types first (Tidy's own files -- design, not a move: kept after an Undo).
    std::string err;
    for (const auto& g : new_types())
        if (const auto w = core::templates::save(m_data_dir, eb::type_for(g)); !w.empty()) err = w;
    if (!err.empty()) { m_prev_summary->set_text("The type was not saved: " + err + " -- nothing moved."); return; }
    core::projects::apply_places(m_data_dir, m_home);   // the new shelves are known now
    auto plan = std::move(*m_plan);
    m_plan.reset();
    const std::string data = m_data_dir;
    auto* door = &m_door;
    busy::door(*this, m_door, std::move(plan), "Ebooks from files",
        [this](const dr::Result& r) {
            if (m_changed) m_changed();
            m_states->set_visible_child("rows");
            m_status->set_text(r.ok ? "Done: " + std::to_string(r.done) + " steps -- one undo in Activity."
                                    : "Not all done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front()));
            if (auto lg = log::get(log::Area::Door)) lg->info("ebooks: {} done, {} failed", r.done, r.failed);
            if (fs::is_directory(m_folder)) look();
        },
        [data, door](const dr::Result& r) {   // the catalog follows the files it knows
            core::catalog::Catalog cat;
            if (cat.open(data, true))
                core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
        });
}

}  // namespace tidy
