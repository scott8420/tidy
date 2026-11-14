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
    void set_on_adopt(std::function<void()> f) { m_on_adopt = std::move(f); }   // s023: the Adopt folders window
    // s027a: Sort only the loose files under `rel` (swept there even if it is
    // not a catch-all), leaving out anything inside `skip` (its projects,
    // bundles). "" = the whole home again.
    void look_in(const std::string& rel, std::vector<std::string> skip = {});
    void look_again() { fill(); }   // s030g: after a Put it with...
    // s030g: right-click -> Put it with... (the selected files' absolute paths).
    void set_on_put(std::function<void(std::vector<std::string> files)> f) { m_on_put_many = std::move(f); }
    // s030b: called once after the next good Do it (Start: back to the steps).
    void set_on_done_once(std::function<void()> f) { m_done_once = std::move(f); }
    // s030d (Scott: "the same thing happens in Loose files"): folders one after
    // another from Start -- after a good Do it (or Skip this folder) the next one;
    // a folder with nothing to sort is passed by itself; the last -> when_done.
    void start_queue(std::vector<std::string> folders, std::vector<std::string> skip, std::function<void()> when_done);
    // s030f: told of each queue folder found with nothing to move.
    void set_on_settled(std::function<void(const std::string& rel)> f) { m_on_settled = std::move(f); }

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
    void on_move_now();                       // s023g: unread downloads, moved on as if read
    void preview(std::vector<core::sort::Row> chosen);
    void on_run();
    std::string shown(const std::string& path) const;

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    bool m_fixture = false;
    Text m_user_dirs;
    Changed m_changed;
    std::function<void()> m_on_adopt;   // s023
    std::function<void()> m_done_once;  // s030b
    std::vector<std::string> m_queue;   // s030d
    std::size_t m_qi = 0;
    std::function<void()> m_q_done;
    std::function<void(const std::string&)> m_on_settled;   // s030f
    std::function<void(const std::string&)> m_on_put;       // s030g (unused)
    std::function<void(std::vector<std::string>)> m_on_put_many;   // s030g
    Gtk::Button* m_qskip = nullptr;
    void q_load(std::size_t i);
    void q_next();
    void q_stop(bool back);
    OffThread<Look> m_look;
    std::string m_scope;                       // s027a: "" = the whole home
    std::vector<std::string> m_skip;           // s027a: rels left out of a scoped look
    Gtk::Button* m_whole = nullptr;
    std::vector<core::sort::Row> m_rows;   // the list's rows, by index
    std::optional<latch::door::Plan> m_plan;
    bool m_archiving = false;
    bool m_busy = false;   // s023a: a run is under its Busy window
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
