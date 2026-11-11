#pragma once
#include <gtkmm/stack.h>

#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// DuePage (s012a on the road) -- the trigger's page: what is due to remove,
// to archive, to settle, and what else needs attention (the Closet, id
// lists). READ-ONLY: Tidy proposes; archive and remove are later milestones
// (s013 / s014 on the road), so there are no buttons that act here.
// GTK here; the rules are core::due.
//
// States: start (hero + Look) | results (grouped list).
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Label; class ListBox; }

namespace tidy {

class DuePage : public Gtk::Stack {
public:
    using Text = std::function<std::string()>;
    DuePage(std::string home, std::string data_dir, bool fixture, Text user_dirs);
    void refresh_if_shown();
    std::string look_line();             // the last look's line ("" before any)

private:
    void fill();
    std::string shown(const std::string& path) const;

    std::string m_home, m_data_dir;
    bool m_fixture = false;
    Text m_user_dirs;
    std::string m_line;
    Gtk::Label*   m_summary = nullptr;
    Gtk::ListBox* m_list = nullptr;
};

}  // namespace tidy
