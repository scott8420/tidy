#include "Shell.hpp"
#include "core/Catalog.hpp"
#include "core/Pages.hpp"
#include "core/Birth.hpp"
#include "core/Closet.hpp"
#include "core/Retention.hpp"
#include "Log.hpp"

#include <gtkmm/checkbutton.h>
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
        return std::string("Look reads the landing places -- Downloads, Screenshots, Desktop, the loose top of ") +
               "Documents. What is undecided is here: accept what its birth proposes, decide by hand, or " +
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
        s = std::string(dr::verb_name(r.verb)) + "  " + rel(r.from) + (r.to.empty() ? "" : " -> " + rel(r.to));
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

// The Closet (s006) from a fresh birth report: what is proposed (checked),
// what waits (unchecked) -- each row a check button: name + where, why, born.
// Decided files never visit; the summary counts them.
void Shell::fill_closet() {  // helper
    namespace cb = core::birth;
    namespace cl = core::closet;
    namespace rt = core::retention;
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    const auto rep = cb::report(m_home, cb::places(m_home, user_dirs_text()));
    const auto v = cl::view(rep, now, m_settings);
    while (auto* row = m_clo_list->get_row_at_index(0)) m_clo_list->remove(*row);
    m_clo_rows.clear();

    const auto heading = [&](const std::string& name, const std::string& title, const std::string& sub) {
        auto hb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        hb->set_margin_top(14);
        hb->set_margin_start(6);
        auto h = Gtk::make_managed<Gtk::Label>(title);
        h->set_xalign(0);
        h->add_css_class("heading");
        hb->append(*h);
        auto m = Gtk::make_managed<Gtk::Label>(sub);
        m->set_xalign(0);
        m->add_css_class("dim");
        hb->append(*m);
        auto hr = Gtk::make_managed<Gtk::ListBoxRow>();
        hr->set_name(name);
        hr->set_activatable(false);
        hr->set_child(*hb);
        m_clo_list->append(*hr);
    };
    constexpr std::size_t kShown = 300;   // per section; the counts are whole
    const auto rows = [&](const std::vector<cb::Born>& items, bool proposed) {
        std::size_t shown = 0;
        for (const auto& b : items) {
            if (++shown > kShown) {
                auto more = Gtk::make_managed<Gtk::Label>("... and " + std::to_string(items.size() - kShown) +
                                                          " more (Accept or decide these, then Look again)");
                more->set_xalign(0);
                more->set_margin_start(20);
                more->add_css_class("dim");
                auto row = Gtk::make_managed<Gtk::ListBoxRow>();
                row->set_activatable(false);
                row->set_child(*more);
                m_clo_list->append(*row);
                break;
            }
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
            const std::string path = tilde(b.file.path, m_home);
            const auto slash = path.find_last_of('/');
            auto name = Gtk::make_managed<Gtk::Label>(path.substr(slash + 1) + (b.file.dir ? "  (package)" : "") +
                                                      (!proposed ? std::string()
                                                       : b.rule == "junk-list" ? std::string("   \u2192 Junk Drawer")
                                                       : "   \u2192 " + rt::title_of(b.level)));
            name->set_xalign(0);
            name->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            box->append(*name);
            std::string line = path.substr(0, slash) + "   ·   " + b.why;
            if (const auto bt = born_text(b.file); !bt.empty()) line += "   ·   " + bt;
            if (!proposed) line += "   ·   " + std::to_string(cl::days_since(b.file.has_btime ? b.file.btime : b.file.mtime, now)) + " days here";
            auto d = Gtk::make_managed<Gtk::Label>(line);
            d->set_xalign(0);
            d->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            d->add_css_class("dim");
            const auto* r = cb::rule(b.rule);
            const std::string says = r ? r->title + " -- " + r->says
                                   : b.rule == "aged-out" ? std::string("Aging out -- undecided, born and unread ") +
                                                                std::to_string(m_settings.aging_days) + " days: incidental, Ephemeral."
                                   : b.rule == "junk-list" ? std::string("Your junk list (Settings) -- the Junk Drawer: kept, no home.")
                                                          : b.rule;
            d->set_tooltip_text(b.origin.empty() ? says : says + "\nOrigin: " + b.origin);
            box->append(*d);
            auto check = Gtk::make_managed<Gtk::CheckButton>();
            check->set_child(*box);
            check->set_active(proposed);
            check->set_margin_start(14);
            check->set_margin_top(2);
            check->set_margin_bottom(2);
            auto row = Gtk::make_managed<Gtk::ListBoxRow>();
            row->set_activatable(false);
            row->set_child(*check);
            m_clo_list->append(*row);
            m_clo_rows.emplace_back(check, b);
        }
    };
    if (!v.proposed.empty()) {
        heading("closet.proposed", "Proposed  (" + std::to_string(v.proposed.size()) + ")",
                "Their birth, your junk list, or their age says what they are. Accept stamps each with its own.");
        rows(v.proposed, true);
    }
    if (!v.waiting.empty()) {
        heading("closet.waiting", "Waiting  (" + std::to_string(v.waiting.size()) + ")",
                "Undecided. Decide by hand, or the Junk Drawer. Unread " + std::to_string(m_settings.aging_days) +
                    " days (Settings), they are proposed to age out.");
        rows(v.waiting, false);
    }
    if (v.total() == 0) heading("closet.empty", "The Closet is empty", "Everything in the landing places is decided.");

    std::string where;
    for (const auto& pl : rep.places) where += (where.empty() ? "" : ", ") + pl.title;
    m_clo_summary->set_text(std::to_string(v.total()) + " in the Closet: " + std::to_string(v.proposed.size()) +
                            " proposed, " + std::to_string(v.waiting.size()) + " waiting   |   " +
                            std::to_string(v.decided) + " decided never visit (" + std::to_string(v.junk) +
                            " in the Junk Drawer)   |   " + where + (m_fixture ? "   |   FIXTURE " + m_home : ""));
    if (auto lg = log::get(log::Area::Shell))
        lg->info("closet: {} proposed, {} waiting, {} decided ({} junk) in {} places",
                 v.proposed.size(), v.waiting.size(), v.decided, v.junk, rep.places.size());
}

std::vector<core::birth::Born> Shell::closet_checked() const {  // helper
    std::vector<core::birth::Born> out;
    for (const auto& [c, b] : m_clo_rows)
        if (c->get_active()) out.push_back(b);
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
