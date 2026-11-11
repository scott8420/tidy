#include "CodeView.hpp"
#include "SelectList.hpp"
#include "Log.hpp"
#include "core/Archive.hpp"
#include "core/Own.hpp"
#include "core/Projects.hpp"
#include "core/Removal.hpp"
#include "core/Settings.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <map>

namespace tidy {

namespace cd = core::code;
namespace dr = latch::door;

namespace {
constexpr const char* kLooking = "Looking at each code project (git, sizes)...";
constexpr std::size_t kShownSteps = 1500;   // the preview lists this many steps, then counts the rest
Gtk::Label* label(const std::string& text, const char* css, float x = 0.5f) {
    auto l = Gtk::make_managed<Gtk::Label>(text);
    if (css) l->add_css_class(css);
    l->set_xalign(x);
    l->set_wrap(true);
    l->set_justify(x == 0.5f ? Gtk::Justification::CENTER : Gtk::Justification::LEFT);
    return l;
}
Gtk::Box* head_box(Gtk::Label** summary, const std::string& title, const std::string& name) {
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(12);
    auto i = Gtk::make_managed<Gtk::Image>();
    i->set_from_icon_name("tidy-hero-projects");
    i->set_pixel_size(48);
    head->append(*i);
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label(title, "hero-title", 0.0f));
    *summary = label("", "dim", 0.0f);
    (*summary)->set_name(name);
    (*summary)->set_selectable(true);
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
Gtk::Button* button(const std::string& text, const std::string& name, const std::string& tip) {
    auto b = Gtk::make_managed<Gtk::Button>(text);
    b->set_name(name);
    b->set_valign(Gtk::Align::CENTER);
    b->set_tooltip_text(tip);
    return b;
}
std::string plural(std::size_t n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); }
std::string title_of(const std::string& status) {
    if (status.empty()) return status;
    std::string t = status;
    t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
    return t;
}
const std::vector<std::string>& choices() {
    static const std::vector<std::string> c{"Active", "Paused", "Done", "Dropped"};   // core::projects::statuses(), titled
    return c;
}
core::settings::Settings settings_of(const std::string& data) {
    return core::settings::parse(core::own::read(data, core::settings::kFile));
}
}  // namespace

CodeView::CodeView(dr::Door& door, std::string home, std::string data_dir, bool fixture, Changed changed, Back back)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_fixture(fixture),
      m_changed(std::move(changed)), m_back(std::move(back)) {
    set_name("code.states");
    build_results();
    build_preview();
    set_visible_child("results");
    // Shown again (from another page, or All projects -> Code projects): look again -- the Archive
    // page's Back up changes what this view says.
    signal_map().connect([this] { if (m_looked && get_visible_child_name() == "results") fill(); });
}

std::string CodeView::shown(const std::string& path) const {
    if (path == m_home) return "~";
    if (path.rfind(m_home + "/", 0) == 0) return "~" + path.substr(m_home.size());
    return path;
}

void CodeView::build_results() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("code.results");
    auto head = head_box(&m_summary, "Code projects", "code.summary");
    auto again = button("Look again", "code.again", "Ask git again and add up the sizes -- reads only");
    again->signal_clicked().connect([this] { fill(); });
    head->append(*again);
    auto back = button("All projects", "code.back", "Back to the Projects list");
    back->signal_clicked().connect([this] { if (m_back) m_back(); });
    head->append(*back);
    box->append(*head);
    m_list = Gtk::make_managed<SelectList>("code.list");
    m_list->set_vexpand(true);
    m_list->set_margin_start(32);
    m_list->set_margin_end(32);
    m_list->set_margin_bottom(16);
    m_list->set_choices(choices());
    m_list->on_choose([this](std::size_t i) { if (i < choices().size()) set_status(core::projects::statuses()[i]); });
    box->append(*m_list);
    auto bar = bar_box();
    m_status = label("", "dim", 0.0f);
    m_status->set_name("code.status");
    m_status->set_hexpand(true);
    m_status->set_width_chars(22);
    bar->append(*m_status);
    auto& setter = m_list->setter();
    setter.set_valign(Gtk::Align::CENTER);
    bar->append(setter);
    auto clean = button("Clean build folders...", "code.clean",
                        "The selected projects' build folders to the Trash -- previewed with their sizes");
    clean->signal_clicked().connect([this] { on_act(Act::Clean); });
    bar->append(*clean);
    auto arch = button("Archive done...", "code.archive",
                       "A copy of each selected Done project into ~/Archive: docs, scripts, the committed source "
                       "(no .git, no build), RESTORE.txt -- only when everything is on its remote");
    arch->add_css_class("act");
    arch->signal_clicked().connect([this] { on_act(Act::Archive); });
    bar->append(*arch);
    auto trash = button("Trash archived...", "code.trash",
                        "The live folder of each selected archived project to the Trash -- only when every archived "
                        "file reads back the same from your backup disks NOW");
    trash->signal_clicked().connect([this] { on_act(Act::Trash); });
    bar->append(*trash);
    box->append(*bar);
    add(*box, "results");
}

void CodeView::build_preview() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("code.preview");
    box->append(*head_box(&m_prev_summary, "Preview", "code.preview.summary"));
    m_prev_list = Gtk::make_managed<Gtk::ListBox>();
    m_prev_list->set_name("code.preview.list");
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
    m_prev_status->set_name("code.preview.status");
    m_prev_status->set_hexpand(true);
    bar->append(*m_prev_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("code.preview.back");
    back->signal_clicked().connect([this] {
        m_plan.reset();
        m_act = Act::None;
        set_visible_child("results");
        m_status->set_text("Nothing done.");
    });
    bar->append(*back);
    m_prev_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_prev_run->set_name("code.preview.run");
    m_prev_run->add_css_class("act");
    m_prev_run->signal_clicked().connect([this] { on_run(); });
    bar->append(*m_prev_run);
    box->append(*bar);
    add(*box, "preview");
}

void CodeView::refresh_if_shown() {
    if (m_looked && get_visible_child_name() == "results" && is_drawable()) fill();
}

void CodeView::fill() {
    m_looked = true;
    m_status->set_text(kLooking);
    if (m_infos.empty()) m_summary->set_text("Looking...");
    const std::string home = m_home, data = m_data_dir;
    m_look.start(
        [home, data] {
            const auto left = core::projects::parse_left(core::own::read(data, core::projects::kFile));
            return cd::look_all(core::projects::find(home, left));
        },
        [this](std::vector<cd::Info> infos) { show(std::move(infos)); });
}

void CodeView::show(std::vector<cd::Info> infos) {
    m_infos = std::move(infos);
    render();
    if (m_status->get_text() == kLooking)
        m_status->set_text(m_infos.empty() ? "No code projects found (a -project with .git or CMakeLists.txt)."
                                           : "Select, then Set to Done -- your call. Archive done... copies them.");
}

void CodeView::render() {
    m_rows.clear();
    core::catalog::Catalog cat;
    std::vector<core::catalog::Archived> archived;
    core::removal::Book book;
    if (cat.open(m_data_dir, false)) {
        m_rows = cat.projects();
        book = core::removal::book_of(cat);
    }
    const auto st = settings_of(m_data_dir);
    const auto status_of = [&](const cd::Info& i) -> std::string {
        if (i.p.id.empty()) return "";
        for (const auto& r : m_rows) if (r.id == i.p.id) return r.status;
        return "active";
    };
    // Grouped: archived, done, then the rest by status; no id last.
    const auto group = [&](const cd::Info& i) {
        if (!cd::archived_of(book.archived, i.p.path).rows.empty()) return 0;
        const auto s = status_of(i);
        return s == "done" ? 1 : s == "active" ? 2 : s == "paused" ? 3 : s == "dropped" ? 4 : 5;
    };
    std::stable_sort(m_infos.begin(), m_infos.end(), [&](const cd::Info& a, const cd::Info& b) { return group(a) < group(b); });
    std::map<int, std::size_t> count;
    for (const auto& i : m_infos) ++count[group(i)];
    static const char* names[] = {"Archived", "Done", "Active", "Paused", "Dropped", "No id yet"};
    static const char* subs[] = {"The copy is in ~/Archive. Back it up (Archive page), then Trash archived... frees the live folder.",
                                 "Ready for Archive done... when everything is pushed.",
                                 "", "", "", "Give ids on the Projects page first -- status needs an id."};
    std::vector<SelectList::Item> items;
    std::uint64_t total = 0, build = 0, git = 0;
    std::size_t done = 0;
    int last = -1;
    for (const auto& i : m_infos) {
        SelectList::Item it;
        const int g = group(i);
        if (g != last) {
            last = g;
            it.header = std::string(names[g]) + "  (" + std::to_string(count[g]) + ")";
            it.header_sub = subs[g];
        }
        const auto s = status_of(i);
        done += s == "done";
        total += i.total;
        build += i.build;
        git += i.git;
        it.title = std::filesystem::path(i.p.path).filename().string() + (s.empty() ? "" : "   -- " + title_of(s));
        std::string line = shown(std::filesystem::path(i.p.path).parent_path().string()) + "   ·   " + i.state();
        if (i.last_commit) line += "   ·   last commit " + core::archive::day_of(i.last_commit);
        line += "   ·   " + cd::size_text(i.total);
        if (i.build || i.git)
            line += " (build " + cd::size_text(i.build) + ", .git " + cd::size_text(i.git) + ")";
        const auto a = cd::archived_of(book.archived, i.p.path);
        if (!a.rows.empty()) line += "\n" + cd::archived_line(a, book, st);
        it.line = line;
        std::string tip = i.p.path;
        for (const auto& r : i.repos)
            tip += "\n" + r.rel + ": " + r.state() + (r.url.empty() ? "" : " -- " + r.url) +
                   (r.head.empty() ? "" : "\n    " + r.head.substr(0, 10) + " " + r.subject);
        it.tooltip = tip + "\n(git as of the last fetch)";
        items.push_back(std::move(it));
    }
    m_list->set_items(items);
    m_summary->set_text(plural(m_infos.size(), "code project") + "   |   " + cd::size_text(total) + ": build folders " +
                        cd::size_text(build) + ", .git " + cd::size_text(git) + "   |   " + std::to_string(done) +
                        " done" + (m_fixture ? "   |   FIXTURE " + m_home : ""));
}

std::vector<cd::Info> CodeView::chosen() const {
    std::vector<cd::Info> out;
    for (auto i : m_list->selected())
        if (i < m_infos.size()) out.push_back(m_infos[i]);
    return out;
}

void CodeView::set_status(const std::string& status) {
    const auto sel = chosen();
    if (sel.empty()) { m_status->set_text("Select some projects first."); return; }
    core::catalog::Catalog cat;
    if (!cat.open(m_data_dir, true)) { m_status->set_text("The catalog could not be opened."); return; }
    std::size_t n = 0, no_id = 0;
    for (const auto& i : sel) {
        if (i.p.id.empty()) { ++no_id; continue; }
        const auto row = cat.project(i.p.id);
        if (!row) {   // not in the catalog yet (never looked at on the Projects page)
            core::catalog::ProjectRow r;
            r.id = i.p.id;
            r.path = i.p.path;
            r.name = i.p.name;
            r.seen = static_cast<std::int64_t>(std::time(nullptr));
            cat.seen_project(r);
        }
        n += cat.set_status(i.p.id, status, row ? row->outcome : "");
    }
    std::vector<std::string> was;
    for (const auto& i : sel) was.push_back(i.p.path);
    render();
    std::vector<std::size_t> again;   // the same projects stay selected (they moved between groups)
    for (std::size_t k = 0; k < m_infos.size(); ++k)
        if (std::find(was.begin(), was.end(), m_infos[k].p.path) != was.end()) again.push_back(k);
    m_list->select(again);
    std::string msg = plural(n, "project") + " set to " + title_of(status) + ".";
    if (no_id) msg += " " + std::to_string(no_id) + " without an id left as they are (Give ids on the Projects page).";
    if (n) msg += " Tidy's record only -- set it back the same way.";
    m_status->set_text(msg);
    if (auto lg = log::get(log::Area::Door)) lg->info("code: {} set to {}", n, status);
}

void CodeView::on_act(Act act) {
    auto sel = chosen();
    if (sel.empty()) { m_status->set_text("Select some projects first."); return; }
    core::catalog::Catalog cat;
    const bool open = cat.open(m_data_dir, false);
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    cd::Planned planned;
    if (act == Act::Archive) {
        // Ask git again: the preview must be of the state now, not of the last look.
        for (auto& i : sel) i = cd::look(i.p);
        planned = cd::plan_archive(m_home, sel, open ? cat.projects() : std::vector<core::catalog::ProjectRow>{},
                                   open ? cat.archived() : std::vector<core::catalog::Archived>{}, now);
    } else if (act == Act::Trash) {
        for (auto& i : sel) i = cd::look(i.p);
        planned = cd::plan_trash(m_home, sel, open ? core::removal::book_of(cat) : core::removal::Book{},
                                 settings_of(m_data_dir));
    } else {
        for (auto& i : sel) i = cd::look(i.p);
        planned = cd::plan_clean(sel);
    }
    m_acting = std::move(sel);
    preview(std::move(planned), act);
}

void CodeView::preview(cd::Planned planned, Act act) {
    while (auto* row = m_prev_list->get_row_at_index(0)) m_prev_list->remove(*row);
    const auto row = [&](const std::string& text, const char* css) {
        auto l = label(text, css, 0.0f);
        l->set_margin(4);
        l->set_selectable(true);
        auto r = Gtk::make_managed<Gtk::ListBoxRow>();
        r->set_activatable(false);
        r->set_child(*l);
        m_prev_list->append(*r);
    };
    for (const auto& [name, why] : planned.refused) row(name + "\nNOT DONE: " + why, "stop");
    for (const auto& n : planned.notes) row(n, n.rfind("    not kept:", 0) == 0 ? "dim" : nullptr);
    std::vector<std::string> made;
    std::size_t refused = 0, listed = 0, dirs = 0, copies = 0, trash = 0, writes = 0;
    for (const auto& a : planned.plan.actions) {
        const std::string why = m_door.refusal(a, made);
        refused += !why.empty();
        if (why.empty() && a.verb == dr::Verb::MakeDir) made.push_back(a.to);
        dirs += a.verb == dr::Verb::MakeDir;
        copies += a.verb == dr::Verb::Copy;
        trash += a.verb == dr::Verb::Trash;
        writes += a.verb == dr::Verb::Write;
        if (listed >= kShownSteps && why.empty()) continue;
        ++listed;
        std::string text;
        const char* css = nullptr;
        if (a.verb == dr::Verb::MakeDir) { text = "new folder   " + shown(a.to); css = "dim"; }
        else if (a.verb == dr::Verb::Copy) { text = "copy   " + shown(a.from) + "   ->   " + shown(a.to); css = "dim"; }
        else if (a.verb == dr::Verb::Write) { text = "write   " + shown(a.to) + "\n" + a.reason + "\n\n" + a.content; }
        else { text = "to the Trash   " + shown(a.from) + "   (" + cd::size_text(a.bytes) + ")\n" + a.reason; }
        if (!why.empty()) { text += "\nREFUSED: " + why; css = "stop"; }
        row(text, css);
    }
    if (planned.plan.actions.size() > listed)
        row("... and " + std::to_string(planned.plan.actions.size() - listed) +
                " more steps of the same kind (new folders and copies, all checked by the door above) -- they run too.",
            "note");
    std::string sum = planned.plan.actions.empty() ? std::string("Nothing to do") : planned.plan.label;
    if (act == Act::Archive)
        sum += "   |   " + plural(planned.projects, "project") + ", " + std::to_string(copies) +
               (copies == 1 ? " copy (" : " copies (") + cd::size_text(planned.bytes) + "), " +
               plural(dirs, "new folder") + ", " + std::to_string(writes) + " RESTORE.txt";
    else if (!planned.plan.actions.empty())
        sum += "   |   " + plural(trash, "folder") + " to the Trash, " + cd::size_text(planned.bytes);
    m_prev_summary->set_text(sum);
    std::string st = refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen. " : std::string();
    if (act == Act::Archive && !planned.plan.actions.empty())
        st += "A COPY -- the live project stays. After Do it each copied file's fingerprint (SHA-256) goes into the "
              "catalog; then Archive -> Back up, then Trash archived... ";
    if (act == Act::Trash && !planned.plan.actions.empty())
        st += "Each archive copy is read back from the disk again at Do it. ";
    if (!planned.plan.actions.empty()) st += "One step to undo in Activity.";
    else st += "Nothing here can go ahead -- each one says why.";
    m_prev_status->set_text(st);
    m_prev_run->set_sensitive(planned.plan.actions.size() > refused && refused == 0);
    if (refused) m_prev_status->set_text(st + " Do it is off: the door would refuse the whole plan.");
    m_plan = std::move(planned.plan);
    m_act = act;
    set_visible_child("preview");
}

void CodeView::on_run() {
    if (!m_plan) return;
    auto plan = std::move(*m_plan);
    m_plan.reset();
    const Act act = m_act;
    m_act = Act::None;
    std::string kept;
    if (act == Act::Trash) {
        // Read back AGAIN, now: only what is still proved goes.
        core::catalog::Catalog cat;
        const bool open = cat.open(m_data_dir, false);
        std::vector<cd::Info> again;
        for (const auto& i : m_acting) again.push_back(cd::look(i.p));
        auto now_plan = cd::plan_trash(m_home, again, open ? core::removal::book_of(cat) : core::removal::Book{},
                                       settings_of(m_data_dir));
        std::vector<dr::Action> keep;
        for (const auto& a : plan.actions)
            if (std::any_of(now_plan.plan.actions.begin(), now_plan.plan.actions.end(),
                            [&](const dr::Action& b) { return b.from == a.from; }))
                keep.push_back(a);
        if (keep.size() < plan.actions.size()) {
            kept = " " + std::to_string(plan.actions.size() - keep.size()) + " kept -- no longer proved at Do it";
            kept += now_plan.refused.empty() ? std::string(".") : ": " + now_plan.refused.front().first + " -- " +
                                                                   now_plan.refused.front().second;
        }
        plan.actions = std::move(keep);
        if (plan.actions.empty()) {
            set_visible_child("results");
            m_status->set_text("Nothing went." + kept);
            return;
        }
    }
    plan.previewed = true;   // shown in full by preview() (RULES: preview first)
    const auto r = m_door.run(plan);
    std::size_t prints = 0;
    if (act == Act::Archive && r.batch) {
        core::catalog::Catalog cat;
        if (cat.open(m_data_dir, true))
            prints = cd::record(cat, m_door, r.batch, m_home, static_cast<std::int64_t>(std::time(nullptr)));
    }
    if (m_changed) m_changed();
    fill();   // it says "Looking..."; the Done line below replaces that and stays
    m_status->set_text(r.ok ? "Done: " + plan.label + " (" + plural(r.done, "step") +
                                  (act == Act::Archive ? "; " + std::to_string(prints) + " fingerprinted" : std::string()) +
                                  ") -- undo in Activity." + kept
                            : plan.label + ": " + std::to_string(r.done) + " done, " + std::to_string(r.failed) +
                                  " not -- " + (r.messages.empty() ? std::string() : r.messages.front()));
    set_visible_child("results");
    if (auto lg = log::get(log::Area::Door))
        lg->info("code run '{}': {} done, {} failed; {} fingerprinted", plan.label, r.done, r.failed, prints);
}

}  // namespace tidy
