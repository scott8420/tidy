#include "LayoutWindow.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Catalog.hpp"
#include "core/Tagging.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>

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
std::string readable(std::string s) {   // A[0-9][0-9][0-9][0-9].png -> A####.png
    for (std::size_t at; (at = s.find("[0-9]")) != std::string::npos;) s.replace(at, 5, "#");
    return s;
}
std::string dir_of(const std::string& rel) {
    const auto slash = rel.rfind('/');
    return slash == std::string::npos ? "" : rel.substr(0, slash);
}
}  // namespace

LayoutWindow::LayoutWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_changed(std::move(changed)) {
    set_name("layout.window");
    set_title("Put in the layout");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(860, 680);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    m_title = text("Put in the layout", "heading");
    m_title->set_name("layout.title");
    box->append(*m_title);
    box->append(*text("Each file goes where the template's routes say -- change any row's folder. What no route fits "
                      "is asked first and stays until you pick. Bundles move whole. The stage folders the book lacks "
                      "are made; folders the moves leave empty go to the Trash. One undo step in Activity.", "dim"));
    m_summary = text("");
    m_summary->set_name("layout.summary");
    box->append(*m_summary);
    m_extra = text("", "dim");
    m_extra->set_name("layout.extra");
    box->append(*m_extra);

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("layout.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    box->append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("layout.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("layout.look");
    again->signal_clicked().connect([this] { on_look(); });
    bar->append(*again);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("layout.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_run->set_name("layout.run");
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

LayoutWindow::~LayoutWindow() = default;

void LayoutWindow::open(const std::string& book, const tp::Template& t) {
    m_book = book;
    m_t = t;
    const std::string shown = book.rfind(m_home + "/", 0) == 0 ? "~" + book.substr(m_home.size()) : book;
    m_title->set_text("Put in the layout -- " + fs::path(book).filename().string());
    m_title->set_tooltip_text(shown + "  (" + t.name + ")");
    m_status->set_text(shown);
    present();
    on_look();
}

void LayoutWindow::on_look() {
    if (m_busy || m_book.empty()) return;
    m_plan.reset();
    m_run->set_sensitive(false);
    const auto t = m_t;
    const std::string book = m_book;
    auto out = std::make_shared<ly::Look>();
    m_busy = true;
    busy::start(*this, "Layout -- looking", "Reading every file in the book (nothing is changed)",
        [out, t, book] { *out = ly::look(t, book); },
        [this, out] {
            m_busy = false;
            m_look = std::move(*out);
            show();
        });
}

void LayoutWindow::show() {
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    // Groups: each asked file alone; proposals folded by (its folder, the route).
    m_groups.clear();
    std::map<std::pair<std::string, std::string>, std::size_t> at;
    for (std::size_t i = 0; i < m_look.rows.size(); ++i) {
        const auto& r = m_look.rows[i];
        if (r.asked) { m_groups.push_back({{i}, r.rel, "", true}); continue; }
        const auto key = std::make_pair(dir_of(r.rel), r.why);
        if (auto it = at.find(key); it != at.end()) { m_groups[it->second].rows.push_back(i); continue; }
        at[key] = m_groups.size();
        m_groups.push_back({{i}, r.rel, r.why, false});
    }
    for (auto& g : m_groups)
        if (g.rows.size() > 1) {
            const auto& a = m_look.rows[g.rows.front()].rel;
            const auto& b = fs::path(m_look.rows[g.rows.back()].rel).filename().string();
            g.label = a + "  ..  " + b + "  (" + std::to_string(g.rows.size()) + " files)";
        }
    std::stable_sort(m_groups.begin(), m_groups.end(), [](const Group& x, const Group& y) { return x.asked > y.asked; });

    // The choices: leave it, then every template folder (tree order).
    std::vector<std::string> folders = {ly::kLeave};
    for (const auto& f : m_t.folders) folders.push_back(f.path);
    auto model = [&] {
        auto m = Gtk::StringList::create({});
        m->append("(leave it where it is)");
        for (std::size_t k = 1; k < folders.size(); ++k) m->append(folders[k] + "/");
        return m;
    };

    bool asked_head = false, prop_head = false;
    for (std::size_t gi = 0; gi < m_groups.size(); ++gi) {
        const auto& g = m_groups[gi];
        if (g.asked && !asked_head) {
            m_list->append(*text("Where do these go? No route fits -- each stays until you pick", "heading"));
            asked_head = true;
        }
        if (!g.asked && !prop_head) {
            m_list->append(*text("Proposed -- by the template's routes; change any", "heading"));
            prop_head = true;
        }
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        row->set_margin(3);
        row->set_margin_start(14);
        auto left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        left->set_hexpand(true);
        left->append(*text(g.label));
        if (!g.why.empty()) left->append(*text(readable(g.why), "dim"));
        row->append(*left);
        auto dd = Gtk::make_managed<Gtk::DropDown>(model());
        dd->set_name("layout.folder." + std::to_string(gi));
        const std::string cur = m_look.rows[g.rows.front()].folder;
        const auto pos = std::find(folders.begin(), folders.end(), cur);
        dd->set_selected(pos == folders.end() ? 0 : static_cast<guint>(pos - folders.begin()));
        dd->property_selected().signal_changed().connect([this, gi, dd, folders] {
            const auto s = dd->get_selected();
            if (s >= folders.size()) return;
            for (auto i : m_groups[gi].rows) m_look.rows[i].folder = folders[s];
            replan();
        });
        row->append(*dd);
        m_list->append(*row);
    }
    if (m_groups.empty()) m_list->append(*text("Nothing to move -- every file is in its place.", "dim"));
    replan();
}

void LayoutWindow::replan() {
    auto pl = ly::plan(m_home, m_t, m_look);
    std::size_t leave = 0;
    for (const auto& r : m_look.rows) leave += r.folder == ly::kLeave;
    std::string s = n_of(pl.moves, "move", "moves") + ", " + std::to_string(m_look.in_place) + " in place";
    if (leave) s += ", " + std::to_string(leave) + " left where they are";
    if (m_look.whole) s += ", " + std::to_string(m_look.whole) + " kept whole inside";
    if (!pl.renamed.empty()) s += "; " + std::to_string(pl.renamed.size()) + " names taken -> \"(2)\"";
    m_summary->set_text(s + ".");
    std::string e;
    if (pl.made) e += "New folders (" + std::to_string(pl.made) + "): the stages the book lacks.";
    if (!pl.emptied.empty()) {
        e += std::string(e.empty() ? "" : "  ") + "To the Trash, left empty: ";
        for (std::size_t i = 0; i < pl.emptied.size(); ++i) e += (i ? ", " : "") + pl.emptied[i] + "/";
        e += ".";
    }
    m_extra->set_text(e);
    m_run->set_sensitive(!pl.plan.actions.empty() && !m_busy);
    if (auto lg = log::get(log::Area::Io))
        lg->info("layout plan: {} moves, {} made, {} emptied, {} left", pl.moves, pl.made, pl.emptied.size(), leave);
    m_plan = std::move(pl);
}

void LayoutWindow::on_run() {
    if (!m_plan || m_busy) return;
    auto plan = m_plan->plan;
    plan.previewed = true;   // shown in this window
    m_busy = true;
    m_run->set_sensitive(false);
    const std::string data = m_data_dir;
    dr::Door* door = &m_door;
    busy::door(*this, m_door, std::move(plan), "Layout",
        [this](const dr::Result& r) {
            m_busy = false;
            if (m_changed) m_changed();
            on_look();
            m_status->set_text(r.ok ? "Done: " + std::to_string(r.done) + " steps -- one step to undo in Activity."
                                    : "Not all done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front()));
            if (auto lg = log::get(log::Area::Door)) lg->info("layout run: {} done, {} failed", r.done, r.failed);
        },
        [data, door](const dr::Result& r) {   // the catalog follows the files it knows
            core::catalog::Catalog cat;
            if (cat.open(data, true))
                core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
        });
}

}  // namespace tidy
