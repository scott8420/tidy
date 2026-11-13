#include "QuarantineWindow.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Own.hpp"
#include "core/Projects.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>

#include <algorithm>
#include <map>
#include <set>

namespace tidy {

namespace qu = core::quarantine;
namespace su = core::suspects;

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
int rank(qu::Answer a) {   // unanswered first, then not now, then answered
    switch (a) {
        case qu::Answer::None:     return 0;
        case qu::Answer::NotNow:   return 1;
        case qu::Answer::Bundle:   return 2;
        case qu::Answer::Ordinary: return 3;
    }
    return 4;
}
std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}
}  // namespace

std::vector<QuarantineWindow::Row> QuarantineWindow::rows_of(const su::Report& r, const qu::Answers& a) {
    std::vector<Row> rows;
    std::set<std::string> seen;
    // Groups 1, 2: one row per extension. Known by latch / ours (not answered) are not asked.
    std::map<std::string, std::vector<const su::Suspect*>> by;
    for (const auto& s : r.list) {
        if (s.ext.empty()) continue;
        const auto ea = qu::of_ext(a, s.ext);
        if (s.known && ea != qu::Answer::Bundle) continue;
        by[s.ext].push_back(&s);
    }
    for (const auto& [ext, list] : by) {
        Row x;
        x.by_ext = true;
        x.key = ext;
        std::uint64_t bytes = 0;
        std::size_t loose = 0;
        for (auto* s : list) { bytes += s->bytes; loose += s->project.empty(); }
        x.title = ext + "  --  " + plural(list.size(), "folder", "folders") + " (" + std::to_string(loose) + " loose, " +
                  std::to_string(list.size() - loose) + " in projects), " + su::size_text(bytes) + "  --  " +
                  (list.front()->group == su::Group::App ? "with a .json" : "no .json");
        for (std::size_t i = 0; i < list.size() && i < 3; ++i) x.detail += (i ? "\n" : "") + ("~/" + list[i]->rel);
        if (list.size() > 3) x.detail += "\n... and " + std::to_string(list.size() - 3) + " more";
        x.was = x.now = qu::of_ext(a, ext);
        seen.insert("e" + ext);
        rows.push_back(std::move(x));
    }
    // Groups 3, 4: one row per folder.
    for (const auto& s : r.list) {
        if (!s.ext.empty()) continue;
        Row x;
        x.by_ext = false;
        x.key = s.rel;
        std::string why;
        for (std::size_t k = 0; k < s.why.size() && k < 3; ++k) why += (k ? " " : "") + s.why[k];
        if (s.why.size() > 3) why += " +" + std::to_string(s.why.size() - 3);
        x.title = "~/" + s.rel;
        x.detail = std::string(s.project.empty() ? "loose" : "in a project") + "; " + su::size_text(s.bytes) + ", " +
                   plural(s.files, "file", "files") + "; " + why;
        x.was = x.now = qu::of_folder(a, s.rel);
        seen.insert("f" + s.rel);
        rows.push_back(std::move(x));
    }
    // Answers the scan no longer lists (ordinary ones are walked, not listed).
    for (const auto& [ext, ans] : a.ext)
        if (!seen.count("e" + ext)) {
            Row x;
            x.key = ext;
            x.title = ext;
            x.detail = ans == qu::Answer::Ordinary ? "walked like any folder -- not asked again" : "none found by this look";
            x.was = x.now = ans;
            rows.push_back(std::move(x));
        }
    for (const auto& [rel, ans] : a.folders)
        if (!seen.count("f" + rel)) {
            Row x;
            x.by_ext = false;
            x.key = rel;
            x.title = "~/" + rel;
            x.detail = ans == qu::Answer::Ordinary ? "walked like any folder -- not asked again" : "not found by this look";
            x.was = x.now = ans;
            rows.push_back(std::move(x));
        }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& p, const Row& q) {
        if (rank(p.was) != rank(q.was)) return rank(p.was) < rank(q.was);
        return p.by_ext > q.by_ext;   // extensions first
    });
    return rows;
}

qu::Answers QuarantineWindow::merged(const qu::Answers& a, const std::vector<Row>& rows) {
    qu::Answers out = a;
    for (const auto& x : rows) {
        auto& m = x.by_ext ? out.ext : out.folders;
        if (x.now == qu::Answer::None) m.erase(x.key);
        else m[x.key] = x.now;
    }
    return out;
}

QuarantineWindow::QuarantineWindow(Gtk::Window& parent, std::string home, std::string data_dir, Changed changed)
    : m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_changed(std::move(changed)) {
    set_name("quarantine.window");
    set_title("Quarantine");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(820, 600);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Quarantine", "heading"));
    box->append(*text("Folders that may be ONE thing (an app's bundle), not a folder of loose files. Until you answer, "
                      "Tidy holds each one whole and does not go inside it. A bundle: one thing, moved only whole. "
                      "An ordinary folder: walked like any other, never asked again. Not now: still held. "
                      "Save writes only Tidy's own list -- nothing in your home moves.", "dim"));
    m_summary = text("");
    m_summary->set_name("quarantine.summary");
    box->append(*m_summary);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("quarantine.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    box->append(*scroll);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("quarantine.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto look = Gtk::make_managed<Gtk::Button>("Look again");
    look->set_name("quarantine.look");
    look->signal_clicked().connect([this] { on_look(); });
    bar->append(*look);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("quarantine.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_save = Gtk::make_managed<Gtk::Button>("Save");
    m_save->set_name("quarantine.save");
    m_save->add_css_class("suggested-action");
    m_save->signal_clicked().connect([this] { on_save(); });
    bar->append(*m_save);
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

QuarantineWindow::~QuarantineWindow() = default;

void QuarantineWindow::open() {
    present();
    m_status->set_text("");
    on_look();
}

void QuarantineWindow::on_look() {
    m_summary->set_text("Looking...");
    m_save->set_sensitive(false);
    const std::string home = m_home;
    auto out = std::make_shared<su::Report>();
    busy::start(*this, "Quarantine -- looking", "Reading your home for folders that may be one thing",
        [home, out] { *out = su::scan(home, home); },
        [this, out] {
            m_report = out;
            show();
        });
}

void QuarantineWindow::show() {
    while (auto* x = m_list->get_row_at_index(0)) m_list->remove(*x);
    const auto answers = qu::current();
    m_rows = rows_of(*m_report, answers);
    static const char* heads[] = {"To answer", "Not now -- still held", "Answered: a bundle", "Answered: an ordinary folder"};
    int last = -1;
    std::size_t open = 0;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        auto& x = m_rows[i];
        open += x.was == qu::Answer::None;
        if (rank(x.was) != last) {
            last = rank(x.was);
            auto h = text(heads[last], "heading");
            h->set_margin_top(10);
            h->set_margin_start(6);
            m_list->append(*h);
        }
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        row->set_margin(6);
        auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        col->set_hexpand(true);
        col->append(*text(x.title));
        if (!x.detail.empty()) col->append(*text(x.detail, "dim"));
        row->append(*col);
        x.pick = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(
            {qu::answer_title(qu::Answer::None), qu::answer_title(qu::Answer::Bundle),
             qu::answer_title(qu::Answer::Ordinary), qu::answer_title(qu::Answer::NotNow)}));
        x.pick->set_name("quarantine.pick");
        x.pick->set_valign(Gtk::Align::CENTER);
        x.pick->set_selected(static_cast<guint>(x.now));
        x.pick->property_selected().signal_changed().connect([this, i] {
            m_rows[i].now = static_cast<qu::Answer>(m_rows[i].pick->get_selected());
            update_bar();
        });
        row->append(*x.pick);
        m_list->append(*row);
    }
    const auto& r = *m_report;
    std::size_t known = 0;
    for (const auto& s : r.list) known += s.known;
    m_summary->set_text(plural(open, "question", "questions") + " to answer. " + plural(known, "bundle", "bundles") +
                        " known already, " + plural(r.quiet.size(), "ordinary folder", "ordinary folders") +
                        " walked. " + plural(r.folders, "folder", "folders") + " looked at, " + std::to_string(r.ms) +
                        " ms.");
    if (m_rows.empty()) m_list->append(*text("Nothing in quarantine.", "dim"));
    update_bar();
}

void QuarantineWindow::update_bar() {
    std::size_t changed = 0;
    for (const auto& x : m_rows) changed += x.now != x.was;
    m_save->set_sensitive(changed > 0);
    m_status->set_text(changed ? plural(changed, "answer", "answers") + " changed -- Save to keep" : "");
}

void QuarantineWindow::on_save() {
    const auto next = merged(qu::current(), m_rows);
    const std::string home = m_home, data = m_data_dir;
    auto err = std::make_shared<std::string>();
    auto out = std::make_shared<su::Report>();
    m_save->set_sensitive(false);
    // Save the answers, look again with them, then save who is still held.
    busy::start(*this, "Quarantine -- saving", "Saving your answers, then looking again with them",
        [home, data, next, err, out] {
            auto a = next;
            *err = core::own::write(data, qu::kFile, qu::to_text(a));
            if (!err->empty()) return;
            core::projects::apply_places(data, home);
            *out = su::scan(home, home);
            a.held = su::held_of(*out);
            *err = core::own::write(data, qu::kFile, qu::to_text(a));
            core::projects::apply_places(data, home);
        },
        [this, err, out] {
            if (!err->empty()) {
                m_status->set_text("Not saved: " + *err);
                update_bar();
                return;
            }
            if (auto lg = log::get(log::Area::Io))
                lg->info("quarantine save: {} held, {} ordinary walked", su::held_of(*out).size(), out->quiet.size());
            m_report = out;
            show();
            m_status->set_text("Saved. " + plural(su::held_of(*out).size(), "folder", "folders") + " still held.");
            if (m_changed) m_changed();
        });
}

}  // namespace tidy
