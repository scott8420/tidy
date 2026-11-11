#include "PurposesPage.hpp"
#include "Log.hpp"
#include "core/Birth.hpp"
#include "core/Catalog.hpp"
#include "core/Own.hpp"
#include "core/Pages.hpp"
#include "core/Projects.hpp"
#include "core/Tagging.hpp"

#include <latch/Protect.hpp>

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>

#include <ctime>
#include <filesystem>
#include <map>

namespace tidy {

namespace pu = core::purposes;
namespace dr = latch::door;
namespace fs = std::filesystem;

namespace {
constexpr std::size_t kMaxRows = 300;
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
    i->set_from_icon_name("tidy-hero-purposes");
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
Gtk::ScrolledWindow* scrolled(Gtk::Widget& child) {
    auto s = Gtk::make_managed<Gtk::ScrolledWindow>();
    s->set_vexpand(true);
    s->set_margin_start(32);
    s->set_margin_end(32);
    s->set_margin_bottom(16);
    s->set_child(child);
    return s;
}
Gtk::ListBox* listbox(const std::string& name) {
    auto l = Gtk::make_managed<Gtk::ListBox>();
    l->set_name(name);
    l->set_selection_mode(Gtk::SelectionMode::NONE);
    return l;
}
void clear(Gtk::ListBox* l) {
    while (auto* row = l->get_row_at_index(0)) l->remove(*row);
}
Gtk::Box* bar_box() {
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    return bar;
}
Gtk::Box* append_row(Gtk::ListBox* list, int margin = 8) {
    auto b = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    b->set_margin(margin);
    auto row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_activatable(false);
    row->set_child(*b);
    list->append(*row);
    return b;
}
}  // namespace

PurposesPage::PurposesPage(dr::Door& door, std::string home, std::string data_dir, bool fixture, Text user_dirs,
                           Changed changed)
    : m_door(door), m_home(std::move(home)), m_data_dir(std::move(data_dir)), m_fixture(fixture),
      m_user_dirs(std::move(user_dirs)), m_changed(std::move(changed)) {
    set_name("purposes.states");
    build_start();
    build_results();
    build_preview();
    set_visible_child("start");
}

std::string PurposesPage::shown(const std::string& path) const {
    if (path == m_home) return "~";
    if (path.rfind(m_home + "/", 0) == 0) return "~" + path.substr(m_home.size());
    return path;
}

void PurposesPage::build_start() {
    const auto* p = core::pages::find("purposes");
    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("purposes.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero(128));
    start->append(*label(p ? p->title : "Purposes", "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(std::string("Name a purpose (Taxes 2025), give it files, and Tidy learns: the folder a file sat in, or a ") +
                      "name pattern you teach, becomes a guess for the next one. Confirm a guess and it grows "
                      "surer; correct it and it learns that too. Close a purpose and its files follow their role. "
                      "Tags only -- nothing moves; every change undoable from Activity." +
                      (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : ""), "dim");
    note->set_name("purposes.note");
    note->set_max_width_chars(60);
    start->append(*note);
    auto orb = Gtk::make_managed<Gtk::Button>("Look");
    orb->set_name("purposes.look");
    orb->add_css_class("orb");
    orb->set_halign(Gtk::Align::CENTER);
    orb->set_margin_top(18);
    orb->signal_clicked().connect([this] { fill(); set_visible_child("results"); });
    start->append(*orb);
    add(*start, "start");
}

void PurposesPage::build_results() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("purposes.results");
    auto head = head_box(&m_summary, "Purposes", "purposes.summary");
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("purposes.again");
    again->set_valign(Gtk::Align::CENTER);
    again->signal_clicked().connect([this] { fill(); });
    head->append(*again);
    box->append(*head);
    m_list = listbox("purposes.list");
    box->append(*scrolled(*m_list));
    auto bar = bar_box();
    m_status = label("Check files, pick each one's purpose, then Assign. Tidy learns from what you choose.", "dim", 0.0f);
    m_status->set_name("purposes.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto assign = Gtk::make_managed<Gtk::Button>("Assign");
    assign->set_name("purposes.assign");
    assign->add_css_class("act");
    assign->set_tooltip_text("The checked files -> the purpose beside each (previewed first)");
    assign->signal_clicked().connect([this] { on_assign(); });
    bar->append(*assign);
    box->append(*bar);
    add(*box, "results");
}

void PurposesPage::build_preview() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("purposes.preview");
    box->append(*head_box(&m_prev_summary, "Preview", "purposes.preview.summary"));
    m_prev_list = listbox("purposes.preview.list");
    box->append(*scrolled(*m_prev_list));
    auto bar = bar_box();
    m_prev_status = label("", "dim", 0.0f);
    m_prev_status->set_hexpand(true);
    bar->append(*m_prev_status);
    auto back = Gtk::make_managed<Gtk::Button>("Back");
    back->set_name("purposes.preview.back");
    back->signal_clicked().connect([this] { m_plan.reset(); m_pending = Pending::None; set_visible_child("results"); });
    bar->append(*back);
    m_prev_run = Gtk::make_managed<Gtk::Button>("Do it");
    m_prev_run->set_name("purposes.preview.run");
    m_prev_run->add_css_class("act");
    m_prev_run->signal_clicked().connect([this] { on_run(); });
    bar->append(*m_prev_run);
    box->append(*bar);
    add(*box, "preview");
}

// The files to give a purpose: the landing places' (the birth report), and the
// files one folder down in Documents (Documents/Taxes/w2.pdf) -- where a
// folder says something. Projects and protected places are left out.
std::vector<core::facts::Facts> PurposesPage::candidates(std::vector<std::string>* landing) const {
    std::vector<core::facts::Facts> out;
    const auto places = core::birth::places(m_home, m_user_dirs ? m_user_dirs() : "");
    for (const auto& pl : places) if (landing) landing->push_back(fs::path(pl.path).filename().string());
    // A project is kept whole: nothing inside a <name>-project is offered file by file.
    const auto in_project = [](const std::string& path) {
        for (const auto& part : fs::path(path).parent_path())
            if (core::projects::is_project_name(part.string())) return true;
        return false;
    };
    for (const auto& b : core::birth::report(m_home, places).items)
        if (!b.file.dir && !in_project(b.file.path)) out.push_back(b.file);
    std::string docs;
    for (const auto& pl : places) if (pl.id == "documents") docs = pl.path;
    std::error_code ec;
    if (!docs.empty())
        for (fs::directory_iterator it(docs, ec), end; !ec && it != end; it.increment(ec)) {
            const auto fn = it->path().filename().string();
            if (fn.empty() || fn[0] == '.' || !it->is_directory(ec) || it->is_symlink(ec) ||
                core::projects::is_project_name(fn) || latch::protect::here(fs::relative(it->path(), m_home).string()))
                continue;
            std::error_code e2;
            for (fs::directory_iterator f(it->path(), e2), fe; !e2 && f != fe; f.increment(e2)) {
                const auto n = f->path().filename().string();
                if (n.empty() || n[0] == '.' || !f->is_regular_file(e2) || f->is_symlink(e2)) continue;
                core::facts::Facts x;
                if (core::facts::of(f->path().string(), x)) out.push_back(x);
            }
        }
    return out;
}

// The files carrying a purpose: the catalog's record, read back fresh.
std::vector<core::facts::Facts> PurposesPage::carrying(const std::string& purpose) const {
    std::vector<core::facts::Facts> out;
    core::catalog::Catalog cat;
    if (!cat.open(m_data_dir, false)) return out;
    for (const auto& e : cat.all()) {
        if (e.gone || e.purpose != purpose) continue;
        core::facts::Facts f;
        if (core::facts::of(e.path, f) && f.tags.purpose == purpose) out.push_back(f);
    }
    return out;
}

void PurposesPage::refresh_if_shown() {
    if (get_visible_child_name() == "results") fill();
}

void PurposesPage::save_book(const std::string& done) {
    const auto err = core::own::write(m_data_dir, pu::kFile, pu::format(m_book));
    m_status->set_text(err.empty() ? done : "Not saved: " + err);
}

void PurposesPage::fill() {
    m_book = pu::parse(core::own::read(m_data_dir, pu::kFile));
    m_landing.clear();
    const auto files = candidates(&m_landing);
    clear(m_list);
    m_rows.clear();
    m_open.clear();
    for (const auto& p : m_book.purposes) if (!p.closed) m_open.push_back(p.id);

    const auto section = [&](const std::string& text) {
        auto l = label(text, "heading", 0.0f);
        l->set_margin_top(12);
        l->set_margin_start(4);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*l);
        m_list->append(*row);
    };
    const auto purpose_model = [&] {
        auto m = Gtk::StringList::create({"--"});
        for (const auto& id : m_open) m->append(m_book.find(id)->title);
        return m;
    };

    // Purposes: add, close / reopen.
    section("Purposes");
    {
        auto b = append_row(m_list);
        auto e = Gtk::make_managed<Gtk::Entry>();
        e->set_name("purposes.new");
        e->set_placeholder_text("A new purpose -- Taxes 2025");
        e->set_hexpand(true);
        b->append(*e);
        auto add = Gtk::make_managed<Gtk::Button>("Add");
        add->set_name("purposes.add");
        const auto go = [this, e] {
            std::string id;
            if (auto err = pu::add(m_book, e->get_text(), &id); !err.empty()) { m_status->set_text(err); return; }
            const std::string t = e->get_text();
            save_book("Added \"" + t + "\".");
            const auto msg = m_status->get_text();
            fill();
            m_status->set_text(msg);
        };
        add->signal_clicked().connect(go);
        e->signal_activate().connect(go);
        b->append(*add);
    }
    std::map<std::string, std::size_t> counts;
    {
        core::catalog::Catalog cat;
        if (cat.open(m_data_dir, false))
            for (const auto& e : cat.all()) if (!e.gone && !e.purpose.empty()) ++counts[e.purpose];
    }
    for (const auto& p : m_book.purposes) {
        auto b = append_row(m_list, 6);
        auto t = label(p.title + (p.closed ? "   (closed)" : ""), p.closed ? "dim" : nullptr, 0.0f);
        t->set_hexpand(true);
        b->append(*t);
        auto n = label(std::to_string(counts[p.id]) + " file" + (counts[p.id] == 1 ? "" : "s"), "dim");
        n->set_wrap(false);
        b->append(*n);
        auto btn = Gtk::make_managed<Gtk::Button>(p.closed ? "Reopen" : "Close...");
        btn->set_name("purposes.close." + p.id);
        btn->set_tooltip_text(p.closed ? "Open it again (the retentions closing set stay -- undo them in Activity)"
                                       : "Its files follow their role: product -> Keep, input -> Ephemeral, history or none -> Historical");
        const std::string id = p.id;
        const bool closed = p.closed;
        btn->signal_clicked().connect([this, id, closed] {
            if (!closed) { on_close(id); return; }
            pu::set_closed(m_book, id, false);
            save_book("Reopened.");
            const auto msg = m_status->get_text();
            fill();
            m_status->set_text(msg);
        });
        b->append(*btn);
    }
    // Teach a name pattern.
    if (!m_open.empty()) {
        auto b = append_row(m_list);
        b->append(*label("Teach:", nullptr, 0.0f));
        auto e = Gtk::make_managed<Gtk::Entry>();
        e->set_name("purposes.teach.pattern");
        e->set_placeholder_text("a name pattern -- *W-2*");
        e->set_hexpand(true);
        b->append(*e);
        auto arrow = label("->", "dim");
        arrow->set_wrap(false);
        b->append(*arrow);
        auto dd = Gtk::make_managed<Gtk::DropDown>(purpose_model());
        dd->set_name("purposes.teach.purpose");
        dd->set_selected(1);
        b->append(*dd);
        auto teach = Gtk::make_managed<Gtk::Button>("Teach");
        teach->set_name("purposes.teach");
        teach->signal_clicked().connect([this, e, dd] {
            const auto i = dd->get_selected();
            if (i == 0 || i > m_open.size()) { m_status->set_text("Pick the purpose it means."); return; }
            if (auto err = pu::teach(m_book, e->get_text(), m_open[i - 1]); !err.empty()) { m_status->set_text(err); return; }
            save_book("Learned: " + std::string(e->get_text()) + " -> " + m_book.find(m_open[i - 1])->title);
            const auto msg = m_status->get_text();
            fill();
            m_status->set_text(msg);
        });
        b->append(*teach);
    }

    // Files without a purpose.
    std::size_t without = 0, guessed = 0;
    for (const auto& f : files) without += f.tags.purpose.empty();
    section("Without a purpose -- " + std::to_string(without) + " in the landing places and Documents' folders" +
            (without > kMaxRows ? " (first " + std::to_string(kMaxRows) + " shown)" : ""));
    for (const auto& f : files) {
        if (!f.tags.purpose.empty() || m_rows.size() >= kMaxRows) continue;
        Row r;
        r.file = f;
        r.guess = pu::guess(m_book, f.path, m_landing);
        auto b = append_row(m_list, 4);
        r.check = Gtk::make_managed<Gtk::CheckButton>();
        r.check->set_name("purposes.file." + f.path);
        b->append(*r.check);
        auto t = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        t->set_hexpand(true);
        t->append(*label(fs::path(f.path).filename().string(), nullptr, 0.0f));
        const auto* gp = r.guess.purpose.empty() ? nullptr : m_book.find(r.guess.purpose);
        t->append(*label(shown(fs::path(f.path).parent_path().string()) +
                         (gp ? "   -- Tidy guesses " + gp->title + " (" + r.guess.because + ")" : ""), "dim", 0.0f));
        b->append(*t);
        r.pick = Gtk::make_managed<Gtk::DropDown>(purpose_model());
        r.pick->set_name("purposes.pick." + f.path);
        if (gp) {
            for (std::size_t i = 0; i < m_open.size(); ++i)
                if (m_open[i] == r.guess.purpose) r.pick->set_selected(static_cast<guint>(i + 1));
            r.check->set_active(true);
            ++guessed;
        }
        auto* chk = r.check;
        r.pick->property_selected().signal_changed().connect([chk, dd = r.pick] { if (dd->get_selected() > 0) chk->set_active(true); });
        b->append(*r.pick);
        m_rows.push_back(r);
    }
    m_summary->set_text(std::to_string(m_open.size()) + " open purpose" + (m_open.size() == 1 ? "" : "s") + "   |   " +
                        std::to_string(without) + " files without one, " + std::to_string(guessed) + " guessed   |   " +
                        std::to_string(m_book.hints.size()) + " learned hint" + (m_book.hints.size() == 1 ? "" : "s"));
}

void PurposesPage::on_assign() {
    std::map<std::string, std::vector<core::facts::Facts>> by;
    m_learn_paths.clear();
    m_learn_guesses.clear();
    for (const auto& r : m_rows) {
        if (!r.check->get_active()) continue;
        const auto i = r.pick->get_selected();
        if (i == 0 || i > m_open.size()) continue;
        by[m_open[i - 1]].push_back(r.file);
        m_learn_paths.emplace_back(r.file.path, m_open[i - 1]);
        m_learn_guesses.push_back(r.guess);
    }
    if (by.empty()) { m_status->set_text("Check a file and pick its purpose first."); return; }
    dr::Plan plan;
    std::size_t n = 0;
    for (const auto& [id, fs_] : by) {
        auto one = pu::assign(fs_, *m_book.find(id));
        plan.actions.insert(plan.actions.end(), one.actions.begin(), one.actions.end());
        n += fs_.size();
    }
    plan.label = by.size() == 1 ? pu::assign(by.begin()->second, *m_book.find(by.begin()->first)).label
                                : "Purpose: " + std::to_string(n) + " files -> " + std::to_string(by.size()) + " purposes";
    preview(std::move(plan), Pending::Assign);
}

void PurposesPage::on_close(const std::string& id) {
    const auto* p = m_book.find(id);
    if (!p) return;
    auto plan = pu::close(carrying(id), *p);
    m_closing = id;
    if (plan.actions.empty()) {   // nothing to change: just mark it closed
        pu::set_closed(m_book, id, true);
        save_book("Closed \"" + p->title + "\" -- no file needed a change.");
        const auto msg = m_status->get_text();
        fill();
        m_status->set_text(msg);
        return;
    }
    preview(std::move(plan), Pending::Close);
}

void PurposesPage::preview(dr::Plan plan, Pending what) {
    clear(m_prev_list);
    std::size_t refused = 0;
    for (const auto& a : plan.actions) {
        const std::string why = m_door.refusal(a);
        refused += !why.empty();
        const std::string key = a.attr.rfind(dr::kTagPrefix, 0) == 0 ? a.attr.substr(std::string(dr::kTagPrefix).size()) : a.attr;
        auto l = label(key + " = " + (key == "id" ? std::string("a new id") : a.content) + "     " + shown(a.from) +
                           (why.empty() ? "" : "     -- REFUSED: " + why), key == "id" ? "dim" : nullptr, 0.0f);
        l->set_margin(4);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*l);
        m_prev_list->append(*row);
    }
    m_prev_summary->set_text(plan.label + "   |   " + std::to_string(plan.actions.size()) + " steps   |   tags only: nothing moves");
    m_prev_status->set_text(refused ? std::to_string(refused) + " step(s) the door refuses -- they will not happen"
                                    : "Every step can be undone from Activity.");
    m_prev_run->set_sensitive(plan.actions.size() > refused);
    m_plan = std::move(plan);
    m_pending = what;
    set_visible_child("preview");
}

void PurposesPage::on_run() {
    if (!m_plan) return;
    auto plan = std::move(*m_plan);
    m_plan.reset();
    std::erase_if(plan.actions, [&](const dr::Action& a) { return !m_door.refusal(a).empty(); });
    plan.previewed = true;   // shown in full by preview() (RULES: preview first)
    const auto r = m_door.run(plan);
    core::catalog::Catalog cat;
    if (cat.open(m_data_dir, true))
        core::tagging::record(cat, core::tagging::paths_of(m_door, r.batch), static_cast<std::int64_t>(std::time(nullptr)));
    std::string learned;
    if (r.ok && m_pending == Pending::Assign) {
        const auto before = m_book.hints.size();
        for (std::size_t i = 0; i < m_learn_paths.size(); ++i)
            pu::learn(m_book, m_learn_paths[i].first, m_learn_paths[i].second, m_learn_guesses[i], m_landing);
        learned = " -- learned (" + std::to_string(m_book.hints.size()) + " hints, " +
                  std::to_string(m_book.hints.size() - before) + " new)";
        core::own::write(m_data_dir, pu::kFile, pu::format(m_book));
    }
    if (r.ok && m_pending == Pending::Close) {
        pu::set_closed(m_book, m_closing, true);
        core::own::write(m_data_dir, pu::kFile, pu::format(m_book));
    }
    m_pending = Pending::None;
    if (m_changed) m_changed();
    fill();
    set_visible_child("results");
    m_status->set_text(r.ok ? "Done: " + plan.label + " (" + std::to_string(r.done) + " steps) -- undo in Activity" + learned
                            : plan.label + ": " + std::to_string(r.done) + " done, " + std::to_string(r.failed) +
                                  " not -- " + (r.messages.empty() ? std::string() : r.messages.front()));
    if (auto lg = log::get(log::Area::Door))
        lg->info("purposes run '{}': {} done, {} failed{}", plan.label, r.done, r.failed, learned);
}

}  // namespace tidy
