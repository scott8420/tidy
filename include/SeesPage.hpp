#pragma once
// SeesPage (s027a) -- Organize -> "What Tidy sees": one folder, read top down
// by core::recognise (READ-ONLY, under Busy) and shown level by level --
// bundles, projects (with their clues), sets, kinds, the Closet -- each with
// the action that already exists for it:
//   a project          -> Organize as... (its type picked when one fits)
//   a loose bundle     -> Send home... (to its kind's home: a preview, then
//                         Do it through the door, one undo step)
//   a held folder      -> Answer in Quarantine
//   sets + kinds       -> Sort these... (Sort, for this folder only)
//   the Closet         -> Open the Closet
// Likely copies are off (slow): Check copies looks again with --deep.
#include <gtkmm/box.h>

#include "core/Recognise.hpp"

#include <latch/Door.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Gtk { class Label; class Button; class Window; }

namespace tidy {

class SeesPage : public Gtk::Box {
public:
    using Go = std::function<void(const std::string& page)>;
    using Organize = std::function<void(const std::string& dir, const std::string& type)>;
    using Sort = std::function<void(const std::string& rel, std::vector<std::string> skip)>;
    using Changed = std::function<void()>;
    SeesPage(Gtk::Window& parent, latch::door::Door& door, std::string home, std::string data_dir, Go go,
             Organize organize, Sort sort, Changed changed);

    void look_at(const std::string& dir);   // abs; "" = the folder picker
    void set_on_ebooks(std::function<void(const std::string& dir)> f) { m_ebooks = std::move(f); }   // s028

    // "1.2 GB", "791 KB", "4.2 KB" (pure).
    static std::string size_of(std::uint64_t bytes);

private:
    void on_pick();
    void look(bool deep);
    void show(const core::recognise::Report& r);
    void send_home(const core::recognise::Bundle& b);
    Gtk::Box* section(const std::string& title, const std::string& sub);
    Gtk::Box* row(Gtk::Box* into, const std::string& title, const std::string& detail);
    Gtk::Button* button(Gtk::Box* into, const std::string& label, std::function<void()> f);
    std::string rel_of(const std::string& abs) const;

    Gtk::Window& m_parent;
    latch::door::Door& m_door;
    std::string m_home, m_data_dir, m_dir;
    std::string m_after;
    std::size_t m_books = 0;   // s028: loose ebooks under the folder   // the status after the next show (a Do it's result)
    Go m_go;
    Organize m_organize;
    Sort m_sort;
    Changed m_changed;
    std::function<void(const std::string&)> m_ebooks;   // s028: Ebooks from files
    std::shared_ptr<core::recognise::Report> m_report;

    Gtk::Label* m_where = nullptr;
    Gtk::Label* m_summary = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Button* m_again = nullptr;
    Gtk::Button* m_copies = nullptr;
    Gtk::Box* m_body = nullptr;
};

}  // namespace tidy
