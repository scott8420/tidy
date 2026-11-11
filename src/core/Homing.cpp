#include "core/Homing.hpp"
#include "core/Projects.hpp"
#include "core/Retention.hpp"

#include <latch/Protect.hpp>

#include <algorithm>
#include <filesystem>
#include <fnmatch.h>

namespace tidy::core::homing {

namespace fs = std::filesystem;
namespace dr = latch::door;

namespace {
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    const auto b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}
std::vector<std::string> parts_of(const std::string& p) {
    std::vector<std::string> out;
    std::size_t a = 0;
    while (a <= p.size()) {
        const auto b = p.find('/', a);
        out.push_back(p.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}
// A folder path under home, relative: each part a plain folder name, no
// project, not protected. `max` parts at most (0 = any).
std::string folder_refusal(const std::string& rel, std::size_t max) {
    if (rel.empty()) return "no folder";
    const auto ps = parts_of(rel);
    if (max && ps.size() > max) return "\"" + rel + "\" is too deep";
    for (const auto& p : ps) {
        if (auto why = templates::part_refusal(p); !why.empty()) return why;
        if (projects::is_project_name(p)) return "\"" + p + "\" is a project -- pick it as a project instead";
    }
    if (latch::protect::here(rel)) return "\"" + rel + "\" is a place Tidy never touches";
    return "";
}
}  // namespace

const std::vector<TypeRoute>& defaults() {
    static const std::vector<TypeRoute> t = [] {
        std::vector<TypeRoute> v;
        const auto add = [&](std::initializer_list<const char*> exts, const char* folder) {
            for (const char* e : exts) v.push_back({std::string("*.") + e, folder});
        };
        add({"png", "jpg", "jpeg", "gif", "webp", "heic", "tif", "tiff", "bmp", "raw", "cr2", "nef"}, "Pictures");
        add({"curvz", "svg"}, "Graphics");
        add({"pdf", "odt", "ods", "odp", "docx", "doc", "xlsx", "xls", "pptx", "rtf", "txt", "md", "epub"}, "Documents");
        add({"mp3", "flac", "ogg", "opus", "wav", "m4a", "aac"}, "Music");
        add({"mp4", "mkv", "webm", "mov", "avi", "m4v"}, "Videos");
        return v;
    }();
    return t;
}

std::vector<TypeRoute> table(const settings::Settings& s) {
    if (!s.home_set) return defaults();
    std::vector<TypeRoute> out;
    for (const auto& l : s.home)
        if (TypeRoute r; parse_line(l, r)) out.push_back(r);
    return out;
}

std::vector<std::string> lines_of(const std::vector<TypeRoute>& t) {
    std::vector<std::string> out;
    for (const auto& r : t) out.push_back(format_line(r));
    return out;
}

std::string format_line(const TypeRoute& r) { return r.pattern + " -> " + r.folder; }

bool parse_line(const std::string& line, TypeRoute& out) {
    const auto arrow = line.find("->");
    if (arrow == std::string::npos) return false;
    TypeRoute r{trim(line.substr(0, arrow)), trim(line.substr(arrow + 2))};
    while (!r.folder.empty() && r.folder.back() == '/') r.folder.pop_back();
    if (!route_refusal(r).empty()) return false;
    out = r;
    return true;
}

std::string route_refusal(const TypeRoute& r) {
    if (r.pattern.empty()) return "no pattern";
    if (r.pattern.find('/') != std::string::npos) return "a pattern names a file, not a path";
    if (r.pattern.find("->") != std::string::npos) return "a pattern with \"->\" in it";
    return folder_refusal(r.folder, 2);
}

const TypeRoute* type_of(const std::vector<TypeRoute>& table, const std::string& file_name) {
    for (const auto& r : table)
        if (::fnmatch(r.pattern.c_str(), file_name.c_str(), FNM_CASEFOLD) == 0) return &r;
    return nullptr;
}

bool offered_level(const std::string& id) {
    retention::Level l;
    if (!retention::parse(id, l)) return false;
    return l != retention::Level::Undecided && l != retention::Level::Ephemeral && l != retention::Level::HandsOff;
}

std::vector<birth::Born> offered(const birth::Report& r) {
    std::vector<birth::Born> out;
    for (const auto& b : r.items) {
        if (b.file.dir || !offered_level(b.file.tags.retention)) continue;
        if (b.file.tags.role == "junk-drawer") continue;
        bool in_project = false;
        for (const auto& part : fs::path(b.file.path).parent_path())
            in_project = in_project || projects::is_project_name(part.string());
        if (in_project) continue;
        std::error_code ec;
        if (!fs::is_regular_file(fs::symlink_status(b.file.path, ec))) continue;
        out.push_back(b);
    }
    return out;
}

std::vector<std::string> subfolders(const std::string& home, const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto n = it->path().filename().string();
        std::error_code e2;
        if (n.empty() || n[0] == '.' || it->is_symlink(e2) || !it->is_directory(e2) || projects::is_project_name(n))
            continue;
        const auto rel = fs::path(it->path()).lexically_relative(home).string();
        if (latch::protect::here(rel)) continue;
        out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> home_folders(const std::string& home, const std::vector<std::string>& skip) {
    std::vector<std::string> out;
    for (auto& n : subfolders(home, home))
        if (n != "Archive" && std::find(skip.begin(), skip.end(), n) == skip.end()) out.push_back(std::move(n));
    return out;
}

std::string by_type(const std::string& type_folder, const std::string& sub_in, std::string* folder) {
    std::string sub = trim(sub_in);
    while (!sub.empty() && sub.back() == '/') sub.pop_back();
    if (sub.empty()) return "pick or name a subfolder of " + type_folder + " -- a file going home by type never sits loose in it";
    const std::string rel = type_folder + "/" + sub;
    if (auto why = folder_refusal(rel, 4); !why.empty()) return why;
    if (folder) *folder = rel;
    return "";
}

std::string into_project(const std::string& project_rel, const templates::Template* t, const std::string& file_name,
                         const std::string& chosen_in, std::string* folder, std::string* route) {
    std::string chosen = trim(chosen_in);
    while (!chosen.empty() && chosen.back() == '/') chosen.pop_back();
    std::string via;
    if (chosen.empty() && t) {
        chosen = templates::route_for(*t, file_name);
        for (const auto& r : t->routes)
            if (r.folder == chosen && ::fnmatch(r.pattern.c_str(), file_name.c_str(), FNM_CASEFOLD) == 0) { via = r.pattern; break; }
    }
    if (chosen.empty())
        return t ? "no route in " + t->name + " for this file -- pick a folder in the project"
                 : "the project has no template -- pick a folder in it";
    for (const auto& p : parts_of(chosen)) {
        if (auto why = templates::part_refusal(p); !why.empty()) return why;
        if (projects::is_project_name(p)) return "\"" + p + "\" is a project inside a project";
    }
    const std::string rel = project_rel + "/" + chosen;
    if (latch::protect::here(rel) || latch::protect::here(chosen)) return "\"" + chosen + "\" is a place Tidy never touches";
    if (folder) *folder = rel;
    if (route) *route = via;
    return "";
}

std::string numbered(const std::string& name, int n) {
    const std::string tag = " (" + std::to_string(n) + ")";
    std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return name + tag;   // no extension, or ".hidden"
    // .tar.gz and friends: the number goes before ".tar".
    if (const auto t = name.rfind(".tar.", dot); t != std::string::npos && t > 0 && t + 4 == dot) dot = t;
    return name.substr(0, dot) + tag + name.substr(dot);
}

std::string free_name(const std::string& dir, const std::string& name, const std::set<std::string>& taken) {
    const auto free = [&](const std::string& n) {
        const std::string p = dir + "/" + n;
        std::error_code ec;
        return !taken.count(p) && !fs::exists(fs::symlink_status(p, ec));
    };
    if (free(name)) return name;
    for (int n = 2; n < 10000; ++n)
        if (const auto c = numbered(name, n); free(c)) return c;
    return "";
}

Planned plan(const std::string& home, const std::vector<Pick>& picks) {
    Planned out;
    std::set<std::string> made, taken;
    std::vector<dr::Action> dirs, moves;
    for (const auto& p : picks) {
        // Each missing level, parents first, once.
        std::string at = home;
        for (const auto& part : parts_of(p.folder)) {
            at += "/" + part;
            std::error_code ec;
            if (made.count(at) || fs::is_directory(fs::symlink_status(at, ec))) continue;
            made.insert(at);
            dr::Action a;
            a.verb = dr::Verb::MakeDir;
            a.to = at;
            a.reason = "a new folder for " + p.why.substr(0, p.why.find(':'));
            dirs.push_back(a);
        }
        const std::string dir = home + "/" + p.folder;
        const std::string name = fs::path(p.from).filename().string();
        const std::string free = free_name(dir, name, taken);
        if (free.empty()) continue;
        taken.insert(dir + "/" + free);
        dr::Action m;
        m.verb = dr::Verb::Move;
        m.from = p.from;
        m.to = dir + "/" + free;
        std::error_code ec;
        m.bytes = fs::file_size(p.from, ec);
        m.reason = p.why;
        if (free != name) {
            m.reason += " -- \"" + name + "\" is taken there: \"" + free + "\"";
            out.renamed.push_back(name + " -> " + free + " in ~/" + p.folder);
        }
        moves.push_back(m);
    }
    out.plan.actions = dirs;
    out.plan.actions.insert(out.plan.actions.end(), moves.begin(), moves.end());
    out.plan.label = "Home: " + std::to_string(moves.size()) + (moves.size() == 1 ? " file" : " files");
    if (moves.size() == 1)
        out.plan.label = "Home: " + fs::path(moves[0].from).filename().string() + " -> ~/" +
                         fs::path(moves[0].to).parent_path().lexically_relative(home).string();
    return out;
}

}  // namespace tidy::core::homing
