#pragma once
#include <gtkmm/stack.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <latch/Door.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// DuePage (s012a on the road) -- the trigger's page: what is due to remove,
// to archive, to settle, and what else needs attention (the Closet, id
// lists). Tidy proposes; nothing acts until you check, preview and Do it.
// s013a: "To archive" rows carry a check; **Archive checked...** -> preview
// (core::archive's tag + folder + move plan) -> Do it through the door ->
// each file's fingerprint into the catalog. Undo in Activity. Remove is s014.
// GTK here; the rules are core::due and core::archive.
//
// States: start (hero + Look) | results (grouped list) | preview.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Label; class ListBox; class Button; class CheckButton; }

namespace tidy {

class DuePage : public Gtk::Stack {
public:
    using Text = std::function<std::string()>;
    using Changed = std::function<void()>;
    DuePage(latch::door::Door& door, std::string home, std::string data_dir, bool fixture, Text user_dirs,
            Changed changed);
    void refresh_if_shown();
    std::string look_line();             // the last look's line ("" before any)

private:
    void fill();
    void build_preview();
    void on_archive();                   // checked "To archive" rows -> the plan, previewed
    void on_run();                       // the previewed plan, through the door
    std::string shown(const std::string& path) const;

    latch::door::Door& m_door;
    Changed m_changed;
    std::string m_home, m_data_dir;
    bool m_fixture = false;
    Text m_user_dirs;
    std::string m_line;
    Gtk::Label*   m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
    Gtk::Label*   m_status = nullptr;
    Gtk::Button*  m_archive = nullptr;
    std::vector<std::pair<Gtk::CheckButton*, std::string>> m_arc_rows;   // (check, path)
    Gtk::Label*   m_prev_summary = nullptr;
    Gtk::ListBox* m_prev_list = nullptr;
    Gtk::Label*   m_prev_status = nullptr;
    Gtk::Button*  m_prev_run = nullptr;
    std::optional<latch::door::Plan> m_plan;   // shown, not yet run
};

}  // namespace tidy
