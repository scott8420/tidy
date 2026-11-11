#include "IdListsWindow.hpp"
#include "Log.hpp"
#include "core/Catalog.hpp"
#include "core/FileLists.hpp"
#include "core/Scan.hpp"
#include "core/Tagging.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>

#include <ctime>

namespace tidy {

namespace dr = latch::door;
namespace fl = core::filelists;

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
std::string shown(const std::string& home, const std::string& p) {
    return p.rfind(home, 0) == 0 ? "~" + p.substr(home.size()) : p;
}
}  // namespace

IdListsWindow::IdListsWindow(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_changed(std::move(changed)) {
    set_name("idlists.window");
    set_title("Id lists");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(720, 520);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);
    box->append(*text("Id lists", "heading"));
    box->append(*text("Like CVS's Entries: each folder holding a file with an id gets a hidden .tidy-files -- "
                      "one line per file: id, size, time, name. It travels with the folder. If a file loses its "
                      "id (a FAT stick, a sync, an app's save-as), the list gives it back: same folder, same "
                      "name, same size. Hidden folders and fixes/ are never written; projects are.", "dim"));
    m_summary = text("");
    m_summary->set_name("idlists.summary");
    box->append(*m_summary);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->set_name("idlists.list");
    m_list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*m_list);
    box->append(*scroll);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_status = text("", "dim");
    m_status->set_name("idlists.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto look = Gtk::make_managed<Gtk::Button>("Look again");
    look->set_name("idlists.look");
    look->signal_clicked().connect([this] { on_look(); });
    bar->append(*look);
    auto close = Gtk::make_managed<Gtk::Button>("Close");
    close->set_name("idlists.close");
    close->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*close);
    m_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_run->set_name("idlists.run");
    m_run->add_css_class("suggested-action");
    m_run->signal_clicked().connect([this] { on_run(); });
    bar->append(*m_run);
    box->append(*bar);
    set_child(*box);
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType) {
        if (key != GDK_KEY_Escape) return false;
        set_visible(false);
        return true;
    }, false);
    add_controller(keys);
}

IdListsWindow::~IdListsWindow() = default;

void IdListsWindow::open() {
    present();
    m_status->set_text("");
    on_look();
}

void IdListsWindow::on_look() {
    m_status->set_text("");
    while (auto* r = m_list->get_row_at_index(0)) m_list->remove(*r);
    const auto files = core::scan::walk(m_home, fl::roots(m_home));
    const auto found = fl::refind(files);
    const auto want = fl::wanted(fl::with_found(files, found));
    std::size_t ids = 0;
    for (const auto& [d, v] : want)
        for (const auto& l : v) ids += !l.kept;  // kept lines have no file carrying the id
    auto work = fl::plan(found, want);
    const auto row = [this](const std::string& what, const std::string& why) {
        auto l = text(what + (why.empty() ? "" : "   REFUSED: " + why), why.empty() ? nullptr : "dim");
        l->set_margin(6);
        m_list->append(*l);
    };
    // In order (the s010d lesson); Tags and new lists are the door's.
    std::vector<std::string> made, freed;
    std::size_t refused = 0;
    for (const auto& a : work.plan.actions) {
        const auto why = m_door.refusal(a, made, freed);
        refused += !why.empty();
        row(a.verb == dr::Verb::Tag ? "Give back its id: " + shown(m_home, a.from)
                                    : "Write " + shown(m_home, a.to) + " -- " + a.reason, why);
    }
    // s010f3: lists already there -- rewritten in place, no undo (latch replace_own).
    for (const auto& x : work.replaces) {
        const auto why = m_door.replace_refusal(x.path, x.text);
        refused += !why.empty();
        row("Rewrite " + shown(m_home, x.path) + " -- " + x.reason + "  (in place, no undo)", why);
    }
    m_summary->set_text(std::to_string(ids) + " file" + (ids == 1 ? "" : "s") + " with an id, in " +
                        std::to_string(want.size()) + " folder" + (want.size() == 1 ? "" : "s") + ". " +
                        (work.kept ? std::to_string(work.kept) + " kept for files that lost their id. " : "") +
                        (work.empty() ? std::string("Every list is up to date.")
                                      : work.label() + " -- " + std::to_string(work.steps()) + (work.steps() == 1 ? " step" : " steps") +
                                            (refused ? ", " + std::to_string(refused) + " refused" : "") + "."));
    m_run->set_sensitive(!work.empty() && refused < work.steps());
    if (auto lg = log::get(log::Area::Io)) lg->info("id lists look: {} files with ids, {} folders, {} re-found, {} kept, {} steps ({} in place)",
                                                    ids, want.size(), found.size(), work.kept, work.steps(), work.replaces.size());
    m_work = work.empty() ? std::nullopt : std::optional<fl::Work>(std::move(work));
}

void IdListsWindow::on_run() {
    if (!m_work) return;
    auto work = *m_work;
    std::string fail;
    std::size_t done = 0;
    if (!work.plan.actions.empty()) {
        work.plan.previewed = true;             // shown in this window's list
        const auto r = m_door.run(work.plan);
        // Re-found ids: the catalog reads them back (Tidy's own record).
        core::catalog::Catalog cat;
        if (r.ok && cat.open(m_data_dir, true))
            core::tagging::record(cat, core::tagging::paths_of(m_door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
        done += r.done;
        if (!r.ok) fail = r.messages.empty() ? std::string("see the log") : r.messages.front();
    }
    // In place, no undo: the log line is the record (Scott, s010f3).
    std::size_t in_place = 0, refused = 0;
    if (fail.empty())
        for (const auto& x : work.replaces) {
            const auto why = m_door.replace_own(x.path, x.text);
            auto lg = log::get(log::Area::Io);
            if (why.empty()) {
                ++in_place;
                if (lg) lg->info("id list rewritten in place (no undo): {} -- {}", x.path, x.reason);
            } else {
                ++refused;
                if (lg) lg->warn("id list NOT rewritten: {} -- {}", x.path, why);
            }
        }
    if (m_changed) m_changed();
    on_look();
    if (!fail.empty()) { m_status->set_text("Not done: " + fail); return; }
    std::string s = "Done: " + work.label() + " (" + std::to_string(done + in_place) + (done + in_place == 1 ? " step)" : " steps)");
    if (done) s += " -- the door's steps undo in Activity";
    if (in_place) s += "; " + std::to_string(in_place) + " rewritten in place (no undo)";
    if (refused) s += "; " + std::to_string(refused) + " refused (see the log)";
    m_status->set_text(s);
}

}  // namespace tidy
