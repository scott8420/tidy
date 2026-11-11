#pragma once
#include "core/Purposes.hpp"
#include "core/Facts.hpp"

#include <gtkmm/stack.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <latch/Door.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// PurposesPage (s009) -- one decision per purpose. GTK here; the rules (the
// book, the learned hints, assign / close plans) are core::purposes. Tags go
// through latch's door only, previewed; the book (<data>/purposes.ini) is
// written by core::own. Tidy LEARNS on Do it: each file's guess confirmed or
// corrected, its folder a hint for the purpose chosen (Scott, §11.4).
//
// States: start (hero + Look) | results (purposes, teach, files without one) | preview.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Label; class ListBox; class Button; class Entry; class DropDown; class CheckButton; }

namespace tidy {

class PurposesPage : public Gtk::Stack {
public:
    using Changed = std::function<void()>;
    using Text = std::function<std::string()>;
    PurposesPage(latch::door::Door& door, std::string home, std::string data_dir, bool fixture,
                 Text user_dirs, Changed changed);
    void refresh_if_shown();

private:
    struct Row {
        core::facts::Facts file;
        core::purposes::Guess guess;
        Gtk::CheckButton* check = nullptr;
        Gtk::DropDown* pick = nullptr;
    };
    enum class Pending { None, Assign, Close };

    void build_start();
    void build_results();
    void build_preview();
    void fill();
    void save_book(const std::string& done);
    std::vector<core::facts::Facts> candidates(std::vector<std::string>* landing) const;
    std::vector<core::facts::Facts> carrying(const std::string& purpose) const;
    void on_assign();
    void on_close(const std::string& id);
    void preview(latch::door::Plan plan, Pending what);
    void on_run();
    std::string shown(const std::string& path) const;

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    bool m_fixture = false;
    Text m_user_dirs;
    Changed m_changed;

    core::purposes::Book m_book;
    std::vector<std::string> m_landing;
    std::vector<Row> m_rows;
    std::vector<std::string> m_open;          // open purpose ids, as in each row's dropdown (after "--")
    std::optional<latch::door::Plan> m_plan;
    Pending m_pending = Pending::None;
    std::vector<std::pair<std::string, std::string>> m_learn_paths;   // path -> chosen (for Do it)
    std::vector<core::purposes::Guess> m_learn_guesses;
    std::string m_closing;

    Gtk::Label*    m_summary = nullptr;
    Gtk::ListBox*  m_list = nullptr;
    Gtk::Label*    m_status = nullptr;
    Gtk::Label*    m_prev_summary = nullptr;
    Gtk::ListBox*  m_prev_list = nullptr;
    Gtk::Label*    m_prev_status = nullptr;
    Gtk::Button*   m_prev_run = nullptr;
};

}  // namespace tidy
