// core::layout -- see Layout.hpp.
#include "core/Layout.hpp"
#include "core/Quarantine.hpp"
#include "core/Conform.hpp"
#include "core/Homing.hpp"
#include "core/Archive.hpp"
#include "core/Projects.hpp"
#include "core/Protection.hpp"

#include <latch/Package.hpp>

#include <algorithm>
#include <filesystem>
#include <cctype>
#include <set>

namespace tidy::core::layout {

namespace fs = std::filesystem;
namespace tp = templates;
namespace dr = latch::door;

namespace {
std::string rel_to(const std::string& base, const std::string& p) {
    return p.size() > base.size() + 1 && p.compare(0, base.size() + 1, base + "/") == 0 ? p.substr(base.size() + 1) : "";
}

// Walk `d`: does anything stay in it after the moves? `touched` = something
// moved out from under it. Emptied sub-folders (touched, nothing left, not a
// template folder) go to `out`.
bool remains(const fs::path& d, const std::set<std::string>& moved, const std::set<std::string>& keep,
             bool& touched, std::vector<std::string>& out) {
    bool rem = false;
    std::error_code ec;
    for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string p = it->path().string();
        if (name.empty() || name[0] == '.') { rem = true; continue; }   // a hidden entry: the folder is not empty
        if (moved.count(p)) { touched = true; continue; }
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (!e2 && fs::is_directory(st) && !quarantine::one(p)) {
            bool t = false;
            const bool r = remains(it->path(), moved, keep, t, out);
            if (t) touched = true;
            if (r || keep.count(p)) rem = true;
            else if (t) out.push_back(p);      // emptied by the moves
            else rem = true;                   // empty before: not ours to trash
            continue;
        }
        rem = true;
    }
    return rem;
}
}  // namespace

std::size_t Look::proposed() const {
    return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](const Row& r) { return !r.asked; }));
}
std::size_t Look::asked() const {
    return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](const Row& r) { return r.asked; }));
}

Look look(const tp::Template& t, const std::string& dir) {
    Look l;
    l.dir = dir;
    const auto c = conform::look(t, dir);
    l.in_place = c.in_place;
    l.whole = c.whole;
    for (const auto& m : c.moves) l.rows.push_back({m.from, rel_to(dir, m.from), m.folder, m.why, false});
    for (const auto& u : c.unmatched) {
        // In a stage folder already, and no route says otherwise: you put it there -- in place.
        const bool placed = std::any_of(t.folders.begin(), t.folders.end(),
                                        [&](const tp::Folder& f) { return u.rfind(f.path + "/", 0) == 0; });
        if (placed) { ++l.in_place; continue; }
        l.rows.push_back({dir + "/" + u, u, kLeave, "", true});
    }
    std::error_code ec;
    for (const auto& f : t.folders)
        if (!fs::is_directory(fs::symlink_status(dir + "/" + f.path, ec))) l.missing.push_back(f.path);
    return l;
}

Planned plan(const std::string& home, const tp::Template& t, const Look& l) {
    Planned out;
    const std::string book = rel_to(home, l.dir);
    std::set<std::string> made;
    for (const auto& m : l.missing) {
        dr::Action a;
        a.verb = dr::Verb::MakeDir;
        a.to = l.dir + "/" + m;
        a.reason = "the layout names it";
        out.plan.actions.push_back(a);
        made.insert(a.to);
        ++out.made;
    }
    std::vector<homing::Pick> picks;
    std::set<std::string> moved;
    for (const auto& r : l.rows) {
        if (r.folder == kLeave) continue;
        picks.push_back({r.from, book + (r.folder.empty() ? "" : "/" + r.folder),
                         "layout: " + (r.asked ? std::string("your pick") : r.why)});
        moved.insert(r.from);
    }
    auto hp = homing::plan(home, picks);
    for (auto& a : hp.plan.actions) {
        if (a.verb == dr::Verb::MakeDir && made.count(a.to)) continue;   // made above, in tree order
        if (a.verb == dr::Verb::Move) ++out.moves;
        out.plan.actions.push_back(std::move(a));
    }
    out.renamed = std::move(hp.renamed);

    std::set<std::string> keep;
    for (const auto& f : t.folders) keep.insert(l.dir + "/" + f.path);
    std::vector<std::string> emptied;
    bool touched = false;
    remains(l.dir, moved, keep, touched, emptied);
    std::sort(emptied.begin(), emptied.end());
    for (const auto& e : emptied) {   // topmost only: a folder's Trash takes its emptied children with it
        if (std::any_of(out.emptied.begin(), out.emptied.end(),
                        [&](const std::string& top) { return e.rfind(l.dir + "/" + top + "/", 0) == 0; }))
            continue;
        out.emptied.push_back(rel_to(l.dir, e));
        dr::Action a;
        a.verb = dr::Verb::Trash;
        a.from = e;
        a.reason = "left empty by the moves";
        out.plan.actions.push_back(a);
    }
    const std::string title = fs::path(l.dir).filename().string();
    out.plan.label = "Layout: " + title + " -- " + std::to_string(out.moves) + (out.moves == 1 ? " move" : " moves") +
                     ", " + std::to_string(out.made) + " new folders" +
                     (out.emptied.empty() ? "" : ", " + std::to_string(out.emptied.size()) + " emptied to the Trash");
    return out;
}

// ── s024b: make it a book ─────────────────────────────────────────────────
std::string guess_title(const std::string& name) {
    std::string s = name;
    if (projects::is_project_name(s)) s = s.substr(0, s.size() - std::string(projects::kSuffix).size());
    std::size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i > 0 && i < s.size()) {   // a leading number, then a separator
        std::size_t j = i;
        while (j < s.size() && (s[j] == ' ' || s[j] == '-' || s[j] == '_' || s[j] == '.')) ++j;
        if (j > i && j < s.size()) s = s.substr(j);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s;
}

std::string guess_author(const std::string& dir) {
    const std::string p = fs::path(dir).parent_path().filename().string();
    const auto c = p.find(", ");
    if (c == std::string::npos || c == 0 || p.find(',', c + 1) != std::string::npos || c + 2 >= p.size()) return "";
    return p;
}

std::vector<std::string> candidates(const std::string& dir, bool each_sub) { return conform::folders(dir, each_sub); }

std::string book_refusal(const std::string& home, const tp::Template& t, const std::string& author, const Book& b) {
    const int pd = tp::place_depth(t);
    if (pd <= 0) return "the template " + t.name + " is not known by its place";
    if (pd == 2 && author.empty()) return "no " + (t.group.empty() ? std::string("author") : t.group);
    if (pd == 2) if (auto w = tp::part_refusal(author); !w.empty()) return "author: " + w;
    if (b.title.empty()) return "no title";
    if (auto w = tp::part_refusal(b.title); !w.empty()) return "title: " + w;
    const std::string rel = rel_to(home, b.dir);
    if (rel.empty()) return "not a folder in your home";
    for (const auto& part : fs::path(rel))
        if (!part.empty() && part.string()[0] == '.') return "a hidden folder";
    if (rel == archive::kFolder || rel.rfind(std::string(archive::kFolder) + "/", 0) == 0) return "~/Archive is Tidy's archive";
    if (protection::under(rel)) return "Hands off (Settings), Samson's, or never-touch";
    std::error_code ec;
    const auto st = fs::symlink_status(b.dir, ec);
    if (ec || !fs::is_directory(st)) return "not a folder";
    if (latch::pkg::is_package_name(fs::path(b.dir).filename().string())) return "a bundle -- it belongs inside a book";
    if (fs::exists(fs::symlink_status(b.dir + "/.git", ec))) return "a git repo";
    if (std::string at; projects::inside_whole(rel, &at))
        return at == rel ? "already a project" : "inside " + fs::path(at).filename().string() + " -- a project kept whole";
    if ((t.category + "/").rfind(rel + "/", 0) == 0) return "the shelf ~/" + t.category + " itself, or above it";
    const std::string to = home + "/" + t.category + (pd == 2 ? "/" + author : "") + "/" + b.title;
    if (to == b.dir) return "already there";
    if (fs::exists(fs::symlink_status(to, ec))) return "~/" + rel_to(home, to) + " is taken";
    return "";
}

Made make_books(const std::string& home, const tp::Template& t, const std::string& author, const std::vector<Book>& books) {
    Made out;
    const int pd = tp::place_depth(t);
    std::set<std::string> taken, made;
    std::vector<dr::Action> moves;
    for (const auto& b : books) {
        const std::string name = fs::path(b.dir).filename().string();
        if (auto why = book_refusal(home, t, author, b); !why.empty()) { out.refused.push_back(name + " -- " + why); continue; }
        const std::string shelf = home + "/" + t.category;
        const std::string dir = pd == 2 ? shelf + "/" + author : shelf;
        const std::string to = dir + "/" + b.title;
        if (!taken.insert(to).second) { out.refused.push_back(name + " -- \"" + b.title + "\" twice"); continue; }
        // The shelf's levels and the author's folder, once each, parents first.
        std::string so_far = home;
        for (const auto& part : fs::path(rel_to(home, dir))) {
            so_far += "/" + part.string();
            std::error_code ec;
            if (made.count(so_far) || fs::is_directory(fs::symlink_status(so_far, ec))) continue;
            made.insert(so_far);
            dr::Action a;
            a.verb = dr::Verb::MakeDir;
            a.to = so_far;
            a.reason = so_far == dir && pd == 2 ? "the author's folder" : "the shelf";
            out.plan.actions.push_back(a);
        }
        dr::Action m;
        m.verb = dr::Verb::Move;
        m.from = b.dir;
        m.to = to;
        m.reason = "a book -- moved whole" + (b.title != name ? " (\"" + name + "\" -> \"" + b.title + "\")" : std::string());
        moves.push_back(m);
        ++out.books;
    }
    for (auto& m : moves) out.plan.actions.push_back(std::move(m));
    out.plan.label = "Make books: " + std::to_string(out.books) + (out.books == 1 ? " book" : " books") + " to ~/" +
                     t.category + (pd == 2 && !author.empty() ? "/" + author : "");
    return out;
}

// ── s024c: bookbinding authors ────────────────────────────────────────────
std::string author_name(const std::string& n) {
    std::string s = n;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.find(',') != std::string::npos) return s;
    const auto sp = s.rfind(' ');
    if (sp == std::string::npos || sp == 0) return s;
    return s.substr(sp + 1) + ", " + s.substr(0, sp);
}

std::string author_refusal(const std::string& home, const tp::Template& t, const Author& a) {
    if (a.name.empty()) return "no author";
    if (auto w = tp::part_refusal(a.name); !w.empty()) return "author: " + w;
    const std::string rel = rel_to(home, a.dir);
    if (rel.empty()) return "not a folder in your home";
    for (const auto& part : fs::path(rel))
        if (!part.empty() && part.string()[0] == '.') return "a hidden folder";
    if (rel == archive::kFolder || rel.rfind(std::string(archive::kFolder) + "/", 0) == 0) return "~/Archive is Tidy's archive";
    if (protection::under(rel)) return "Hands off (Settings), Samson's, or never-touch";
    if (rel == t.category || rel.rfind(t.category + "/", 0) == 0) return "on the shelf already";
    if ((t.category + "/").rfind(rel + "/", 0) == 0) return "the shelf is inside it";
    if (std::string at; projects::inside_whole(rel, &at)) return "a project, or inside one";
    std::error_code ec;
    if (!fs::is_directory(fs::symlink_status(a.dir, ec))) return "not a folder";
    if (fs::exists(fs::symlink_status(a.dir + "/.git", ec))) return "a git repo";
    return "";
}

Authors plan_authors(const std::string& home, const tp::Template& t, const std::vector<Author>& authors, bool lay_out) {
    Authors out;
    std::set<std::string> made;
    std::vector<dr::Action> dirs, books, laid, last;
    for (const auto& a : authors) {
        const std::string name = fs::path(a.dir).filename().string();
        if (auto why = author_refusal(home, t, a); !why.empty()) { out.refused.push_back(name + " -- " + why); continue; }
        std::vector<Book> bs;
        for (const auto& d : candidates(a.dir, true)) bs.push_back({d, guess_title(fs::path(d).filename().string())});
        auto m = make_books(home, t, a.name, bs);
        for (const auto& r : m.refused) out.refused.push_back(name + "/" + r);
        std::set<std::string> gone;
        for (auto& x : m.plan.actions) {
            if (x.verb == dr::Verb::MakeDir) {
                if (made.insert(x.to).second) dirs.push_back(x);
                continue;
            }
            gone.insert(x.from);
            if (lay_out) {   // the book laid out where it is, THEN moved whole (the door judges each step on the disk as it is)
                auto lp = plan(home, t, look(t, x.from));
                for (auto& y : lp.plan.actions) laid.push_back(std::move(y));
                out.laid_out += lp.moves;
            }
            books.push_back(std::move(x));
        }
        out.books += m.books;
        if (m.books == 0) continue;
        ++out.authors;
        // What stays in the old author folder: anything not hidden and not moved -> the Closet (a catch-all).
        bool loose = false, hidden = false;
        std::error_code ec;
        for (fs::directory_iterator it(a.dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string n = it->path().filename().string();
            if (n.empty() || n[0] == '.') { hidden = true; continue; }
            if (!gone.count(it->path().string())) loose = true;
        }
        const std::string rel = rel_to(home, a.dir);
        if (loose) out.closet.push_back(rel);
        else if (!hidden) {
            dr::Action tr;
            tr.verb = dr::Verb::Trash;
            tr.from = a.dir;
            tr.reason = "the author's old folder, left empty";
            last.push_back(tr);
            out.trashed.push_back(rel);
        }
    }
    for (auto* v : {&dirs, &laid, &books, &last})
        for (auto& x : *v) out.plan.actions.push_back(std::move(x));
    out.plan.label = "Bookbinding authors: " + std::to_string(out.authors) + (out.authors == 1 ? " author, " : " authors, ") +
                     std::to_string(out.books) + (out.books == 1 ? " book" : " books") +
                     (out.laid_out ? ", " + std::to_string(out.laid_out) + " files laid out" : std::string());
    return out;
}

}  // namespace tidy::core::layout
