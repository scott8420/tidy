#pragma once
#include <gtkmm/stack.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <latch/Door.hpp>

#include "core/Backup.hpp"
#include "core/Settings.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// ArchivePage (s013b on the road) -- what is in ~/Archive (the catalog's
// `archived` rows), and on which media each file is PROVED. A media folder
// (Settings -> Backup media) + **Back up...** -> preview (new folders, copies,
// "already there -- read back only") -> Do it through the door -> each copy
// read back from the disk, SHA-256 compared -> "on <label> <day>". Undo in
// Activity (copies into that disk's own trash; proofs dropped).
// GTK here; the rules are core::backup.
//
// States: start (hero + Look) | results (the list + media bar) | preview.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Label; class ListBox; class Button; class DropDown; class StringList; }

namespace tidy {

class ArchivePage : public Gtk::Stack {
public:
    using Media = std::function<std::vector<core::settings::Media>()>;
    using Changed = std::function<void()>;
    ArchivePage(latch::door::Door& door, std::string home, std::string data_dir, bool fixture, Media media,
                Changed changed);
    void refresh_if_shown();

private:
    void fill();
    void fill_media();                   // the dropdown + the chosen media's state
    void build_preview();
    void on_backup();                    // the chosen media -> the plan, previewed
    void on_run();                       // the previewed plan, through the door, then the proofs
    void on_remove();                    // s014: proved files -> core::removal (read back NOW), previewed
    void run_remove(latch::door::Plan plan);
    std::optional<core::settings::Media> chosen() const;
    std::string shown(const std::string& path) const;

    latch::door::Door& m_door;
    Changed m_changed;
    std::string m_home, m_data_dir;
    bool m_fixture = false;
    Media m_media;
    std::vector<core::settings::Media> m_shown_media;   // what the dropdown lists, in order
    Gtk::Label*    m_summary = nullptr;
    Gtk::ListBox*  m_list = nullptr;
    Gtk::Label*    m_status = nullptr;
    Gtk::DropDown* m_pick = nullptr;
    Glib::RefPtr<Gtk::StringList> m_pick_names;
    Gtk::Label*    m_pick_state = nullptr;
    Gtk::Button*   m_backup = nullptr;
    Gtk::Label*    m_prev_summary = nullptr;
    Gtk::ListBox*  m_prev_list = nullptr;
    Gtk::Label*    m_prev_status = nullptr;
    Gtk::Button*   m_prev_run = nullptr;
    std::optional<latch::door::Plan> m_plan;              // shown, not yet run
    bool m_busy = false;                                  // s023e: a plan or a run is under Busy
    void preview_rows(const std::vector<std::pair<std::string, const char*>>& rows);   // s023e: capped
    std::vector<core::backup::Check> m_checks;            // shown with it
    std::optional<core::settings::Media> m_plan_media;
    bool m_filling = false;
    Gtk::Button*   m_remove = nullptr;                    // s014
    bool m_removing = false;
    std::vector<std::string> m_rem_paths;
};

}  // namespace tidy
