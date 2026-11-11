#include "core/Projects.hpp"

#include <latch/Protect.hpp>

#include <algorithm>
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

}  // namespace tidy::core::projects
