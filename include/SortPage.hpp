#pragma once
#include "OffThread.hpp"
#include "core/Sort.hpp"

#include <gtkmm/stack.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <latch/Door.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// SortPage (s020) -- the one-step Sort. Look sweeps the home (off the window's
// thread); every loose file gets a level and a place (core::sort), grouped by
// where it goes, ALL selected. Sort -> the preview (every tag, new folder,
// move, archive) -> Do it. One undo step in Activity.
// States: start (hero + Look) | results | preview.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Label; class ListBox; class Button; }
namespace tidy { class SelectList; }

namespace tidy {

class SortPage : public Gtk::Stack {
public:
    using Changed = std::function<void()>;
    using Text = std::function<std::string()>;
    SortPage(latch::door::Door& door, std::string home, std::string data_dir, bool fixture, Text user_dirs,
             Changed changed);
    void refresh_if_shown();

private:
    struct Look {
        std::vector<core::sort::Row> rows;
        std::int64_t now = 0;
    };
    void build_start();
    void build_results();
    void build_preview();
    void fill();
    void show(Look look);
    void on_sort();
    void on_run();
    std::string shown(const std::string& path) const;

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    bool m_fixture = false;
    Text m_user_dirs;
    Changed m_changed;
    OffThread<Look> m_look;
    std::vector<core::sort::Row> m_rows;   // the list's rows, by index
    std::optional<latch::door::Plan> m_plan;
    bool m_archiving = false;
    std::vector<core::sort::Row> m_learning;   // s022: rows moved by a learned route -- a yes each at Do it

    Gtk::Label* m_summary = nullptr;
    Gtk::Label* m_status = nullptr;
    SelectList* m_list = nullptr;
    Gtk::Label* m_prev_summary = nullptr;
    Gtk::Label* m_prev_status = nullptr;
    Gtk::ListBox* m_prev_list = nullptr;
    Gtk::Button* m_prev_run = nullptr;
};

}  // namespace tidy
