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

namespace dr = latch::door;
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
        case qu::Answer::None:
        case qu::Answer::Trash:     return 0;
        case qu::Answer::NotNow:    return 1;
        case qu::Answer::DontTouch: return 2;
        case qu::Answer::Bundle:    return 3;
        case qu::Answer::Ordinary:  return 4;
    }
    return 5;
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
        for (auto* sp : list) x.rels.push_back(sp->rel);
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
        x.rels = {s.rel};
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
            x.detail = ans == qu::Answer::Ordinary ? "walked like any folder -- not asked again"
                     : ans == qu::Answer::DontTouch ? "Hands off -- not looked inside" : "none found by this look";
            x.was = x.now = ans;
            rows.push_back(std::move(x));
        }
    for (const auto& [rel, ans] : a.folders)
        if (!seen.count("f" + rel)) {
            Row x;
            x.by_ext = false;
            x.key = rel;
            x.title = "~/" + rel;
            x.detail = ans == qu::Answer::Ordinary ? "walked like any folder -- not asked again"
                     : ans == qu::Answer::DontTouch ? "Hands off -- not looked inside" : "not found by this look";
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
        if (x.now == qu::Answer::None || x.now == qu::Answer::Trash) m.erase(x.key);   // the Trash is not kept
        else m[x.key] = x.now;
    }
    return out;
}

QuarantineWindow::QuarantineWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_changed(std::move(changed)) {
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
                      "Don't touch: kept as it is, Hands off for good. Each answer is saved the moment you pick it. "
                      "To the Trash is the one exception: pick it, then To the Trash... shows the list first; "
                      "one undo step in Activity.", "dim"));
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
    m_look = Gtk::make_managed<Gtk::Button>("Look again");
    m_look->set_name("quarantine.look");
    m_look->signal_clicked().connect([this] { if (m_plan) back(); else on_look(); });
    bar->append(*m_look);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("quarantine.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_save = Gtk::make_managed<Gtk::Button>("To the Trash\u2026");
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
            const auto now = qu::current();
            if (const auto a = core::suspects::with_lists(now, *out); !(a == now) &&
                core::own::write(m_data_dir, qu::kFile, qu::to_text(a)).empty())
                core::projects::apply_places(m_data_dir, m_home);   // who is held, as of this look
            show();
        });
}

void QuarantineWindow::show() {
    while (auto* x = m_list->get_row_at_index(0)) m_list->remove(*x);
    const auto answers = qu::current();
    m_rows = rows_of(*m_report, answers);
    static const char* heads[] = {"To answer", "Not now -- still held", "Answered: don't touch (Hands off)",
                                  "Answered: a bundle", "Answered: an ordinary folder"};
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
             qu::answer_title(qu::Answer::Ordinary), qu::answer_title(qu::Answer::NotNow),
             qu::answer_title(qu::Answer::DontTouch), qu::answer_title(qu::Answer::Trash)}));
        x.pick->set_name("quarantine.pick");
        x.pick->set_valign(Gtk::Align::CENTER);
        x.pick->set_selected(static_cast<guint>(x.now));
        x.pick->property_selected().signal_changed().connect([this, i] {
            m_rows[i].now = static_cast<qu::Answer>(m_rows[i].pick->get_selected());
            if (m_rows[i].now != qu::Answer::Trash && m_rows[i].now != m_rows[i].was) answer_now(i);
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
    std::size_t trash = 0;
    for (const auto& x : m_rows) trash += x.now == qu::Answer::Trash;
    if (m_plan) return;   // the preview owns the button
    m_save->set_label(trash ? "To the Trash (" + std::to_string(trash) + ")\u2026" : std::string("To the Trash\u2026"));
    m_save->set_sensitive(trash > 0);
}

// s025b2 (Scott: "the answers I gave were not saved"): each answer is saved
// the moment it is picked -- Tidy's own file, then Tidy re-reads it.
void QuarantineWindow::answer_now(std::size_t i) {
    auto& x = m_rows[i];
    auto a = core::suspects::with_lists(merged(qu::current(), {x}), *m_report);
    const auto err = core::own::write(m_data_dir, qu::kFile, qu::to_text(a));
    if (!err.empty()) {
        m_status->set_text("Not saved: " + err);
        return;
    }
    core::projects::apply_places(m_data_dir, m_home);
    x.was = x.now;
    if (auto lg = log::get(log::Area::Io))
        lg->info("quarantine: {} = {} (saved; {} held, {} hands off)", x.key, qu::answer_title(x.now), a.held.size(),
                 a.hands_off.size());
    m_status->set_text("Saved: " + (x.by_ext ? x.key : "~/" + x.key) + " -- " + qu::answer_title(x.now));
    if (m_changed) m_changed();
}

void QuarantineWindow::on_save() {
    if (m_plan) {   // the preview's Do it: trash first, then the answers
        auto plan = *m_plan;
        plan.previewed = true;   // shown in this window's list
        m_plan.reset();
        m_save->set_sensitive(false);
        busy::door(*this, m_door, std::move(plan), "Quarantine -- to the Trash",
            [this](const dr::Result& r) {
                save_answers(r.ok ? "Trashed " + plural(r.done, "folder", "folders") + " (one step to undo in Activity). "
                                  : "Trash not done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front()) + ". ");
            });
        return;
    }
    // s025b1: the Trash rows -> a door plan, previewed first.
    dr::Plan plan;
    plan.label = "Quarantine: to the Trash";
    std::set<std::string> seen;
    for (const auto& x : m_rows)
        if (x.now == qu::Answer::Trash)
            for (const auto& rel : x.rels)
                if (seen.insert(rel).second) {
                    dr::Action a;
                    a.verb = dr::Verb::Trash;
                    a.from = m_home + "/" + rel;
                    a.reason = "Quarantine: you answered \"to the Trash\"";
                    for (const auto& s : m_report->list)
                        if (s.rel == rel) a.bytes = s.bytes;
                    plan.actions.push_back(std::move(a));
                }
    if (plan.actions.empty()) { save_answers(""); return; }
    preview(std::move(plan));
}

void QuarantineWindow::preview(dr::Plan plan) {
    while (auto* x = m_list->get_row_at_index(0)) m_list->remove(*x);
    auto h = text("To the Trash -- check before Do it (one undo step in Activity)", "heading");
    h->set_margin(6);
    m_list->append(*h);
    std::vector<std::string> made, freed;
    std::size_t refused = 0;
    std::uint64_t bytes = 0;
    for (const auto& a : plan.actions) {
        const auto why = m_door.refusal(a, made, freed);
        refused += !why.empty();
        bytes += a.bytes;
        auto l = text("~" + a.from.substr(m_home.size()) + "   [" + su::size_text(a.bytes) + "]" +
                          (why.empty() ? "" : "   REFUSED: " + why), why.empty() ? nullptr : "dim");
        l->set_margin(6);
        m_list->append(*l);
    }
    std::erase_if(plan.actions, [&](const dr::Action& a) {
        std::vector<std::string> m2, f2;
        return !m_door.refusal(a, m2, f2).empty();
    });
    m_summary->set_text(plural(plan.actions.size(), "folder", "folders") + " to the Trash, " + su::size_text(bytes) +
                        (refused ? "; " + std::to_string(refused) + " refused (left as they are)" : "") +
                        ". Your other answers are saved after it.");
    m_status->set_text("");
    m_look->set_label("Back");
    m_save->set_label("Do it");
    m_save->set_sensitive(true);
    m_plan = std::move(plan);
}

void QuarantineWindow::back() {   // to the list, with the picks as they were
    std::map<std::string, qu::Answer> picks;
    for (const auto& x : m_rows) picks[(x.by_ext ? "e" : "f") + x.key] = x.now;
    m_plan.reset();
    m_look->set_label("Look again");
    show();
    for (auto& x : m_rows)
        if (const auto it = picks.find((x.by_ext ? "e" : "f") + x.key); it != picks.end() && it->second != x.now)
            x.pick->set_selected(static_cast<guint>(it->second));   // its signal sets x.now
    update_bar();
}

void QuarantineWindow::save_answers(const std::string& done_text) {
    m_look->set_label("Look again");
    const auto next = merged(qu::current(), m_rows);
    const std::string home = m_home, data = m_data_dir;
    auto err = std::make_shared<std::string>();
    auto out = std::make_shared<su::Report>();
    m_save->set_sensitive(false);
    // Save the answers, look again with them, then save who is still held
    // and who is Hands off.
    busy::start(*this, "Quarantine -- saving", "Saving your answers, then looking again with them",
        [home, data, next, err, out] {
            auto a = next;
            a.hands_off = qu::still_hands_off(a);
            *err = core::own::write(data, qu::kFile, qu::to_text(a));
            if (!err->empty()) return;
            core::projects::apply_places(data, home);
            *out = su::scan(home, home);
            a.held = su::held_of(*out);
            for (const auto& h : su::hands_off_of(*out))
                if (std::find(a.hands_off.begin(), a.hands_off.end(), h) == a.hands_off.end()) a.hands_off.push_back(h);
            *err = core::own::write(data, qu::kFile, qu::to_text(a));
            core::projects::apply_places(data, home);
        },
        [this, err, out, done_text] {
            if (!err->empty()) {
                m_status->set_text(done_text + "Not saved: " + *err);
                update_bar();
                return;
            }
            const auto off = qu::hands_off().size();
            if (auto lg = log::get(log::Area::Io))
                lg->info("quarantine save: {} held, {} hands off, {} ordinary walked", su::held_of(*out).size(), off,
                         out->quiet.size());
            m_report = out;
            show();
            m_status->set_text(done_text + "Saved. " + plural(su::held_of(*out).size(), "folder", "folders") +
                               " still held, " + std::to_string(off) + " Hands off.");
            if (m_changed) m_changed();
        });
}

}  // namespace tidy
