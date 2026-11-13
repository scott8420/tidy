#include "core/Shape.hpp"
#include "core/Quarantine.hpp"

#include "core/Projects.hpp"

#include <latch/Package.hpp>
#include <latch/Protect.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace tidy::core::shape {

namespace fs = std::filesystem;
namespace tp = templates;

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool is_repo(const fs::path& p) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p / ".git", ec));
}
// The folder a file at `rel` is in, two deep at most ("" = the top).
std::string folder_of(const std::string& rel) {
    const auto slash = rel.rfind('/');
    if (slash == std::string::npos) return "";
    const std::string dir = rel.substr(0, slash);
    const auto a = dir.find('/');
    if (a == std::string::npos) return dir;
    const auto b = dir.find('/', a + 1);
    return b == std::string::npos ? dir : dir.substr(0, b);
}
std::string glob_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '*' || c == '?' || c == '[' || c == ']' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

struct File {
    std::string rel, name, ext, prefix;
    int width = 0;
    std::size_t dir = 0;
};

void walk(const fs::path& root, const fs::path& dir, std::size_t idx, std::vector<File>& files,
          std::map<std::string, FolderSeen>& folders, std::map<std::string, std::set<std::size_t>>& folder_in, Scan& out) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || fs::is_symlink(st)) continue;
        const std::string rel = it->path().lexically_relative(root).generic_string();
        const bool pkg = fs::is_directory(st) && quarantine::one(it->path().string());
        if (fs::is_directory(st) && !pkg) {
            if (projects::is_project_name(name) || is_repo(it->path()) || latch::protect::protected_name(name)) {
                ++out.whole;
                continue;
            }
            if (std::count(rel.begin(), rel.end(), '/') < tp::kLiftDepth) {
                folders[rel].path = rel;
                folder_in[rel].insert(idx);
            }
            walk(root, it->path(), idx, files, folders, folder_in, out);
            continue;
        }
        if (!pkg && !fs::is_regular_file(st)) continue;
        File f;
        f.rel = rel;
        f.name = name;
        const fs::path p(name);
        f.ext = lower(p.extension().string());
        const auto [prefix, width] = numbered(p.stem().string());
        f.prefix = prefix;
        f.width = width;
        f.dir = idx;
        const std::string in = folder_of(rel);
        if (!in.empty() && rel.substr(0, rel.rfind('/')) == in) ++folders[in].files;   // directly in it
        files.push_back(std::move(f));
    }
}

struct Acc {
    Row row;
    std::map<std::string, std::size_t> where;
    std::set<std::size_t> in;
};
void add(Acc& a, const File& f) {
    ++a.row.count;
    ++a.where[folder_of(f.rel)];
    a.in.insert(f.dir);
    if (a.row.first.empty() || f.name < a.row.first) a.row.first = f.name;
    if (a.row.last.empty() || f.name > a.row.last) a.row.last = f.name;
}
}  // namespace

std::string Row::key() const { return run() ? ext + "#" + prefix + "#" + std::to_string(width) : ext; }

std::string Row::pattern() const {
    if (!run()) return "*" + ext;
    std::string p = glob_escape(prefix);
    for (int i = 0; i < width; ++i) p += "[0-9]";
    return p + ext;
}

std::string Row::title() const {
    const std::string e = ext.empty() ? std::string("(no extension)") : ext;
    if (run()) return e + " numbered " + first + " .. " + last;
    return e;
}

std::string Row::mostly() const {
    if (where.empty() || where.front().second * 2 < count) return "";
    return where.front().first;
}

std::pair<std::string, int> numbered(const std::string& stem) {
    std::size_t d = 0;
    while (d < stem.size() && std::isdigit(static_cast<unsigned char>(stem[stem.size() - 1 - d]))) ++d;
    if (d == 0 || d > 9) return {"", 0};
    return {stem.substr(0, stem.size() - d), static_cast<int>(d)};
}

Scan scan(const std::vector<std::string>& project_dirs) {
    Scan out;
    out.dirs = project_dirs;
    std::vector<File> files;
    std::map<std::string, FolderSeen> folders;
    std::map<std::string, std::set<std::size_t>> folder_in;
    for (std::size_t i = 0; i < project_dirs.size(); ++i) walk(project_dirs[i], project_dirs[i], i, files, folders, folder_in, out);
    out.files = files.size();

    // The runs: one type, one prefix, one number of digits, kRunMin files or more.
    std::map<std::string, std::size_t> group;
    const auto gkey = [](const File& f) { return f.ext + "#" + f.prefix + "#" + std::to_string(f.width); };
    for (const auto& f : files)
        if (f.width > 0 && !f.ext.empty()) ++group[gkey(f)];
    std::map<std::string, Acc> acc;
    for (const auto& f : files) {
        const bool run = f.width > 0 && !f.ext.empty() && group[gkey(f)] >= kRunMin;
        Row r;
        r.ext = f.ext;
        if (run) { r.prefix = f.prefix; r.width = f.width; }
        auto& a = acc[r.key()];
        if (a.row.count == 0) a.row = r;
        add(a, f);
    }
    for (auto& [k, a] : acc) {
        for (const auto& w : a.where) a.row.where.push_back(w);
        std::stable_sort(a.row.where.begin(), a.row.where.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
        a.row.projects = a.in.size();
        if (!a.row.run()) a.row.last.clear();   // one example name
        out.rows.push_back(std::move(a.row));
    }
    // By type (most files of the type first), a type's runs before its others.
    std::map<std::string, std::size_t> per_ext;
    for (const auto& r : out.rows) per_ext[r.ext] += r.count;
    std::sort(out.rows.begin(), out.rows.end(), [&](const Row& a, const Row& b) {
        if (a.ext != b.ext) {
            if (per_ext[a.ext] != per_ext[b.ext]) return per_ext[a.ext] > per_ext[b.ext];
            return a.ext < b.ext;
        }
        if (a.run() != b.run()) return a.run();
        if (a.count != b.count) return a.count > b.count;
        return a.key() < b.key();
    });
    for (auto& [p, f] : folders) {
        f.projects = folder_in[p].size();
        out.folders.push_back(f);
    }
    return out;
}

tp::Template make(const std::string& name, const std::string& category, const std::vector<std::string>& folders,
                  const std::vector<Row>& rows, const std::map<std::string, Pick>& picks) {
    tp::Template t;
    t.name = name;
    t.category = category;
    std::set<std::string> all;
    for (const auto& f : folders) {
        if (f.empty()) continue;
        all.insert(f);
        const auto slash = f.find('/');
        if (slash != std::string::npos) all.insert(f.substr(0, slash));
    }
    for (const auto& f : all) t.folders.push_back({f, tp::guess_role(f)});
    std::vector<tp::Route> sources, runs, rest;
    for (const auto& r : rows) {
        if (!r.routable()) continue;
        const auto it = picks.find(r.key());
        if (it == picks.end()) continue;
        if (it->second.source) { sources.push_back({r.pattern(), tp::kTop}); continue; }
        if (it->second.to.empty()) continue;
        (r.run() ? runs : rest).push_back({r.pattern(), it->second.to});
    }
    for (auto* v : {&sources, &runs, &rest}) t.routes.insert(t.routes.end(), v->begin(), v->end());
    return t;
}

std::string name_from(const std::string& folder_name) {
    std::string out;
    for (char c : lower(folder_name)) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += c;
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) out = "my";
    if (out.size() < 8 || out.compare(out.size() - 8, 8, "-project") != 0) out += "-project";
    return out.substr(0, 64);
}

}  // namespace tidy::core::shape
