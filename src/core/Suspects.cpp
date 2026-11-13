#include "core/Suspects.hpp"

#include "core/Archive.hpp"
#include "core/Code.hpp"
#include "core/Due.hpp"
#include "core/Own.hpp"
#include "core/Projects.hpp"
#include "core/Protection.hpp"
#include "core/Settings.hpp"

#include <latch/Package.hpp>
#include <latch/Protect.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include <sstream>

namespace tidy::core::suspects {

namespace fs = std::filesystem;

const char* group_title(Group g) {
    switch (g) {
        case Group::App:      return "Your apps -- Name.ext/ with a .json at its top";
        case Group::Ext:      return "Name.ext/ without a .json";
        case Group::Manifest: return "A database or manifest at its top";
        case Group::Marker:   return "App markers at its top (cache/ thumbnails/)";
    }
    return "";
}

std::string ext_of(const std::string& n) {
    const auto dot = n.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= n.size()) return "";
    const std::string e = n.substr(dot + 1);
    if (e.size() > 12) return "";
    bool alpha = false;
    for (unsigned char c : e) {
        if (!std::isalnum(c)) return "";
        if (std::isalpha(c)) alpha = true;
    }
    if (!alpha) return "";
    std::string low = "." + e;
    for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return low;
}

std::string size_text(std::uint64_t b) {
    const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(b);
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    std::ostringstream o;
    if (i == 0 || v >= 10.0) o << static_cast<std::uint64_t>(v + 0.5);
    else { o.setf(std::ios::fixed); o.precision(1); o << v; }
    o << " " << u[i];
    return o.str();
}

const char* git_title(Git g) {
    switch (g) {
        case Git::GitHub: return "on GitHub";
        case Git::Remote: return "git, another remote";
        case Git::Local:  return "git, NO remote";
        case Git::None:   return "NOT in git";
    }
    return "";
}

namespace {

std::string read_text(const std::string& path) {   // POSIX, read-only, 64 KB at most
    std::string out;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return out;
    char buf[4096];
    ssize_t n;
    while (out.size() < 65536 && (n = ::read(fd, buf, sizeof buf)) > 0) out.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    return out;
}
bool has(const std::string& p) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p, ec));
}
Git git_here(const std::string& dir) {
    if (!has(dir + "/.git")) return Git::None;
    const std::string c = read_text(dir + "/.git/config");
    if (c.find("[remote ") == std::string::npos) return Git::Local;
    return c.find("github.com") != std::string::npos ? Git::GitHub : Git::Remote;
}

}  // namespace

bool is_build_output(const std::string& dir) {
    const std::string n = fs::path(dir).filename().string();
    if (n == "autom4te.cache" || n == "CMakeFiles") return true;
    if (n == "builddir" && (has(dir + "/compile_commands.json") || has(dir + "/meson-private"))) return true;
    return code::is_build(dir);
}

Git git_of(const std::string& dir) {
    Git best = git_here(dir);
    if (best == Git::GitHub) return best;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || !fs::is_directory(st) || n.empty() || n[0] == '.' || is_build_output(it->path().string())) {
            if (fs::is_directory(st)) it.disable_recursion_pending();
            continue;
        }
        const Git g = git_here(it->path().string());
        if (static_cast<int>(g) < static_cast<int>(best)) best = g;
        if (best == Git::GitHub) break;
        if (g != Git::None || it.depth() >= 1) it.disable_recursion_pending();   // a repo is one; 2 deep at most
    }
    return best;
}

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool ends(const std::string& s, const std::string& t) {
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

// All of it: every regular file below (hidden too -- it is the thing's size),
// symlinks not followed. READ-ONLY.
void measure(const std::string& dir, Suspect& s) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || fs::is_symlink(st)) continue;
        if (fs::is_regular_file(st)) {
            ++s.files;
            const auto n = it->file_size(e2);
            if (!e2) s.bytes += n;
        }
    }
}

template <class T>
void measure_any(const std::string& dir, T& s) {
    Suspect tmp;
    measure(dir, tmp);
    s.bytes = tmp.bytes;
    s.files = tmp.files;
}
bool holds_projects(const std::string& dir) {   // a container: -project folders or repos among its children
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        std::error_code e2;
        if (n.empty() || n[0] == '.' || !fs::is_directory(it->symlink_status(e2))) continue;
        if (projects::is_project_name(n) || has(it->path().string() + "/.git")) return true;
    }
    return false;
}

struct Top {                      // what sits at a folder's top
    std::vector<std::string> json, manifest, marker;
    bool git = false;
};
Top top_of(const std::string& dir) {
    Top t;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name == ".git") { t.git = true; continue; }
        if (name.empty() || name[0] == '.') continue;
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || fs::is_symlink(st)) continue;
        const std::string l = lower(name);
        if (fs::is_directory(st)) {
            if (l == "cache" || l == "thumbnails") t.marker.push_back(name + "/");
            continue;
        }
        if (!fs::is_regular_file(st)) continue;
        if (ends(l, ".json")) { t.json.push_back(name); t.manifest.push_back(name); }
        else if (ends(l, ".db") || ends(l, ".sqlite") || ends(l, ".sqlite3") || l.rfind("metadata.", 0) == 0 ||
                 l == "info.plist")
            t.manifest.push_back(name);
    }
    for (auto* v : {&t.json, &t.manifest, &t.marker}) std::sort(v->begin(), v->end());
    return t;
}

struct Ctx {
    std::string home;
    Report* r;
    std::string code_at;   // the code container being walked ("Develop", "Develop/gtk"); "" outside code
    quarantine::Answers answers;   // s025b
};

void walk(const Ctx& cx, const std::string& dir, const std::string& rel, const std::string& project) {
    std::error_code ec;
    std::vector<fs::directory_entry> kids;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) kids.push_back(*it);
    std::sort(kids.begin(), kids.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
    for (const auto& e : kids) {
        const std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue;     // hidden: never
        std::error_code e2;
        const auto st = e.symlink_status(e2);
        if (e2 || fs::is_symlink(st) || !fs::is_directory(st)) continue;
        const std::string path = e.path().string();
        const std::string crel = rel.empty() ? name : rel + "/" + name;
        if (latch::protect::here(crel) || protection::under(crel)) { ++cx.r->shut; continue; }
        if (rel.empty() && name == archive::kFolder) { ++cx.r->shut; continue; }   // archived: held already
        ++cx.r->folders;
        if ((rel.empty() && crel == kCodeRoot) || (!cx.code_at.empty() && rel == cx.code_at)) {
            const bool root = crel == kCodeRoot;
            const bool container = root || (rel == kCodeRoot && holds_projects(path));
            if (container) {
                Ctx in = cx;
                in.code_at = crel;
                walk(in, path, crel, project);
                continue;
            }
            Item it;
            it.rel = crel;
            it.git = git_of(path);
            measure_any(path, it);
            cx.r->code.push_back(std::move(it));
            continue;                                      // code: one item
        }
        if (is_build_output(path)) {
            Item it;
            it.rel = crel;
            measure_any(path, it);
            cx.r->build.push_back(std::move(it));
            continue;
        }
        const Top t = top_of(path);
        if (t.git) { ++cx.r->repos; continue; }           // a code project: one item
        const bool head = projects::is_project_name(name) || projects::is_whole_at(crel);
        const std::string ext = head ? "" : ext_of(name);
        Suspect s;
        s.rel = crel;
        s.project = project;
        const auto& ans = cx.answers;
        bool quiet = false;                                // s025b: answered "an ordinary folder"
        if (!ext.empty()) {
            const auto a = quarantine::of_ext(ans, ext);
            const auto af = quarantine::of_folder(ans, crel);   // a folder answer wins over its extension's
            const auto eff = af != quarantine::Answer::None ? af : a;
            if (eff == quarantine::Answer::Ordinary && !latch::pkg::is_package_name(name)) quiet = true;
            else {
                s.ext = ext;
                s.known = latch::pkg::is_package_name(name) || eff == quarantine::Answer::Bundle;
                s.answer = s.known ? (eff == quarantine::Answer::Bundle ? eff : quarantine::Answer::None) : eff;
                s.group = t.json.empty() ? Group::Ext : Group::App;
                s.why = t.json;
                measure(path, s);
                cx.r->list.push_back(std::move(s));
                continue;                                  // held whole
            }
        }
        const bool home_top = rel.empty();
        if (!quiet && !head && !home_top && (!t.manifest.empty() || !t.marker.empty())) {
            const auto a = quarantine::of_folder(ans, crel);
            if (a == quarantine::Answer::Ordinary) quiet = true;
            else {
                s.group = t.manifest.empty() ? Group::Marker : Group::Manifest;
                s.why = t.manifest;
                s.why.insert(s.why.end(), t.marker.begin(), t.marker.end());
                s.known = a == quarantine::Answer::Bundle;
                s.answer = a;
                measure(path, s);
                cx.r->list.push_back(std::move(s));
                continue;                                  // held whole
            }
        }
        if (quiet) cx.r->quiet.push_back(crel);
        if (head) ++cx.r->projects;
        walk(cx, path, crel, head && project.empty() ? crel : project);
    }
}

std::string pad(std::string s, std::size_t w) {
    if (s.size() < w) s.append(w - s.size(), ' ');
    return s;
}
std::string num(std::size_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}
std::string line_of(const Suspect& s) {
    return size_text(s.bytes) + ", " + num(s.files) + (s.files == 1 ? " file" : " files");
}

void by_ext(std::ostringstream& o, const std::vector<const Suspect*>& v) {
    std::map<std::string, std::vector<const Suspect*>> m;
    for (auto* s : v) m[s->ext].push_back(s);
    std::vector<std::pair<std::string, std::vector<const Suspect*>>> rows(m.begin(), m.end());
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
    for (auto& [ext, list] : rows) {
        std::size_t loose = 0, files = 0;
        std::uint64_t bytes = 0;
        for (auto* s : list) { loose += s->project.empty(); files += s->files; bytes += s->bytes; }
        o << "  " << pad(ext, 12) << (ext.size() >= 12 ? " " : "") << num(list.size()) << (list.size() == 1 ? " folder" : " folders") << "  ("
          << loose << " loose, " << list.size() - loose << " in projects)  " << size_text(bytes) << ", " << num(files)
          << (files == 1 ? " file\n" : " files\n");
        std::stable_sort(list.begin(), list.end(), [](auto* a, auto* b) { return a->project.empty() > b->project.empty(); });
        for (auto* s : list)
            o << "      " << (s->project.empty() ? "loose    " : "project  ") << s->rel << "   [" << line_of(*s) << "]\n";
    }
}

}  // namespace

Report scan(const std::string& home, const std::string& from) {
    const auto t0 = std::chrono::steady_clock::now();
    Report r;
    r.home = home;
    r.from = from;
    std::string rel;
    if (from != home) {
        if (from.rfind(home + "/", 0) != 0) return r;
        rel = from.substr(home.size() + 1);
    }
    // The walked folder's own project, if it is in one.
    std::string at;
    projects::inside_whole(rel, &at);
    if (!rel.empty() && projects::is_whole_at(rel)) at = rel;
    Ctx cx{home, &r, "", quarantine::current()};
    // Walked from inside ~/Develop: Develop itself, a container, or (inside an
    // item) the item it is in -- one line.
    const std::string cr = kCodeRoot;
    if (rel == cr) cx.code_at = cr;
    else if (rel.rfind(cr + "/", 0) == 0) {
        const auto s1 = rel.find('/', cr.size() + 1);
        const std::string first = rel.substr(0, s1);                 // Develop/<x>
        std::string item = first;
        if (holds_projects(home + "/" + first)) {
            if (s1 == std::string::npos) item.clear();               // the container itself
            else item = rel.substr(0, rel.find('/', s1 + 1));        // Develop/<x>/<y>
        }
        if (item.empty()) cx.code_at = rel;
        else {
            Item it;
            it.rel = item;
            it.git = git_of(home + "/" + item);
            measure_any(home + "/" + item, it);
            r.code.push_back(std::move(it));
            r.ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - t0).count());
            return r;
        }
    }
    walk(cx, from, rel, at);
    r.ms = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    return r;
}

std::string format(const Report& r) {
    std::ostringstream o;
    o << "Tidy -- suspect folders under " << r.from << "\n"
      << "READ-ONLY: nothing moved, nothing written. Each suspect is held whole (not looked inside).\n\n";
    std::vector<const Suspect*> known, known_dirs, later;
    std::vector<const Suspect*> g[4];
    for (const auto& s : r.list) {
        if (s.known) (s.ext.empty() ? known_dirs : known).push_back(&s);
        else if (s.answer == quarantine::Answer::NotNow) later.push_back(&s);
        else g[static_cast<int>(s.group)].push_back(&s);
    }
    const auto folder_line = [&o](const Suspect* s) {
        std::string why;
        for (std::size_t k = 0; k < s->why.size() && k < 3; ++k) why += (k ? " " : "") + s->why[k];
        if (s->why.size() > 3) why += " +" + std::to_string(s->why.size() - 3);
        o << "      " << (s->project.empty() ? "loose    " : "project  ") << s->rel << "   [" << line_of(*s)
          << (why.empty() ? "" : "; " + why) << "]\n";
    };
    o << "Known bundles already (not asked): " << known.size() + known_dirs.size() << "\n";
    by_ext(o, known);
    if (!known_dirs.empty()) {
        o << "  folders you answered \"a bundle\": " << known_dirs.size() << "\n";
        for (auto* s : known_dirs) folder_line(s);
    }
    for (int i = 0; i < 4; ++i) {
        o << "\n" << i + 1 << ". " << group_title(static_cast<Group>(i)) << ": " << g[i].size() << "\n";
        if (i < 2) { by_ext(o, g[i]); continue; }
        for (auto* s : g[i]) folder_line(s);
    }
    o << "\nNot now (still held, asked again later): " << later.size() << "\n";
    for (auto* s : later) folder_line(s);
    o << "\nAnswered \"an ordinary folder\" (walked, not asked again): " << r.quiet.size() << "\n";
    {
        std::size_t n[4] = {};
        std::uint64_t bytes = 0;
        for (const auto& c : r.code) { ++n[static_cast<int>(c.git)]; bytes += c.bytes; }
        o << "\nCode (~/" << kCodeRoot << ", one item each, not walked): " << r.code.size() << " -- " << n[0]
          << " on GitHub, " << n[1] << " another remote, " << n[2] << " git with no remote, " << n[3]
          << " not in git; " << size_text(bytes) << "\n";
        for (int g = 3; g >= 1; --g)
            for (const auto& c : r.code)
                if (static_cast<int>(c.git) == g)
                    o << "      " << pad(git_title(c.git), 21) << c.rel << "   [" << size_text(c.bytes) << ", "
                      << num(c.files) << (c.files == 1 ? " file]\n" : " files]\n");
    }
    {
        std::uint64_t bytes = 0;
        for (const auto& b : r.build) bytes += b.bytes;
        o << "\nBuild output outside ~/" << kCodeRoot << " (to clean later, not walked): " << r.build.size() << ", "
          << size_text(bytes) << "\n";
        for (const auto& b : r.build) o << "      " << b.rel << "   [" << size_text(b.bytes) << "]\n";
    }
    o << "\nNot suspects: " << num(r.repos) << " git repos (one item each, not entered), " << num(r.projects)
      << " projects walked through, " << r.shut << (r.shut == 1 ? " folder" : " folders") << " not looked inside (Hands off, Samson's, never-touch, ~/Archive).\n"
      << num(r.folders) << " folders looked at, " << r.ms << " ms.\n";
    return o.str();
}

std::vector<std::string> held_of(const Report& r) {
    std::vector<std::string> v;
    for (const auto& s : r.list)
        if (!s.known) v.push_back(s.rel);   // unanswered or not now: quarantined
    return v;
}

int main_cli(int argc, char** argv) {
    if (argc > 3) { std::cerr << "usage: tidy --suspects [folder]\n"; return 2; }
    const auto w = due::where(std::getenv("TIDY_HOME"), std::getenv("HOME"), std::getenv("XDG_DATA_HOME"),
                              std::getenv("XDG_CONFIG_HOME"));
    if (w.home.empty() || w.home[0] != '/') { std::cerr << "tidy --suspects: no home\n"; return 2; }
    std::string from = w.home;
    if (argc == 3) {
        std::string rel;
        const std::string err = protection::rel_of(w.home, argv[2], rel);
        if (!err.empty()) {
            if (std::string(argv[2]) != "~" && fs::weakly_canonical(argv[2]).string() != w.home) {
                std::cerr << "tidy --suspects: " << err << "\n";
                return 2;
            }
        } else from = w.home + "/" + rel;
        std::error_code ec;
        if (!fs::is_directory(from, ec)) { std::cerr << "tidy --suspects: not a folder: " << from << "\n"; return 2; }
    }
    const auto st = settings::parse(own::read(w.data_dir, settings::kFile));
    protection::apply(w.home, protection::config_home(w.home), st);
    projects::apply_places(w.data_dir, w.home);
    std::cout << format(scan(w.home, from));
    if (w.fixture) std::cout << "(FIXTURE " << w.home << ")\n";
    return 0;
}

}  // namespace tidy::core::suspects
