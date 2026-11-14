// s030g -- Put it with... (Scott: "how to just find a file or select one and say
// it is in this folder"). Pick the files (or they come from a Sort row), pick
// the folder, see the plan, Do it -- one undo; each moved file teaches its route.
#include "Shell.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "OrganizeStart.hpp"
#include "SortPage.hpp"
#include "core/Catalog.hpp"
#include "core/Comics.hpp"
#include "core/Put.hpp"
#include "core/Tagging.hpp"

#include <giomm/file.h>
#include <giomm/liststore.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/filedialog.h>

#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;
namespace dr = latch::door;

namespace tidy {

void Shell::on_put(std::vector<std::string> files) {
    if (files.empty()) {
        auto dlg = Gtk::FileDialog::create();
        dlg->set_title("Put it with... -- which files?");
        dlg->set_modal(true);
        dlg->set_initial_folder(Gio::File::create_for_path(m_home + "/Downloads"));
        dlg->open_multiple(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
            std::vector<std::string> picked;
            try {
                const auto list = dlg->open_multiple_finish(res);
                for (const auto& f : list) if (f && !f->get_path().empty()) picked.push_back(f->get_path());
            } catch (const Glib::Error&) { return; }
            if (!picked.empty()) put_pick_folder(std::move(picked));
        });
        return;
    }
    put_pick_folder(std::move(files));
}

void Shell::put_pick_folder(std::vector<std::string> files) {
    // Start the folder picker somewhere likely: a comic -> the Comics shelf.
    std::string start = m_home;
    bool all_comics = true;
    for (const auto& f : files) all_comics = all_comics && core::comics::is_comic(f);
    std::error_code ec;
    if (all_comics && fs::is_directory(m_home + "/" + core::comics::kShelf, ec)) start = m_home + "/" + core::comics::kShelf;
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title(files.size() == 1 ? "Put " + fs::path(files.front()).filename().string() + " with... -- which folder?"
                                     : "Put " + std::to_string(files.size()) + " files with... -- which folder?");
    dlg->set_modal(true);
    dlg->set_initial_folder(Gio::File::create_for_path(start));
    dlg->select_folder(*this, [this, dlg, files](const Glib::RefPtr<Gio::AsyncResult>& res) {
        std::string to;
        try {
            const auto f = dlg->select_folder_finish(res);
            if (!f || f->get_path().empty()) return;
            to = f->get_path();
        } catch (const Glib::Error&) { return; }
        std::string rel;
        if (const auto why = core::put::folder_rel(m_home, to, &rel); !why.empty()) {
            auto a = Gtk::AlertDialog::create("Not that folder");
            a->set_detail(why);
            a->show(*this);
            return;
        }
        put_preview(files, rel);
    });
}

void Shell::put_preview(const std::vector<std::string>& files, const std::string& rel) {
    auto planned = core::put::plan(m_home, files, rel);
    std::string text, refused;
    for (const auto& l : planned.lines) text += "\n  " + l;
    for (const auto& s : planned.skipped) text += "\n  (left: " + s + ")";
    for (const auto& r : planned.renamed) text += "\n  (taken: " + r + ")";
    std::vector<std::string> made;
    for (const auto& a : planned.plan.actions) {
        if (const auto why = m_door->refusal(a, made); !why.empty()) refused += "\n  refused: " + why;
        if (a.verb == dr::Verb::MakeDir) made.push_back(a.to);
    }
    const bool any = !planned.lines.empty();
    auto dlg = Gtk::AlertDialog::create(any ? planned.plan.label : "Nothing to put");
    dlg->set_detail((any ? "Tidy remembers this for files named like these. One undo in Review → Activity.\n" : "") +
                    text + refused);
    if (any && refused.empty()) {
        dlg->set_buttons({"Back", "Do it"});
        dlg->set_default_button(1);
    } else {
        dlg->set_buttons({"Back"});
    }
    dlg->set_cancel_button(0);
    auto keep = std::make_shared<dr::Plan>(std::move(planned.plan));
    dlg->choose(*this, [this, dlg, keep, files, rel, ok = any && refused.empty()](const Glib::RefPtr<Gio::AsyncResult>& res) {
        int pick = 0;
        try { pick = dlg->choose_finish(res); } catch (const Glib::Error&) { return; }
        if (pick != 1 || !ok) return;
        keep->previewed = true;
        const std::string data = m_data_dir, home = m_home;
        auto* door = m_door.get();
        busy::door(*this, *m_door, std::move(*keep), "Put it with",
            [this, files, rel, data, home](const dr::Result& r) {
                std::string why;
                const auto taught = core::put::learn(home, data, files, rel, &why);
                if (auto lg = log::get(log::Area::Door))
                    lg->info("put: {} done, {} failed -> ~/{}; {} taught{}", r.done, r.failed, rel, taught,
                             why.empty() ? "" : " (routes not saved: " + why + ")");
                refresh_activity();
                if (m_org_start && std::string(m_stack.get_visible_child_name()) == "organize") {
                    m_org_start->note(r.ok ? "Put with ~/" + rel + " -- one undo in Review → Activity." : "Put: not done -- see the log.");
                    m_org_start->refresh();
                }
                if (m_sort_page && std::string(m_stack.get_visible_child_name()) == "sort") m_sort_page->look_again();   // fill()
            },
            [data, door](const dr::Result& r) {
                core::catalog::Catalog cat;
                if (cat.open(data, true))
                    core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch),
                                          static_cast<std::int64_t>(std::time(nullptr)));
            });
    });
}

}  // namespace tidy
