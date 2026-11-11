#include "core/Settings.hpp"

#include <algorithm>
#include <fnmatch.h>
#include <sstream>

namespace tidy::core::settings {

namespace {
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    const auto b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}
}  // namespace

Settings parse(const std::string& text) {
    Settings s;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "aging_days") {
            try {
                s.aging_days = std::clamp(std::stoi(v), kAgingMin, kAgingMax);
            } catch (...) {}
        } else if (k == "rescan_minutes") {
            try {
                s.rescan_minutes = std::clamp(std::stoi(v), kRescanMin, kRescanMax);
            } catch (...) {}
        } else if (k == "home") {
            s.home_set = true;
            if (!v.empty() && std::find(s.home.begin(), s.home.end(), v) == s.home.end()) s.home.push_back(v);
        } else if (k == "junk" && !v.empty() &&
                   std::find(s.junk.begin(), s.junk.end(), v) == s.junk.end()) {
            s.junk.push_back(v);
        }
    }
    return s;
}

std::string format(const Settings& s) {
    std::ostringstream o;
    o << "# Tidy settings -- written by Tidy (Settings). One junk pattern per line.\n";
    o << "aging_days=" << std::clamp(s.aging_days, kAgingMin, kAgingMax) << "\n";
    o << "rescan_minutes=" << std::clamp(s.rescan_minutes, kRescanMin, kRescanMax) << "\n";
    for (const auto& j : s.junk) o << "junk=" << j << "\n";
    if (s.home_set) {
        if (s.home.empty()) o << "home=\n";   // the table emptied on purpose
        for (const auto& h : s.home) o << "home=" << h << "\n";
    }
    return o.str();
}

std::vector<std::string> patterns_of(const std::string& lines) {
    std::vector<std::string> out;
    std::istringstream in(lines);
    for (std::string l; std::getline(in, l);) {
        l = trim(l);
        if (!l.empty() && std::find(out.begin(), out.end(), l) == out.end()) out.push_back(l);
    }
    return out;
}

std::string junk_match(const std::vector<std::string>& patterns, const std::string& name) {
    for (const auto& p : patterns)
        if (::fnmatch(p.c_str(), name.c_str(), FNM_CASEFOLD) == 0) return p;
    return "";
}

}  // namespace tidy::core::settings
