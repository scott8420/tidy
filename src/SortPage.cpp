#include "SortPage.hpp"
#include <glibmm/main.h>
#include "Busy.hpp"
#include "SelectList.hpp"
#include "Log.hpp"
#include "core/Archive.hpp"
#include "core/Catalog.hpp"
#include "core/Own.hpp"
#include "core/Pages.hpp"
#include "core/Projects.hpp"
#include "core/Templates.hpp"
#include "core/Retention.hpp"
#include "core/Routes.hpp"
#include "core/Settings.hpp"
#include "core/Tagging.hpp"

#include <gtkmm/box.h>
#include <gtkmm/window.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>

#include <ctime>
#include <memory>
#include <filesystem>
#include <map>

namespace tidy {

namespace so = core::sort;
namespace rt = core::retention;
namespace dr = latch::door;

namespace {
constexpr const char* kLooking = "Looking through the home...";
Gtk::Label* label(const std::string& text, const char* css, float x = 0.5f) {
    auto l = Gtk::make_managed<Gtk::Label>(text);
    if (css) l->add_css_class(css);
    l->set_xalign(x);
    l->set_wrap(true);
    l->set_justify(x == 0.5f ? Gtk::Justification::CENTER : Gtk::Justification::LEFT);
    return l;
}
Gtk::Image* hero(int px) {
    auto i = Gtk::make_managed<Gtk::Image>();
    i->set_from_icon_name("tidy-hero-sort");
    i->set_pixel_size(px);
    return i;
}
Gtk::Box* head_box(Gtk::Label** summary, const std::string& title, const std::string& name) {
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(12);
    head->append(*hero(48));
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label(title, "hero-title", 0.0f));
    *summary = label("", "dim", 0.0f);
    (*summary)->set_name(name);
    titles->append(**summary);
    titles->set_hexpand(true);
    head->append(*titles);
    return head;
}
Gtk::Box* bar_box() {
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    return bar;
}
std::string plural(std::size_t n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); }
}  // namespace

SortPage::SortPage(dr::Door& door, std::string home, std::string data_dir, bool fixture, Text user_dirs, Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_fixture(fixture),
      m_user_dirs(std::move(user_dirs)), m_changed(std::move(changed)) {
    set_name("sort.page");
    build_start();
    build_results();
    build_preview();
    set_visible_child("start");
}

std::string SortPage::shown(const std::string& path) const {
    if (path == m_home) return "~";
    if (path.rfind(m_home + "/", 0) == 0) return "~" + path.substr(m_home.size());
    return path;
}

void SortPage::build_start() {
    const auto* p = core::pages::find("sort");
    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("sort.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero(128));
    start->append(*label(p ? p->title : "Sort", "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(std::string("Look sweeps the home for loose files. Each gets a place: kept files go ") +
                      "where you sent the same kind before (cover*.png -> ~/Pictures/Covers), else where your Home design "
                      "says (Design -> Types -> Home: *.png -> Pictures, *.otf -> Fonts/G/Garamond); no rule -> it waits "
                      "in the Closet; a download "
                      "you have opened goes on by Home's Downloads/ rules (Movies, Series/<Show>/Season 01, EBooks, Music; Move on now takes an unread one); "
                      "old records go to ~/Archive, fingerprinted; a folder moves whole; a -project goes to its "
                      "category. Unread downloads and your junk list stay, labelled. Hands-off "
                      "folders, hidden files, projects and repos are never touched. All of it is selected -- "
                      "unselect what is wrong, then Sort. One step to undo in Activity." +
                      (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : ""), "dim");
    note->set_name("sort.note");
    note->set_max_width_chars(60);
    start->append(*note);
    auto orb = Gtk::make_managed<Gtk::Button>("Look");
    orb->set_name("sort.look");
    orb->add_css_class("orb");
    orb->set_halign(Gtk::Align::CENTER);
    orb->set_margin_top(18);
    orb->signal_clicked().connect([this] { fill(); set_visible_child("results"); });
    start->append(*orb);
    // s023: folders already organized by hand -- cataloged in place, or a catch-all swept here.
    auto adopt = Gtk::make_managed<Gtk::Button>("Adopt folders...");
    adopt->set_name("sort.adopt");
    adopt->set_halign(Gtk::Align::CENTER);
    adopt->set_margin_top(10);
    adopt->set_tooltip_text("Folders you organized by hand: catalog them where they are, or mark a catch-all whose "
                            "files Sort should see");
    adopt->signal_clicked().connect([this] { if (m_on_adopt) m_on_adopt(); });
    start->append(*adopt);
    add(*start, "start");
}

void SortPage::build_results() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("sort.results");
    auto head = head_box(&m_summary, "Sort", "sort.summary");
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("sort.again");
    again->set_valign(Gtk::Align::CENTER);
    again->signal_clicked().connect([this] { fill(); });
    m_whole = Gtk::make_managed<Gtk::Button>("Whole home");   // s027a: back from a folder's look
    m_whole->set_name("sort.whole");
    m_whole->set_valign(Gtk::Align::CENTER);
    m_whole->set_visible(false);
    m_whole->set_tooltip_text("Sort the whole home again, not just this folder");
    m_whole->signal_clicked().connect([this] { q_stop(false); look_in(""); });
    head->append(*m_whole);
    m_qskip = Gtk::make_managed<Gtk::Button>("Skip this folder");   // s030d: the queue from Start
    m_qskip->set_name("sort.qskip");
    m_qskip->set_valign(Gtk::Align::CENTER);
    m_qskip->set_visible(false);
    m_qskip->signal_clicked().connect([this] { if (!m_busy) q_next(); });
    head->append(*m_qskip);
    head->append(*again);
    box->append(*head);
    m_list = Gtk::make_managed<SelectList>("sort.list");
    m_list->set_vexpand(true);
    m_list->set_margin_start(32);
    m_list->set_margin_end(32);
    m_list->set_margin_bottom(16);
    m_list->set_choices({"Sort these", "Move on now (as read)", "Put it with\u2026"});   // s023g; s030g
    m_list->on_choose([this](std::size_t c) {
        if (c == 1) { on_move_now(); return; }
        if (c == 2) {   // s030g: the selected files -> a folder you pick
            std::vector<std::string> files;
            for (auto i : m_list->selected())
                if (i < m_rows.size() && !m_rows[i].is_project && !m_rows[i].folder_whole) files.push_back(m_rows[i].born.file.path);
            if (files.empty()) { m_status->set_text("Put it with... is for files -- a project or a whole folder moves as one."); return; }
            if (m_on_put_many) m_on_put_many(std::move(files));
            return;
        }
        on_sort();
    });
    box->append(*m_list);
    auto bar = bar_box();
    m_status = label("", "dim", 0.0f);
    m_status->set_name("sort.status");
    m_status->set_hexpand(true);
    m_status->set_width_chars(26);
    bar->append(*m_status);
    auto& setter = m_list->setter();
    setter.set_valign(Gtk::Align::CENTER);
    // Only "N selected" here: the one choice is the Sort... button (right-click still offers it).
    for (auto* c = setter.get_first_child() ? setter.get_first_child()->get_next_sibling() : nullptr; c;
         c = c->get_next_sibling())
        c->set_visible(false);
    bar->append(setter);
    // s023g: unread downloads (files and whole folders) -> where they go once read.
    auto now = Gtk::make_managed<Gtk::Button>("Move on now...");
    now->set_name("sort.now");
    now->set_valign(Gtk::Align::CENTER);
    now->set_tooltip_text("The selected UNREAD downloads, moved on as if read -- a file becomes Keep; a folder moves "
                          "whole (Home's Downloads/ rules: Design -> Types -> Home). Previewed first.");
    now->signal_clicked().connect([this] { on_move_now(); });
    bar->append(*now);
    auto go = Gtk::make_managed<Gtk::Button>("Sort...");
    go->set_name("sort.go");
    go->add_css_class("act");
    go->set_valign(Gtk::Align::CENTER);
    go->set_tooltip_text("Preview the selected files' sort -- nothing happens until Do it");
    go->signal_clicked().connect([this] { on_sort(); });
    bar->append(*go);
    box->append(*bar);
    add(*box, "results");
}

void SortPage::build_preview() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("sort.preview");
    box->append(*head_box(&m_prev_summary, "Preview", "sort.preview.summary"));
    m_prev_list = Gtk::make_managed<Gtk::ListBox>();
    m_prev_list->set_name("sort.preview.list");
    m_prev_list->set_selection_mode(Gtk::SelectionMode::NONE);
    auto sc = Gtk::make_managed<Gtk::ScrolledWindow>();
    sc->set_vexpand(true);
    sc->set_margin_start(32);
    sc->set_margin_end(32);
    sc->set_margin_bottom(16);
    sc->set_child(*m_prev_list);
    box->append(*sc);
    auto bar = bar_box();
    m_prev_status = label("", "dim", 0.0f);
    m_prev_status->set_name("sort.preview.status");
    m_prev_status->set_hexpand(true);
    bar->append(*m_prev_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("sort.preview.back");
    back->signal_clicked().connect([this] { m_plan.reset(); set_visible_child("results"); m_status->set_text("Nothing done."); });
    bar->append(*back);
    m_prev_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_prev_run->set_name("sort.preview.run");
    m_prev_run->add_css_class("act");
    m_prev_run->signal_clicked().connect([this] { on_run(); });
    bar->append(*m_prev_run);
    box->append(*bar);
    add(*box, "preview");
}

void SortPage::refresh_if_shown() {
    if (get_visible_child_name() == "results") fill();
}

void SortPage::start_queue(std::vector<std::string> folders, std::vector<std::string> skip, std::function<void()> when_done) {
    m_queue = std::move(folders);
    m_skip = std::move(skip);
    m_q_done = std::move(when_done);
    if (m_queue.empty()) { q_stop(true); return; }
    q_load(0);
}

void SortPage::q_load(std::size_t i) {
    m_qi = i;
    if (m_qskip) m_qskip->set_visible(true);
    look_in(m_queue[i], m_skip);
}

void SortPage::q_next() {
    if (m_qi + 1 < m_queue.size()) { q_load(m_qi + 1); return; }
    q_stop(true);
}

void SortPage::q_stop(bool back) {
    const bool had = !m_queue.empty();
    m_queue.clear();
    if (m_qskip) m_qskip->set_visible(false);
    auto f = std::move(m_q_done);
    m_q_done = nullptr;
    if (back && had && f) f();
}

void SortPage::look_in(const std::string& rel, std::vector<std::string> skip) {   // s027a
    m_scope = rel;
    m_skip = std::move(skip);
    m_rows.clear();
    set_visible_child("results");
    fill();
}

void SortPage::fill() {
    m_status->set_text(kLooking);
    if (m_rows.empty()) m_summary->set_text("Looking...");
    const std::string home = m_home, data = m_data_dir, dirs = m_user_dirs ? m_user_dirs() : "";
    const std::string scope = m_scope;
    const auto skip = m_skip;
    if (m_whole) m_whole->set_visible(!scope.empty());
    m_look.start(
        [home, data, dirs, scope, skip] {
            Look l;
            l.now = static_cast<std::int64_t>(std::time(nullptr));
            const auto st = core::settings::parse(core::own::read(data, core::settings::kFile));
            auto places = st.catch_all;
            if (!scope.empty()) places.push_back(scope);   // s027a: swept like a catch-all, this look only
            const auto rep = core::birth::report(home, core::birth::sweep(home, dirs, places), l.now, st.old_years);
            const auto found = core::projects::find(home, core::projects::parse_left(core::own::read(data, core::projects::kFile)));
            const auto book = core::routes::parse(core::own::read(data, core::routes::kFile));   // s022
            const auto dz = core::home::design(core::home::load(data, st));   // s029: the Home design
            l.rows = so::rows(home, rep, st, l.now, found, core::templates::list(data), &book, &dz);
            if (!scope.empty()) {   // s027a: only what is under the folder, nothing inside its projects / bundles
                const auto under = [&](const std::string& path, const std::string& rel) {
                    const std::string root = home + "/" + rel;
                    return path == root || path.rfind(root + "/", 0) == 0;
                };
                std::vector<so::Row> kept;
                for (auto& r : l.rows) {
                    if (!under(r.born.file.path, scope)) continue;
                    bool out = false;
                    for (const auto& x : skip) out = out || under(r.born.file.path, x);
                    if (!out) kept.push_back(std::move(r));
                }
                l.rows = std::move(kept);
            }
            return l;
        },
        [this](Look l) { show(std::move(l)); });
}

void SortPage::show(Look look) {
    // Grouped by where they go: each home folder, then ~/Archive, then what stays.
    auto rows = std::move(look.rows);
    const auto key = [](const so::Row& r) {
        return std::string(r.go == so::Go::Home ? "0" : r.go == so::Go::Archive ? "1" : r.go == so::Go::Trash ? "15" : "2") + r.folder;
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const so::Row& a, const so::Row& b) { return key(a) < key(b); });
    m_rows = std::move(rows);
    std::vector<SelectList::Item> items;
    std::map<std::string, std::size_t> count;
    for (const auto& r : m_rows) ++count[key(r)];
    std::string last;
    std::size_t home = 0, arch = 0, stays = 0, trashn = 0;
    for (const auto& r : m_rows) {
        SelectList::Item it;
        const std::string k = key(r);
        if (k != last) {
            last = k;
            const auto n = plural(count[k], "file");
            if (r.go == so::Go::Home) { it.header = "To ~/" + r.folder + "  (" + n + ")"; }
            else if (r.go == so::Go::Archive) { it.header = "To ~/" + r.folder + "  (" + n + ")"; it.header_sub = "Historical -- archived and fingerprinted; Back up from the Archive page."; }
            else if (r.go == so::Go::Trash) { it.header = "To the Trash  (" + n + ")"; it.header_sub = "Your Home design's Trash row says so. Undo in Review -> Activity brings them back."; }
            else { it.header = "Stays where it is  (" + n + ")"; it.header_sub = "Unread downloads and Ephemeral files (Due proposes the Trash when their days are up), your junk list (the Junk Drawer), files no Home rule fits (they wait in the Closet), and projects with no category (pick one on the Projects page)."; }
        }
        home += r.go == so::Go::Home;
        arch += r.go == so::Go::Archive;
        stays += r.go == so::Go::Stays;
        trashn += r.go == so::Go::Trash;   // s030i
        it.title = std::filesystem::path(r.born.file.path).filename().string() +
                   (r.is_project ? std::string("   (a project, whole)")
                    : r.folder_whole ? std::string("   (a folder, whole)")
                    : r.closet ? std::string("   -> the Closet")   // s029: no Home rule fits
                    : r.go == so::Go::Trash ? std::string("   -> the Trash")   // s030i
                    : "   -> " + (r.junk ? std::string("the Junk Drawer") : rt::title_of(r.level)));
        it.line = shown(std::filesystem::path(r.born.file.path).parent_path().string()) + "   ·   " + r.why +
                  (r.now_folder.empty() ? std::string() : "   ·   Move on now -> ~/" + r.now_folder) +
                  (r.tag || r.folder_whole || r.is_project || r.closet ? "" : "   ·   decided already");
        it.tooltip = r.born.file.path;
        items.push_back(std::move(it));
    }
    m_list->set_items(items);
    m_list->select_all();
    const std::string qpre = m_queue.empty() ? std::string()
        : "Folder " + std::to_string(m_qi + 1) + " of " + std::to_string(m_queue.size()) + " -- ";   // s030d
    m_summary->set_text(qpre + (m_scope.empty() ? std::string() : "In ~/" + m_scope + ": ") +
                        plural(m_rows.size(), "loose file") + ": " + std::to_string(home) + " to go home, " +
                        std::to_string(arch) + " to archive, " + (trashn ? std::to_string(trashn) + " to the Trash, " : std::string()) +
                        std::to_string(stays) + " to stay, labelled" +
                        (m_fixture ? "   |   FIXTURE " + m_home : ""));
    if (m_status->get_text() == kLooking)
        m_status->set_text(m_rows.empty() ? "Nothing to sort -- every loose file has its place."
                                          : "All selected. Unselect what is wrong, then Sort...");
    // s030d: in the queue, a folder with nothing to move or archive is passed by itself.
    if (!m_queue.empty() && home == 0 && arch == 0 && trashn == 0) {
        if (m_on_settled) m_on_settled(m_scope);   // s030f: never offered again
        Glib::signal_idle().connect_once([this] { if (!m_queue.empty() && !m_busy) q_next(); });
    }
}

void SortPage::on_move_now() {   // s023g
    std::vector<so::Row> chosen;
    std::size_t other = 0;
    for (auto i : m_list->selected()) {
        if (i >= m_rows.size()) continue;
        auto r = m_rows[i];
        if (so::move_on_now(r)) chosen.push_back(std::move(r));
        else ++other;
    }
    if (chosen.empty()) {
        m_status->set_text("Select unread downloads first (\"unread download -- stays\", \"a folder ..., unread\").");
        return;
    }
    if (other) m_status->set_text(std::to_string(other) + " selected that are not unread downloads -- left out.");
    preview(std::move(chosen));
}

void SortPage::on_sort() {
    std::vector<so::Row> chosen;
    for (auto i : m_list->selected())
        if (i < m_rows.size()) chosen.push_back(m_rows[i]);
    if (chosen.empty()) { m_status->set_text("Select some files first."); return; }
    preview(std::move(chosen));
}

void SortPage::preview(std::vector<so::Row> chosen) {
    auto planned = so::plan(m_home, chosen, static_cast<std::int64_t>(std::time(nullptr)));
    m_archiving = planned.archiving > 0;
    m_learning.clear();
    for (const auto& r : chosen)
        if (r.go == so::Go::Home && !r.learned.folder.empty()) m_learning.push_back(r);
    while (auto* row = m_prev_list->get_row_at_index(0)) m_prev_list->remove(*row);
    const auto row = [&](const std::string& text, const char* css) {
        auto l = label(text, css, 0.0f);
        l->set_margin(4);
        auto r = Gtk::make_managed<Gtk::ListBoxRow>();
        r->set_activatable(false);
        r->set_child(*l);
        m_prev_list->append(*r);
    };
    for (const auto& [p, why] : planned.refused) row(shown(p) + "\nNOT ARCHIVED: " + why, "stop");
    std::vector<std::string> made;
    std::size_t refused = 0, moves = 0, dirs = 0, tags = 0;
    for (const auto& a : planned.plan.actions) {
        const std::string why = m_door.refusal(a, made);
        refused += !why.empty();
        if (why.empty() && a.verb == dr::Verb::MakeDir) made.push_back(a.to);
        std::string text;
        const char* css = nullptr;
        if (a.verb == dr::Verb::Tag) {
            ++tags;
            const std::string k = a.attr.rfind(dr::kTagPrefix, 0) == 0 ? a.attr.substr(std::string(dr::kTagPrefix).size()) : a.attr;
            text = "tag   " + shown(a.from) + "   " + k + " = " + (k == "id" ? std::string("a new id") : a.content);
            css = "dim";
        } else if (a.verb == dr::Verb::MakeDir) {
            ++dirs;
            text = "new folder   " + shown(a.to);
            css = "dim";
        } else {
            ++moves;
            text = shown(a.from) + "   ->   " + shown(a.to) + "\n" + a.reason;
            if (a.reason.find("is taken there") != std::string::npos) css = "note";
        }
        if (!why.empty()) { text += "\nREFUSED: " + why; css = "stop"; }
        row(text, css);
    }
    m_prev_summary->set_text(planned.plan.label + "   |   " + plural(moves, "move") + ", " + plural(dirs, "new folder") +
                             ", " + plural(tags, "tag"));
    std::string st = refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen. " : std::string();
    if (!planned.renamed.empty())
        st += std::to_string(planned.renamed.size()) + " name(s) taken there -- renamed with (2), (3)... Nothing is overwritten. ";
    if (m_archiving) st += "After Do it each archived file's fingerprint (SHA-256) goes into the catalog. ";
    st += "One step to undo in Activity.";
    m_prev_status->set_text(st);
    m_prev_run->set_sensitive(planned.plan.actions.size() > refused);
    m_plan = std::move(planned.plan);
    set_visible_child("preview");
}

void SortPage::on_run() {
    if (!m_plan || m_busy) return;
    auto plan = std::move(*m_plan);
    m_plan.reset();
    plan.previewed = true;   // shown in full by on_sort() (RULES: preview first)
    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    if (!win) return;
    m_busy = true;
    m_prev_run->set_sensitive(false);
    // s023a: the run, the catalog and the fingerprints happen off the window's
    // thread under a progress bar (Busy); the routes and the look again after.
    auto prints = std::make_shared<std::size_t>(0);
    const std::string data = m_data_dir;
    const bool archiving = m_archiving;
    dr::Door* door = &m_door;
    const std::string label = plan.label;
    busy::door(*win, m_door, std::move(plan), "Sort",
        [this, prints, label](const dr::Result& r) {
            m_busy = false;
            m_archiving = false;
            std::size_t taught = 0;
            if (r.done > 0 && !m_learning.empty()) {   // s022: each learned route followed is a yes
                auto book = core::routes::parse(core::own::read(m_data_dir, core::routes::kFile));
                for (const auto& row : m_learning) {
                    std::error_code ec;
                    if (std::filesystem::exists(row.born.file.path, ec)) continue;   // refused / not moved: no yes
                    core::routes::learn(book, row.born, row.folder, row.learned);
                    ++taught;
                }
                if (taught) core::own::write(m_data_dir, core::routes::kFile, core::routes::format(book));
            }
            m_learning.clear();
            if (m_changed) m_changed();
            fill();   // first: it says "Looking..."; the Done line below replaces that and stays
            m_status->set_text(r.ok ? "Done: " + label + " (" + std::to_string(r.done) + " steps; " +
                                          std::to_string(*prints) + " fingerprinted) -- undo in Activity"
                                    : label + ": " + std::to_string(r.done) + " done, " + std::to_string(r.failed) +
                                          " not -- " + (r.messages.empty() ? std::string() : r.messages.front()));
            set_visible_child("results");
            if (auto lg = log::get(log::Area::Door))
                lg->info("sort run '{}': {} done, {} failed; {} fingerprinted", label, r.done, r.failed, *prints);
            if (r.ok && !m_queue.empty()) { q_next(); return; }   // s030d: the next folder, or back to Start
            if (r.ok && m_done_once) { auto f = std::move(m_done_once); m_done_once = nullptr; f(); }   // s030b
        },
        [data, door, archiving, prints](const dr::Result& r) {   // the catalog: the tagged files where they now are
            core::catalog::Catalog cat;
            if (!cat.open(data, true)) return;
            const auto now = static_cast<std::int64_t>(std::time(nullptr));
            core::tagging::record(cat, core::tagging::paths_of(*door, r.batch), now);       // tagged and staying
            core::tagging::record(cat, core::tagging::moved_paths(*door, r.batch), now);    // tagged and moved
            if (archiving) *prints = core::archive::record(cat, *door, r.batch, now);
        },
        [door](dr::Plan& p) {   // drop what the door refuses, judged in order as previewed
            std::vector<std::string> made;
            std::vector<dr::Action> keep;
            for (const auto& a : p.actions) {
                if (!door->refusal(a, made).empty()) continue;
                if (a.verb == dr::Verb::MakeDir) made.push_back(a.to);
                keep.push_back(a);
            }
            p.actions = std::move(keep);
        });
}

}  // namespace tidy
