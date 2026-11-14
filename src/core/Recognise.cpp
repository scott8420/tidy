#include "core/Recognise.hpp"
#include "core/Home.hpp"

#include "core/Archive.hpp"
#include "core/Due.hpp"
#include "core/Homing.hpp"
#include "core/Own.hpp"
#include "core/Projects.hpp"
#include "core/Protection.hpp"
#include "core/Quarantine.hpp"
#include "core/Settings.hpp"
#include "core/Sha256.hpp"
#include "core/Shape.hpp"
#include "core/Sort.hpp"
#include "core/Suspects.hpp"
#include "core/Templates.hpp"

#include <latch/Package.hpp>
#include <latch/Protect.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>

namespace tidy::core::recognise {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool ends(const std::string& s, const std::string& t) {
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
bool starts(const std::string& s, const std::string& t) { return s.rfind(t, 0) == 0; }
std::string parent_of(const std::string& rel) {
    const auto p = rel.rfind('/');
    return p == std::string::npos ? "" : rel.substr(0, p);
}
std::string name_of(const std::string& rel) {
    const auto p = rel.rfind('/');
    return p == std::string::npos ? rel : rel.substr(p + 1);
}
std::string stem_of(const std::string& name) {
    const auto d = name.rfind('.');
    return d == std::string::npos || d == 0 ? name : name.substr(0, d);
}
std::string fext_of(const std::string& name) {   // file extension, lower case, with the dot; "" none
    const auto d = name.rfind('.');
    return d == std::string::npos || d == 0 || d + 1 == name.size() ? "" : lower(name.substr(d));
}

constexpr const char* kNever = "(never moved -- a password database)";
constexpr const char* kWithVideo = "(with its video -- subtitles, nfo)";

std::set<std::string>& vocabulary() {   // the working-folder names (lower case)
    static std::set<std::string> v = {"workups", "images", "fonts", "assets", "drafts", "archive", "print", "layout"};
    return v;
}

// s026a: held is worked out NOW (the suspect scan's held list), not read
// from the last Quarantine Save -- a stale "held" folder is walked as answered.
std::set<std::string> g_live;
std::string g_home;
std::string rel_in_home(const std::string& abs) {
    return abs.size() > g_home.size() + 1 && starts(abs, g_home + "/") ? abs.substr(g_home.size() + 1) : abs;
}
bool is_held(const std::string& abs) { return g_live.count(rel_in_home(abs)) > 0; }
bool is_one(const std::string& abs) {
    if (is_held(abs)) return true;
    if (!quarantine::one(abs)) return false;
    // one only because a stale held list says so -> not one
    return !(quarantine::held(abs) && !latch::pkg::is_package_name(fs::path(abs).filename().string()));
}
bool is_video(const std::string& name) {
    static const std::set<std::string> v = {".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v"};
    const auto d = name.rfind('.');
    return d != std::string::npos && v.count(lower(name.substr(d))) > 0;
}

void measure(const std::string& dir, std::uint64_t& bytes, std::size_t& files) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || fs::is_symlink(st)) continue;
        if (fs::is_regular_file(st)) {
            ++files;
            const auto n = it->file_size(e2);
            if (!e2) bytes += n;
        }
    }
}

// A folder's digest: every regular file's relative path + SHA-256, sorted.
std::string digest(const std::string& dir) {
    std::vector<std::string> lines;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || !fs::is_regular_file(st)) continue;
        lines.push_back(fs::relative(it->path(), dir, e2).string() + " " + sha256::of_file(it->path().string()));
    }
    std::sort(lines.begin(), lines.end());
    std::string all;
    for (const auto& l : lines) all += l + "\n";
    return sha256::of_text(all);
}

struct Kids {
    std::vector<std::string> dirs, files;   // names, not hidden, no symlinks
};
Kids kids_of(const std::string& dir) {
    Kids k;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.empty() || n[0] == '.') continue;
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || fs::is_symlink(st)) continue;
        if (fs::is_directory(st)) k.dirs.push_back(n);
        else if (fs::is_regular_file(st)) k.files.push_back(n);
    }
    std::sort(k.dirs.begin(), k.dirs.end());
    std::sort(k.files.begin(), k.files.end());
    return k;
}

struct Fit {
    std::string tpl;
    std::size_t have = 0, of = 0;
};
Fit best_fit(const std::vector<templates::Template>& ts, const std::string& dir) {
    Fit best;
    for (const auto& t : ts) {
        if (t.folders.empty()) continue;
        const auto got = templates::folders_of(dir, templates::depth_of(t));
        std::set<std::string> low;
        for (const auto& g : got) low.insert(lower(g));
        std::size_t have = 0;
        for (const auto& f : t.folders) have += low.count(lower(f.path));
        if (have * 2 < t.folders.size() || have == 0) continue;   // half at least
        if (best.of == 0 || have * best.of > best.have * t.folders.size()) best = {t.name, have, t.folders.size()};
    }
    return best;
}

// Items in a project (files, bundles whole) checked against a type's routes.
void placement(const templates::Template& t, const std::string& dir, Project& p) {
    const auto walk = [&](const std::string& at, const std::string& rel, auto& self) -> void {
        const Kids k = kids_of(at);
        const auto check = [&](const std::string& item) {
            const std::string r = rel.empty() ? item : rel + "/" + item;
            const std::string to = templates::route_for_path(t, r);
            if (to.empty()) return;
            ++p.routed;
            const std::string in = parent_of(r);
            if (to == templates::kStays || (to == templates::kTop && in.empty()) ||
                (to != templates::kTop && (lower(in) == lower(to) || starts(lower(in), lower(to) + "/"))))
                ++p.placed;
        };
        for (const auto& f : k.files) check(f);
        for (const auto& d : k.dirs) {
            const std::string abs = at + "/" + d;
            if (is_one(abs)) { check(d); continue; }   // a bundle: one item
            self(abs, rel.empty() ? d : rel + "/" + d, self);
        }
    };
    walk(dir, "", walk);
}

std::size_t count_bundles(const std::string& dir) {
    std::size_t n = 0;
    const auto walk = [&](const std::string& at, auto& self) -> void {
        for (const auto& d : kids_of(at).dirs) {
            const std::string abs = at + "/" + d;
            if (is_one(abs)) { ++n; continue; }
            self(abs, self);
        }
    };
    walk(dir, walk);
    return n;
}

struct Loose {
    std::string folder, name;
};

struct Ctx {
    std::string home;
    Report* r;
    const std::vector<templates::Template>* ts;
    std::vector<Loose>* loose;
    std::vector<std::pair<std::string, std::vector<std::string>>>* lookalikes;   // rel -> its folder names (no type)
};

void walk(const Ctx& cx, const std::string& dir, const std::string& rel, const std::string& project);

// The bundles inside a whole thing (a project, a book): listed, marked in it.
void bundles_in(const Ctx& cx, const std::string& path, const std::string& crel) {
    const auto in = [&](const std::string& at, const std::string& r, auto& self) -> void {
        for (const auto& d : kids_of(at).dirs) {
            const std::string abs = at + "/" + d, rr = r + "/" + d;
            if (is_one(abs)) {
                Bundle b;
                b.rel = rr;
                b.ext = suspects::ext_of(d);
                b.project = crel;
                b.held = is_held(abs);
                measure(abs, b.bytes, b.files);
                cx.r->bundles.push_back(std::move(b));
                continue;
            }
            self(abs, rr, self);
        }
    };
    in(path, crel, in);
}

// No type fits half, but every working folder is one of a type's folder
// names -> "maybe an old <type>" (the Carolyn Keene books: Archive/ Images/).
std::string maybe_of(const std::vector<templates::Template>& ts, const std::vector<std::string>& working) {
    if (working.empty()) return "";
    for (const auto& t : ts) {
        std::set<std::string> leaves;
        for (const auto& f : t.folders) leaves.insert(lower(name_of(f.path)));
        bool all = true;
        for (const auto& w : working) all = all && leaves.count(lower(w));
        if (all) return t.name;
    }
    return "";
}

void add_project(const Ctx& cx, const std::string& path, const std::string& crel, Project::How how,
                 const Kids& k, const std::string& project) {
    Project p;
    p.rel = crel;
    p.how = how;
    for (const auto& d : k.dirs)
        if (vocabulary().count(lower(d))) p.working.push_back(d);
    const Fit f = best_fit(*cx.ts, path);
    p.tpl = f.tpl;
    p.have = f.have;
    p.of = f.of;
    if (!f.tpl.empty()) {
        for (const auto& t : *cx.ts)
            if (t.name == f.tpl) placement(t, path, p);
        if (how != Project::How::Place)
            for (const auto& s : projects::shelves())
                if (s.tpl == f.tpl) {
                    std::string g;
                    for (const auto& t : *cx.ts)
                        if (t.name == f.tpl) g = t.group;
                    p.shelf = s.category + "/" + (s.depth >= 2 ? "<" + (g.empty() ? std::string("group") : g) + ">/" : "") +
                              "<Title>";
                }
    } else
        p.maybe = maybe_of(*cx.ts, p.working);
    p.bundles = count_bundles(path);
    measure(path, p.bytes, p.files);
    cx.r->projects.push_back(std::move(p));
    if (project.empty()) bundles_in(cx, path, crel);
}

// s026a: a title in the EBooks library -- no sub-folders, or a working folder
// (Archive/ Images/ Print/ ...) among them. Else it is an author's folder.
bool is_title(const std::string& path, const Kids& k) {
    std::size_t plain = 0;
    for (const auto& d : k.dirs) {
        if (is_one(path + "/" + d)) continue;     // a bundle (.optik) is a part, not a book
        if (vocabulary().count(lower(d))) return true;
        ++plain;
    }
    return plain == 0;
}

void walk(const Ctx& cx, const std::string& dir, const std::string& rel, const std::string& project) {
    const Kids k = kids_of(dir);
    // s026a: a saved web page -- X.html + X_files/ -- one item.
    std::set<std::string> pages;   // the X_files folder names
    for (const auto& f : k.files) {
        const std::string e = fext_of(f);
        if (e != ".html" && e != ".htm") continue;
        const std::string fd = stem_of(f) + "_files";
        if (std::binary_search(k.dirs.begin(), k.dirs.end(), fd)) {
            pages.insert(fd);
            Set s;
            s.kind = Set::Kind::WebPage;
            s.title = f;
            s.folders.push_back(rel);
            std::uint64_t b = 0;
            measure(dir + "/" + fd, b, s.count);
            ++s.count;
            cx.r->sets.push_back(std::move(s));
        }
    }
    for (const auto& f : k.files) {
        if (pages.count(stem_of(f) + "_files") && (fext_of(f) == ".html" || fext_of(f) == ".htm")) continue;
        cx.loose->push_back({rel, f});
    }
    const std::string lib = kLibrary;
    for (const auto& name : k.dirs) {
        const std::string path = dir + "/" + name;
        const std::string crel = rel.empty() ? name : rel + "/" + name;
        if (latch::protect::here(crel) || protection::under(crel)) { ++cx.r->shut; continue; }
        if (rel.empty() && name == archive::kFolder) { ++cx.r->shut; continue; }
        if (pages.count(name)) continue;                          // a saved page's folder: with its page
        ++cx.r->folders;
        if (rel.empty() && name == suspects::kCodeRoot) {        // code: one line
            const auto s = suspects::scan(cx.home, path);
            cx.r->code_items += s.code.size();
            for (const auto& c : s.code) cx.r->code_bytes += c.bytes;
            continue;
        }
        if (suspects::is_build_output(path)) { ++cx.r->build; continue; }
        if (fs::exists(fs::symlink_status(path + "/.git"))) { ++cx.r->repos; continue; }
        // 1 Bundles
        const bool held = is_held(path);
        const bool head = projects::is_project_name(name) || projects::is_whole_at(crel);
        if (held || (!head && is_one(path))) {
            Bundle b;
            b.rel = crel;
            b.ext = suspects::ext_of(name);
            b.project = project;
            b.held = held;
            measure(path, b.bytes, b.files);
            if (project.empty() && !held) {
                b.home = bundle_home(b.ext);
                if (!b.home.empty() && starts(crel, b.home + "/")) b.home = "=";   // already home
            }
            cx.r->bundles.push_back(std::move(b));
            continue;
        }
        // 2 Projects
        if (head) {
            projects::Shelf sh;
            const bool place = !projects::is_project_name(name) && projects::is_whole_at(crel, &sh);
            add_project(cx, path, crel, place ? Project::How::Place : Project::How::Name, kids_of(path), project);
            continue;
        }
        const Kids ck = kids_of(path);
        bool holds = false;                                       // holds projects -> a folder of them, not one
        for (const auto& d : ck.dirs)
            if (projects::is_project_name(d)) holds = true;
        // s026a: the EBooks library -- <lib>/<genre>/<Author>/<Title>, or a title at <genre>/.
        if (project.empty() && starts(crel, lib + "/") && !holds) {
            const auto depth = std::count(crel.begin(), crel.end(), '/');   // lib = 1 slash
            if (depth == 4 || (depth == 3 && is_title(path, ck))) {
                add_project(cx, path, crel, Project::How::Book, ck, project);
                continue;
            }
        }
        // s026a: a movie folder -- one video + its subs / nfo -> one set, whole.
        if (starts(crel, std::string(sort::kEntertainment) + "/")) {
            std::size_t videos = 0;
            for (const auto& f : ck.files) videos += is_video(f);
            if (videos == 1 && ck.files.size() + ck.dirs.size() > 1) {
                Set s;
                s.kind = Set::Kind::Movie;
                s.title = name;
                s.folders.push_back(rel);
                std::uint64_t b = 0;
                measure(path, b, s.count);
                cx.r->sets.push_back(std::move(s));
                continue;
            }
        }
        if (!rel.empty()) {                                       // never a top folder of home
            std::size_t working = 0;
            for (const auto& d : ck.dirs) working += vocabulary().count(lower(d));
            const Fit f = holds ? Fit{} : best_fit(*cx.ts, path);
            if (!holds && (working >= 2 || (!f.tpl.empty() && f.have >= 3))) {
                add_project(cx, path, crel, Project::How::LooksLike, ck, project);
                const auto& p = cx.r->projects.back();
                if (p.tpl.empty() && p.maybe.empty()) cx.lookalikes->push_back({crel, ck.dirs});
                continue;
            }
        }
        walk(cx, path, crel, project);
    }
}

// Sibling look-alikes sharing 2+ folder names -> a proposed type.
void propose(Report& r, const std::vector<std::pair<std::string, std::vector<std::string>>>& la) {
    std::map<std::string, std::vector<const std::pair<std::string, std::vector<std::string>>*>> by;
    for (const auto& x : la) by[parent_of(x.first)].push_back(&x);
    for (const auto& [par, v] : by) {
        if (v.size() < 2) continue;
        std::map<std::string, std::size_t> seen;
        for (auto* x : v)
            for (const auto& d : x->second) ++seen[lower(d)];
        Proposed p;
        p.parent = par;
        for (const auto& [d, n] : seen)
            if (n * 2 >= v.size() && n >= 2) p.shared.push_back(d);
        if (p.shared.size() < 2) continue;
        std::size_t best = 0;
        for (auto* x : v) {
            p.members.push_back(name_of(x->first));
            std::size_t n = 0;
            for (const auto& d : x->second) n += std::count(p.shared.begin(), p.shared.end(), lower(d));
            if (n > best || p.fullest.empty()) { best = n; p.fullest = name_of(x->first); }
        }
        r.proposed.push_back(std::move(p));
    }
}

void add_set(Report& r, Set::Kind kind, const std::string& title, const std::vector<const Loose*>& members) {
    Set s;
    s.kind = kind;
    s.title = title;
    s.count = members.size();
    std::map<std::string, std::size_t> n;
    for (auto* m : members) ++n[m->folder];
    std::vector<std::pair<std::string, std::size_t>> v(n.begin(), n.end());
    std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [f, c] : v) s.folders.push_back(f);
    r.sets.push_back(std::move(s));
}

void sets_and_kinds(Report& r, const std::vector<Loose>& loose, const std::vector<homing::TypeRoute>& table,
                    const std::vector<homing::TypeRoute>& ent) {
    r.loose_files = loose.size();
    std::vector<bool> claimed(loose.size(), false);
    // Runs: per folder, (prefix, ext); zero-padded ones by width, unpadded
    // ones together (1.png .. 9.png and 10.png .. 56.png are one run). 5+.
    {
        std::map<std::tuple<std::string, std::string, int, std::string>, std::vector<std::size_t>> g;
        for (std::size_t i = 0; i < loose.size(); ++i) {
            const std::string st = stem_of(loose[i].name);
            const auto [pre, w] = shape::numbered(st);
            if (w == 0) continue;
            const bool padded = w > 1 && st[st.size() - static_cast<std::size_t>(w)] == '0';
            g[{loose[i].folder, pre, padded ? w : 0, fext_of(loose[i].name)}].push_back(i);
        }
        const auto numof = [&](std::size_t i) {
            const std::string st = stem_of(loose[i].name);
            const auto w = static_cast<std::size_t>(shape::numbered(st).second);
            return std::stoull(st.substr(st.size() - w));
        };
        for (auto& [key, v] : g) {
            if (v.size() < shape::kRunMin) continue;
            std::sort(v.begin(), v.end(), [&](auto a, auto b) { return numof(a) < numof(b); });
            std::vector<const Loose*> m;
            for (auto i : v) { claimed[i] = true; m.push_back(&loose[i]); }
            add_set(r, Set::Kind::Run, loose[v.front()].name + " .. " + loose[v.back()].name, m);
        }
    }
    // Episodes: by show, wherever they are.
    {
        std::map<std::string, std::pair<std::string, std::vector<std::size_t>>> g;
        for (std::size_t i = 0; i < loose.size(); ++i) {
            if (claimed[i]) continue;
            std::string show;
            int season = 0;
            if (!sort::episode(loose[i].name, &show, &season) || show.empty()) continue;
            std::string key = lower(show);   // s026a: "Foundation 2021", "Mayor Of Kingstown (2021)" -> the show
            for (bool cut = true; cut;) {
                cut = false;
                while (!key.empty() && (key.back() == ' ' || key.back() == ')' || key.back() == '(')) { key.pop_back(); cut = true; }
                if (key.size() > 5 && std::all_of(key.end() - 4, key.end(), [](unsigned char c) { return std::isdigit(c); }) &&
                    (key[key.size() - 5] == ' ' || key[key.size() - 5] == '(') && (starts(key.substr(key.size() - 4), "19") || starts(key.substr(key.size() - 4), "20"))) {
                    key.erase(key.size() - 4);
                    cut = true;
                }
            }
            auto& e = g[key];
            if (e.first.empty() || show.size() < e.first.size()) e.first = show;
            e.second.push_back(i);
        }
        for (const auto& [k, e] : g) {
            if (e.second.size() < 2) continue;
            std::vector<const Loose*> m;
            for (auto i : e.second) { claimed[i] = true; m.push_back(&loose[i]); }
            add_set(r, Set::Kind::Episodes, e.first, m);
        }
    }
    // Versions of one name: per folder, same base + extension, 2+ different stems.
    {
        std::map<std::tuple<std::string, std::string, std::string>, std::vector<std::size_t>> g;
        for (std::size_t i = 0; i < loose.size(); ++i) {
            if (claimed[i]) continue;
            const std::string base = version_base(stem_of(loose[i].name));
            if (!base.empty()) g[{loose[i].folder, base, fext_of(loose[i].name)}].push_back(i);
        }
        for (const auto& [k, v] : g) {
            if (v.size() < 2) continue;
            std::vector<const Loose*> m;
            for (auto i : v) { claimed[i] = true; m.push_back(&loose[i]); }
            std::string shortest = loose[v.front()].name;
            for (auto i : v)
                if (loose[i].name.size() < shortest.size()) shortest = loose[i].name;
            add_set(r, Set::Kind::Versions, shortest, m);
        }
    }
    for (const auto& s : r.sets) r.in_sets += s.count;
    // Kinds, then the Closet.
    std::map<std::string, KindRow> kinds;
    for (std::size_t i = 0; i < loose.size(); ++i) {
        if (claimed[i]) continue;
        const auto& f = loose[i];
        std::string kind;
        if (fext_of(f.name) == ".kdbx") kind = kNever;   // a password database: never moved
        else if (sort::is_companion(f.name)) kind = kWithVideo;   // s030: subtitles go with their video
        else if (const auto* t = homing::type_of(table, f.name))   // s029: the Home design
            kind = home::is_trash(t->folder) ? std::string("the Trash (by your Home design)") : home::fixed_part(t->folder);
        if (kind.empty()) kind = sort::ent_folder(ent, f.name);
        if (kind.empty()) {
            const std::string e = fext_of(f.name);
            auto& c = r.closet[e.empty() ? "(no extension)" : e];
            ++c.first;
            ++c.second[f.folder];
            ++r.in_closet;
            continue;
        }
        auto& k = kinds[kind];
        k.kind = kind;
        ++k.count;
        ++r.in_kinds;
        if (f.folder == kind || starts(f.folder, kind + "/")) ++k.at_home;
        else ++k.elsewhere[f.folder];
    }
    for (auto& [n, k] : kinds) r.kinds.push_back(std::move(k));
    std::stable_sort(r.kinds.begin(), r.kinds.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
}

void find_copies(Report& r, const std::string& home, bool deep, const std::vector<std::tuple<std::string, std::uint64_t, std::size_t>>& code) {
    std::map<std::pair<std::uint64_t, std::size_t>, std::vector<std::string>> g;
    constexpr std::uint64_t kMin = 64 * 1024;   // small, near-empty folders are alike by chance
    for (const auto& b : r.bundles)
        if (b.bytes >= kMin) g[{b.bytes, b.files}].push_back(b.rel);
    for (const auto& p : r.projects)
        if (p.bytes >= kMin) g[{p.bytes, p.files}].push_back(p.rel);
    for (const auto& [rel, bytes, files] : code)
        if (bytes >= kMin) g[{bytes, files}].push_back(rel);
    for (auto& [k, v] : g) {
        if (v.size() < 2) continue;
        Copies c;
        c.bytes = k.first;
        c.files = k.second;
        if (!deep) {
            c.rels = v;
            r.copies.push_back(std::move(c));
            continue;
        }
        // Read: split by what is in them -- each group of 2+ the same; the rest differ.
        std::map<std::string, std::vector<std::string>> by;
        for (const auto& rel : v) by[digest(home + "/" + rel)].push_back(rel);
        Copies differ = c;
        differ.same = 0;
        for (auto& [d, rels] : by) {
            if (rels.size() < 2) { differ.rels.push_back(rels.front()); continue; }
            Copies s = c;
            s.same = 1;
            s.rels = rels;
            r.copies.push_back(std::move(s));
        }
        if (differ.rels.size() >= 2)
            r.copies.push_back(std::move(differ));
    }
}

std::string num(std::size_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}
std::string files_text(std::size_t n) { return num(n) + (n == 1 ? " file" : " files"); }
std::string sz(std::uint64_t b, std::size_t f) { return suspects::size_text(b) + ", " + files_text(f); }
constexpr std::size_t kCap = 20;

void more(std::ostringstream& o, std::size_t shown, std::size_t all) {
    if (all > shown) o << "      ... and " << num(all - shown) << " more\n";
}
std::string folder_text(const std::string& f) { return f.empty() ? "(home)" : f; }

}  // namespace

std::string bundle_home(const std::string& ext) {
    static const std::map<std::string, std::string> m = {{".optik", "Documents/OCR"},
                                                         {".curvz", "Documents/Design/Curvz"},
                                                         {".composix", "Documents/Design/Layout"},
                                                         {".klimt", "Documents/Design/Photo"},
                                                         {".jots", "Documents/Jots"},        // s030 (Scott)
                                                         {".mort", "Develop/Mortimer"}};     // s030 (Scott)
    const auto it = m.find(lower(ext));
    return it == m.end() ? "" : it->second;
}

bool is_working_name(const std::string& n) { return vocabulary().count(lower(n)) > 0; }

std::string version_base(const std::string& stem) {
    std::string s = lower(stem);
    static const char* tails[] = {" backup", "-backup", "_backup", " copy", "-copy", "_copy", " old", "-old", "_old",
                                  "(old)", " final", "-final", "_final", "(final)"};
    bool changed = true;
    while (changed && !s.empty()) {
        changed = false;
        while (!s.empty() && (s.back() == ' ' || s.back() == '-' || s.back() == '_')) { s.pop_back(); changed = true; }
        for (const char* t : tails)
            if (ends(s, t) && s.size() > std::strlen(t)) { s.erase(s.size() - std::strlen(t)); changed = true; }
        // " (2)" and "-1" .. "-9": a copy's number
        if (s.size() > 4 && s.back() == ')') {
            const auto o = s.rfind('(');
            if (o != std::string::npos && o + 2 < s.size() &&
                std::all_of(s.begin() + static_cast<long>(o) + 1, s.end() - 1, [](unsigned char c) { return std::isdigit(c); })) {
                s.erase(o);
                changed = true;
            }
        }
        if (s.size() > 2 && std::isdigit(static_cast<unsigned char>(s.back())) && s[s.size() - 2] == '-' &&
            !std::isdigit(static_cast<unsigned char>(s[s.size() - 3]))) {
            s.erase(s.size() - 2);
            changed = true;
        }
    }
    return s;
}

std::string font_family(const std::string& file_name) {
    const std::string e = fext_of(file_name);
    if (e != ".otf" && e != ".ttf" && e != ".woff" && e != ".woff2" && e != ".pfb") return "";
    std::string st = stem_of(file_name);
    const auto cut = st.find_first_of("-_");
    if (cut != std::string::npos && cut > 0) st = st.substr(0, cut);
    return lower(st);
}

Report scan(const std::string& home, const std::string& from, const std::string& data_dir, bool deep) {
    const auto t0 = std::chrono::steady_clock::now();
    Report r;
    r.home = home;
    r.from = from;
    std::string rel;
    if (from != home) {
        if (!starts(from, home + "/")) return r;
        rel = from.substr(home.size() + 1);
    }
    const auto ts = projects::known_templates(data_dir);
    for (const auto& t : ts)
        for (const auto& f : t.folders) {
            const auto p = f.path.rfind('/');
            vocabulary().insert(lower(p == std::string::npos ? f.path : f.path.substr(p + 1)));
        }
    const auto st = settings::parse(own::read(data_dir, settings::kFile));
    g_home = home;
    g_live.clear();
    for (const auto& h : suspects::held_of(suspects::scan(home, from))) g_live.insert(h);
    std::vector<Loose> loose;
    std::vector<std::pair<std::string, std::vector<std::string>>> la;
    std::string at;
    projects::inside_whole(rel, &at);
    if (!rel.empty() && projects::is_whole_at(rel)) at = rel;
    Ctx cx{home, &r, &ts, &loose, &la};
    walk(cx, from, rel, at);
    propose(r, la);
    const auto dz = home::design(home::load(data_dir, st));   // s029: the Home design
    sets_and_kinds(r, loose, dz.types, dz.ent);
    std::vector<std::tuple<std::string, std::uint64_t, std::size_t>> code;
    if (rel.empty()) {
        const auto s = suspects::scan(home, home + "/" + suspects::kCodeRoot);
        for (const auto& c : s.code) code.emplace_back(c.rel, c.bytes, c.files);
    }
    find_copies(r, home, deep, code);
    r.ms = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    return r;
}

std::string format(const Report& r) {
    std::ostringstream o;
    o << "Tidy -- what Tidy recognises under " << r.from << "\n"
      << "READ-ONLY: nothing moved, nothing written. Top down: each file is claimed once, by the highest level that fits.\n\n";
    std::size_t bfiles = 0, pfiles = 0;
    for (const auto& b : r.bundles)
        if (b.project.empty()) bfiles += b.files;
    for (const auto& p : r.projects) pfiles += p.files;
    o << "Summary: bundles " << files_text(bfiles) << " (loose ones; in projects counted there), projects "
      << files_text(pfiles) << ", sets " << files_text(r.in_sets) << ", kinds " << files_text(r.in_kinds)
      << ", the Closet " << files_text(r.in_closet) << ". Code: " << num(r.code_items) << " items in ~/"
      << suspects::kCodeRoot << ", " << suspects::size_text(r.code_bytes) << " (see --suspects).\n";

    // 1 Bundles
    std::vector<const Bundle*> loose, held, inproj;
    for (const auto& b : r.bundles) (b.held ? held : b.project.empty() ? loose : inproj).push_back(&b);
    o << "\n1. Bundles -- one thing each, not looked inside: " << r.bundles.size() << " (" << loose.size() << " loose, "
      << inproj.size() << " in projects, " << held.size() << " held in Quarantine)\n";
    o << "  Loose -- each by itself, a home by its kind:\n";
    for (std::size_t i = 0; i < loose.size() && i < kCap * 2; ++i) {
        const auto* b = loose[i];
        o << "      " << b->rel << "   [" << sz(b->bytes, b->files) << "]";
        if (b->home == "=") o << "  at home\n";
        else if (b->home.empty()) o << "  (" << (b->ext.empty() ? "no" : b->ext) << " has no home yet)\n";
        else o << "  -> " << b->home << "\n";
    }
    more(o, std::min(loose.size(), kCap * 2), loose.size());
    if (!held.empty()) {
        o << "  Held in Quarantine (answer in Quarantine...; not entered):\n";
        for (const auto* b : held) o << "      " << b->rel << "   [" << sz(b->bytes, b->files) << "]\n";
    }

    // 2 Projects
    const char* hows[] = {"By name (-project)", "By place (a shelf)", "Books in the EBooks library (Documents/EBooks/<genre>/<Author>/<Title>)",
                          "Looks like a project (no -project, no shelf)"};
    o << "\n2. Projects -- trees kept whole: " << r.projects.size() << "\n";
    for (int h = 0; h < 4; ++h) {
        std::vector<const Project*> v;
        for (const auto& p : r.projects)
            if (static_cast<int>(p.how) == h) v.push_back(&p);
        if (v.empty()) continue;
        o << "  " << hows[h] << ": " << v.size() << "\n";
        for (std::size_t i = 0; i < v.size() && i < kCap * 2; ++i) {
            const auto* p = v[i];
            o << "      " << p->rel << "   [" << sz(p->bytes, p->files);
            if (p->bundles) o << "; " << p->bundles << (p->bundles == 1 ? " bundle" : " bundles");
            o << "]\n";
            std::string clues;
            if (!p->tpl.empty()) {
                clues += "tree: " + p->tpl + " " + std::to_string(p->have) + "/" + std::to_string(p->of) + " folders";
                if (p->routed)
                    clues += "; placed: " + num(p->placed) + " of " + num(p->routed) + " where " + p->tpl + " says";
            } else if (!p->maybe.empty())
                clues += "maybe an old " + p->maybe + " (its folders are " + p->maybe + "'s)";
            else if (p->how == Project::How::Place)
                clues += "on its shelf";   // s030: a shelf type with no folders (an ebook title) has no tree to fit
            else if (p->how == Project::How::Book)
                clues += "a book in the library";
            else
                clues += "tree: no type fits half";
            if (!p->working.empty()) {
                clues += "; working folders:";
                for (const auto& w : p->working) clues += " " + w;
            }
            o << "          " << clues << "\n";
            if (!p->shelf.empty()) o << "          NOT ON ITS SHELF -- a " << p->tpl << " lives at " << p->shelf << "\n";
        }
        more(o, std::min(v.size(), kCap * 2), v.size());
    }
    if (!r.proposed.empty()) {
        o << "  Proposed types -- sibling look-alikes sharing folder names:\n";
        for (const auto& p : r.proposed) {
            o << "      " << folder_text(p.parent) << ": " << p.members.size() << " folders share";
            for (const auto& s : p.shared) o << " " << s << "/";
            o << " -- the fullest: " << p.fullest << "\n";
        }
    }

    // 3 Sets
    const char* kinds[] = {"Numbered runs", "Series episodes", "Versions of one name", "Saved web pages (X.html + X_files/)",
                           "Movie folders (one video + its subs / nfo)"};
    o << "\n3. Sets -- loose files that belong together: " << r.sets.size() << " (" << files_text(r.in_sets) << ")\n";
    for (int k = 0; k < 5; ++k) {
        std::vector<const Set*> v;
        for (const auto& s : r.sets)
            if (static_cast<int>(s.kind) == k) v.push_back(&s);
        if (v.empty()) continue;
        o << "  " << kinds[k] << ": " << v.size() << "\n";
        for (std::size_t i = 0; i < v.size() && i < kCap; ++i) {
            const auto* s = v[i];
            o << "      " << s->title << "   (" << s->count << ") in " << folder_text(s->folders.front());
            if (s->folders.size() > 1) o << " +" << s->folders.size() - 1 << " more folders";
            o << "\n";
        }
        more(o, std::min(v.size(), kCap), v.size());
    }

    // 4 Kinds
    o << "\n4. Kinds -- what is left, by your Home design: " << files_text(r.in_kinds) << "\n";
    for (const auto& k : r.kinds) {
        if (k.kind == kNever || k.kind == kWithVideo) o << "  " << k.kind << ": " << num(k.count) << "\n";
        else
            o << "  " << k.kind << ": " << num(k.count) << " -- " << num(k.at_home) << " at home, "
              << num(k.count - k.at_home) << " elsewhere\n";
        std::vector<std::pair<std::string, std::size_t>> v(k.elsewhere.begin(), k.elsewhere.end());
        std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (std::size_t i = 0; i < v.size() && i < 5; ++i)
            o << "      " << num(v[i].second) << "  " << folder_text(v[i].first) << "\n";
        if (v.size() > 5) o << "      ... and " << v.size() - 5 << " more folders\n";
    }

    // 5 Closet
    o << "\n5. The Closet -- nothing claims these: " << files_text(r.in_closet) << "\n";
    {
        std::vector<std::pair<std::string, const std::pair<std::size_t, std::map<std::string, std::size_t>>*>> v;
        for (const auto& [e, c] : r.closet) v.emplace_back(e, &c);
        std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second->first > b.second->first; });
        for (std::size_t i = 0; i < v.size() && i < kCap; ++i) {
            std::string where;
            std::size_t best = 0;
            for (const auto& [f, n] : v[i].second->second)
                if (n > best) { best = n; where = f; }
            o << "  " << v[i].first << ": " << num(v[i].second->first) << "  (most in " << folder_text(where) << ")\n";
        }
        if (v.size() > kCap) o << "  ... and " << v.size() - kCap << " more kinds\n";
    }

    // Copies
    o << "\nLikely copies -- the same size and file count: " << r.copies.size() << " groups\n";
    for (const auto& c : r.copies) {
        o << "  [" << sz(c.bytes, c.files) << "] "
          << (c.same == 1 ? "THE SAME bytes (read)" : c.same == 0 ? "they DIFFER (read)" : "not read -- --deep to check")
          << "\n";
        for (const auto& rel : c.rels) o << "      " << rel << "\n";
    }

    o << "\nNot walked: " << num(r.repos) << " git repos (one item each), " << num(r.build) << " build output, "
      << r.shut << (r.shut == 1 ? " folder" : " folders") << " not looked inside (Hands off, Samson's, never-touch, ~/Archive).\n"
      << num(r.folders) << " folders looked at, " << r.ms << " ms.\n";
    return o.str();
}

int main_cli(int argc, char** argv) {
    bool deep = false;
    std::string arg;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--deep") == 0) deep = true;
        else if (arg.empty()) arg = argv[i];
        else { std::cerr << "usage: tidy --recognise [--deep] [folder]\n"; return 2; }
    }
    const auto w = due::where(std::getenv("TIDY_HOME"), std::getenv("HOME"), std::getenv("XDG_DATA_HOME"),
                              std::getenv("XDG_CONFIG_HOME"));
    if (w.home.empty() || w.home[0] != '/') { std::cerr << "tidy --recognise: no home\n"; return 2; }
    std::string from = w.home;
    if (!arg.empty()) {
        std::string rel;
        const std::string err = protection::rel_of(w.home, arg, rel);
        if (!err.empty()) {
            if (arg != "~" && fs::weakly_canonical(arg).string() != w.home) {
                std::cerr << "tidy --recognise: " << err << "\n";
                return 2;
            }
        } else from = w.home + "/" + rel;
        std::error_code ec;
        if (!fs::is_directory(from, ec)) { std::cerr << "tidy --recognise: not a folder: " << from << "\n"; return 2; }
    }
    const auto st = settings::parse(own::read(w.data_dir, settings::kFile));
    protection::apply(w.home, protection::config_home(w.home), st);
    projects::apply_places(w.data_dir, w.home);
    std::cout << format(scan(w.home, from, w.data_dir, deep));
    if (w.fixture) std::cout << "(FIXTURE " << w.home << ")\n";
    return 0;
}

}  // namespace tidy::core::recognise
