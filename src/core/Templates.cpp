#include "core/Templates.hpp"
#include "core/Quarantine.hpp"

#include <latch/Package.hpp>
#include "core/Own.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

#include <fnmatch.h>

namespace tidy::core::templates {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
std::vector<std::string> parts_of(const std::string& path) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const auto end = path.find('/', start);
        out.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}
std::string parent_of(const std::string& path) {
    const auto p = path.rfind('/');
    return p == std::string::npos ? "" : path.substr(0, p);
}
bool under(const std::string& path, const std::string& dir) {
    return path == dir || path.rfind(dir + "/", 0) == 0;
}
// Tree order: a parent, then its children, then the next parent.
bool tree_less(const std::string& a, const std::string& b) {
    const auto pa = parts_of(a), pb = parts_of(b);
    for (std::size_t i = 0; i < pa.size() && i < pb.size(); ++i) {
        const auto la = lower(pa[i]), lb = lower(pb[i]);
        if (la != lb) return la < lb;
        if (pa[i] != pb[i]) return pa[i] < pb[i];
    }
    return pa.size() < pb.size();
}
void sort_folders(Template& t) {
    std::sort(t.folders.begin(), t.folders.end(),
              [](const Folder& a, const Folder& b) { return tree_less(a.path, b.path); });
}
bool glob(const std::string& pattern, const std::string& name) {
    return ::fnmatch(pattern.c_str(), name.c_str(), FNM_CASEFOLD) == 0;
}
std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : sep) + s;
    return out;
}

}  // namespace

// ── Roles ──────────────────────────────────────────────────────────────────
const std::vector<Role>& roles() {
    static const std::vector<Role> r = {Role::Product, Role::Input, Role::History};
    return r;
}
const char* id_of(Role r) {
    switch (r) {
        case Role::Product: return "product";
        case Role::Input:   return "input";
        case Role::History: return "history";
    }
    return "input";
}
const char* title_of(Role r) {
    switch (r) {
        case Role::Product: return "Product";
        case Role::Input:   return "Input";
        case Role::History: return "History";
    }
    return "Input";
}
const char* meaning_of(Role r) {
    switch (r) {
        case Role::Product: return "what the project makes -- kept at close";
        case Role::Input:   return "what went into it -- its own path at close";
        case Role::History: return "how it got here -- archived at close";
    }
    return "";
}
bool role_of(const std::string& id, Role* out) {
    for (auto r : roles())
        if (id == id_of(r)) { if (out) *out = r; return true; }
    return false;
}

Template shell(const std::string& name, const std::string& category) {
    Template t;
    t.name = name;
    t.category = category;
    t.folders = {{"backups", Role::History}, {"docs", Role::Input},
                 {"fixes", Role::Input}, {"scripts", Role::Input}};
    return t;
}

// ── Names ──────────────────────────────────────────────────────────────────
std::string name_refusal(const std::string& name) {
    if (name.empty()) return "a template needs a name";
    if (name.size() > 64) return "the name is longer than 64";
    for (char c : name)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-'))
            return "the name takes a-z, 0-9 and '-' only (\"" + name + "\")";
    if (name[0] == '-') return "the name can't start with '-'";
    return "";
}
std::string part_refusal(const std::string& part) {
    if (part.empty()) return "an empty folder name";
    if (part == "." || part == "..") return "\"" + part + "\" is not a folder name";
    if (part[0] == '.') return "\"" + part + "\" is hidden";
    if (part.size() > 255) return "a folder name longer than 255";
    for (char c : part)
        if (c == '/' || static_cast<unsigned char>(c) < 0x20) return "\"" + part + "\" has a '/' or a control character";
    if (part != trim(part)) return "\"" + part + "\" starts or ends with a space";
    return "";
}
std::string path_refusal(const std::string& path) {
    if (path.empty()) return "an empty folder";
    const auto ps = parts_of(path);
    if (static_cast<int>(ps.size()) > kDepth)
        return "\"" + path + "\" is " + std::to_string(ps.size()) + " deep -- a template holds " + std::to_string(kDepth) + " levels at most";
    for (const auto& p : ps)
        if (auto why = part_refusal(p); !why.empty()) return why;
    return "";
}

std::string category_refusal(const std::string& category, bool by_place) {
    // Two deep at most (Scott, s010c): ~/Develop holds plain GitHub repos,
    // ~/Develop/gtk our workspaces. s028 (Scott: "b"): a shelf -- a type known
    // by its place -- may be one deeper: Entertainment/EBooks/Fiction.
    const std::size_t most = by_place ? 3 : 2;
    if (parts_of(category).size() > most)
        return "\"" + category + "\" -- a category is " + (by_place ? "one to three" : "one or two") + " folders under home";
    return path_refusal(category);
}

int place_depth(const Template& t) {
    if (!t.by_place) return 0;
    return t.group.empty() ? 1 : 2;
}

std::vector<std::string> problems(const Template& t) {
    std::vector<std::string> out;
    if (auto w = name_refusal(t.name); !w.empty()) out.push_back(w);
    if (t.category.empty()) out.push_back("a template needs a category (the home folder its projects live in)");
    else if (auto w = category_refusal(t.category, t.by_place); !w.empty()) out.push_back("category: " + w);
    // s028: a shelf type may be just its title folder (an ebook: the file at its top).
    if (t.folders.empty() && !t.by_place) out.push_back("a template needs at least one folder");
    if (!t.by_place && !t.group.empty()) out.push_back("a group level is only for projects known by their place");
    if (t.group.find('\n') != std::string::npos) out.push_back("the group's label is one line");
    std::set<std::string> seen;
    for (const auto& f : t.folders) {
        if (auto w = path_refusal(f.path); !w.empty()) { out.push_back(w); continue; }
        if (!seen.insert(f.path).second) out.push_back("\"" + f.path + "\" twice");
        const auto par = parent_of(f.path);
        if (!par.empty() && !find(t, par)) out.push_back("\"" + f.path + "\" has no parent \"" + par + "\"");
    }
    std::set<std::string> pats;
    for (const auto& r : t.routes) {
        if (auto w = pattern_refusal(r.pattern); !w.empty()) out.push_back("route \"" + r.pattern + "\": " + w);
        if (r.folder != kStays && r.folder != kTop && !find(t, r.folder))
            out.push_back("route \"" + r.pattern + "\" goes to \"" + r.folder + "\", not a folder of the template");
        if (!pats.insert(lower(r.pattern)).second) out.push_back("route \"" + r.pattern + "\" twice");
    }
    return out;
}

// ── Text ───────────────────────────────────────────────────────────────────
std::string format(const Template& t) {
    std::ostringstream o;
    o << "# Tidy template -- written by Tidy; edit it in Tidy's builder\n"
      << "name=" << t.name << "\n"
      << "category=" << t.category << "\n";
    if (t.by_place) o << "place=by-place\n";
    if (!t.group.empty()) o << "group=" << t.group << "\n";
    for (const auto& f : t.folders) o << "folder=" << id_of(f.role) << " " << f.path << "\n";
    for (const auto& r : t.routes) o << "route=" << r.pattern << " -> " << r.folder << "\n";
    return o.str();
}

Template parse(const std::string& text) {
    Template t;
    std::istringstream in(text);
    std::string line;
    std::set<std::string> seen;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (key == "name") t.name = val;
        else if (key == "category") t.category = val;
        else if (key == "place") t.by_place = (val == "by-place");
        else if (key == "group") t.group = val;
        else if (key == "folder") {
            const auto sp = val.find(' ');
            Role r;
            if (sp == std::string::npos || !role_of(val.substr(0, sp), &r)) continue;
            const std::string path = trim(val.substr(sp + 1));
            if (!path_refusal(path).empty() || !seen.insert(path).second) continue;
            t.folders.push_back({path, r});
        } else if (key == "route") {
            const auto ar = val.find(" -> ");
            if (ar == std::string::npos) continue;
            t.routes.push_back({trim(val.substr(0, ar)), trim(val.substr(ar + 4))});
        }
    }
    sort_folders(t);
    return t;
}

std::string rel_of(const std::string& name) { return std::string(kDir) + "/" + name + kExt; }

std::vector<Template> list(const std::string& data_dir) {
    std::vector<Template> out;
    std::error_code ec;
    const fs::path dir = fs::path(data_dir) / kDir;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto fn = it->path().filename().string();
        if (fn.empty() || fn[0] == '.' || it->path().extension() != kExt) continue;
        if (!it->is_regular_file(ec)) continue;
        bool found = false;
        auto t = parse(own::read(data_dir, std::string(kDir) + "/" + fn, &found));
        if (found && !t.name.empty()) out.push_back(std::move(t));
    }
    std::sort(out.begin(), out.end(), [](const Template& a, const Template& b) { return a.name < b.name; });
    return out;
}

std::string save(const std::string& data_dir, const Template& t) {
    const auto ps = problems(t);
    if (!ps.empty()) return join(ps, "; ");
    Template s = t;
    sort_folders(s);
    return own::write(data_dir, rel_of(s.name), format(s));
}

int specificity(const std::string& p) {
    if (p.find('/') != std::string::npos) return 0;
    if (p.size() > 2 && p[0] == '*' && p[1] == '.' && p.find_first_of("*?[", 1) == std::string::npos) return 2;
    return 1;
}

std::string pattern_for_file(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) return name;
    const std::string stem = name.substr(0, dot), ext = lower(name.substr(dot));
    // A numbered scan: letters then 3+ digits, nothing else (A0001, IMG_0042 is not -- the '_').
    std::size_t i = 0;
    while (i < stem.size() && std::isalpha(static_cast<unsigned char>(stem[i]))) ++i;
    const std::size_t digits = stem.size() - i;
    if (i > 0 && digits >= 3 && std::all_of(stem.begin() + static_cast<long>(i), stem.end(),
                                            [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        std::string p = stem.substr(0, i);
        for (std::size_t k = 0; k < digits; ++k) p += "[0-9]";
        return p + ext;
    }
    return "*" + ext;
}

std::string pattern_refusal(const std::string& pattern) {
    if (pattern.empty() || pattern.find("->") != std::string::npos) return "a file-name pattern, like *.curvz";
    const auto slash = pattern.rfind('/');
    if (slash == std::string::npos) return "";
    // "Folder/glob" or "Folder/Sub/glob": the folder part one or two deep, the glob a name.
    const std::string dir = pattern.substr(0, slash), name = pattern.substr(slash + 1);
    if (name.empty()) return "a folder then a file-name pattern, like Archive/*.png";
    if (auto w = path_refusal(dir); !w.empty()) return w;
    return "";
}

std::string route_for_path(const Template& t, const std::string& rel) {
    const std::string name = fs::path(rel).filename().string();
    for (const auto& r : t.routes) {
        const auto slash = r.pattern.rfind('/');
        if (slash == std::string::npos) {
            if (glob(r.pattern, name)) return r.folder;
            continue;
        }
        const std::string dir = r.pattern.substr(0, slash);
        if (rel.size() > dir.size() + 1 && lower(rel.substr(0, dir.size())) == lower(dir) && rel[dir.size()] == '/' &&
            glob(r.pattern.substr(slash + 1), name))
            return r.folder;
    }
    return "";
}

std::string route_for(const Template& t, const std::string& file_name) {
    for (const auto& r : t.routes) {
        // s023h: Conform's own routes (stays / top / "Folder/glob") never send a file INTO a project.
        if (r.folder == kStays || r.folder == kTop || r.pattern.find('/') != std::string::npos) continue;
        if (glob(r.pattern, file_name)) return r.folder;
    }
    return "";
}
const Folder* find(const Template& t, const std::string& path) {
    for (const auto& f : t.folders)
        if (f.path == path) return &f;
    return nullptr;
}

// ── Lift and drift ─────────────────────────────────────────────────────────
int depth_of(const Template& t) {
    int d = kLiftDepth;
    for (const auto& f : t.folders) d = std::max(d, static_cast<int>(parts_of(f.path).size()));
    return d;
}

std::vector<std::string> folders_of(const std::string& dir, int max_depth) {
    std::vector<std::string> out;
    const auto walk = [&](const fs::path& at, const std::string& prefix, int depth, auto& self) -> void {
        std::error_code ec;
        for (fs::directory_iterator it(at, ec), end; !ec && it != end; it.increment(ec)) {
            const auto fn = it->path().filename().string();
            if (fn.empty() || fn[0] == '.') continue;
            const auto st = it->symlink_status(ec);
            if (ec || !fs::is_directory(st)) continue;   // files and symlinks skipped
            if (quarantine::one(it->path().string())) continue;   // s025b: or held / your bundle folder; s024a: a bundle (.optik, .composix) is a file here
            const std::string rel = prefix.empty() ? fn : prefix + "/" + fn;
            out.push_back(rel);
            if (depth < max_depth) self(it->path(), rel, depth + 1, self);
        }
    };
    walk(fs::path(dir), "", 1, walk);
    std::sort(out.begin(), out.end(), tree_less);
    return out;
}

Role guess_role(const std::string& path) {
    const auto last = lower(parts_of(path).back());
    for (const char* h : {"backups", "backup", "old", "archive", "archives", "history", "versions", "attic"})
        if (last == h) return Role::History;
    for (const char* p : {"final", "release", "releases", "export", "exports", "out", "output", "dist",
                          "published", "product", "products", "deliverables"})
        if (last == p) return Role::Product;
    return Role::Input;
}

Template lift(const std::string& project_dir, const std::vector<std::string>& folders) {
    Template t;
    const fs::path p = fs::path(project_dir).lexically_normal();
    const fs::path base = p.has_filename() ? p : p.parent_path();
    for (char c : lower(base.filename().string()))
        t.name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-') ? c : '-';
    while (!t.name.empty() && t.name[0] == '-') t.name.erase(0, 1);
    if (t.name.size() > 64) t.name.resize(64);
    t.category = base.parent_path().filename().string();
    for (const auto& f : folders)
        if (path_refusal(f).empty()) t.folders.push_back({f, guess_role(f)});
    sort_folders(t);
    return t;
}

Draft proposal(const std::string& project_dir) {
    Draft d;
    d.t = lift(project_dir, folders_of(project_dir));
    for (const auto& f : d.t.folders) d.unconfirmed.insert(f.path);
    d.origin = project_dir;
    return d;
}

std::vector<Drift> drift(const Template& t, const std::vector<std::string>& folders) {
    std::vector<Drift> out;
    const std::set<std::string> on_disk(folders.begin(), folders.end());
    std::set<std::string> with_children;
    for (const auto& f : t.folders)
        if (auto par = parent_of(f.path); !par.empty()) with_children.insert(par);
    for (const auto& f : t.folders)
        if (!on_disk.count(f.path)) out.push_back({Drift::Kind::Missing, f.path});
    for (const auto& f : folders) {
        if (find(t, f)) continue;
        const auto par = parent_of(f);
        if (par.empty()) out.push_back({Drift::Kind::Extra, f});
        else if (find(t, par) && with_children.count(par)) out.push_back({Drift::Kind::Extra, f});
        // else: below template depth, or under an extra already reported -- free.
    }
    std::sort(out.begin(), out.end(), [](const Drift& a, const Drift& b) { return tree_less(a.path, b.path); });
    return out;
}

// ── Builder ────────────────────────────────────────────────────────────────
std::string Builder::apply(Draft next) {
    sort_folders(next.t);
    if (next == m_now) return "nothing changed";
    m_past.push_back(std::move(m_now));
    m_now = std::move(next);
    return "";
}
bool Builder::undo() {
    if (m_past.empty()) return false;
    m_now = std::move(m_past.back());
    m_past.pop_back();
    return true;
}

std::string Builder::set_name(const std::string& name) {
    if (auto w = name_refusal(name); !w.empty()) return w;
    Draft d = m_now;
    d.t.name = name;
    return apply(std::move(d));
}
std::string Builder::set_category(const std::string& category) {
    if (auto w = category_refusal(category, m_now.t.by_place); !w.empty()) return "category: " + w;
    Draft d = m_now;
    d.t.category = category;
    return apply(std::move(d));
}
std::string Builder::set_by_place(bool on) {
    if (m_now.t.by_place == on) return "";
    Draft d = m_now;
    d.t.by_place = on;
    if (!on) d.t.group.clear();
    return apply(std::move(d));
}
std::string Builder::set_group(const std::string& raw) {
    const std::string label = trim(raw);
    if (!m_now.t.by_place && !label.empty()) return "a group level is only for projects known by their place";
    if (label.find('/') != std::string::npos || label.find('\n') != std::string::npos) return "a label, one line, no '/'";
    if (label == m_now.t.group) return "";
    Draft d = m_now;
    d.t.group = label;
    return apply(std::move(d));
}
std::string Builder::add_folder(const std::string& raw, Role role) {
    std::string path = trim(raw);
    while (!path.empty() && path.back() == '/') path.pop_back();
    if (auto w = path_refusal(path); !w.empty()) return w;
    if (find(m_now.t, path)) return "\"" + path + "\" is already there";
    const auto par = parent_of(path);
    if (!par.empty() && !find(m_now.t, par)) return "add \"" + par + "\" first";
    Draft d = m_now;
    d.t.folders.push_back({path, role});
    return apply(std::move(d));
}
std::string Builder::remove_folder(const std::string& path) {
    if (!find(m_now.t, path)) return "no folder \"" + path + "\"";
    Draft d = m_now;
    std::erase_if(d.t.folders, [&](const Folder& f) { return under(f.path, path); });
    std::erase_if(d.t.routes, [&](const Route& r) { return under(r.folder, path); });
    std::erase_if(d.unconfirmed, [&](const std::string& u) { return under(u, path); });
    return apply(std::move(d));
}
std::string Builder::set_role(const std::string& path, Role role) {
    if (!find(m_now.t, path)) return "no folder \"" + path + "\"";
    Draft d = m_now;
    for (auto& f : d.t.folders)
        if (f.path == path) f.role = role;
    return apply(std::move(d));
}
std::string Builder::confirm(const std::string& path) {
    if (!m_now.unconfirmed.count(path)) return "\"" + path + "\" needs no confirming";
    Draft d = m_now;
    d.unconfirmed.erase(path);
    d.unconfirmed.erase(parent_of(path));   // a kept child keeps its parent
    return apply(std::move(d));
}
std::string Builder::add_route(const std::string& raw, const std::string& folder) {
    const std::string pattern = trim(raw);
    if (auto w = pattern_refusal(pattern); !w.empty()) return "a route takes " + w;
    if (folder != kStays && folder != kTop && !find(m_now.t, folder)) return "no folder \"" + folder + "\"";
    for (const auto& r : m_now.t.routes)
        if (lower(r.pattern) == lower(pattern)) return "\"" + pattern + "\" already goes to " + r.folder;
    Draft d = m_now;
    // s023h / s024d: the more particular first -- before the first route less particular
    // than it ("Archive/*.png" before "*cover*.png" before "*.png").
    const int sp = specificity(pattern);
    const auto at = std::find_if(d.t.routes.begin(), d.t.routes.end(),
                                 [&](const Route& r) { return specificity(r.pattern) > sp; });
    d.t.routes.insert(at, {pattern, folder});
    return apply(std::move(d));
}
std::string Builder::remove_route(const std::string& pattern) {
    Draft d = m_now;
    if (std::erase_if(d.t.routes, [&](const Route& r) { return r.pattern == pattern; }) == 0)
        return "no route \"" + pattern + "\"";
    return apply(std::move(d));
}
std::string Builder::rename_folder(const std::string& path, const std::string& raw) {
    const std::string name = trim(raw);
    if (!find(m_now.t, path)) return "no folder \"" + path + "\"";
    if (auto w = part_refusal(name); !w.empty()) return w;
    const auto par = parent_of(path);
    const std::string to = par.empty() ? name : par + "/" + name;
    if (to == path) return "";
    if (find(m_now.t, to)) return "\"" + to + "\" is already there";
    Draft d = m_now;
    const auto re = [&](std::string& p) {
        if (p == path) p = to;
        else if (p.rfind(path + "/", 0) == 0) p = to + p.substr(path.size());
    };
    for (auto& f : d.t.folders) re(f.path);
    for (auto& r : d.t.routes) {
        re(r.folder);
        // a "Folder/glob" pattern naming it follows too
        const auto slash = r.pattern.rfind('/');
        if (slash != std::string::npos) {
            std::string dir = r.pattern.substr(0, slash);
            re(dir);
            r.pattern = dir + r.pattern.substr(slash);
        }
    }
    std::set<std::string> un;
    for (auto u : d.unconfirmed) { re(u); un.insert(u); }
    d.unconfirmed = std::move(un);
    return apply(std::move(d));
}
std::string Builder::move_route(const std::string& pattern, const std::string& folder) {
    if (folder != kStays && folder != kTop && !find(m_now.t, folder)) return "no folder \"" + folder + "\"";
    Draft d = m_now;
    for (auto& r : d.t.routes)
        if (r.pattern == pattern) {
            if (r.folder == folder) return "";
            r.folder = folder;
            return apply(std::move(d));
        }
    return "no route \"" + pattern + "\"";
}
std::string Builder::adopt(const Drift& dr) {
    if (dr.kind == Drift::Kind::Extra) return add_folder(dr.path, guess_role(dr.path));
    return remove_folder(dr.path);
}

std::string Builder::save_refusal(const std::vector<std::string>& existing) const {
    if (!m_now.unconfirmed.empty()) {
        std::vector<std::string> v(m_now.unconfirmed.begin(), m_now.unconfirmed.end());
        return std::to_string(v.size()) + " lifted folder" + (v.size() == 1 ? "" : "s") +
               " not yet kept or removed: " + join(v, ", ");
    }
    if (const auto ps = problems(m_now.t); !ps.empty()) return join(ps, "; ");
    if (m_now.t.name != m_saved_as &&
        std::find(existing.begin(), existing.end(), m_now.t.name) != existing.end())
        return "a template named \"" + m_now.t.name + "\" already exists -- open it, or choose another name";
    return "";
}

}  // namespace tidy::core::templates
