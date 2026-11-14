#pragma once
// OrganizeStart -- Organize's landing page.
// s027: five cards. s030a (Scott, 2026-11-13: "Do this, do this now"; "a set
// of icons with arrow ... Step 1: set basics -> Step 2: set bundles"): the
// GUIDED start --
//
//   [1 Basics] -> [2 Bundles] -> [3 Projects] -> [4 Sets] -> [5 Loose files] -> [6 Closet]
//
//   Step 2 -- Bundles
//   Do this now: send 7 app files to their homes ...      [Do it] [Skip]
//                                                         Show me the details
//
// The steps come from core::guide on ONE read-only look of the whole home
// (under Busy, each time the page shows). A done step is ticked, the step to
// do lit, Sets marked "not ready yet". Click an icon to see that step. Do it
// opens the plan (a preview, then Do it through the door) or the page that
// does it -- never acts without a preview.
#include <gtkmm/box.h>

#include "core/Comics.hpp"
#include "core/Library.hpp"
#include "core/Media.hpp"
#include "core/Guide.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Gtk { class Label; class Button; class Image; class Window; }

namespace tidy {

class OrganizeStart : public Gtk::Box {
public:
    using Go = std::function<void(const std::string& page)>;
    using Organize = std::function<void(std::vector<std::pair<std::string, std::string>> items)>;   // (abs dir, type) each
    using Sort = std::function<void(std::vector<std::string> folders, std::vector<std::string> skip)>;   // s030d: a queue
    using Changed = std::function<void()>;
    OrganizeStart(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir, Go go,
                  Organize organize, Sort sort, Changed changed);

    void refresh();   // look again (under Busy): called each time the page shows
    void set_on_put(std::function<void()> f) { m_on_put = std::move(f); }   // s030g
    // s030n: Put it with... for given files, `then` when done or given up.
    void set_put_files(std::function<void(std::vector<std::string>, std::function<void()>)> f) { m_put_files = std::move(f); }
    // s030l: the sweep's places (the Closet page's own), read on the main thread.
    void set_dirs(std::function<std::string()> f) { m_dirs = std::move(f); }
    void note(const std::string& line) { m_after += (m_after.empty() ? "" : "  ") + line; }   // s030f: shown after the next look

private:
    void look();
    void show_steps();
    void show_step(int i);
    void do_it();
    void send_home(const core::guide::Step& s);
    void reshelve();                              // s030e: the comics -> their series
    std::vector<core::comics::Item> m_comics;     // s030e: from the last look
    std::vector<core::media::Item> m_media;       // s030j: from the last look
    std::vector<core::library::Item> m_books;     // s030m: from the last look

    Gtk::Window& m_parent;
    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    Go m_go;
    Organize m_organize;
    Sort m_sort;
    Changed m_changed;
    std::function<void()> m_on_put;   // s030g
    std::function<std::string()> m_dirs;   // s030l
    std::function<void(std::vector<std::string>, std::function<void()>)> m_put_files;   // s030n
    std::vector<std::string> m_unclaimed;          // s030n: abs paths, nothing claims them
    std::vector<core::ebooks::Row> m_ask;          // s030n: library ebooks with no author
    void unclaimed_at(std::size_t i);
    void ask_at(std::size_t i);

    std::vector<core::guide::Step> m_steps;
    std::set<int> m_skipped;
    int m_view = -1;           // the step shown below the strip
    bool m_looking = false;
    std::string m_after;       // a line after a Do it

    struct Chip { Gtk::Button* button = nullptr; Gtk::Image* icon = nullptr; Gtk::Label* name = nullptr; Gtk::Label* state = nullptr; };
    std::vector<Chip> m_chips;
    Gtk::Label* m_summary = nullptr;
    Gtk::Label* m_title = nullptr;
    Gtk::Label* m_state = nullptr;
    Gtk::Label* m_now = nullptr;
    Gtk::Label* m_more = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Button* m_do = nullptr;
    Gtk::Button* m_skip = nullptr;
    Gtk::Button* m_details = nullptr;
    Gtk::Box* m_panel = nullptr;
};

}  // namespace tidy
