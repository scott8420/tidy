#include "core/Protection.hpp"
#include "core/Quarantine.hpp"

#include <latch/Protect.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace tidy::core::protection {

namespace fs = std::filesystem;

std::string config_home(const std::string& home) {
    const char* real = std::getenv("HOME");
    const char* x = std::getenv("XDG_CONFIG_HOME");
    if (real && home == real && x && x[0] == '/') return x;
    return home + "/.config";
}

std::vector<std::string> samson(const std::string& home, const std::string& cfg) {
    std::error_code ec;
    if (!fs::is_directory(cfg + "/samson", ec)) return {};   // Samson is not installed: nothing of its own
    return latch::protect::samson_places(home, cfg);
}

std::vector<std::string> all(const std::string& home, const std::string& cfg, const settings::Settings& st) {
    std::vector<std::string> out = st.hands_off;
    for (auto& r : samson(home, cfg))
        if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(std::move(r));
    return out;
}

void apply(const std::string& home, const std::string& cfg, const settings::Settings& st) {
    auto v = all(home, cfg, st);
    for (const auto& h : quarantine::hands_off())   // s025b1: answered "don't touch" in Quarantine
        if (std::find(v.begin(), v.end(), h) == v.end()) v.push_back(h);
    latch::protect::set_owned(std::move(v));
}

bool under(const std::string& rel) {
    for (std::size_t i = 0;; ++i) {
        i = rel.find('/', i);
        if (latch::protect::here(rel.substr(0, i))) return true;
        if (i == std::string::npos) return false;
    }
}

std::string rel_of(const std::string& home, const std::string& text, std::string& rel) {
    std::string p = text;
    while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) p.pop_back();
    while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) p.erase(0, 1);
    if (p.empty()) return "an empty line";
    if (p == "~") return "not your whole home";
    if (p.rfind("~/", 0) == 0) p = p.substr(2);
    else if (p[0] == '/') {
        if (p == home || p == home + "/") return "not your whole home";
        if (p.rfind(home + "/", 0) != 0) return "\"" + text + "\" is not in your home";
        p = p.substr(home.size() + 1);
    }
    while (!p.empty() && p.back() == '/') p.pop_back();
    if (p.empty()) return "not your whole home";
    for (std::size_t i = 0, j; i <= p.size(); i = j + 1) {
        j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        const std::string part = p.substr(i, j - i);
        if (part.empty() || part == "." || part == "..") return "\"" + text + "\" is not a plain folder in your home";
    }
    rel = p;
    return "";
}

}  // namespace tidy::core::protection
