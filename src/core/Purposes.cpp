#include "core/Purposes.hpp"
#include "core/Tagging.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

#include <fnmatch.h>

namespace tidy::core::purposes {

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
// key=a|b|c -- fields may not hold '|' or a newline (refused on the way in).
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '|') { out.push_back(cur); cur.clear(); } else cur += c;
    }
    out.push_back(cur);
    return out;
}
bool clean(const std::string& s) {
    return !s.empty() && s.find('|') == std::string::npos && s.find('\n') == std::string::npos;
}
bool matches(const Hint& h, const std::string& folder, const std::string& name) {
    if (h.kind == Hint::Kind::Folder) return lower(h.pattern) == lower(folder);
    return ::fnmatch(h.pattern.c_str(), name.c_str(), FNM_CASEFOLD) == 0;
}
bool is_landing(const std::string& folder, const std::vector<std::string>& landing) {
    for (const auto& l : landing)
        if (lower(l) == lower(folder)) return true;
    return false;
}
}  // namespace

const Purpose* Book::find(const std::string& id) const {
    for (const auto& p : purposes)
        if (p.id == id) return &p;
    return nullptr;
}

Book parse(const std::string& text) {
    Book b;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const auto f = split(line.substr(eq + 1));
        if (key == "purpose" && f.size() == 3 && !f[0].empty() && !b.find(f[0]))
            b.purposes.push_back({f[0], f[1], f[2] == "closed"});
        else if (key == "hint" && f.size() == 5 && (f[0] == "folder" || f[0] == "name") && !f[1].empty()) {
            Hint h;
            h.kind = f[0] == "folder" ? Hint::Kind::Folder : Hint::Kind::Name;
            h.pattern = f[1];
            h.purpose = f[2];
            try { h.yes = std::stoi(f[3]); h.no = std::stoi(f[4]); } catch (...) { continue; }
            b.hints.push_back(h);
        }
    }
    return b;
}

std::string format(const Book& b) {
    std::ostringstream o;
    o << "# Tidy purposes -- written by Tidy\n"
      << "# purpose=id|title|open/closed    hint=folder/name|pattern|purpose|yes|no\n";
    for (const auto& p : b.purposes) o << "purpose=" << p.id << "|" << p.title << "|" << (p.closed ? "closed" : "open") << "\n";
    for (const auto& h : b.hints)
        o << "hint=" << (h.kind == Hint::Kind::Folder ? "folder" : "name") << "|" << h.pattern << "|" << h.purpose
          << "|" << h.yes << "|" << h.no << "\n";
    return o.str();
}

std::string slug_of(const std::string& title) {
    std::string s;
    for (char c : lower(trim(title))) {
        if (std::isalnum(static_cast<unsigned char>(c))) s += c;
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.substr(0, 64);
}

std::string add(Book& b, const std::string& raw, std::string* id) {
    const std::string title = trim(raw);
    if (!clean(title)) return "a purpose needs a name (no '|')";
    const std::string s = slug_of(title);
    if (s.empty()) return "a purpose's name needs a letter or a digit";
    if (b.find(s)) return "\"" + title + "\" is already a purpose";
    b.purposes.push_back({s, title, false});
    if (id) *id = s;
    return "";
}

std::string set_closed(Book& b, const std::string& id, bool closed) {
    for (auto& p : b.purposes)
        if (p.id == id) { p.closed = closed; return ""; }
    return "no purpose \"" + id + "\"";
}

std::string teach(Book& b, const std::string& raw, const std::string& purpose) {
    const std::string glob = trim(raw);
    if (!clean(glob) || glob.find('/') != std::string::npos) return "a name pattern, like *W-2*";
    if (!b.find(purpose)) return "no purpose \"" + purpose + "\"";
    for (auto& h : b.hints)
        if (h.kind == Hint::Kind::Name && h.pattern == glob && h.purpose == purpose) { ++h.yes; return ""; }
    b.hints.push_back({Hint::Kind::Name, glob, purpose, 1, 0});
    return "";
}

Guess guess(const Book& b, const std::string& path, const std::vector<std::string>& landing) {
    const fs::path p(path);
    const std::string name = p.filename().string(), folder = p.parent_path().filename().string();
    Guess best;
    int best_rank = -1, best_score = 0;
    for (std::size_t i = 0; i < b.hints.size(); ++i) {
        const auto& h = b.hints[i];
        if (!h.active() || !matches(h, folder, name)) continue;
        if (h.kind == Hint::Kind::Folder && is_landing(folder, landing)) continue;
        const auto* pp = b.find(h.purpose);
        if (!pp || pp->closed) continue;
        const int rank = h.kind == Hint::Kind::Name ? 1 : 0, score = h.yes - h.no;
        if (rank > best_rank || (rank == best_rank && score > best_score)) {
            best_rank = rank;
            best_score = score;
            best = {h.purpose, std::string(rank ? "name " : "folder ") + h.pattern, static_cast<int>(i)};
        }
    }
    return best;
}

void learn(Book& b, const std::string& path, const std::string& chosen, const Guess& g,
           const std::vector<std::string>& landing) {
    if (g.hint >= 0 && static_cast<std::size_t>(g.hint) < b.hints.size()) {
        if (g.purpose == chosen) ++b.hints[g.hint].yes; else ++b.hints[g.hint].no;
    }
    const std::string folder = fs::path(path).parent_path().filename().string();
    if (folder.empty() || is_landing(folder, landing) || !clean(folder)) return;
    for (std::size_t i = 0; i < b.hints.size(); ++i) {
        auto& h = b.hints[i];
        if (h.kind == Hint::Kind::Folder && lower(h.pattern) == lower(folder) && h.purpose == chosen) {
            if (static_cast<int>(i) != g.hint) ++h.yes;   // the confirmed guess was already counted
            return;
        }
    }
    b.hints.push_back({Hint::Kind::Folder, folder, chosen, 1, 0});
}

latch::door::Plan assign(const std::vector<facts::Facts>& files, const Purpose& p) {
    facts::Tags want;
    want.purpose = p.id;
    return tagging::stamp(files, want, "Purpose: " + std::to_string(files.size()) + " file" +
                                           (files.size() == 1 ? "" : "s") + " -> " + p.title);
}

std::string retention_at_close(const std::string& role) {
    if (role == "product") return "keep";
    if (role == "history") return "historical";
    if (role == "junk-drawer") return "";
    if (role == "incidental" || role == "input") return "ephemeral";
    return "historical";   // no role known: archived, never lost (safety is the product)
}

latch::door::Plan close(const std::vector<facts::Facts>& files, const Purpose& p) {
    latch::door::Plan plan;
    plan.label = "Close: " + p.title;
    for (const auto& f : files) {
        const auto r = retention_at_close(f.tags.role);
        if (r.empty() || f.tags.purpose != p.id) continue;
        facts::Tags want;
        want.retention = r;
        auto one = tagging::stamp({f}, want, plan.label);
        plan.actions.insert(plan.actions.end(), one.actions.begin(), one.actions.end());
    }
    return plan;
}

}  // namespace tidy::core::purposes
