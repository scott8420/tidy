#pragma once
// ShapeWindow (s023i) -- a template made from a scan (Scott, 2026-11-11). The
// template starts from WHAT IS THERE, not from an empty box of routes:
//   1. Scan one project folder, or a folder of many (each sub-folder a project)
//      -> every file type with counts (a numbered run, A0001.png .., its own
//      row), and every folder found.
//   2. Keep / add the folders; give each file type a folder from a list.
//   3. Tick the source types (they build the others) -> the project's top.
//   4. Name, and where projects of this kind live (the category).
//   5. Save and conform -> Conform's preview of the scanned folders (s023h),
//      nothing moved until its Do it.
// READ-ONLY until Save (Tidy's own template file, through core::own).
#include <gtkmm/window.h>

#include "core/Shape.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Gtk { class Label; class ListBox; class Button; class DropDown; class CheckButton; class Entry; }

namespace tidy {

class ShapeWindow : public Gtk::Window {
public:
    // A template saved: its name, the folder scanned, each sub-folder a project; conform = go on to Conform.
    using Saved = std::function<void(const std::string& name, const std::string& folder, bool each, bool conform)>;
    ShapeWindow(Gtk::Window& parent, std::string home, std::string data_dir, Saved saved);
    ~ShapeWindow() override;

    void open(const std::string& folder = "");   // present; a folder picked elsewhere (the Map) is scanned at once

private:
    struct TypeRow {
        std::string key;
        Gtk::DropDown* to = nullptr;
        Gtk::CheckButton* source = nullptr;
        std::vector<std::string> choices;   // what the drop-down shows now: "" (leave), kTop, folders
    };
    void on_pick();
    void on_scan();
    void show_scan();
    void fill_folders();
    void refill_choices();
    std::vector<std::string> kept() const;
    void on_add_folder();
    void on_save(bool conform);

    std::string m_home, m_data_dir;
    Saved m_saved;
    std::string m_folder;          // absolute
    bool m_busy = false, m_filling = false;
    core::shape::Scan m_scan;
    std::map<std::string, bool> m_keep;                    // folder -> kept
    std::map<std::string, core::shape::Pick> m_picks;      // row key -> its pick
    std::vector<TypeRow> m_rows;

    Gtk::Label* m_where = nullptr;
    Gtk::CheckButton* m_each = nullptr;
    Gtk::Label* m_summary = nullptr;
    Gtk::ListBox* m_types = nullptr;
    Gtk::ListBox* m_folders = nullptr;
    Gtk::Entry* m_new_folder = nullptr;
    Gtk::Entry* m_name = nullptr;
    Gtk::Entry* m_category = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Button* m_save = nullptr;
    Gtk::Button* m_save_conform = nullptr;
};

}  // namespace tidy
