#include "Shell.hpp"
#include "core/Catalog.hpp"
#include "core/Pages.hpp"

#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/stack.h>
#include <glibmm/datetime.h>

// Shell_helpers.cpp -- HELPERS. Shared orchestration for zones and handlers.

namespace tidy {

// The line under a page's promise. A page not alive yet says when it comes;
// Activity (alive, s004) says where Tidy's records are and that nothing is done.
std::string Shell::page_note(const std::string& id) const {  // helper
    const auto* p = core::pages::find(id);
    if (id == "activity")
        return std::string("Nothing done yet. Every change Tidy makes is listed here, with the way back.\n") +
               "Records: " + m_data_dir + (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : "");
    return p ? "Comes with " + p->milestone + "." : "";
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
