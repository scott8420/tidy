#include "OrganizeStart.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Catalog.hpp"
#include "core/Own.hpp"
#include "core/Settings.hpp"
#include "core/Birth.hpp"
#include "core/Closet.hpp"
#include "core/Home.hpp"
#include "core/Homing.hpp"
#include "core/Recognise.hpp"
#include "core/Tagging.hpp"

#include <glibmm/main.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/window.h>

#include <ctime>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace tidy {

namespace gd = core::guide;
namespace rc = core::recognise;
namespace dr = latch::door;

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr, float x = 0.0f) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(x);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
const char* mark(gd::State s) {
    switch (s) {
        case gd::State::Done:     return "✓ done";
        case gd::State::Now:      return "● now";
        case gd::State::NotReady: return "not ready";
        case gd::State::Skipped:  return "skipped";
        case gd::State::Todo:     return "";
    }
    return "";
}
}  // namespace

OrganizeStart::OrganizeStart(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir, Go go,
                             Organize organize, Sort sort, Changed changed)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_parent(parent), m_door(door), m_home(std::move(home)),
      m_data_dir(std::move(data_dir)), m_go(std::move(go)), m_organize(std::move(organize)), m_sort(std::move(sort)),
      m_changed(std::move(changed)) {
    set_name("organize.start");
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    col->set_margin_start(32);
    col->set_margin_end(32);
    col->set_margin_top(24);
    col->set_margin_bottom(24);
    col->set_halign(Gtk::Align::CENTER);

    // The head.
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
    auto emblem = Gtk::make_managed<Gtk::Image>();
    emblem->set_from_icon_name("tidy-hero-organize");
    emblem->set_pixel_size(56);
    head->append(*emblem);
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->set_valign(Gtk::Align::CENTER);
    titles->append(*text("Organize your home", "hero-title"));
    titles->append(*text("Six steps, in order. Do the lit one. Nothing moves until you have seen the plan, "
                         "and every Do it is one Undo in Review \u2192 Activity.", "hero-sub"));
    head->append(*titles);
    col->append(*head);

    // The strip: icons with arrows.
    auto strip = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    strip->set_name("organize.strip");
    strip->set_halign(Gtk::Align::CENTER);
    strip->set_margin_top(8);
    const char* names[] = {"Basics", "Bundles", "Projects", "Sets", "Loose files", "Closet"};
    const char* icons[] = {"tidy-settings-symbolic", "package-x-generic-symbolic", "folder-symbolic",
                           "view-grid-symbolic", "text-x-generic-symbolic", "user-trash-symbolic"};
    for (int i = 0; i < 6; ++i) {
        if (i) {
            auto arrow = Gtk::make_managed<Gtk::Image>();
            arrow->set_from_icon_name("go-next-symbolic");
            arrow->add_css_class("dim");
            arrow->set_valign(Gtk::Align::CENTER);
            strip->append(*arrow);
        }
        Chip c;
        c.button = Gtk::make_managed<Gtk::Button>();
        c.button->set_name("organize.step." + std::to_string(i + 1));
        c.button->add_css_class("flat");
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        c.icon = Gtk::make_managed<Gtk::Image>();
        c.icon->set_from_icon_name(icons[i]);
        c.icon->set_pixel_size(32);
        box->append(*c.icon);
        c.name = text(std::to_string(i + 1) + " " + names[i], "card-title", 0.5f);
        c.name->set_wrap(false);
        box->append(*c.name);
        c.state = text("", "card-sub", 0.5f);
        c.state->set_wrap(false);
        box->append(*c.state);
        box->set_size_request(92, -1);
        c.button->set_child(*box);
        c.button->signal_clicked().connect([this, i] { show_step(i); });
        strip->append(*c.button);
        m_chips.push_back(c);
    }
    col->append(*strip);

    m_summary = text("", "dim", 0.5f);
    m_summary->set_name("organize.start.summary");
    col->append(*m_summary);

    // The panel: the one thing to do.
    m_panel = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    m_panel->set_name("organize.panel");
    m_panel->add_css_class("card");
    m_panel->set_size_request(620, -1);
    m_panel->set_halign(Gtk::Align::CENTER);
    auto top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    m_title = text("", "card-title");
    m_title->set_hexpand(true);
    top->append(*m_title);
    m_state = text("", "card-sub", 1.0f);
    top->append(*m_state);
    m_panel->append(*top);
    m_now = text("", "card-big");
    m_now->set_name("organize.panel.now");
    m_panel->append(*m_now);
    m_more = text("", "dim");
    m_panel->append(*m_more);
    auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row->set_margin_top(6);
    m_do = Gtk::make_managed<Gtk::Button>("Do it");
    m_do->set_name("organize.panel.do");
    m_do->add_css_class("suggested-action");
    m_do->signal_clicked().connect([this] { do_it(); });
    row->append(*m_do);
    m_skip = Gtk::make_managed<Gtk::Button>("Skip");
    m_skip->set_name("organize.panel.skip");
    m_skip->signal_clicked().connect([this] {
        if (m_view < 0) return;
        m_skipped.insert(m_view);
        m_view = -1;   // the panel follows to the step lit next
        show_steps();
    });
    row->append(*m_skip);
    auto gap = Gtk::make_managed<Gtk::Box>();
    gap->set_hexpand(true);
    row->append(*gap);
    m_details = Gtk::make_managed<Gtk::Button>("Show me the details");
    m_details->set_name("organize.panel.details");
    m_details->add_css_class("flat");
    m_details->signal_clicked().connect([this] {
        if (m_view >= 0 && m_view < static_cast<int>(m_steps.size()) && m_go) m_go(m_steps[m_view].details);
    });
    row->append(*m_details);
    m_panel->append(*row);
    col->append(*m_panel);

    auto foot = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    foot->set_halign(Gtk::Align::CENTER);
    m_status = text("", "dim", 0.5f);
    m_status->set_wrap(false);
    m_status->set_name("organize.start.status");
    foot->append(*m_status);
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("organize.start.again");
    again->add_css_class("flat");
    again->signal_clicked().connect([this] { look(); });
    foot->append(*again);
    auto put = Gtk::make_managed<Gtk::Button>("Put a file\u2026");   // s030g: any file, any folder
    put->set_name("organize.start.put");
    put->set_tooltip_text("Pick files, pick the folder they belong in -- Tidy remembers for next time");
    put->signal_clicked().connect([this] { if (m_on_put) m_on_put(); });
    foot->append(*put);
    col->append(*foot);

    scroll->set_child(*col);
    append(*scroll);
    m_summary->set_text("Looking over your home…");
}

void OrganizeStart::refresh() {
    // The window may not be drawn yet (Tidy opens on this page): look a moment later.
    Glib::signal_timeout().connect_once([this] { look(); }, 200);
}

void OrganizeStart::look() {
    if (m_looking) return;
    m_looking = true;
    const std::string home = m_home, data = m_data_dir;
    auto out = std::make_shared<rc::Report>();
    auto saved = std::make_shared<bool>(false);
    auto comics = std::make_shared<std::vector<core::comics::Item>>();
    auto settled = std::make_shared<std::set<std::string>>();
    auto media = std::make_shared<std::vector<core::media::Item>>();
    auto closet_n = std::make_shared<long long>(-1);
    auto books = std::make_shared<std::vector<core::library::Item>>();
    auto unclaimed = std::make_shared<std::vector<std::string>>();
    auto ask = std::make_shared<std::vector<core::ebooks::Row>>();
    const std::string dirs = m_dirs ? m_dirs() : std::string();
    const bool have_dirs = static_cast<bool>(m_dirs);
    busy::start(m_parent, "Organize -- looking over your home", "Reading your home top down (nothing moves)",
                [out, saved, comics, settled, media, closet_n, books, unclaimed, ask, dirs, have_dirs, home, data] {
                    *out = rc::scan(home, home, data, false);
                    *books = core::library::find(home, *out, ask.get());   // s030m; s030n: the authorless asked
                    if (have_dirs) {   // s030l: exactly what the Closet page will list
                        const auto st = core::settings::parse(core::own::read(data, core::settings::kFile));
                        const auto now = static_cast<std::int64_t>(std::time(nullptr));
                        const auto rep = core::birth::report(home, core::birth::sweep(home, dirs, st.catch_all), now, st.old_years);
                        const auto v = core::closet::view(rep, now, st);
                        *closet_n = static_cast<long long>(v.total());
                        // s030n: the files nothing claims that the Closet page does NOT list, less those you left.
                        std::set<std::string> on_page;
                        for (const auto& b : v.proposed) on_page.insert(b.file.path);
                        for (const auto& b : v.waiting) on_page.insert(b.file.path);
                        const auto gone = gd::left(data);
                        for (const auto& rel : out->unclaimed) {
                            const std::string abs = home + "/" + rel;
                            if (!on_page.count(abs) && !gone.count(rel)) unclaimed->push_back(abs);
                        }
                    }
                    *media = core::media::find(home);   // s030j
                    *settled = gd::settled(data);   // s030f
                    *saved = core::home::saved(data);
                    *comics = core::comics::find(home);   // s030e
                },
                [this, out, saved, comics, settled, media, closet_n, books, unclaimed, ask] {
                    m_unclaimed = std::move(*unclaimed);
                    m_ask = std::move(*ask);
                    m_books = std::move(*books);
                    m_media = std::move(*media);
                    m_comics = std::move(*comics);
                    m_looking = false;
                    gd::Inputs in;
                    for (const auto& b : out->bundles) in.held += b.held;
                    in.home_saved = *saved;
                    in.comics = m_comics.size();
                    in.settled = *settled;
                    in.closet = *closet_n;
                    in.books = m_books.size();
                    in.unclaimed = m_unclaimed.size();
                    in.authorless = m_ask.size();
                    for (const auto& m : m_media) {
                        if (m.what == "its subtitles") continue;
                        if (m.to.rfind(core::media::kMovies, 0) == 0) ++in.movies; else ++in.episodes;
                    }
                    {
                        std::set<std::string> ss;
                        for (const auto& c : m_comics) ss.insert(c.series);
                        in.series = ss.size();
                    }
                    in.skipped = m_skipped;
                    m_steps = gd::steps(*out, in);
                    m_view = -1;
                    show_steps();
                    if (auto lg = log::get(log::Area::Shell))
                        lg->info("start: step {} now ({} ms)", gd::current(m_steps) + 1, out->ms);
                });
}

void OrganizeStart::show_steps() {
    if (m_steps.empty()) return;
    // A skip re-works the states from the counts (no new look).
    for (std::size_t i = 0; i < m_steps.size(); ++i) {
        auto& s = m_steps[i];
        if (m_skipped.count(static_cast<int>(i))) s.state = gd::State::Skipped;
        else if (s.id == gd::Id::Sets && !s.todo) s.state = gd::State::NotReady;
        else s.state = s.todo ? gd::State::Todo : gd::State::Done;
    }
    for (auto& s : m_steps)
        if (s.state == gd::State::Todo) { s.state = gd::State::Now; break; }

    const int now = gd::current(m_steps);
    for (std::size_t i = 0; i < m_chips.size() && i < m_steps.size(); ++i) {
        auto& c = m_chips[i];
        const auto st = m_steps[i].state;
        c.state->set_text(mark(st));
        if (st == gd::State::Now) c.button->add_css_class("selected");
        else c.button->remove_css_class("selected");
        c.button->set_opacity(st == gd::State::Done || st == gd::State::NotReady || st == gd::State::Skipped ? 0.55 : 1.0);
    }
    if (now < 0) {
        m_summary->set_text(m_skipped.empty() ? "Your home is tidy. Nothing to do right now."
                                              : "Nothing left but the steps you skipped -- click one to go back to it.");
    } else {
        m_summary->set_text("Step " + std::to_string(now + 1) + " of 6 is next.");
    }
    show_step(m_view >= 0 ? m_view : (now >= 0 ? now : 0));
    if (now < 0 && m_skipped.empty()) {
        m_title->set_text("All done");
        m_state->set_text("");
        m_now->set_text("Your home is tidy.");
        m_more->set_text("Tidy keeps watching; come back when new files arrive.");
        m_do->set_visible(false);
        m_skip->set_visible(false);
    }
    m_status->set_text(m_after.empty() ? "Read-only until you press Do it." : m_after);
    m_after.clear();
}

void OrganizeStart::show_step(int i) {
    if (i < 0 || i >= static_cast<int>(m_steps.size())) return;
    m_view = i;
    const auto& s = m_steps[i];
    for (std::size_t k = 0; k < m_chips.size(); ++k) {
        if (static_cast<int>(k) == i) m_chips[k].name->add_css_class("accent");
        else m_chips[k].name->remove_css_class("accent");
    }
    m_title->set_text("Step " + std::to_string(i + 1) + " -- " + s.title);
    m_state->set_text(gd::state_text(s.state));
    const bool can = s.act != gd::Act::None && s.todo > 0;
    if (s.state == gd::State::Done) {
        m_now->set_text("Done -- nothing to do here.");
        m_more->set_text("");
    } else {
        m_now->set_text((s.state == gd::State::Now ? "Do this now: " : "") + s.now);
        m_more->set_text(s.after);
    }
    m_more->set_visible(!m_more->get_text().empty());
    m_do->set_visible(can);
    m_skip->set_visible(s.state == gd::State::Now || s.state == gd::State::NotReady || s.state == gd::State::Todo);
    m_skip->set_label(s.state == gd::State::NotReady ? "Skip for now" : "Skip");
    m_details->set_visible(!s.details.empty());
}

void OrganizeStart::do_it() {
    if (m_view < 0 || m_view >= static_cast<int>(m_steps.size())) return;
    const auto s = m_steps[m_view];
    switch (s.act) {
        case gd::Act::Quarantine: if (m_go) m_go("quarantine"); break;
        case gd::Act::HomeDesign: if (m_go) m_go("templates"); break;
        case gd::Act::Closet:     if (m_go) m_go("closet"); break;
        case gd::Act::SendHome:   send_home(s); break;
        case gd::Act::OrganizeAs: {
            std::vector<std::pair<std::string, std::string>> items;
            for (const auto& [rel, type] : s.items) items.emplace_back(m_home + "/" + rel, type);
            if (items.empty()) items.emplace_back(m_home + "/" + s.folder, s.type);
            if (m_organize) m_organize(std::move(items));
            break;
        }
        case gd::Act::SortFolder: if (m_sort) m_sort(s.folders.empty() ? std::vector<std::string>{s.folder} : s.folders, s.whole); break;
        case gd::Act::Reshelve:   reshelve(); break;
        case gd::Act::Unclaimed:  unclaimed_at(0); break;   // s030n
        case gd::Act::AskAuthors: ask_at(0); break;         // s030n
        case gd::Act::None: break;
    }
}

void OrganizeStart::reshelve() {   // s030e comics; s030j movies + series -- one plan, one undo
    if (m_comics.empty() && m_media.empty() && m_books.empty()) return;
    core::comics::Planned planned;
    if (!m_comics.empty()) planned = core::comics::reshelve(m_home, m_comics);
    std::map<std::string, std::size_t> per;
    for (const auto& c : m_comics) ++per[c.series];
    std::string lines;
    if (!per.empty()) lines += "\nComics:";
    for (const auto& [s, n] : per) lines += "\n  " + s + "  (" + std::to_string(n) + ")";
    if (!m_media.empty()) {
        auto mp = core::media::shelve(m_home, m_media);
        lines += "\nMovies and series:";
        std::size_t shown = 0;
        for (const auto& l : mp.lines) {
            if (++shown > 14) { lines += "\n  ... and " + std::to_string(mp.lines.size() - 14) + " more"; break; }
            lines += "\n  " + l;
        }
        planned.renamed.insert(planned.renamed.end(), mp.renamed.begin(), mp.renamed.end());
        planned.plan.actions.insert(planned.plan.actions.end(), mp.plan.actions.begin(), mp.plan.actions.end());
    }
    if (!m_books.empty()) {   // s030m
        auto lp = core::library::shelve(m_home, m_books);
        lines += "\nBooks (from Documents/EBooks):";
        std::size_t shown = 0;
        for (const auto& l : lp.lines) {
            if (++shown > 14) { lines += "\n  ... and " + std::to_string(lp.lines.size() - 14) + " more"; break; }
            lines += "\n  " + l;
        }
        planned.trashed.insert(planned.trashed.end(), lp.trashed.begin(), lp.trashed.end());
        planned.renamed.insert(planned.renamed.end(), lp.renamed.begin(), lp.renamed.end());
        planned.plan.actions.insert(planned.plan.actions.end(), lp.plan.actions.begin(), lp.plan.actions.end());
    }
    const std::size_t total = m_comics.size() + m_media.size() + m_books.size();
    planned.plan.label = "Shelves: " + std::to_string(total) + (total == 1 ? " thing" : " things");
    std::string refused;
    std::vector<std::string> made;
    for (const auto& a : planned.plan.actions) {
        if (const auto why = m_door.refusal(a, made); !why.empty()) refused += "\n  refused: " + why;
        if (a.verb == dr::Verb::MakeDir) made.push_back(a.to);
    }
    std::string trash;
    if (!planned.trashed.empty())
        trash = "\n\nLeft empty -> the Trash: " + std::to_string(planned.trashed.size()) +
                (planned.trashed.size() == 1 ? " folder" : " folders") + " (e.g. ~/" + planned.trashed.front() + ")";
    for (const auto& r : planned.renamed) trash += "\n  (taken: " + r + ")";
    auto dlg = Gtk::AlertDialog::create("Shelves -- " + std::to_string(total) + " to ~/Entertainment");
    dlg->set_detail("Comics by series, movies by title, series by show and season; files keep their names. "
                    "One undo step in Review \u2192 Activity.\n" + lines +
                    trash + refused);
    if (refused.empty()) {
        dlg->set_buttons({"Back", "Do it"});
        dlg->set_default_button(1);
    } else {
        dlg->set_buttons({"Back"});
    }
    dlg->set_cancel_button(0);
    auto keep = std::make_shared<dr::Plan>(std::move(planned.plan));
    dlg->choose(m_parent, [this, dlg, keep, ok = refused.empty()](const Glib::RefPtr<Gio::AsyncResult>& res) {
        int pick = 0;
        try { pick = dlg->choose_finish(res); } catch (const Glib::Error&) { return; }
        if (pick != 1 || !ok) { m_status->set_text("Nothing done."); return; }
        keep->previewed = true;
        const std::string data = m_data_dir;
        auto* door = &m_door;
        busy::door(m_parent, m_door, std::move(*keep), "Shelves",
            [this](const dr::Result& r) {
                if (m_changed) m_changed();
                m_after = r.ok ? "On their shelves -- one undo in Review \u2192 Activity."
                               : "Not done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front());
                if (auto lg = log::get(log::Area::Door)) lg->info("start: shelves -- {} done, {} failed", r.done, r.failed);
                look();
            },
            [data, door](const dr::Result& r) {
                core::catalog::Catalog cat;
                if (cat.open(data, true))
                    core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch),
                                          static_cast<std::int64_t>(std::time(nullptr)));
            });
    });
}

// s030n: the files nothing claims, one at a time.
void OrganizeStart::unclaimed_at(std::size_t i) {
    if (i >= m_unclaimed.size()) { look(); return; }
    const std::string abs = m_unclaimed[i];
    const std::string rel = abs.rfind(m_home + "/", 0) == 0 ? abs.substr(m_home.size() + 1) : abs;
    std::error_code ec;
    if (!fs::exists(abs, ec)) { unclaimed_at(i + 1); return; }   // gone meanwhile
    const auto size = fs::file_size(abs, ec);
    auto dlg = Gtk::AlertDialog::create(fs::path(abs).filename().string() + "  (" + std::to_string(i + 1) + " of " +
                                        std::to_string(m_unclaimed.size()) + ")");
    dlg->set_detail("In ~/" + fs::path(rel).parent_path().string() + (ec ? std::string() : "   --   " + std::to_string(size / 1024) + " KB") +
                    "\n\nNo Home rule claims this type. Put it with a folder (Tidy remembers names like it), send it to "
                    "the Trash (one undo), or leave it where it is -- Tidy won't ask again.\nTip: a rule for the whole type is "
                    "Design \u2192 Types \u2192 Home \u2192 + file type.");
    dlg->set_buttons({"Stop", "Leave it", "To the Trash", "Put it with\u2026"});
    dlg->set_cancel_button(0);
    dlg->set_default_button(3);
    dlg->choose(m_parent, [this, dlg, i, abs, rel](const Glib::RefPtr<Gio::AsyncResult>& res) {
        int pick = 0;
        try { pick = dlg->choose_finish(res); } catch (const Glib::Error&) { pick = 0; }
        if (pick == 0) { look(); return; }
        if (pick == 1) {
            core::guide::leave(m_data_dir, rel);
            unclaimed_at(i + 1);
            return;
        }
        if (pick == 3) {
            if (m_put_files) m_put_files({abs}, [this, i] { unclaimed_at(i + 1); });
            else unclaimed_at(i + 1);
            return;
        }
        dr::Plan plan;
        dr::Action a;
        a.verb = dr::Verb::Trash;
        a.from = abs;
        a.reason = "nothing claims it -- you sent it to the Trash";
        plan.actions.push_back(a);
        plan.label = "Trash: " + fs::path(abs).filename().string();
        plan.previewed = true;   // the dialog above was its preview
        busy::door(m_parent, m_door, std::move(plan), "To the Trash", [this, i](const dr::Result& r) {
            if (m_changed) m_changed();
            if (auto lg = log::get(log::Area::Door)) lg->info("start: unclaimed to the Trash -- {} done, {} failed", r.done, r.failed);
            unclaimed_at(i + 1);
        });
    });
}

// s030n: the ebooks with no author found -- who wrote it?
void OrganizeStart::ask_at(std::size_t i) {
    if (i >= m_ask.size()) { look(); return; }
    auto row = m_ask[i];
    auto win = new Gtk::Window();
    win->set_transient_for(m_parent);
    win->set_modal(true);
    win->set_title("Who wrote it? (" + std::to_string(i + 1) + " of " + std::to_string(m_ask.size()) + ")");
    win->set_default_size(560, -1);
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_margin(18);
    box->append(*text(fs::path(row.path).filename().string(), "card-title"));
    box->append(*text("In ~/" + fs::path(row.path).parent_path().string().substr(m_home.size() + 1) +
                      ". Tidy found no author inside the file. Type it as Last, First (\"Norton, Andre\") -- it goes to "
                      "Entertainment/EBooks/" + row.genre + "/<Last, First>/<Title>.", "dim"));
    auto author = Gtk::make_managed<Gtk::Entry>();
    author->set_placeholder_text("Last, First");
    {   // the folder above, when it reads like a name ("Andre Norton") and is no genre or the title itself
        const std::string up = fs::path(row.path).parent_path().filename().string();
        std::size_t words = 1;
        bool digits = false;
        for (char c : up) { words += c == ' '; digits = digits || std::isdigit(static_cast<unsigned char>(c)); }
        if (!digits && words >= 2 && words <= 4 && up != row.title)
            author->set_text(core::ebooks::last_first(up));
    }
    author->set_name("organize.ask.author");
    auto title = Gtk::make_managed<Gtk::Entry>();
    title->set_text(row.title);
    title->set_name("organize.ask.title");
    auto grid = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    grid->append(*text("Author", "dim"));
    grid->append(*author);
    grid->append(*text("Title", "dim"));
    grid->append(*title);
    box->append(*grid);
    auto why = text("", "dim");
    box->append(*why);
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    bar->set_halign(Gtk::Align::END);
    auto stop = Gtk::make_managed<Gtk::Button>("Stop");
    auto skip = Gtk::make_managed<Gtk::Button>("Skip");
    auto put = Gtk::make_managed<Gtk::Button>("Put it on the shelf");
    put->add_css_class("suggested-action");
    put->set_name("organize.ask.put");
    bar->append(*stop);
    bar->append(*skip);
    bar->append(*put);
    box->append(*bar);
    win->set_child(*box);
    auto done = [win](bool) { win->set_visible(false); Glib::signal_idle().connect_once([win] { delete win; }); };
    stop->signal_clicked().connect([this, done] { done(true); look(); });
    skip->signal_clicked().connect([this, done, i] { done(true); ask_at(i + 1); });
    put->signal_clicked().connect([this, done, i, row, author, title, why]() mutable {
        row.author = core::ebooks::last_first(author->get_text());
        row.title = core::ebooks::folder_name(title->get_text());
        if (const auto p = core::ebooks::row_problem(row); !p.empty()) { why->set_text(p); return; }
        auto planned = core::ebooks::plan(m_home, {row});
        planned.plan.previewed = true;   // this window shows where it goes
        done(true);
        busy::door(m_parent, m_door, std::move(planned.plan), "On the shelf", [this, i](const dr::Result& r) {
            if (m_changed) m_changed();
            if (auto lg = log::get(log::Area::Door)) lg->info("start: an ebook on its shelf -- {} done, {} failed", r.done, r.failed);
            ask_at(i + 1);
        });
    });
    win->present();
    author->grab_focus();
}

void OrganizeStart::send_home(const gd::Step& s) {
    std::vector<core::homing::Pick> picks;
    for (std::size_t i = 0; i < s.bundles.size(); ++i)
        picks.push_back({m_home + "/" + s.bundles[i], s.homes[i], "a loose bundle -> its kind's home"});
    auto planned = core::homing::plan(m_home, picks);
    auto plan = std::move(planned.plan);
    plan.label = "Send home: " + std::to_string(s.bundles.size()) + (s.bundles.size() == 1 ? " app file" : " app files");
    const auto rel = [this](const std::string& abs) {
        return abs.rfind(m_home + "/", 0) == 0 ? abs.substr(m_home.size() + 1) : abs;
    };
    std::string steps, refused;
    std::vector<std::string> made;
    for (const auto& a : plan.actions) {
        if (const auto why = m_door.refusal(a, made); !why.empty()) refused += "\n  refused: " + why;
        if (a.verb == dr::Verb::MakeDir) { made.push_back(a.to); steps += "\n  make ~/" + rel(a.to); }
        else if (a.verb == dr::Verb::Move) steps += "\n  " + fs::path(a.from).filename().string() + "  ->  ~/" +
                                                    rel(fs::path(a.to).parent_path().string());
    }
    for (const auto& r : planned.renamed) steps += "\n  (taken: " + r + ")";
    auto dlg = Gtk::AlertDialog::create("Send home -- " + std::to_string(s.bundles.size()) +
                                        (s.bundles.size() == 1 ? " app file" : " app files"));
    dlg->set_detail("Each moved whole, never opened. One undo step in Review -> Activity." + steps + refused);
    if (refused.empty()) {
        dlg->set_buttons({"Back", "Do it"});
        dlg->set_default_button(1);
    } else {
        dlg->set_buttons({"Back"});
    }
    dlg->set_cancel_button(0);
    auto keep = std::make_shared<dr::Plan>(std::move(plan));
    dlg->choose(m_parent, [this, dlg, keep, ok = refused.empty()](const Glib::RefPtr<Gio::AsyncResult>& res) {
        int pick = 0;
        try { pick = dlg->choose_finish(res); } catch (const Glib::Error&) { return; }
        if (pick != 1 || !ok) { m_status->set_text("Nothing done."); return; }
        keep->previewed = true;
        const std::string data = m_data_dir;
        auto* door = &m_door;
        busy::door(m_parent, m_door, std::move(*keep), "Send home",
            [this](const dr::Result& r) {
                if (m_changed) m_changed();
                m_after = r.ok ? "Sent home -- one undo in Review -> Activity."
                               : "Not done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front());
                if (auto lg = log::get(log::Area::Door)) lg->info("start: send home -- {} done, {} failed", r.done, r.failed);
                look();
            },
            [data, door](const dr::Result& r) {   // the catalog follows the files it knows
                core::catalog::Catalog cat;
                if (cat.open(data, true))
                    core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch),
                                          static_cast<std::int64_t>(std::time(nullptr)));
            });
    });
}

}  // namespace tidy
