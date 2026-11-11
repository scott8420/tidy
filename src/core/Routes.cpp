#include "core/Routes.hpp"

#include <cctype>
#include <filesystem>
#include <sstream>

#include <fnmatch.h>

namespace tidy::core::routes {

namespace fs = std::filesystem;

namespace {
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '|') { out.push_back(cur); cur.clear(); } else cur += c;
    }
    out.push_back(cur);
    return out;
}
bool clean(const std::string& s) { return s.find('|') == std::string::npos && s.find('\n') == std::string::npos; }
// A folder a route may hold: relative, no "..", nothing hidden.
bool fine_folder(const std::string& f) {
    if (f.empty() || f[0] == '/' || !clean(f)) return false;
    for (const auto& part : fs::path(f))
        if (const auto p = part.string(); p.empty() || p == ".." || p == "." || p[0] == '.') return false;
    return true;
}
std::string ext_of(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) return "";
    return lower(name.substr(dot + 1));
}
int find(const Book& b, Route::Kind k, const std::string& pattern, const std::string& from, const std::string& folder) {
    for (std::size_t i = 0; i < b.routes.size(); ++i) {
        const auto& r = b.routes[i];
        if (r.kind == k && r.pattern == pattern && r.from == from && r.folder == folder) return static_cast<int>(i);
    }
    return -1;
}
}  // namespace

Book parse(const std::string& text) {
    Book b;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.substr(0, eq) != "route") continue;
        const auto f = split(line.substr(eq + 1));
        if (f.size() != 6 || (f[0] != "name" && f[0] != "from") || f[1].empty() || !fine_folder(f[3])) continue;
        Route r;
        r.kind = f[0] == "name" ? Route::Kind::Name : Route::Kind::From;
        r.pattern = f[1];
        r.from = r.kind == Route::Kind::From ? f[2] : "";
        r.folder = f[3];
        try { r.yes = std::stoi(f[4]); r.no = std::stoi(f[5]); } catch (...) { continue; }
        if (r.kind == Route::Kind::From && r.from.empty()) continue;
        if (find(b, r.kind, r.pattern, r.from, r.folder) >= 0) continue;
        b.routes.push_back(r);
    }
    return b;
}

std::string format(const Book& b) {
    std::ostringstream o;
    o << "# Tidy's learned routes (s022) -- where you sent files home.\n"
         "# route=name|<glob>||<folder>|<yes>|<no>   route=from|<*.ext>|<from>|<folder>|<yes>|<no>\n";
    for (const auto& r : b.routes)
        o << "route=" << (r.kind == Route::Kind::Name ? "name" : "from") << '|' << r.pattern << '|' << r.from << '|'
          << r.folder << '|' << r.yes << '|' << r.no << '\n';
    return o.str();
}

std::string name_glob(const std::string& file_name) {
    const std::string ext = ext_of(file_name);
    if (ext.empty()) return "";
    std::string word;
    for (char c : file_name) {
        if (!std::isalpha(static_cast<unsigned char>(c))) break;
        word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (word.size() < 3) return "";
    return word + "*." + ext;
}

std::string ext_glob(const std::string& file_name) {
    const std::string ext = ext_of(file_name);
    return ext.empty() ? "" : "*." + ext;
}

std::string from_of(const birth::Born& b) {
    if (b.place == "home") return "Home";
    if (b.place == "screenshots") return "Screenshots";
    if (b.place == "downloads") return "Downloads";
    if (b.place == "desktop") return "Desktop";
    const std::string parent = fs::path(b.file.path).parent_path().filename().string();
    return parent.empty() ? "Loose" : parent;
}

Proposal propose(const Book& b, const std::string& home, const birth::Born& file) {
    Proposal best;
    const std::string name = fs::path(file.file.path).filename().string();
    const std::string from = from_of(file), ext = ext_glob(name);
    int best_kind = 2, best_score = 0, best_yes = 0;   // kind: 0 name, 1 from
    for (std::size_t i = 0; i < b.routes.size(); ++i) {
        const auto& r = b.routes[i];
        if (!r.active()) continue;
        const int kind = r.kind == Route::Kind::Name ? 0 : 1;
        const bool fits = kind == 0 ? ::fnmatch(r.pattern.c_str(), name.c_str(), FNM_CASEFOLD) == 0
                                    : (!ext.empty() && lower(r.pattern) == ext && lower(r.from) == lower(from));
        if (!fits) continue;
        const int score = r.yes - r.no;
        if (kind > best_kind || (kind == best_kind && (score < best_score || (score == best_score && r.yes <= best_yes))))
            continue;
        std::error_code ec;
        if (!fs::is_directory(fs::path(home) / r.folder, ec)) continue;   // removed: proposes nothing
        // Already there (a loose file at that folder's top): no move to propose.
        if (fs::path(file.file.path).parent_path() == fs::path(home) / r.folder) continue;
        best_kind = kind;
        best_score = score;
        best_yes = r.yes;
        best.folder = r.folder;
        best.because = kind == 0 ? r.pattern : r.pattern + " from " + r.from;
        best.times = r.yes;
        best.route = static_cast<int>(i);
    }
    return best;
}

void learn(Book& b, const birth::Born& file, const std::string& folder, const Proposal& proposed) {
    if (!fine_folder(folder)) return;
    const std::string name = fs::path(file.file.path).filename().string();
    const auto bump = [&](Route::Kind k, const std::string& pattern, const std::string& from) {
        if (pattern.empty() || !clean(pattern) || !clean(from)) return;
        const int i = find(b, k, pattern, from, folder);
        if (i >= 0) { ++b.routes[i].yes; return; }
        Route r;
        r.kind = k;
        r.pattern = pattern;
        r.from = from;
        r.folder = folder;
        r.yes = 1;
        b.routes.push_back(r);
    };
    if (proposed.route >= 0 && static_cast<std::size_t>(proposed.route) < b.routes.size() && proposed.folder != folder)
        ++b.routes[proposed.route].no;
    bump(Route::Kind::Name, name_glob(name), "");
    bump(Route::Kind::From, ext_glob(name), from_of(file));
}

}  // namespace tidy::core::routes
