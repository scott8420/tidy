#include "SeesPage.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Catalog.hpp"
#include "core/Ebooks.hpp"
#include "core/Homing.hpp"
#include "core/Tagging.hpp"

#include <giomm/file.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/button.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/window.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace tidy {

namespace rc = core::recognise;
namespace dr = latch::door;

namespace {
constexpr std::size_t kRows = 150;   // per section; the rest counted

Gtk::Label* text(const std::string& s, const char* css = nullptr, float x = 0.0f) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(x);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
std::string n_of(std::size_t n, const std::string& one, const std::string& many = "") {
    return std::to_string(n) + " " + (n == 1 ? one : (many.empty() ? one + "s" : many));
}
std::string how_of(rc::Project::How h) {
    switch (h) {
        case rc::Project::How::Name: return "by name (-project)";
        case rc::Project::How::Place: return "on its shelf";
        case rc::Project::How::Book: return "a book in the EBooks library";
        case rc::Project::How::LooksLike: return "looks like a project";
    }
    return "";
}
std::string set_kind(rc::Set::Kind k) {
    switch (k) {
        case rc::Set::Kind::Run: return "a numbered run";
        case rc::Set::Kind::Episodes: return "a series' episodes";
        case rc::Set::Kind::Versions: return "versions of one name";
        case rc::Set::Kind::WebPage: return "a saved web page";
        case rc::Set::Kind::Movie: return "a movie folder";
    }
    return "";
}
}  // namespace

std::string SeesPage::size_of(std::uint64_t b) {
    const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(b);
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    char buf[32];
    if (i == 0) std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(b));
    else std::snprintf(buf, sizeof buf, v < 10 ? "%.1f %s" : "%.0f %s", v, u[i]);
    return buf;
}

SeesPage::SeesPage(Gtk::Window& parent, dr::Door& door, std::string home, std::string data_dir, Go go,
                   Organize organize, Sort sort, Changed changed)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_parent(parent), m_door(door), m_home(std::move(home)),
      m_data_dir(std::move(data_dir)), m_go(std::move(go)), m_organize(std::move(organize)), m_sort(std::move(sort)),
      m_changed(std::move(changed)) {
    set_name("sees.page");

    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(18);
    head->set_margin_bottom(8);
    auto emblem = Gtk::make_managed<Gtk::Image>();
    emblem->set_from_icon_name("tidy-hero-sees");
    emblem->set_pixel_size(48);
    head->append(*emblem);
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    titles->set_hexpand(true);
    titles->set_valign(Gtk::Align::CENTER);
    titles->append(*text("What Tidy sees", "hero-title"));
    m_where = text("Pick a folder: Tidy reads it top down -- bundles, projects, sets, kinds, the Closet. "
                   "Nothing moves until you pick an action and Do it.", "dim");
    m_where->set_name("sees.where");
    m_where->set_selectable(true);
    titles->append(*m_where);
    head->append(*titles);
    auto pick = Gtk::make_managed<Gtk::Button>("Folder…");
    pick->set_name("sees.pick");
    pick->set_valign(Gtk::Align::CENTER);
    pick->signal_clicked().connect([this] { on_pick(); });
    head->append(*pick);
    auto whole = Gtk::make_managed<Gtk::Button>("Whole home");
    whole->set_name("sees.home");
    whole->set_valign(Gtk::Align::CENTER);
    whole->set_tooltip_text("Read the whole home (as tidy --recognise)");
    whole->signal_clicked().connect([this] { look_at(m_home); });
    head->append(*whole);
    append(*head);

    m_summary = text("", nullptr);
    m_summary->set_name("sees.summary");
    m_summary->set_margin_start(32);
    m_summary->set_margin_end(32);
    m_summary->set_selectable(true);
    append(*m_summary);

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_margin_start(32);
    scroll->set_margin_end(32);
    scroll->set_margin_top(8);
    m_body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    m_body->set_name("sees.body");
    m_body->set_margin_bottom(12);
    scroll->set_child(*m_body);
    append(*scroll);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_status = text("", "dim");
    m_status->set_name("sees.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    m_copies = Gtk::make_managed<Gtk::Button>("Check copies");
    m_copies->set_name("sees.copies");
    m_copies->set_sensitive(false);
    m_copies->set_tooltip_text("Look again, reading every likely copy (SHA-256) to say whether they are the same -- slow "
                               "on big folders");
    m_copies->signal_clicked().connect([this] { look(true); });
    bar->append(*m_copies);
    m_again = Gtk::make_managed<Gtk::Button>("Look again");
    m_again->set_name("sees.again");
    m_again->set_sensitive(false);
    m_again->signal_clicked().connect([this] { look(false); });
    bar->append(*m_again);
    append(*bar);
}

std::string SeesPage::rel_of(const std::string& abs) const {
    if (abs == m_home) return "";
    if (abs.rfind(m_home + "/", 0) == 0) return abs.substr(m_home.size() + 1);
    return abs;
}

void SeesPage::look_at(const std::string& dir) {
    if (dir.empty()) { on_pick(); return; }
    m_dir = dir;
    look(false);
}

void SeesPage::on_pick() {
    auto dlg = Gtk::FileDialog::create();
    dlg->set_title("A folder for Tidy to look at");
    dlg->set_modal(true);
    dlg->set_initial_folder(Gio::File::create_for_path(m_dir.empty() ? m_home : m_dir));
    dlg->select_folder(m_parent, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            const auto f = dlg->select_folder_finish(res);
            if (!f || f->get_path().empty()) return;
            const std::string p = f->get_path();
            if (p != m_home && p.rfind(m_home + "/", 0) != 0) {
                m_status->set_text("That folder is not in your home -- Tidy only looks inside " + m_home + ".");
                return;
            }
            look_at(p);
        } catch (const Glib::Error&) {}   // cancelled
    });
}

void SeesPage::look(bool deep) {
    if (m_dir.empty()) return;
    const std::string home = m_home, from = m_dir, data = m_data_dir;
    auto out = std::make_shared<rc::Report>();
    auto books = std::make_shared<std::size_t>(0);
    busy::start(m_parent, deep ? "What Tidy sees -- checking copies" : "What Tidy sees -- looking",
                deep ? "Reading the likely copies, file by file" : "Reading the folder top down (nothing moves)",
                [out, books, home, from, data, deep] {
                    *out = rc::scan(home, from, data, deep);
                    *books = core::ebooks::find(home, from).size();   // s028: Make them books...
                },
                [this, out, books] {
                    m_report = out;
                    m_books = *books;
                    show(*out);
                    if (auto lg = log::get(log::Area::Shell))
                        lg->info("sees: ~/{} -- {} bundles, {} projects, {} sets, {} kinds ({} ms)", rel_of(out->from),
                                 out->bundles.size(), out->projects.size(), out->sets.size(), out->kinds.size(), out->ms);
                });
}

Gtk::Box* SeesPage::section(const std::string& title, const std::string& sub) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->add_css_class("card");
    auto top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    auto t = text(title, "card-title");
    top->append(*t);
    auto s = text(sub, "card-sub");
    s->set_hexpand(true);
    top->append(*s);
    box->append(*top);
    m_body->append(*box);
    return box;
}

Gtk::Box* SeesPage::row(Gtk::Box* into, const std::string& title, const std::string& detail) {
    auto r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    r->set_margin_start(8);
    auto words = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
    words->set_hexpand(true);
    words->append(*text(title));
    if (!detail.empty()) words->append(*text(detail, "card-sub"));
    r->append(*words);
    into->append(*r);
    return r;
}

Gtk::Button* SeesPage::button(Gtk::Box* into, const std::string& label, std::function<void()> f) {
    auto b = Gtk::make_managed<Gtk::Button>(label);
    b->add_css_class("ghost");
    b->add_css_class("small");
    b->set_valign(Gtk::Align::CENTER);
    b->signal_clicked().connect(std::move(f));
    into->append(*b);
    return b;
}

void SeesPage::show(const rc::Report& r) {
    while (auto* c = m_body->get_first_child()) m_body->remove(*c);
    m_again->set_sensitive(true);
    m_copies->set_sensitive(true);
    const std::string where = rel_of(r.from);
    m_where->set_text(where.empty() ? "Your whole home (~)" : "~/" + where);

    std::size_t loose_b = 0, in_proj = 0, held = 0;
    for (const auto& b : r.bundles) (b.held ? held : b.project.empty() ? loose_b : in_proj) += 1;
    m_summary->set_text(n_of(r.bundles.size(), "bundle") + ", " + n_of(r.projects.size(), "project") + ", " +
                        n_of(r.sets.size(), "set") + " (" + n_of(r.in_sets, "file") + "), " +
                        n_of(r.in_kinds, "file") + " by kind, " + n_of(r.in_closet, "file") + " in the Closet" +
                        (r.code_items ? ", " + n_of(r.code_items, "code item") : "") + ".   " +
                        n_of(r.folders, "folder") + " looked at, " + std::to_string(r.ms) + " ms.");

    // Projects and bundles are left out of a folder's Sort (they are kept whole).
    std::vector<std::string> whole;
    for (const auto& p : r.projects) whole.push_back(p.rel);
    for (const auto& b : r.bundles) whole.push_back(b.rel);

    // 1 Bundles
    if (!r.bundles.empty()) {
        auto s = section("1  Bundles", "one thing each, never looked inside -- " + std::to_string(loose_b) + " loose, " +
                                           std::to_string(in_proj) + " in projects, " + std::to_string(held) + " held");
        std::size_t shown = 0, more = 0;
        for (const auto& b : r.bundles) {
            if (!b.held && !b.project.empty()) continue;   // counted with its project
            if (shown++ >= kRows) { ++more; continue; }
            const std::string facts = "[" + size_of(b.bytes) + ", " + n_of(b.files, "file") + "]";
            if (b.held) {
                auto rw = row(s, "~/" + b.rel, "held in Quarantine -- not entered until you answer  " + facts);
                rw->set_name("sees.bundle.held");
                button(rw, "Answer in Quarantine", [this] { m_go("quarantine"); });
                continue;
            }
            const bool there = b.home == "=";   // core::recognise: already under its kind's home
            auto rw = row(s, "~/" + b.rel,
                          (b.home.empty() ? "." + b.ext + " has no home yet" : there ? "at home" : "its home: ~/" + b.home) +
                              "  " + facts);
            rw->set_name("sees.bundle");
            if (!b.home.empty() && !there) button(rw, "Send home…", [this, b] { send_home(b); });
        }
        if (more) s->append(*text("... and " + std::to_string(more) + " more", "card-sub"));
        if (in_proj) s->append(*text(n_of(in_proj, "bundle") + " inside projects stay with them.", "card-sub"));
    }

    // 2 Projects
    if (!r.projects.empty()) {
        auto s = section("2  Projects", "trees kept whole -- organize one as its type");
        std::size_t shown = 0;
        for (const auto& p : r.projects) {
            if (shown++ >= kRows) continue;
            std::string d = how_of(p.how) + "  [" + size_of(p.bytes) + ", " + n_of(p.files, "file") +
                            (p.bundles ? "; " + n_of(p.bundles, "bundle") : "") + "]";
            if (!p.tpl.empty()) {
                d += "\ntree: " + p.tpl + " " + std::to_string(p.have) + "/" + std::to_string(p.of) + " folders";
                if (p.routed) d += "; placed " + std::to_string(p.placed) + " of " + std::to_string(p.routed);
            } else if (!p.maybe.empty()) {
                d += "\nmaybe an old " + p.maybe + " (its folders are " + p.maybe + "'s)";
            } else {
                d += "\nno type fits half its tree";
            }
            if (!p.working.empty()) {
                d += "; working folders:";
                for (const auto& w : p.working) d += " " + w;
            }
            if (!p.shelf.empty()) d += "\nNOT ON ITS SHELF -- it lives at ~/" + p.shelf;
            auto rw = row(s, "~/" + p.rel, d);
            rw->set_name("sees.project");
            const std::string type = !p.tpl.empty() ? p.tpl : p.maybe;
            const std::string abs = m_home + "/" + p.rel;
            button(rw, type.empty() ? "Organize as…" : "Organize as " + type + "…",
                   [this, abs, type] { if (m_organize) m_organize(abs, type); });
        }
        if (r.projects.size() > kRows)
            s->append(*text("... and " + std::to_string(r.projects.size() - kRows) + " more", "card-sub"));
        for (const auto& pr : r.proposed) {
            std::string shared;
            for (const auto& f : pr.shared) shared += (shared.empty() ? "" : " ") + f;
            row(s, "A new type? ~/" + pr.parent + ": " + n_of(pr.members.size(), "look-alike"),
                "they share: " + shared + "; the fullest: " + pr.fullest + " -- design it in Types");
        }
    }

    // 3 Sets + 4 Kinds: loose files -> Sort, for this folder
    const auto sort_here = [this, where, whole] { if (m_sort) m_sort(where, whole); };
    if (!r.sets.empty()) {
        auto s = section("3  Sets", "loose files that belong together -- " + n_of(r.in_sets, "file"));
        std::size_t shown = 0;
        for (const auto& st : r.sets) {
            if (shown++ >= kRows) continue;
            std::string in = st.folders.empty() ? "" : "in ~/" + st.folders.front();
            if (st.folders.size() > 1) in += " +" + std::to_string(st.folders.size() - 1) + " more folders";
            row(s, st.title + "  (" + std::to_string(st.count) + ")", set_kind(st.kind) + (in.empty() ? "" : ", " + in));
        }
        if (r.sets.size() > kRows) s->append(*text("... and " + std::to_string(r.sets.size() - kRows) + " more", "card-sub"));
        auto b = button(s, "Sort these…", sort_here);
        b->set_name("sees.sets.sort");
        b->set_halign(Gtk::Align::END);
    }
    if (!r.kinds.empty()) {
        auto s = section("4  Kinds", "what is left, by the type table -- " + n_of(r.in_kinds, "file"));
        for (const auto& k : r.kinds) {
            std::vector<std::pair<std::size_t, std::string>> e;
            for (const auto& [f, n] : k.elsewhere) e.push_back({n, f});
            std::sort(e.rbegin(), e.rend());
            std::string d = std::to_string(k.at_home) + " at home, " + std::to_string(k.count - k.at_home) + " elsewhere";
            for (std::size_t i = 0; i < e.size() && i < 3; ++i)
                d += (i ? ", " : ": ") + std::to_string(e[i].first) + " in ~/" + e[i].second;
            if (e.size() > 3) d += " +" + std::to_string(e.size() - 3) + " more folders";
            row(s, k.kind + "  (" + std::to_string(k.count) + ")", d);
        }
        const bool books = m_books > 0;   // s028: loose ebooks -> their own title folders on a shelf
        if (books)
            row(s, "Ebooks  (" + std::to_string(m_books) + ")", "loose ebook files -- each can get its own title folder on a shelf");
        auto acts = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        acts->set_halign(Gtk::Align::END);
        s->append(*acts);
        if (books && m_ebooks) {
            auto e = button(acts, "Make them books\u2026", [this] { if (m_ebooks) m_ebooks(m_dir); });
            e->set_name("sees.kinds.ebooks");
            e->set_tooltip_text("Each loose ebook into its own title folder: ~/Entertainment/EBooks/<Genre>/<Last, First>/<Title>");
        }
        auto b = button(acts, "Sort these\u2026", sort_here);
        b->set_name("sees.kinds.sort");
    }

    // 5 The Closet
    if (!r.closet.empty()) {
        auto s = section("5  The Closet", "nothing claims these -- " + n_of(r.in_closet, "file"));
        std::vector<std::pair<std::size_t, std::string>> by;
        for (const auto& [ext, v] : r.closet) by.push_back({v.first, ext});
        std::sort(by.rbegin(), by.rend());
        for (const auto& [n, ext] : by) {
            const auto& folders = r.closet.at(ext).second;
            std::string most;
            std::size_t mn = 0;
            for (const auto& [f, c] : folders)
                if (c > mn) { mn = c; most = f; }
            row(s, (ext.empty() ? "(no extension)" : ext) + ": " + std::to_string(n),
                most.empty() ? "" : "most in ~/" + most);
        }
        auto b = button(s, "Open the Closet", [this] { m_go("closet"); });
        b->set_halign(Gtk::Align::END);
    }

    // Likely copies (Check copies reads them)
    if (!r.copies.empty()) {
        auto s = section("Likely copies", "the same size and file count");
        for (const auto& c : r.copies) {
            std::string names;
            for (const auto& x : c.rels) names += (names.empty() ? "~/" : "\n~/") + x;
            row(s, names, size_of(c.bytes) + ", " + n_of(c.files, "file") + " each -- " +
                              (c.same == 1 ? "THE SAME (read)" : c.same == 0 ? "they DIFFER (read)" : "not read: Check copies"));
        }
    }
    if (r.code_items) {
        auto s = section("Code", "~/Develop -- one item each, not walked");
        row(s, n_of(r.code_items, "item") + ", " + size_of(r.code_bytes), "see Projects -> Code");
    }
    if (r.bundles.empty() && r.projects.empty() && r.sets.empty() && r.kinds.empty() && r.closet.empty())
        m_body->append(*text("Nothing here for Tidy to see -- an empty folder, or all of it kept whole.", "dim"));
    m_status->set_text(m_after.empty() ? "Read-only: nothing moved. Pick an action -- each shows its plan first." : m_after);
    m_after.clear();
}

void SeesPage::send_home(const rc::Bundle& b) {
    const std::string abs = m_home + "/" + b.rel;
    auto planned = core::homing::plan(m_home, {{abs, b.home, "a loose ." + b.ext + " bundle -> its kind's home"}});
    auto plan = std::move(planned.plan);
    plan.label = "Send home: " + fs::path(b.rel).filename().string();
    std::string steps, refused;
    std::vector<std::string> made;
    for (const auto& a : plan.actions) {
        if (const auto why = m_door.refusal(a, made); !why.empty()) refused += "\n  refused: " + why;
        if (a.verb == dr::Verb::MakeDir) { made.push_back(a.to); steps += "\n  make ~/" + rel_of(a.to); }
        else if (a.verb == dr::Verb::Move) steps += "\n  move ~/" + rel_of(a.from) + "\n    -> ~/" + rel_of(a.to);
    }
    for (const auto& r : planned.renamed) steps += "\n  (taken: " + r + ")";
    auto dlg = Gtk::AlertDialog::create("Send home -- " + fs::path(b.rel).filename().string());
    dlg->set_detail("Moved whole, never opened. One undo step in Activity." + steps + refused);
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
                m_after = r.ok ? "Sent home -- one undo in Activity."
                               : "Not done: " + (r.messages.empty() ? std::string("see the log") : r.messages.front());
                m_status->set_text(m_after);
                if (auto lg = log::get(log::Area::Door)) lg->info("sees: send home -- {} done, {} failed", r.done, r.failed);
                if (fs::is_directory(m_dir)) look(false);
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
