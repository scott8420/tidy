#include "core/Background.hpp"

#include <filesystem>
#include <fcntl.h>
#include <unistd.h>

namespace tidy::core::background {

namespace fs = std::filesystem;
namespace dr = latch::door;

namespace {
std::string read_small(const std::string& path) {   // POSIX, read-only
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return "";
    std::string out;
    char buf[4096];
    for (ssize_t n; (n = ::read(fd, buf, sizeof buf)) > 0 && out.size() < 65536;) out.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    return out;
}
std::string quoted(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}
const std::vector<std::string> kReload = {"systemctl", "--user", "daemon-reload"};
}  // namespace

std::string unit_dir(const std::string& home, const char* xdg_config, bool fixture) {
    const std::string cfg = (!fixture && xdg_config && xdg_config[0] == '/') ? xdg_config : home + "/.config";
    return cfg + "/systemd/user";
}

std::string service_text(const std::string& exe, const std::string& fixture_home) {
    std::string s = std::string(kHeader) + " -- written by Tidy (main menu, Background scan...). Turn it off there.\n"
                    "[Unit]\nDescription=Tidy background scan (read-only look; notifies at N)\n\n"
                    "[Service]\nType=oneshot\n";
    if (!fixture_home.empty()) s += "Environment=" + quoted("TIDY_HOME=" + fixture_home) + "\n";
    s += "ExecStart=" + quoted(exe) + " --scan --notify\nNice=10\nIOSchedulingClass=idle\n";
    return s;
}

std::string timer_text(int minutes) {
    return std::string(kHeader) + " -- written by Tidy (main menu, Background scan...). Turn it off there.\n"
           "[Unit]\nDescription=Tidy background scan, every " + std::to_string(minutes) + " minutes\n\n"
           "[Timer]\nOnStartupSec=5min\nOnUnitActiveSec=" + std::to_string(minutes) + "min\n"
           "Unit=" + kService + "\n\n[Install]\nWantedBy=timers.target\n";
}

int minutes_of(const std::string& t) {
    if (t.rfind(kHeader, 0) != 0) return 0;
    const auto k = t.find("\nOnUnitActiveSec=");
    if (k == std::string::npos) return 0;
    try { return std::stoi(t.substr(k + 17)); } catch (...) { return 0; }
}

State state_of(const std::string& dir) {
    State s;
    std::error_code ec;
    const auto sv = dir + "/" + kService, tm = dir + "/" + kTimer;
    s.service = fs::is_regular_file(fs::symlink_status(sv, ec));
    s.timer = fs::is_regular_file(fs::symlink_status(tm, ec));
    const auto ts = read_small(tm);
    s.ours = s.service && s.timer && read_small(sv).rfind(kHeader, 0) == 0 && ts.rfind(kHeader, 0) == 0;
    s.minutes = minutes_of(ts);
    return s;
}

Work turn_on(const std::string& home, const std::string& dir, const std::string& exe,
             const std::string& fixture_home, int minutes) {
    Work w;
    w.plan.label = "Background scan on: every " + std::to_string(minutes) + " minutes";
    // Each missing level below home, parents first.
    std::vector<std::string> levels;
    std::error_code ec;
    for (fs::path p = dir; p.string().size() > home.size() && !fs::exists(fs::symlink_status(p, ec)); p = p.parent_path())
        levels.insert(levels.begin(), p.string());
    for (const auto& l : levels) {
        dr::Action a;
        a.verb = dr::Verb::MakeDir;
        a.to = l;
        a.reason = "for the systemd user units";
        w.plan.actions.push_back(a);
    }
    const auto st = state_of(dir);
    const std::pair<std::string, std::string> files[] = {{kService, service_text(exe, fixture_home)},
                                                         {kTimer, timer_text(minutes)}};
    for (const auto& [name, text] : files) {
        const auto at = dir + "/" + name;
        const bool there = name == std::string(kService) ? st.service : st.timer;
        if (there) {
            w.replaces.push_back({at, text, "ours, rewritten in place"});
            continue;
        }
        dr::Action a;
        a.verb = dr::Verb::Write;
        a.to = at;
        a.content = text;
        a.reason = name == std::string(kService) ? "runs: " + exe + " --scan --notify (read-only look)"
                                                 : "every " + std::to_string(minutes) + " minutes";
        w.plan.actions.push_back(a);
    }
    w.commands = {kReload, {"systemctl", "--user", "enable", "--now", kTimer}};
    return w;
}

Work turn_off(const std::string& dir) {
    Work w;
    w.plan.label = "Background scan off";
    w.first = {{"systemctl", "--user", "disable", "--now", kTimer}};
    const auto st = state_of(dir);
    for (const auto* name : {kTimer, kService}) {
        if (!(name == std::string(kTimer) ? st.timer : st.service)) continue;
        dr::Action a;
        a.verb = dr::Verb::Trash;
        a.from = dir + "/" + name;
        a.reason = "the unit (undo puts it back, still off)";
        w.plan.actions.push_back(a);
    }
    w.commands.push_back(kReload);
    return w;
}

Work retime(const std::string& dir, int minutes) {
    Work w;
    w.replaces.push_back({dir + "/" + kTimer, timer_text(minutes), "every " + std::to_string(minutes) + " minutes"});
    w.commands = {kReload, {"systemctl", "--user", "restart", kTimer}};
    return w;
}

std::string shown_command(const std::vector<std::string>& argv) {
    std::string s;
    for (const auto& a : argv) s += (s.empty() ? "" : " ") + a;
    return s;
}

}  // namespace tidy::core::background
