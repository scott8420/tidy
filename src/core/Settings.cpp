#include "core/Settings.hpp"

#include <algorithm>
#include <filesystem>
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

bool media_of(const std::string& line, Media& out) {
    const auto bar = line.find('|');
    std::string label = bar == std::string::npos ? "" : trim(line.substr(0, bar));
    std::string path = trim(bar == std::string::npos ? line : line.substr(bar + 1));
    if (path.empty() || path[0] != '/') return false;
    path = std::filesystem::path(path).lexically_normal().string();   // latch wants a clean folder
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    if (label.empty()) label = path.substr(path.find_last_of('/') + 1);
    if (label.empty()) return false;
    out = {label, path};
    return true;
}

std::string media_line(const Media& m) { return m.label + " | " + m.path; }

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
        } else if (k == "bg_minutes") {
            try {
                s.bg_minutes = std::clamp(std::stoi(v), kBgMin, kBgMax);
            } catch (...) {}
        } else if (k == "notify_at") {
            try {
                s.notify_at = std::clamp(std::stoi(v), kNotifyMin, kNotifyMax);
            } catch (...) {}
        } else if (k == "held_years") {
            try {
                s.held_years = std::clamp(std::stoi(v), kHeldMin, kHeldMax);
            } catch (...) {}
        } else if (k == "archive_days") {
            try {
                s.archive_days = std::clamp(std::stoi(v), kArchiveMin, kArchiveMax);
            } catch (...) {}
        } else if (k == "home") {
            s.home_set = true;
            if (!v.empty() && std::find(s.home.begin(), s.home.end(), v) == s.home.end()) s.home.push_back(v);
        } else if (k == "proofs_needed") {
            try {
                s.proofs_needed = std::clamp(std::stoi(v), kProofsMin, kProofsMax);
            } catch (...) {}
        } else if (k == "media") {
            Media m;
            if (media_of(v, m) && std::none_of(s.media.begin(), s.media.end(),
                                               [&](const Media& x) { return x.label == m.label; }))
                s.media.push_back(m);
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
    o << "bg_minutes=" << std::clamp(s.bg_minutes, kBgMin, kBgMax) << "\n";
    o << "notify_at=" << std::clamp(s.notify_at, kNotifyMin, kNotifyMax) << "\n";
    o << "held_years=" << std::clamp(s.held_years, kHeldMin, kHeldMax) << "\n";
    o << "archive_days=" << std::clamp(s.archive_days, kArchiveMin, kArchiveMax) << "\n";
    o << "rescan_minutes=" << std::clamp(s.rescan_minutes, kRescanMin, kRescanMax) << "\n";
    for (const auto& j : s.junk) o << "junk=" << j << "\n";
    o << "proofs_needed=" << std::clamp(s.proofs_needed, kProofsMin, kProofsMax) << "\n";
    for (const auto& m : s.media) o << "media=" << m.label << "|" << m.path << "\n";
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
