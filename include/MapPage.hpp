#pragma once
#include <gtkmm/stack.h>

#include "core/Map.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// MapPage (s023f) -- the home as a treemap (core::map): a box per folder, as big
// as its files, coloured by what Tidy knows of them (cataloged, loose,
// untracked, kept whole, archived). Look reads the home under Busy; click a box
// to go in, Up / Backspace to come back; hover for counts; right-click ->
// Adopt in place..., Make it a catch-all... (the Adopt window), Open in Files.
// READ-ONLY: the page itself never writes.
// States: start (hero + Look) | results.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Label; class Button; class DrawingArea; class Popover; class Box; }

namespace tidy {

class MapPage : public Gtk::Stack {
public:
    using Text = std::function<std::string()>;
    using CatchAll = std::function<std::vector<std::string>()>;
    using Adopt = std::function<void(const std::string& rel, bool catch_all)>;
    MapPage(std::string home, bool fixture, Text user_dirs, CatchAll catch_all, Adopt adopt);
    void refresh_if_shown();
    void set_on_conform(std::function<void(const std::string& dir)> f) { m_conform = std::move(f); }   // s023h
    void set_on_scan(std::function<void(const std::string& dir)> f) { m_scan = std::move(f); }         // s023i
    void set_on_book(std::function<void(const std::string& dir)> f) { m_book = std::move(f); }         // s024b
    void set_on_layout(std::function<void(const std::string& dir)> f) { m_layout = std::move(f); }     // s024b

private:
    struct Box {
        core::map::Rect r;
        const core::map::Node* node = nullptr;   // nullptr: the files directly in this folder
    };
    void fill();
    void go(const std::string& rel);
    void layout(int w, int h);
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    const Box* at(double x, double y) const;
    void menu_for(const Box& b, double x, double y);
    std::string shown(const std::string& rel) const;

    std::string m_home;
    bool m_fixture = false;
    Text m_user_dirs;
    CatchAll m_catch_all;
    Adopt m_adopt;
    std::function<void(const std::string&)> m_conform;   // s023h
    std::function<void(const std::string&)> m_scan;      // s023i
    std::function<void(const std::string&)> m_book;      // s024b: Make it a book...
    std::function<void(const std::string&)> m_layout;    // s024b: a book -> Put in the layout...
    bool m_busy = false;
    std::shared_ptr<core::map::Node> m_root;
    std::string m_rel;                       // the folder shown
    std::vector<Box> m_boxes;
    int m_w = 0, m_h = 0;

    Gtk::Label* m_summary = nullptr;
    Gtk::Label* m_where = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Button* m_up = nullptr;
    Gtk::DrawingArea* m_area = nullptr;
    Gtk::Popover* m_menu = nullptr;
    Gtk::Box* m_menu_box = nullptr;
};

}  // namespace tidy
