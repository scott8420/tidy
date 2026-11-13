#include "core/Quarantine.hpp"

#include "core/Own.hpp"

#include <latch/Package.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <set>
#include <sstream>

namespace tidy::core::quarantine {

const char* answer_title(Answer a) {
    switch (a) {
        case Answer::None:     return "Not answered";
        case Answer::Bundle:   return "A bundle";
        case Answer::Ordinary: return "An ordinary folder";
        case Answer::NotNow:   return "Not now";
    }
    return "";
}

namespace {
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    const auto b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}
const char* word_of(Answer a) {
    switch (a) {
        case Answer::Bundle:   return "bundle";
        case Answer::Ordinary: return "ordinary";
        case Answer::NotNow:   return "not now";
        case Answer::None:     break;
    }
    return "";
}
Answer answer_of(const std::string& w) {
    if (w == "bundle") return Answer::Bundle;
    if (w == "ordinary") return Answer::Ordinary;
    if (w == "not now") return Answer::NotNow;
    return Answer::None;
}
std::string lower_ext(std::string x) {
    for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!x.empty() && x[0] != '.') x = "." + x;
    if (x.size() < 2 || x.find('/') != std::string::npos || x.find('.', 1) != std::string::npos) return "";
    return x;
}
bool good_rel(const std::string& r) {
    if (r.empty() || r[0] == '/' || r.back() == '/') return false;
    std::istringstream in(r);
    for (std::string p; std::getline(in, p, '/');)
        if (p.empty() || p == "." || p == "..") return false;
    return true;
}
}  // namespace

Answers parse(const std::string& text) {
    Answers a;
    std::set<std::string> held;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string kind = line.substr(0, sp);
        std::string rest = trim(line.substr(sp + 1));
        if (kind == "held") {
            if (good_rel(rest) && held.insert(rest).second) a.held.push_back(rest);
            continue;
        }
        const auto eq = rest.rfind(" = ");
        if (eq == std::string::npos) continue;
        const Answer ans = answer_of(trim(rest.substr(eq + 3)));
        const std::string key = trim(rest.substr(0, eq));
        if (ans == Answer::None) continue;
        if (kind == "ext") {
            if (const auto e = lower_ext(key); !e.empty()) a.ext[e] = ans;
        } else if (kind == "folder") {
            if (good_rel(key)) a.folders[key] = ans;
        }
    }
    return a;
}

std::string to_text(const Answers& a) {
    std::ostringstream o;
    o << "# Tidy -- answers to the suspect scan (Quarantine). Written by Tidy's Save;\n"
         "# one line each: ext .x = bundle|ordinary|not now, folder <rel> = ..., held <rel>.\n";
    for (const auto& [e, ans] : a.ext)
        if (ans != Answer::None) o << "ext " << e << " = " << word_of(ans) << "\n";
    for (const auto& [f, ans] : a.folders)
        if (ans != Answer::None) o << "folder " << f << " = " << word_of(ans) << "\n";
    for (const auto& h : a.held) o << "held " << h << "\n";
    return o.str();
}

Answers load(const std::string& data_dir) { return parse(own::read(data_dir, kFile)); }

Answer of_ext(const Answers& a, const std::string& ext) {
    const auto it = a.ext.find(lower_ext(ext));
    return it == a.ext.end() ? Answer::None : it->second;
}
Answer of_folder(const Answers& a, const std::string& rel) {
    const auto it = a.folders.find(rel);
    return it == a.folders.end() ? Answer::None : it->second;
}

std::vector<std::string> bundle_exts(const Answers& a) {
    std::vector<std::string> v;
    for (const auto& [e, ans] : a.ext)
        if (ans == Answer::Bundle) v.push_back(e);
    return v;
}

namespace {
struct State {
    std::string home;
    Answers a;
    std::set<std::string> held, bundles;   // absolute
};
std::atomic<std::shared_ptr<const State>> g_state{std::make_shared<const State>()};
}  // namespace

void set(const std::string& home, Answers a) {
    auto s = std::make_shared<State>();
    s->home = home;
    for (const auto& h : a.held)
        if (of_folder(a, h) != Answer::Ordinary && of_folder(a, h) != Answer::Bundle) s->held.insert(home + "/" + h);
    for (const auto& [f, ans] : a.folders)
        if (ans == Answer::Bundle) s->bundles.insert(home + "/" + f);
        else if (ans == Answer::NotNow) s->held.insert(home + "/" + f);
    s->a = std::move(a);
    g_state.store(std::move(s));
}
Answers current() { return g_state.load()->a; }
std::string current_home() { return g_state.load()->home; }

bool held(const std::string& abs) {
    const auto s = g_state.load();
    return !s->held.empty() && s->held.count(abs);
}

bool one(const std::string& abs) {
    const auto slash = abs.rfind('/');
    if (latch::pkg::is_package_name(slash == std::string::npos ? abs : abs.substr(slash + 1))) return true;
    const auto s = g_state.load();
    return s->bundles.count(abs) || s->held.count(abs);
}

}  // namespace tidy::core::quarantine
