#include "core/Own.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tidy::core::own {

namespace fs = std::filesystem;

std::string refusal(const std::string& rel) {
    if (rel.empty()) return "no name";
    if (rel[0] == '/') return "not relative to Tidy's data folder";
    std::size_t start = 0;
    while (start <= rel.size()) {
        const auto end = rel.find('/', start);
        const std::string part = rel.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") return "a bad part in \"" + rel + "\"";
        if (part[0] == '.') return "a hidden part in \"" + rel + "\"";
        if (end == std::string::npos) break;
        start = end + 1;
    }
    for (const char* theirs : {"catalog.db", "catalog.db-wal", "catalog.db-shm", "manifest", "trash"})
        if (rel == theirs || rel.rfind(std::string(theirs) + "/", 0) == 0 || rel.rfind(std::string(theirs) + ".", 0) == 0)
            return "\"" + rel + "\" belongs to the catalog or the door";
    return "";
}

std::string write(const std::string& data_dir, const std::string& rel, const std::string& text) {
    if (const auto why = refusal(rel); !why.empty()) return why;
    if (data_dir.empty() || data_dir[0] != '/') return "no data folder";
    const fs::path target = fs::path(data_dir) / rel;
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) return "can't make " + target.parent_path().string() + ": " + ec.message();
    const fs::path tmp = target.parent_path() / ("." + target.filename().string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        out.flush();
        if (!out) return "can't write " + tmp.string();
    }
    if (std::rename(tmp.c_str(), target.c_str()) != 0) return "can't put " + target.string() + " in place";
    return "";
}

std::string read(const std::string& data_dir, const std::string& rel, bool* found) {
    if (found) *found = false;
    if (!refusal(rel).empty()) return "";
    std::ifstream in(fs::path(data_dir) / rel, std::ios::binary);
    if (!in) return "";
    if (found) *found = true;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace tidy::core::own
