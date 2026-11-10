#include "Shell.hpp"
#include "core/Catalog.hpp"
#include "core/Pages.hpp"
#include "core/Birth.hpp"
#include "core/Retention.hpp"
#include "Log.hpp"

#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/stack.h>
#include <glibmm/datetime.h>

#include <glibmm/fileutils.h>

#include <cstdlib>

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
               "Documents -- and shows what each file's birth says it should be. Report only: nothing is " +
               "tagged. Deciding comes with " + (p ? p->milestone : "s006") + "." +
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

// The birth report -> the Closet's list: one heading per level that has items
// (in §2's order), then its files -- name, and a dim line: where, why, born.
void Shell::fill_closet() {  // helper
    namespace cb = core::birth;
    namespace rt = core::retention;
    const auto rep = cb::report(m_home, cb::places(m_home, user_dirs_text()));
    while (auto* row = m_clo_list->get_row_at_index(0)) m_clo_list->remove(*row);

    constexpr std::size_t kShown = 300;   // per level; the counts above are whole
    std::string counts;
    for (const auto& lv : rt::all()) {
        const auto n = rep.count(lv.level);
        if (!n) continue;
        counts += (counts.empty() ? "" : ", ") + std::to_string(n) + " " + lv.title;

        auto hb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        hb->set_margin_top(14);
        hb->set_margin_start(6);
        auto h = Gtk::make_managed<Gtk::Label>(lv.title + "  (" + std::to_string(n) + ")");
        h->set_xalign(0);
        h->add_css_class("heading");
        hb->append(*h);
        auto m = Gtk::make_managed<Gtk::Label>(lv.meaning);
        m->set_xalign(0);
        m->add_css_class("dim");
        hb->append(*m);
        auto hr = Gtk::make_managed<Gtk::ListBoxRow>();
        hr->set_name("closet.level." + lv.id);
        hr->set_activatable(false);
        hr->set_child(*hb);
        m_clo_list->append(*hr);

        std::size_t shown = 0;
        for (const auto& b : rep.items) {
            if (b.level != lv.level) continue;
            if (++shown > kShown) break;
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
            box->set_margin_start(20);
            box->set_margin_top(4);
            box->set_margin_bottom(4);
            const std::string path = tilde(b.file.path, m_home);
            const auto slash = path.find_last_of('/');
            auto name = Gtk::make_managed<Gtk::Label>(path.substr(slash + 1) + (b.file.dir ? "  (package)" : ""));
            name->set_xalign(0);
            name->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            box->append(*name);
            const auto* r = cb::rule(b.rule);
            std::string line = path.substr(0, slash) + "   \u00b7   " + b.why;
            if (const auto bt = born_text(b.file); !bt.empty()) line += "   \u00b7   " + bt;
            auto d = Gtk::make_managed<Gtk::Label>(line);
            d->set_xalign(0);
            d->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            d->add_css_class("dim");
            const std::string says = r ? r->title + " -- " + r->says : b.rule;
            d->set_tooltip_text(b.origin.empty() ? says : says + "\nOrigin: " + b.origin);
            box->append(*d);
            auto row = Gtk::make_managed<Gtk::ListBoxRow>();
            row->set_activatable(false);
            row->set_child(*box);
            m_clo_list->append(*row);
        }
        if (n > kShown) {
            auto more = Gtk::make_managed<Gtk::Label>("... and " + std::to_string(n - kShown) + " more");
            more->set_xalign(0);
            more->set_margin_start(20);
            more->add_css_class("dim");
            auto row = Gtk::make_managed<Gtk::ListBoxRow>();
            row->set_activatable(false);
            row->set_child(*more);
            m_clo_list->append(*row);
        }
    }
    std::string where;
    for (const auto& pl : rep.places) where += (where.empty() ? "" : ", ") + pl.title;
    m_clo_summary->set_text(std::to_string(rep.items.size()) + " items in " + std::to_string(rep.places.size()) +
                            " landing places (" + where + ")" + (counts.empty() ? "" : "   |   " + counts) +
                            (m_fixture ? "   |   FIXTURE " + m_home : ""));
    m_clo_status->set_text("Report only -- nothing is tagged. Deciding comes with s006.");
    if (auto lg = log::get(log::Area::Shell))
        lg->info("birth report: {} items in {} places; {}", rep.items.size(), rep.places.size(), counts);
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
