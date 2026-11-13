#pragma once
// QuarantineWindow (s025b) -- the answers to the suspect scan (REBUILD 3,
// 13.1). Look (core::suspects, read-only, under Busy) -> one row per
// extension (groups 1, 2) or per folder (groups 3, 4), each with its answer:
// Not answered / A bundle / An ordinary folder / Not now. Save writes Tidy's
// own quarantine.txt (core::own) and nothing else -- no user file moves -- then
// Tidy re-reads it: bundle extensions join latch's extras, held folders are
// kept out of every walk. Answered rows stay listed so an answer can change.
// GTK here; the rules are core::quarantine + core::suspects. Becomes Review's
// Quarantine in the new shell (s027).
#include <gtkmm/window.h>

#include "core/Quarantine.hpp"
#include "core/Suspects.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; class DropDown; }

namespace tidy {

class QuarantineWindow : public Gtk::Window {
public:
    using Changed = std::function<void()>;   // after a Save: the Map etc. may look again
    QuarantineWindow(Gtk::Window& parent, std::string home, std::string data_dir, Changed changed);
    ~QuarantineWindow() override;

    void open();                             // present + Look

    // One question: an extension (groups 1, 2) or a folder (groups 3, 4).
    struct Row {
        bool by_ext = true;
        std::string key;                     // ".itmsp" or "Documents/DBs"
        std::string title, detail;
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
