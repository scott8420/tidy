#include "ShapeWindow.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Conform.hpp"

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
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>

#include <algorithm>
#include <filesystem>
#include <memory>

namespace tidy {

namespace sh = core::shape;
namespace tp = core::templates;
namespace cf = core::conform;

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
std::string commas(std::size_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}
std::string n_of(std::size_t n, const char* one, const char* many) { return commas(n) + " " + (n == 1 ? one : many); }
std::string choice_title(const std::string& c) {
    if (c.empty()) return "(leave it where it is)";
    if (c == tp::kTop) return "(the project's top)";
    return c + "/";
}
std::string folder_title(const std::string& f) { return f.empty() ? std::string("the top") : f + "/"; }
}  // namespace

ShapeWindow::ShapeWindow(Gtk::Window& parent, std::string home, std::string data_dir, Saved saved)
    : m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_saved(std::move(saved)) {
    set_name("shape.window");
    set_title("Template from a scan");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(1040, 720);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Template from a scan", "heading"));
    box->append(*text("Scan a project folder, or a folder of many (your books in ~/Documents/EBooks). Every file type "
                      "found is listed -- a numbered run (A0001.png ..) on a line of its own. Keep or add the folders, "
                      "give each type a folder, tick the types that build the others (they stay at the project's top), "
                      "name it and say where projects of this kind live. Nothing is moved here; Save and conform shows "
                      "Conform's preview first.", "dim"));

    auto row1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    auto pick = Gtk::make_managed<Gtk::Button>("Folder...");
    pick->set_name("shape.pick");
    pick->signal_clicked().connect([this] { on_pick(); });
    row1->append(*pick);
    m_where = text("No folder yet", "dim");
    m_where->set_name("shape.where");
    m_where->set_hexpand(true);
    row1->append(*m_where);
    m_each = Gtk::make_managed<Gtk::CheckButton>("Each sub-folder is a project");
    m_each->set_name("shape.each");
    m_each->set_active(true);
    m_each->signal_toggled().connect([this] { if (!m_folder.empty()) on_scan(); });
    row1->append(*m_each);
    auto scan = Gtk::make_managed<Gtk::Button>("Scan");
    scan->set_name("shape.scan");
    scan->signal_clicked().connect([this] { on_scan(); });
    row1->append(*scan);
    box->append(*row1);

    m_summary = text("");
    m_summary->set_name("shape.summary");
    box->append(*m_summary);

    auto paned = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);
    paned->set_vexpand(true);
    paned->set_wide_handle(true);
    // Left: the file types.
    auto left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    left->append(*text("File types -- where each goes", "heading"));
    left->append(*text("Source = it builds the others (an .optik, a .quire): it stays at the project's top.", "dim"));
    auto ls = Gtk::make_managed<Gtk::ScrolledWindow>();
    ls->set_vexpand(true);
    ls->set_has_frame(true);
    m_types = Gtk::make_managed<Gtk::ListBox>();
    m_types->set_name("shape.types");
    m_types->set_selection_mode(Gtk::SelectionMode::NONE);
    ls->set_child(*m_types);
    left->append(*ls);
    paned->set_start_child(*left);
    paned->set_resize_start_child(true);
    // Right: the folders.
    auto right = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    right->set_margin_start(8);
    right->append(*text("Folders of the template", "heading"));
    right->append(*text("Ticked = in the template (two deep at most). Found in N of the scanned folders.", "dim"));
    auto rs = Gtk::make_managed<Gtk::ScrolledWindow>();
    rs->set_vexpand(true);
    rs->set_has_frame(true);
    m_folders = Gtk::make_managed<Gtk::ListBox>();
    m_folders->set_name("shape.folders");
    m_folders->set_selection_mode(Gtk::SelectionMode::NONE);
    rs->set_child(*m_folders);
    right->append(*rs);
    auto addrow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    m_new_folder = Gtk::make_managed<Gtk::Entry>();
    m_new_folder->set_name("shape.new_folder");
    m_new_folder->set_placeholder_text("New folder (Originals, Workups/Images)");
    m_new_folder->set_hexpand(true);
    m_new_folder->signal_activate().connect([this] { on_add_folder(); });
    addrow->append(*m_new_folder);
    auto add = Gtk::make_managed<Gtk::Button>("Add");
    add->set_name("shape.add_folder");
    add->signal_clicked().connect([this] { on_add_folder(); });
    addrow->append(*add);
    right->append(*addrow);
    paned->set_end_child(*right);
    paned->set_resize_end_child(false);
    paned->set_position(660);
    box->append(*paned);

    auto names = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    names->append(*text("Name"));
    m_name = Gtk::make_managed<Gtk::Entry>();
    m_name->set_name("shape.name");
    m_name->set_placeholder_text("book-project");
    m_name->set_hexpand(true);
    names->append(*m_name);
    auto lives = text("Projects of this kind live in  ~/");
    lives->set_wrap(false);
    names->append(*lives);
    m_category = Gtk::make_managed<Gtk::Entry>();
    m_category->set_name("shape.category");
    m_category->set_placeholder_text("Entertainment/EBooks");
    m_category->set_hexpand(true);
    names->append(*m_category);
    box->append(*names);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("shape.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("shape.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_save = Gtk::make_managed<Gtk::Button>("Save template");
    m_save->set_name("shape.save");
    m_save->set_sensitive(false);
    m_save->signal_clicked().connect([this] { on_save(false); });
    bar->append(*m_save);
    m_save_conform = Gtk::make_managed<Gtk::Button>("Save and conform...");
    m_save_conform->set_name("shape.save_conform");
    m_save_conform->add_css_class("suggested-action");
    m_save_conform->set_tooltip_text("Save, then Conform's preview of the scanned folders -- nothing moves until its Do it");
    m_save_conform->set_sensitive(false);
    m_save_conform->signal_clicked().connect([this] { on_save(true); });
    bar->append(*m_save_conform);
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

ShapeWindow::~ShapeWindow() = default;

void ShapeWindow::open(const std::string& folder) {
    m_status->set_text("");
    present();
    if (!folder.empty()) {
        m_folder = folder;
        m_where->set_text("~" + (m_folder.rfind(m_home, 0) == 0 ? m_folder.substr(m_home.size()) : m_folder));
        on_scan();
    }
}

void ShapeWindow::on_pick() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("A project folder, or a folder of many (e.g. ~/Documents/EBooks)");
    dlg->set_modal(true);
    dlg->set_initial_folder(Gio::File::create_for_path(m_folder.empty() ? m_home : m_folder));
    dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            const auto f = dlg->select_folder_finish(res);
            if (!f || f->get_path().empty()) return;
            m_folder = f->get_path();
            m_where->set_text("~" + (m_folder.rfind(m_home, 0) == 0 ? m_folder.substr(m_home.size()) : m_folder));
            on_scan();
        } catch (const Glib::Error&) {}   // cancelled
    });
}

void ShapeWindow::on_scan() {
    if (m_busy) return;
    if (m_folder.empty()) { m_status->set_text("Pick a folder first."); return; }
    if (const auto why = cf::refusal(m_home, m_folder); !why.empty()) { m_status->set_text("Not that folder: " + why); return; }
    m_status->set_text("");
    const std::string folder = m_folder, home = m_home;
    const bool each = m_each->get_active();
    auto out = std::make_shared<sh::Scan>();
    m_busy = true;
    busy::start(*this, "Scanning", "Reading every file name in each folder (nothing is changed)",
        [out, folder, home, each] {
            std::vector<std::string> dirs;
            for (const auto& d : cf::folders(folder, each))
                if (cf::refusal(home, d).empty()) dirs.push_back(d);
            *out = sh::scan(dirs);
        },
        [this, out, folder] {
            m_busy = false;
            m_scan = std::move(*out);
            // The folders found in at least half the scanned folders start kept.
            m_keep.clear();
            const std::size_t n = m_scan.dirs.size();
            for (const auto& f : m_scan.folders) m_keep[f.path] = f.projects * 2 >= n;
            // Each type starts where most of it already is, when that is a kept folder; else left alone.
            m_picks.clear();
            for (const auto& r : m_scan.rows) {
                const std::string m = r.mostly();
                m_picks[r.key()] = sh::Pick{(!m.empty() && m_keep.count(m) && m_keep[m]) ? m : std::string(), false};
            }
            const std::string base = std::filesystem::path(folder).filename().string();
            if (m_name->get_text().empty()) m_name->set_text(sh::name_from(base));
            if (m_category->get_text().empty() && folder.rfind(m_home + "/", 0) == 0) {
                const std::string rel = folder.substr(m_home.size() + 1);
                if (tp::category_refusal(rel).empty()) m_category->set_text(rel);
            }
            show_scan();
            if (auto lg = log::get(log::Area::Io))
                lg->info("shape scan: {} folders, {} files, {} rows, {} sub-folders", m_scan.dirs.size(), m_scan.files,
                         m_scan.rows.size(), m_scan.folders.size());
        });
}

void ShapeWindow::show_scan() {
    const std::size_t n = m_scan.dirs.size();
    std::size_t types = 0;
    {
        std::string last = "\x01";
        for (const auto& r : m_scan.rows) if (r.ext != last) { ++types; last = r.ext; }
    }
    std::string s = n_of(n, "folder", "folders") + " scanned: " + n_of(m_scan.files, "file", "files") + ", " +
                    n_of(types, "type", "types") + ", " + n_of(m_scan.folders.size(), "sub-folder", "sub-folders");
    if (m_scan.whole) s += "; " + std::to_string(m_scan.whole) + " kept whole (-projects, repos)";
    m_summary->set_text(s + ".");

    while (auto* r = m_types->get_row_at_index(0)) m_types->remove(*r);
    m_rows.clear();
    for (const auto& r : m_scan.rows) {
        auto line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        line->set_margin(4);
        auto src = Gtk::make_managed<Gtk::CheckButton>("Source");
        src->set_valign(Gtk::Align::CENTER);
        src->set_tooltip_text("This type builds the others -- it stays at the project's top");
        line->append(*src);
        auto words = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        words->set_hexpand(true);
        auto title = text(r.title() + "   " + commas(r.count), "heading");
        words->append(*title);
        std::string where = "in " + std::to_string(r.projects) + " of " + std::to_string(n) + "; now: ";
        for (std::size_t i = 0; i < r.where.size() && i < 3; ++i)
            where += (i ? ", " : "") + folder_title(r.where[i].first) + " " + commas(r.where[i].second);
        if (r.where.size() > 3) where += ", +" + std::to_string(r.where.size() - 3) + " more";
        if (!r.run() && !r.first.empty()) where += "   (e.g. " + r.first + ")";
        words->append(*text(where, "dim"));
        line->append(*words);
        auto to = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({}));
        to->set_valign(Gtk::Align::CENTER);
        to->set_size_request(220, -1);
        line->append(*to);
        if (!r.routable()) {
            src->set_sensitive(false);
            to->set_sensitive(false);
            to->set_tooltip_text("A file with no extension can't be routed by type -- it stays");
        }
        m_types->append(*line);
        TypeRow tr;
        tr.key = r.key();
        tr.to = to;
        tr.source = src;
        m_rows.push_back(tr);
        const std::size_t idx = m_rows.size() - 1;
        to->property_selected().signal_changed().connect([this, idx] {
            if (m_filling) return;
            auto& row = m_rows[idx];
            const auto i = row.to->get_selected();
            if (i < row.choices.size()) m_picks[row.key].to = row.choices[i];
        });
        src->signal_toggled().connect([this, idx] {
            auto& row = m_rows[idx];
            m_picks[row.key].source = row.source->get_active();
            row.to->set_sensitive(!row.source->get_active());
        });
    }
    fill_folders();
    refill_choices();
    const bool any = !m_scan.dirs.empty();
    m_save->set_sensitive(any);
    m_save_conform->set_sensitive(any);
    if (!any) m_status->set_text("No folders to scan there (hidden, repos and protected ones are skipped).");
}

void ShapeWindow::fill_folders() {
    while (auto* r = m_folders->get_row_at_index(0)) m_folders->remove(*r);
    const std::size_t n = m_scan.dirs.size();
    std::map<std::string, std::size_t> found;
    for (const auto& f : m_scan.folders) found[f.path] = f.projects;
    for (const auto& [path, keep] : m_keep) {
        std::string label = path + "/";
        if (auto it = found.find(path); it != found.end()) label += "   in " + std::to_string(it->second) + " of " + std::to_string(n);
        else label += "   (new)";
        auto cb = Gtk::make_managed<Gtk::CheckButton>(label);
        cb->set_margin(3);
        cb->set_active(keep);
        const std::string p = path;
        cb->signal_toggled().connect([this, p, cb] {
            m_keep[p] = cb->get_active();
            refill_choices();
        });
        m_folders->append(*cb);
    }
}

std::vector<std::string> ShapeWindow::kept() const {
    std::vector<std::string> out;
    for (const auto& [p, k] : m_keep)
        if (k) out.push_back(p);
    return out;
}

void ShapeWindow::refill_choices() {
    std::vector<std::string> choices{"", tp::kTop};
    for (const auto& k : kept()) choices.push_back(k);
    m_filling = true;
    for (auto& row : m_rows) {
        auto& pick = m_picks[row.key];
        if (pick.to != tp::kTop && !pick.to.empty() && std::find(choices.begin(), choices.end(), pick.to) == choices.end())
            pick.to.clear();   // its folder was un-ticked: left alone
        auto model = Gtk::StringList::create({});
        unsigned sel = 0;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            model->append(choice_title(choices[i]));
            if (choices[i] == pick.to) sel = static_cast<unsigned>(i);
        }
        row.choices = choices;
        row.to->set_model(model);
        row.to->set_selected(sel);
    }
    m_filling = false;
}

void ShapeWindow::on_add_folder() {
    std::string p = m_new_folder->get_text();
    while (!p.empty() && p.back() == '/') p.pop_back();
    while (!p.empty() && p.front() == '/') p.erase(0, 1);
    if (p.empty()) return;
    if (const auto why = tp::path_refusal(p); !why.empty()) { m_status->set_text("Not that folder: " + why); return; }
    m_keep[p] = true;
    if (const auto slash = p.find('/'); slash != std::string::npos) m_keep[p.substr(0, slash)] = true;   // its parent too
    m_new_folder->set_text("");
    m_status->set_text("");
    fill_folders();
    refill_choices();
}

void ShapeWindow::on_save(bool conform) {
    const auto t = sh::make(m_name->get_text(), m_category->get_text(), kept(), m_scan.rows, m_picks);
    if (const auto ps = tp::problems(t); !ps.empty()) { m_status->set_text("Not yet: " + ps.front()); return; }
    for (const auto& x : tp::list(m_data_dir))
        if (x.name == t.name) { m_status->set_text("A template named \"" + t.name + "\" is saved already -- pick another name."); return; }
    if (t.routes.empty()) { m_status->set_text("Not yet: give at least one file type a folder."); return; }
    if (const auto err = tp::save(m_data_dir, t); !err.empty()) { m_status->set_text("Not saved: " + err); return; }
    if (auto lg = log::get(log::Area::Io))
        lg->info("shape saved: {} ({} folders, {} routes) from {}", t.name, t.folders.size(), t.routes.size(), m_folder);
    m_status->set_text("Saved \"" + t.name + "\": " + n_of(t.folders.size(), "folder", "folders") + ", " +
                       n_of(t.routes.size(), "route", "routes") + ".");
    if (conform) set_visible(false);
    if (m_saved) m_saved(t.name, m_folder, m_each->get_active(), conform);
}

}  // namespace tidy
