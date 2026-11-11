#include "core/FileLists.hpp"

#include <algorithm>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <set>
#include <sstream>

namespace tidy::core::filelists {

namespace fs = std::filesystem;
namespace dr = latch::door;

namespace {
// POSIX read, read-only (the gate forbids <fstream> in Tidy's sources).
std::string read_text(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return "";
    std::string out;
    char buf[65536];
    for (ssize_t n; (n = ::read(fd, buf, sizeof buf)) > 0 && out.size() < dr::kWriteMax;)
        out.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    return out;
}
bool plain(const std::string& name) { return !name.empty() && name.find_first_of("\t\n\r") == std::string::npos; }
}  // namespace

std::vector<std::string> roots(const std::string& home) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(home, ec)) {
        const auto n = e.path().filename().string();
        if (n.empty() || n[0] == '.' || !e.is_directory(ec) || e.is_symlink(ec)) continue;
        out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string format(std::vector<Line> lines) {
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.name < b.name; });
    std::ostringstream o;
    o << "# Tidy file ids -- a backup of each file's user.tidy.id, written by Tidy. id size mtime name\n";
    for (const auto& l : lines)
        if (plain(l.name) && facts::is_id(l.id)) o << l.id << '\t' << l.size << '\t' << l.mtime << '\t' << l.name << '\n';
    return o.str();
}

std::vector<Line> parse(const std::string& text) {
    std::vector<Line> out;
    std::istringstream in(text);
    for (std::string s; std::getline(in, s);) {
        if (s.empty() || s[0] == '#') continue;
        const auto a = s.find('\t'), b = a == std::string::npos ? a : s.find('\t', a + 1),
                   c = b == std::string::npos ? b : s.find('\t', b + 1);
        if (c == std::string::npos) continue;
        Line l;
        l.id = s.substr(0, a);
        try {
            l.size = std::stoull(s.substr(a + 1, b - a - 1));
            l.mtime = std::stoll(s.substr(b + 1, c - b - 1));
        } catch (...) { continue; }
        l.name = s.substr(c + 1);
        if (facts::is_id(l.id) && plain(l.name)) out.push_back(std::move(l));
    }
    return out;
}

std::map<std::string, std::vector<Line>> wanted(const std::vector<facts::Facts>& files) {
    std::map<std::string, std::vector<Line>> out;
    for (const auto& f : files) {
        if (f.tags.id.empty()) continue;
        const fs::path p(f.path);
        const auto name = p.filename().string();
        if (!plain(name)) continue;
        out[p.parent_path().string()].push_back({f.tags.id, f.size, f.mtime, name});
    }
    for (auto& [dir, v] : out)
        std::sort(v.begin(), v.end(), [](const Line& a, const Line& b) { return a.name < b.name; });
    return out;
}

std::vector<Found> refind(const std::vector<facts::Facts>& files) {
    std::set<std::string> carried;
    for (const auto& f : files) if (!f.tags.id.empty()) carried.insert(f.tags.id);
    std::map<std::string, std::vector<Line>> lists;     // folder -> its list, read once
    std::vector<Found> out;
    std::map<std::string, int> claims;
    for (const auto& f : files) {
        if (!f.tags.id.empty()) continue;
        const fs::path p(f.path);
        const auto dir = p.parent_path().string();
        auto it = lists.find(dir);
        if (it == lists.end()) it = lists.emplace(dir, parse(read_text(dir + "/" + kFile))).first;
        for (const auto& l : it->second)
            if (l.name == p.filename().string() && l.size == f.size && !carried.count(l.id)) {
                out.push_back({f.path, l.id});
                ++claims[l.id];
                break;
            }
    }
    std::erase_if(out, [&](const Found& x) { return claims[x.id] > 1; });
    return out;
}

std::vector<facts::Facts> with_found(std::vector<facts::Facts> files, const std::vector<Found>& found) {
    for (auto& f : files)
        for (const auto& x : found)
            if (x.path == f.path) f.tags.id = x.id;
    return files;
}

dr::Plan plan(const std::vector<Found>& found, const std::map<std::string, std::vector<Line>>& want) {
    dr::Plan p;
    for (const auto& x : found) {
        dr::Action a;
        a.verb = dr::Verb::Tag;
        a.from = x.path;
        a.attr = facts::kId;
        a.content = x.id;
        a.reason = "its id again, from the folder's " + std::string(kFile);
        p.actions.push_back(a);
    }
    std::size_t lists = 0;
    for (const auto& [dir, lines] : want) {
        const std::string at = dir + "/" + kFile;
        const std::string text = format(lines), have = read_text(at);
        if (have == text) continue;
        std::error_code ec;
        if (fs::exists(fs::symlink_status(at, ec))) {
            dr::Action t;
            t.verb = dr::Verb::Trash;
            t.from = at;
            t.reason = "the old id list (replaced next)";
            p.actions.push_back(t);
        }
        dr::Action w;
        w.verb = dr::Verb::Write;
        w.to = at;
        w.content = text;
        w.reason = std::to_string(lines.size()) + " id" + (lines.size() == 1 ? "" : "s");
        p.actions.push_back(w);
        ++lists;
    }
    if (!p.actions.empty())
        p.label = "Id lists: " + std::to_string(lists) + " folder" + (lists == 1 ? "" : "s") +
                  (found.empty() ? "" : ", " + std::to_string(found.size()) + " re-found");
    return p;
}

}  // namespace tidy::core::filelists
