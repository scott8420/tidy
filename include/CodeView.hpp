#pragma once
#include "OffThread.hpp"
#include "core/Catalog.hpp"
#include "core/Code.hpp"

#include <gtkmm/stack.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <latch/Door.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// CodeView (s021) -- the Projects page's "Code projects": one row per code
// project (name, status, pushed state, last commit, size with the build and
// .git shares, archived / proved), selected like Nautilus (SelectList).
//   Set to [Active | Paused | Done | Dropped] -- the catalog's status, the
//     user's call ("that has to be up to the user").
//   Archive done...   -> a COPY into ~/Archive (core::code::plan_archive)
//   Trash archived... -> the live folder, once proved on media read back now
//   Clean build folders...
// Every act: the preview, then Do it, one undo step in Activity.
// The look (git + sizes) runs off the window's thread (OffThread).
// States: results | preview.
// ─────────────────────────────────────────────────────────────────────────────
namespace Gtk { class Label; class ListBox; class Button; }
namespace tidy { class SelectList; }

namespace tidy {

class CodeView : public Gtk::Stack {
public:
    using Changed = std::function<void()>;
    using Back = std::function<void()>;
    CodeView(latch::door::Door& door, std::string home, std::string data_dir, bool fixture, Changed changed, Back back);

    void fill();                 // look again (off the window's thread)
    void refresh_if_shown();     // after an undo in Activity
    bool looked() const { return m_looked; }

private:
    enum class Act { None, Archive, Trash, Clean };
    void build_results();
    void build_preview();
    void show(std::vector<core::code::Info> infos);
    void render();
    std::vector<core::code::Info> chosen() const;
    void set_status(const std::string& status);
    void on_act(Act act);
    void preview(core::code::Planned planned, Act act);
    void on_run();
    std::string shown(const std::string& path) const;

    latch::door::Door& m_door;
    std::string m_home, m_data_dir;
    bool m_fixture = false;
    Changed m_changed;
    Back m_back;
    OffThread<std::vector<core::code::Info>> m_look;
    bool m_looked = false;
    std::vector<core::code::Info> m_infos;      // the list's rows, by index
    std::vector<core::catalog::ProjectRow> m_rows;
    std::optional<latch::door::Plan> m_plan;
    Act m_act = Act::None;
    std::vector<core::code::Info> m_acting;     // the projects the previewed plan is for

    Gtk::Label* m_summary = nullptr;
    Gtk::Label* m_status = nullptr;
    SelectList* m_list = nullptr;
    Gtk::Label* m_prev_summary = nullptr;
    Gtk::Label* m_prev_status = nullptr;
    Gtk::ListBox* m_prev_list = nullptr;
    Gtk::Button* m_prev_run = nullptr;
};

}  // namespace tidy
