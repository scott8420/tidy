#include "core/Home.hpp"
#include "core/Own.hpp"
#include "core/Sort.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fnmatch.h>
#include <set>
#include <unistd.h>

namespace tidy::core::home {

namespace fs = std::filesystem;
namespace tp = templates;

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
void add_folder(tp::Template& t, const std::string& path) {
    std::string at;
    std::size_t a = 0;
    while (a <= path.size()) {
        const auto b = path.find('/', a);
        at = path.substr(0, b);
        if (!tp::find(t, at)) t.folders.push_back({at, tp::Role::Input});
        if (b == std::string::npos) break;
        a = b + 1;
    }
}
void add_route(tp::Template& t, std::set<std::string>& seen, const std::string& pattern, const std::string& folder) {
    if (!seen.insert(lower(pattern)).second) return;   // first fit wins, as the old tables
    add_folder(t, folder);
    t.routes.push_back({pattern, folder});
}
// open + pread only (the write gate).
struct Fd {
    int fd = -1;
    explicit Fd(const std::string& p) : fd(::open(p.c_str(), O_RDONLY | O_CLOEXEC)) {}
    ~Fd() { if (fd >= 0) ::close(fd); }
    bool read(std::uint64_t at, void* buf, std::size_t n) const {
        return fd >= 0 && ::pread(fd, buf, n, static_cast<off_t>(at)) == static_cast<ssize_t>(n);
    }
};
std::uint32_t be32(const unsigned char* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint16_t be16(const unsigned char* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
std::string utf16be(const std::string& raw) {
    std::string out;
    for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
        std::uint32_t c = be16(reinterpret_cast<const unsigned char*>(raw.data() + i));
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < raw.size()) {
            const std::uint32_t lo = be16(reinterpret_cast<const unsigned char*>(raw.data() + i + 2));
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}
std::string latin(const std::string& raw) {   // Mac Roman: the ASCII half is enough for a name
    std::string out;
    for (unsigned char c : raw) if (c >= 0x20 && c < 0x7F) out += static_cast<char>(c);
    return out;
}
}  // namespace

bool is_home(const tp::Template& t) { return t.name == kName && t.category == kTop; }

tp::Template seed(const settings::Settings& st) {
    tp::Template t;
    t.name = kName;
    t.category = kTop;
    std::set<std::string> seen;
    // The Entertainment table first: "Downloads/<glob>" is the more particular.
    for (const auto& r : sort::ent_table(st)) add_route(t, seen, kDownloads + r.pattern, r.folder);
    const std::string font_folder = std::string("Fonts/") + kLetter + "/" + kFullName;
    auto table = homing::table(st);
    if (st.home_set)   // s026a's Graphics + Fonts kinds were recognise's fallback after your table: kept
        for (const auto& d : homing::defaults())
            if (d.folder == "Graphics" || d.folder == "Fonts") table.push_back(d);
    for (const auto& r : table) {
        const auto p = lower(r.pattern);
        if (r.folder == "Fonts" && (p == "*.otf" || p == "*.ttf")) add_route(t, seen, r.pattern, font_folder);
        else add_route(t, seen, r.pattern, r.folder);
    }
    std::sort(t.folders.begin(), t.folders.end(), [](const tp::Folder& a, const tp::Folder& b) { return a.path < b.path; });
    ensure_trash(t);   // s030i
    return t;
}

bool saved(const std::string& data_dir) {
    bool found = false;
    (void)own::read(data_dir, tp::rel_of(kName), &found);
    return found;
}

bool is_trash(const std::string& folder) { return folder == kTrash; }

void ensure_trash(tp::Template& t) {
    for (const auto& f : t.folders) if (f.path == kTrash) return;
    t.folders.push_back({kTrash, tp::Role::Input});
    tp::sort_folders(t);
}

tp::Template load(const std::string& data_dir, const settings::Settings& st) {
    bool found = false;
    const auto text = own::read(data_dir, tp::rel_of(kName), &found);
    if (found) {
        auto t = tp::parse(text);
        if (is_home(t)) { ensure_trash(t); return t; }
    }
    return seed(st);   // seed() has the Trash row
}
tp::Template load(const std::string& data_dir) {
    return load(data_dir, settings::parse(own::read(data_dir, settings::kFile)));
}

Design design(const tp::Template& t) {
    Design d;
    d.t = t;
    for (const auto& r : t.routes) {
        if (r.folder == tp::kStays || r.folder == tp::kTop) continue;
        if (r.pattern.rfind(kDownloads, 0) == 0) {
            const auto rest = r.pattern.substr(std::string(kDownloads).size());
            if (!rest.empty() && rest.find('/') == std::string::npos) d.ent.push_back({rest, r.folder});
            continue;
        }
        if (r.pattern.find('/') != std::string::npos) continue;   // other folder routes: not yet read by Sort
        d.types.push_back({r.pattern, r.folder});
    }
    // the particular first, as the builder keeps them
    std::stable_sort(d.types.begin(), d.types.end(), [](const homing::TypeRoute& a, const homing::TypeRoute& b) {
        return tp::specificity(a.pattern) < tp::specificity(b.pattern);
    });
    return d;
}
Design design_of(const std::string& data_dir) { return design(load(data_dir)); }

const homing::TypeRoute* rule_for(const Design& d, const std::string& file_name) {
    return homing::type_of(d.types, file_name);
}

bool has_slot(const std::string& folder) { return folder.find('<') != std::string::npos; }

std::string fixed_part(const std::string& folder) {
    const auto lt = folder.find('<');
    if (lt == std::string::npos) return folder;
    std::string out = folder.substr(0, lt);
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

bool generic_folder_name(const std::string& name) {
    std::string l;
    for (unsigned char c : name) l += static_cast<char>(std::tolower(c));
    static const std::set<std::string> g = {"static", "variable", "ttf", "otf", "woff", "woff2", "fonts", "font",
                                            "webfonts", "web", "desktop", "truetype", "opentype"};
    return g.count(l) > 0;
}

std::string clean_name(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c == '/' ? '-' : c;
    }
    const auto a = out.find_first_not_of(" .");
    if (a == std::string::npos) return "";
    const auto b = out.find_last_not_of(' ');
    return out.substr(a, b - a + 1);
}

std::string letter_of(const std::string& name) {
    if (!name.empty() && std::isalpha(static_cast<unsigned char>(name[0])))
        return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
    return "OTHER";
}

std::string font_name(const std::string& abs) {
    Fd f(abs);
    unsigned char h[12];
    if (!f.read(0, h, sizeof h)) return "";
    std::uint64_t base = 0;
    if (be32(h) == 0x74746366u) {   // 'ttcf' -- the first font of the collection
        unsigned char o[4];
        if (!f.read(12, o, 4)) return "";
        base = be32(o);
        if (!f.read(base, h, sizeof h)) return "";
    }
    const std::uint32_t ver = be32(h);
    if (ver != 0x00010000u && ver != 0x4F54544Fu /*OTTO*/ && ver != 0x74727565u /*true*/) return "";
    const std::uint16_t n = be16(h + 4);
    if (n == 0 || n > 512) return "";
    std::uint32_t name_at = 0, name_len = 0;
    for (std::uint16_t i = 0; i < n; ++i) {
        unsigned char r[16];
        if (!f.read(base + 12 + 16u * i, r, sizeof r)) return "";
        if (std::equal(r, r + 4, "name")) { name_at = be32(r + 8); name_len = be32(r + 12); break; }
    }
    if (!name_at || name_len < 6 || name_len > (1u << 20)) return "";
    std::string tbl(name_len, '\0');
    if (!f.read(name_at, tbl.data(), name_len)) return "";
    const auto* p = reinterpret_cast<const unsigned char*>(tbl.data());
    const std::uint16_t count = be16(p + 2), strings = be16(p + 4);
    // best: nameID 4 on Windows (US English first), then Mac; else family (1) + style (2).
    std::string best[3][5];   // [rank][nameID 0..4]
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t at = 6 + 12u * i;
        if (at + 12 > name_len) break;
        const std::uint16_t plat = be16(p + at), enc = be16(p + at + 2), lang = be16(p + at + 4), id = be16(p + at + 6),
                            len = be16(p + at + 8), off = be16(p + at + 10);
        if (id > 4 || std::size_t(strings) + off + len > name_len) continue;
        const std::string raw = tbl.substr(std::size_t(strings) + off, len);
        int rank = -1;
        std::string s;
        if (plat == 3 && (enc == 1 || enc == 10 || enc == 0)) { rank = lang == 0x409 ? 0 : 1; s = utf16be(raw); }
        else if (plat == 0) { rank = 1; s = utf16be(raw); }
        else if (plat == 1 && enc == 0) { rank = 2; s = latin(raw); }
        if (rank < 0 || s.empty() || !best[rank][id].empty()) continue;
        best[rank][id] = s;
    }
    for (auto& r : best) if (!clean_name(r[4]).empty()) return clean_name(r[4]);
    for (auto& r : best)
        if (!clean_name(r[1]).empty()) {
            const auto st = clean_name(r[2]);
            return clean_name(r[1]) + (st.empty() || lower(st) == "regular" ? "" : " " + st);
        }
    return "";
}

std::string expand(const std::string& folder, const std::string& abs, std::string* why) {
    if (is_trash(folder)) return folder;   // s030i: no folder -- the Trash
    std::string out = folder;
    if (has_slot(folder)) {
        std::string name = font_name(abs);
        const bool from_font = !name.empty();
        if (!from_font) name = clean_name(fs::path(abs).stem().string());
        if (name.empty()) { if (why) *why = "no name to put in " + folder; return ""; }
        const auto fill = [&](const std::string& slot, const std::string& with) {
            for (auto at = out.find(slot); at != std::string::npos; at = out.find(slot, at + with.size()))
                out.replace(at, slot.size(), with);
        };
        fill(kLetter, letter_of(name));
        fill(kFullName, name);
        if (has_slot(out)) { if (why) *why = "\"" + folder + "\" has a slot Tidy can't fill"; return ""; }
    }
    if (auto w = homing::folder_check(out); !w.empty()) { if (why) *why = w; return ""; }
    return out;
}

}  // namespace tidy::core::home
