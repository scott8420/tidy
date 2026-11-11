#include "core/Projects.hpp"

#include <latch/Protect.hpp>

#include <algorithm>
#include <cctype>

#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <sstream>

namespace tidy::core::projects {

namespace fs = std::filesystem;
namespace dr = latch::door;

bool is_project_name(const std::string& n) {
    const std::string s = kSuffix;
    return n.size() > s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0 && n[0] != '.';
}

std::string category_of(const std::string& rel) {
    const auto slash = rel.find('/');
    if (slash == std::string::npos) return "";                 // the home's top
    const std::string top = rel.substr(0, slash);
    if (top == "Desktop" || top == "Downloads") return "";       // landing places
    return top;
}

// Code: .git or CMakeLists.txt at the project's top, or one folder down --
// our workspace keeps the repo inside (tidy-project/tidy/.git; s010c). Hidden
// folders and symlinks one down are not looked into.
bool is_code(const fs::path& dir) {
    std::error_code ec;
    const auto marks = [&](const fs::path& d) {
        return fs::exists(d / ".git", ec) || fs::exists(d / "CMakeLists.txt", ec);
    };
    if (marks(dir)) return true;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string fn = it->path().filename().string();
        if (fn.empty() || fn[0] == '.') continue;
        const auto st = it->symlink_status(ec);
        if (ec || !fs::is_directory(st)) continue;
        if (marks(it->path())) return true;
    }
    return false;
}

bool is_uuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? s[i] != '-' : !std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

std::string id_text(const std::string& id) { return "id=" + id + "\n"; }

std::string read_id(const std::string& dir) {
    // POSIX read, read-only (the gate forbids <fstream> in Tidy's sources).
    const int fd = ::open((dir + "/" + kIdFile).c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return "";
    char buf[256];
    const auto n = ::read(fd, buf, sizeof buf - 1);
    ::close(fd);
    if (n <= 0) return "";
    std::istringstream in(std::string(buf, static_cast<std::size_t>(n)));
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("id=", 0) == 0 && is_uuid(line.substr(3))) return line.substr(3);
    }
    return "";
}

std::string folder_for(const std::string& name) {
    return is_project_name(name) ? name : name + kSuffix;
}

dr::Plan create(const templates::Template& t, const std::string& name, const std::string& home,
                const std::string& id, std::string* why) {
    const auto no = [&](const std::string& w) { if (why) *why = w; return dr::Plan{}; };
    if (name.empty()) return no("a project needs a name");
    if (const auto w = templates::part_refusal(name); !w.empty()) return no("name: " + w);
    if (!is_uuid(id)) return no("no id");
    if (t.category.empty()) return no("the template " + t.name + " has no category");
    if (const auto w = templates::category_refusal(t.category); !w.empty()) return no("category: " + w);
    const std::string top = t.category.substr(0, t.category.find('/'));
    if (top == "Desktop" || top == "Downloads") return no("a landing place is not a category");
    const std::string folder = folder_for(name);
    const std::string dir = home + "/" + t.category;
    const std::string at = dir + "/" + folder;
    std::error_code ec;
    if (fs::exists(fs::symlink_status(at, ec))) return no("~/" + t.category + "/" + folder + " already exists");
    dr::Plan plan;
    plan.label = "New project: " + folder + " (" + t.name + ")";
    auto mk = [&](const std::string& p, const std::string& reason) {
        dr::Action a;
        a.verb = dr::Verb::MakeDir;
        a.to = p;
        a.reason = reason;
        plan.actions.push_back(a);
    };
    if (!fs::is_directory(home + "/" + top, ec)) mk(home + "/" + top, "the category's folder");
    if (dir != home + "/" + top && !fs::is_directory(dir, ec)) mk(dir, "the category's folder");
    mk(at, "the project");
    for (const auto& f : t.folders) mk(at + "/" + f.path, "the template names it");   // tree order: parents first
    dr::Action w;
    w.verb = dr::Verb::Write;
    w.to = at + "/" + kIdFile;
    w.content = id_text(id);
    w.reason = "the project's id";
    plan.actions.push_back(w);
    if (why) why->clear();
    return plan;
}

dr::Plan give_ids(const std::vector<Project>& found, const std::function<std::string()>& new_id) {
    dr::Plan plan;
    for (const auto& p : found) {
        if (!p.id.empty()) continue;
        std::error_code ec;
        if (fs::exists(fs::symlink_status(p.path + "/" + kIdFile, ec))) continue;   // there, but not a uuid: left alone
        dr::Action w;
        w.verb = dr::Verb::Write;
        w.to = p.path + "/" + kIdFile;
        w.content = id_text(new_id());
        w.reason = "the project's id";
        plan.actions.push_back(w);
    }
    if (!plan.actions.empty())
        plan.label = "Ids: " + std::to_string(plan.actions.size()) + " project" + (plan.actions.size() == 1 ? "" : "s");
    return plan;
}

const std::vector<std::string>& statuses() {
    static const std::vector<std::string> s = {"active", "paused", "done", "dropped"};
    return s;
}

bool is_status(const std::string& s) {
    return std::find(statuses().begin(), statuses().end(), s) != statuses().end();
}

bool fits(const std::string& query, const std::vector<std::string>& fields) {
    const auto low = [](std::string x) {
        for (auto& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return x;
    };
    std::string hay;
    for (const auto& f : fields) hay += low(f) + "\n";
    std::istringstream in(low(query));
    std::string word;
    while (in >> word)
        if (hay.find(word) == std::string::npos) return false;
    return true;
}

std::vector<Project> find(const std::string& home, const std::set<std::string>& left) {
    std::vector<Project> out;
    const auto walk = [&](const fs::path& at, const std::string& rel, int depth, auto& self) -> void {
        std::error_code ec;
        for (fs::directory_iterator it(at, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string fn = it->path().filename().string();
            if (fn.empty() || fn[0] == '.') continue;                 // hidden: Tidy's records, the Trash, config
            const auto st = it->symlink_status(ec);
            if (ec || !fs::is_directory(st)) continue;               // files and symlinks
            const std::string r = rel.empty() ? fn : rel + "/" + fn;
            if (latch::protect::here(r)) continue;                   // never touched -- never listed
            if (is_project_name(fn)) {
                Project p;
                p.path = it->path().string();
                p.rel = r;
                p.name = fn.substr(0, fn.size() - std::string(kSuffix).size());
                p.category = category_of(r);
                p.code = is_code(it->path());
                p.left = p.category.empty() && left.count(r);
                p.id = read_id(p.path);
                out.push_back(std::move(p));
                continue;                                            // never inside a project
            }
            if (depth < kDepth) self(it->path(), r, depth + 1, self);
        }
    };
    walk(fs::path(home), "", 1, walk);
    std::sort(out.begin(), out.end(), [](const Project& a, const Project& b) { return a.rel < b.rel; });
    return out;
}

const templates::Template* template_for(const Project& p, const std::vector<templates::Template>& ts) {
    // The template whose category holds the project, the deepest one winning
    // (s010c): Develop/gtk/tidy-project -> a "Develop/gtk" template over a
    // "Develop" one; Develop/foo-project -> only "Develop".
    if (p.category.empty()) return nullptr;
    const auto slash = p.rel.rfind('/');
    const std::string parent = slash == std::string::npos ? "" : p.rel.substr(0, slash);
    const templates::Template* best = nullptr;
    for (const auto& t : ts) {
        if (t.category.empty()) continue;
        const bool holds = parent == t.category || parent.rfind(t.category + "/", 0) == 0;
        if (holds && (!best || t.category.size() > best->category.size())) best = &t;
    }
    return best;
}

dr::Plan place(const Project& p, const std::string& home, const std::string& category, std::string* why) {
    dr::Plan plan;
    const auto no = [&](const std::string& w) { if (why) *why = w; return dr::Plan{}; };
    if (!p.category.empty()) return no("already placed in ~/" + p.category);
    if (p.code) return no("a code project stays where it is");
    if (const auto w = templates::category_refusal(category); !w.empty()) return no("category: " + w);
    const std::string top = category.substr(0, category.find('/'));
    if (top == "Desktop" || top == "Downloads") return no("a landing place is not a category");
    const fs::path folder = fs::path(p.path).filename();
    const std::string dir = home + "/" + category;
    const std::string to = dir + "/" + folder.string();
    std::error_code ec;
    if (fs::exists(to, ec)) return no("~/" + category + "/" + folder.string() + " already exists");
    plan.label = "Place: " + folder.string() + " -> ~/" + category;
    // Each missing level, parents first (a category may be two deep).
    if (const std::string first = home + "/" + top; !fs::is_directory(first, ec))
        plan.actions.push_back({dr::Verb::MakeDir, "", first, "the category's folder", 0});
    if (dir != home + "/" + top && !fs::is_directory(dir, ec))
        plan.actions.push_back({dr::Verb::MakeDir, "", dir, "the category's folder", 0});
    dr::Action mv;
    mv.verb = dr::Verb::Move;
    mv.from = p.path;
    mv.to = to;
    mv.reason = "uncategorized -> " + category;
    plan.actions.push_back(mv);
    if (why) why->clear();
    return plan;
}

dr::Plan fix(const Project& p, const std::vector<templates::Drift>& drift) {
    dr::Plan plan;
    std::vector<std::string> missing;
    for (const auto& d : drift)
        if (d.kind == templates::Drift::Kind::Missing) missing.push_back(d.path);
    std::sort(missing.begin(), missing.end(), [](const std::string& a, const std::string& b) {
        const auto da = std::count(a.begin(), a.end(), '/'), db = std::count(b.begin(), b.end(), '/');
        return da != db ? da < db : a < b;                          // parents first
    });
    for (const auto& m : missing) {
        dr::Action a;
        a.verb = dr::Verb::MakeDir;
        a.to = p.path + "/" + m;
        a.reason = "the template names it";
        plan.actions.push_back(a);
    }
    if (!plan.actions.empty())
        plan.label = "Fix: " + fs::path(p.path).filename().string() + " -- " + std::to_string(missing.size()) +
                     " missing folder" + (missing.size() == 1 ? "" : "s");
    return plan;
}

std::set<std::string> parse_left(const std::string& text) {
    std::set<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("left=", 0) == 0 && line.size() > 5) out.insert(line.substr(5));
    }
    return out;
}

std::string format_left(const std::set<std::string>& left) {
    std::string s = "# Tidy: uncategorized projects you chose to leave where they are\n";
    for (const auto& l : left) s += "left=" + l + "\n";
    return s;
}

// ── Uses (s010g) ─────────────────────────────────────────────────────────────
std::vector<std::string> uses_of(const std::vector<Use>& uses, const std::string& id) {
    std::vector<std::string> out;
    for (const auto& u : uses) if (u.project == id) out.push_back(u.used);
    return out;
}

std::vector<std::string> used_by(const std::vector<Use>& uses, const std::string& id) {
    std::vector<std::string> out;
    for (const auto& u : uses) if (u.used == id) out.push_back(u.project);
    return out;
}

bool can_use(const std::vector<Use>& uses, const std::string& project, const std::string& used, std::string* why) {
    const auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (project.empty() || used.empty()) return no("no id");
    if (project == used) return no("a project cannot use itself");
    for (const auto& u : uses)
        if (u.project == project && u.used == used) return no("already used");
    // A loop: `project` reachable from `used` along uses.
    std::vector<std::string> todo{used};
    std::set<std::string> seen;
    while (!todo.empty()) {
        const auto at = todo.back();
        todo.pop_back();
        if (!seen.insert(at).second) continue;
        for (const auto& next : uses_of(uses, at)) {
            if (next == project) return no("it is made with this one already -- a loop");
            todo.push_back(next);
        }
    }
    return true;
}

}  // namespace tidy::core::projects
