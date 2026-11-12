#include "Shell.hpp"
#include "core/Catalog.hpp"
#include "core/Pages.hpp"
#include "core/Birth.hpp"
#include "core/Closet.hpp"
#include "core/Retention.hpp"
#include "Log.hpp"

#include <gtkmm/checkbutton.h>
#include "SelectList.hpp"
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/stack.h>
#include <glibmm/datetime.h>

#include <glibmm/fileutils.h>

#include <cstdlib>
#include <ctime>
#include <set>

// Shell_helpers.cpp -- HELPERS. Shared orchestration for zones and handlers.

namespace tidy {

// The line under a page's promise. A page not alive yet says when it comes;
// Activity (alive, s004) says where Tidy's records are and that nothing is done.
std::string Shell::page_note(const std::string& id) const {  // helper
    const auto* p = core::pages::find(id);
    if (id == "activity")
        return std::string("Nothing done yet. Every change Tidy makes is listed here, with the way back.\n") +
               "Records: " + m_data_dir + (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : "");
    if (id == "closet")
        return std::string("Look sweeps the whole home for loose files -- at home's top, at the top of each folder ") +
               "there, and in Downloads, Screenshots and the Desktop. What is undecided is here: select files, then " +
               "Set to: what its birth (or its age) proposes, a level, or " +
               "the Junk Drawer. Every choice is a preview first, and undoable from Activity." +
               (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : "");
    return p ? "Comes with " + p->milestone + "." : "";
}

// ~/.config/user-dirs.dirs (or $XDG_CONFIG_HOME's), read. A fixture home uses
// its own .config and ignores XDG, as its records do.
std::string Shell::user_dirs_text() const {  // helper
    std::string cfg = m_home + "/.config";
    const char* x = std::getenv("XDG_CONFIG_HOME");
    if (!m_fixture && x && x[0] == '/') cfg = x;
    try {
        return Glib::file_get_contents(cfg + "/user-dirs.dirs");
    } catch (const Glib::Error&) {
        return "";   // none: the defaults
    }
}

namespace {
std::string when_text(std::int64_t t) {
    auto d = Glib::DateTime::create_now_local(static_cast<gint64>(t));
    return d.format("%b %e, %H:%M");
}
std::string step_text(const latch::door::Record& r, const std::string& home) {
    namespace dr = latch::door;
    auto rel = [&](const std::string& p) { return p.rfind(home + "/", 0) == 0 ? "~/" + p.substr(home.size() + 1) : p; };
    std::string s;
    if (r.verb == dr::Verb::Tag) {
        const std::string key = r.attr.substr(std::string(dr::kTagPrefix).size());
        s = r.untag ? "untag " + key : key + " = " + (r.after.size() > 40 ? r.after.substr(0, 8) + "..." : r.after);
        s += "  " + rel(r.from);
    } else {
        // s013b: a copy's `from` is its staging name; the file it copied is `src`.
        const std::string& from = r.verb == dr::Verb::Copy && !r.src.empty() ? r.src : r.from;
        s = std::string(dr::verb_name(r.verb)) + "  " + rel(from) + (r.to.empty() ? "" : " -> " + rel(r.to));
    }
    switch (r.state) {
        case dr::State::Undone: s += "   (undone)"; break;
        case dr::State::Failed: s += "   (failed: " + r.error + ")"; break;
        default: break;
    }
    return s;
}
}  // namespace

namespace {
std::string tilde(const std::string& p, const std::string& home) {
    return p.rfind(home + "/", 0) == 0 ? "~/" + p.substr(home.size() + 1) : p;
}
std::string born_text(const tidy::core::facts::Facts& f) {
    const std::int64_t t = f.has_btime ? f.btime : f.mtime;
    if (t <= 0) return "";
    return (f.has_btime ? "born " : "changed ") + Glib::DateTime::create_now_local(static_cast<gint64>(t)).format("%b %e, %Y");
}
}  // namespace

// The Closet (s006) from a fresh birth report: what is proposed, what waits --
// each row name (+ its proposal), where, why, born. s018: one SelectList; the
// Proposed rows start selected (as they started checked), so "Set to -> Its
// proposal" is the one-step path. Decided files never visit; the summary counts them.
namespace { constexpr const char* kLooking = "Looking through the home..."; }

// s019 the home sweep: the whole home's loose files, walked off the window's
// thread (a real home is big). The page says "Looking..." and stays live; the
// rows land in show_closet when the walk is done.
void Shell::fill_closet() {  // helper
    if (m_clo_status) m_clo_status->set_text(kLooking);
    if (m_clo_summary && m_clo_rows.empty()) m_clo_summary->set_text("Looking...");
    const std::string home = m_home, dirs = user_dirs_text();
    const auto st = m_settings;
    m_clo_look.start(
        [home, dirs, st] {
            ClosetLook l;
            l.now = static_cast<std::int64_t>(std::time(nullptr));
            l.rep = core::birth::report(home, core::birth::sweep(home, dirs, st.catch_all), l.now, st.old_years);
            l.view = core::closet::view(l.rep, l.now, st);
            return l;
        },
        [this](ClosetLook l) { show_closet(l); });
}

void Shell::show_closet(const ClosetLook& look) {  // helper
    namespace cb = core::birth;
    namespace cl = core::closet;
    namespace rt = core::retention;
    const auto now = look.now;
    const auto& rep = look.rep;
    const auto& v = look.view;
    m_clo_rows.clear();
    std::vector<SelectList::Item> items;
    std::vector<std::size_t> pre;

    const auto rows = [&](const std::vector<cb::Born>& born, bool proposed, const std::string& head, const std::string& sub) {
        bool first = true;
        for (const auto& b : born) {
            SelectList::Item it;
            if (first) { it.header = head; it.header_sub = sub; first = false; }
            const std::string path = tilde(b.file.path, m_home);
            const auto slash = path.find_last_of('/');
            it.title = path.substr(slash + 1) + (b.file.dir ? "  (package)" : "") +
                       (!proposed ? std::string()
                        : b.rule == "junk-list" ? std::string("   \u2192 Junk Drawer")
                        : "   \u2192 " + rt::title_of(b.level));
            std::string line = path.substr(0, slash) + "   \u00b7   " + b.why;
            if (const auto bt = born_text(b.file); !bt.empty()) line += "   \u00b7   " + bt;
            if (!proposed) line += "   \u00b7   " + std::to_string(cl::days_since(b.file.has_btime ? b.file.btime : b.file.mtime, now)) + " days here";
            it.line = line;
            const auto* r = cb::rule(b.rule);
            const std::string says = r && r->id == "old" ? r->title + " -- " + r->says.substr(0, r->says.find(" (Settings)")) +
                                                             " (" + std::to_string(m_settings.old_years) + " years, Settings)" +
                                                             r->says.substr(r->says.find(" (Settings)") + 11)
                                   : r ? r->title + " -- " + r->says
                                   : b.rule == "aged-out" ? std::string("Aging out -- undecided, born and unread ") +
                                                                std::to_string(m_settings.aging_days) + " days: incidental, Ephemeral."
                                   : b.rule == "junk-list" ? std::string("Your junk list (Settings) -- the Junk Drawer: kept, no home.")
                                                          : b.rule;
            it.tooltip = b.origin.empty() ? says : says + "\nOrigin: " + b.origin;
            if (proposed) pre.push_back(items.size());
            items.push_back(std::move(it));
            m_clo_rows.push_back(b);
        }
    };
    rows(v.proposed, true, "Proposed  (" + std::to_string(v.proposed.size()) + ")",
         "Their birth, your junk list, or their age says what they are. Set to \u2192 Its proposal stamps each with its own.");
    rows(v.waiting, false, "Waiting  (" + std::to_string(v.waiting.size()) + ")",
         "Undecided. Set a level by hand, or the Junk Drawer. Unread " + std::to_string(m_settings.aging_days) +
             " days (Settings), they are proposed to age out.");
    m_clo_list->set_items(items);
    m_clo_list->select(pre);
    if (m_clo_status->get_text() == kLooking)   // s019: a "Done: ..." set while it looked stays
        m_clo_status->set_text(v.total() == 0 ? "The Closet is empty -- every loose file in the home is decided."
                                              : "Select files, then Set to. Every choice is previewed first.");

    std::size_t old = 0, tops = 0;
    for (const auto& b : v.proposed) old += b.rule == "old";
    for (const auto& pl : rep.places) tops += pl.id == "top";
    m_clo_summary->set_text(std::to_string(v.total()) + " in the Closet: " + std::to_string(v.proposed.size()) +
                            " proposed (" + std::to_string(old) + " untouched for years), " +
                            std::to_string(v.waiting.size()) + " waiting   |   " +
                            std::to_string(v.decided) + " decided never visit (" + std::to_string(v.junk) +
                            " in the Junk Drawer)   |   the whole home: the landing places, home's top, " +
                            std::to_string(tops) + " top folders" + (m_fixture ? "   |   FIXTURE " + m_home : ""));
    if (auto lg = log::get(log::Area::Shell))
        lg->info("closet: {} proposed, {} waiting, {} decided ({} junk) in {} places",
                 v.proposed.size(), v.waiting.size(), v.decided, v.junk, rep.places.size());
}

std::vector<core::birth::Born> Shell::closet_selected() const {  // helper
    std::vector<core::birth::Born> out;
    for (auto i : m_clo_list->selected())
        if (i < m_clo_rows.size()) out.push_back(m_clo_rows[i]);
    return out;
}

// A plan -> the preview state: every step, each refusal the door would give,
// and Do it only when something would happen. The plan waits in m_clo_plan.
void Shell::show_closet_preview(latch::door::Plan plan) {  // helper
    namespace dr = latch::door;
    while (auto* row = m_clo_prev_list->get_row_at_index(0)) m_clo_prev_list->remove(*row);
    std::size_t refused = 0;
    std::set<std::string> files;
    for (const auto& a : plan.actions) {
        files.insert(a.from);
        const std::string why = m_door->refusal(a);
        if (!why.empty()) ++refused;
        const std::string key = a.attr.substr(std::string(dr::kTagPrefix).size());
        const std::string val = key == "id" ? "a new id" : a.content;
        auto l = Gtk::make_managed<Gtk::Label>(key + " = " + val + "     " + tilde(a.from, m_home) +
                                               (why.empty() ? "" : "     -- REFUSED: " + why));
        l->set_xalign(0);
        l->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        if (key == "id") l->add_css_class("dim");
        l->set_margin(4);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*l);
        m_clo_prev_list->append(*row);
    }
    m_clo_prev_summary->set_text(plan.label + "   |   " + std::to_string(files.size()) + " files, " +
                                 std::to_string(plan.actions.size()) + " steps   |   tags only: nothing moves");
    m_clo_prev_status->set_text(refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen"
                                        : "Every step can be undone from Activity.");
    m_clo_prev_run->set_sensitive(plan.actions.size() > refused);
    m_clo_plan = std::move(plan);
    m_clo_states->set_visible_child("preview");
}

// Activity from the door's manifest: newest batch first, each with its steps.
void Shell::refresh_activity() {  // helper
    if (!m_act_states) return;
    const auto& bs = m_door->batches();
    m_act_states->set_visible_child(bs.empty() ? "start" : "results");
    while (auto* row = m_act_list->get_row_at_index(0)) m_act_list->remove(*row);
    std::size_t steps = 0;
    for (auto it = bs.rbegin(); it != bs.rend(); ++it) {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        box->set_margin(10);
        auto head = Gtk::make_managed<Gtk::Label>(it->label + "   " + when_text(it->when) + "   " +
                                                  std::to_string(it->records.size()) + " step" +
                                                  (it->records.size() == 1 ? "" : "s") + (it->undone ? "   -- undone" : ""));
        head->set_xalign(0);
        head->add_css_class(it->undone ? "dim" : "heading");
        box->append(*head);
        for (auto id : it->records)
            if (const auto* r = m_door->record(id)) {
                ++steps;
                auto l = Gtk::make_managed<Gtk::Label>(step_text(*r, m_home));
                l->set_xalign(0);
                l->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
                l->add_css_class("dim");
                l->set_margin_start(14);
                box->append(*l);
            }
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name("activity.batch." + std::to_string(it->id));
        row->set_child(*box);
        m_act_list->append(*row);
    }
    core::catalog::Catalog cat;
    const bool have = cat.open(m_data_dir, false);
    m_act_summary->set_text(std::to_string(bs.size()) + " batches, " + std::to_string(steps) + " steps   |   " +
                            (have ? std::to_string(cat.count()) + " files in the catalog" : "no catalog yet") +
                            (m_fixture ? "   |   FIXTURE " + m_home : ""));
    m_act_undo->set_sensitive(m_door->can_undo());
    m_act_undo->set_label(m_door->can_undo() ? "Undo \u201c" + m_door->undo_label() + "\u201d" : "Nothing to undo");
    if (!m_door->unfinished().empty())
        m_act_status->set_text("After a crash: " + m_door->unfinished().front());
}

}  // namespace tidy
