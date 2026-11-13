#pragma once
// QuarantinePage (s025b) -- the answers to the suspect scan (REBUILD 3,
// 13.1). Look (core::suspects, read-only, under Busy) -> one row per
// extension (groups 1, 2) or per folder (groups 3, 4), each with its answer:
// Not answered / A bundle / An ordinary folder / Not now. Save writes Tidy's
// own quarantine.txt (core::own) -- then (s025b1) "Don't touch" makes a
// folder Hands off, and "To the Trash" shows a preview first, then trashes
// through the door (one undo step in Activity); nothing else moves -- then
// Tidy re-reads it: bundle extensions join latch's extras, held folders are
// kept out of every walk. Answered rows stay listed so an answer can change.
// GTK here; the rules are core::quarantine + core::suspects. Becomes Review's
// Quarantine in the new shell (s027).
#include <gtkmm/box.h>
#include <gtkmm/window.h>

#include "core/Quarantine.hpp"
#include "core/Suspects.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; class DropDown; }

namespace tidy {

class QuarantinePage : public Gtk::Box {
public:
    using Changed = std::function<void()>;   // after a Save: the Map etc. may look again
    QuarantinePage(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir, Changed changed);
    ~QuarantinePage() override;

    void open();                             // the first time shown: Look

    // One question: an extension (groups 1, 2) or a folder (groups 3, 4).
    struct Row {
        bool by_ext = true;
        std::string key;                     // ".itmsp" or "Documents/DBs"
        std::string title, detail;
        std::vector<std::string> rels;       // the folders it covers (the Trash takes them all)
        core::quarantine::Answer was = core::quarantine::Answer::None, now = core::quarantine::Answer::None;
        Gtk::DropDown* pick = nullptr;
    };
    // The rows a report + the saved answers give: unanswered first, then
    // not now, then answered (bundle, ordinary -- those come from the file
    // when the scan no longer lists them). Pure.
    static std::vector<Row> rows_of(const core::suspects::Report& r, const core::quarantine::Answers& a);
    // The answers the rows say, over `a` (None removes one). Pure.
    static core::quarantine::Answers merged(const core::quarantine::Answers& a, const std::vector<Row>& rows);

private:
    void on_look();
    void show();
    void on_save();
    void update_bar();
    void preview(latch::door::Plan plan);   // s025b1: the Trash rows, shown before Do it
    void save_answers(const std::string& done_text);
    void answer_now(std::size_t i);         // s025b2: saved the moment it is picked
    void back();

    Gtk::Window& m_parent;                     // s027: Busy sits over the main window
    bool m_looked = false;
    latch::door::Door& m_door;
    std::optional<latch::door::Plan> m_plan;   // shown, not yet run
    Gtk::Button*  m_look = nullptr;
    std::string m_home, m_data_dir;
    Changed m_changed;
    std::shared_ptr<core::suspects::Report> m_report;
    std::vector<Row> m_rows;
    Gtk::Label*   m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label*   m_status = nullptr;
    Gtk::Button*  m_save = nullptr;
};

}  // namespace tidy
