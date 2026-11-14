#include "core/Ebooks.hpp"
#include "core/Projects.hpp"
#include "core/Quarantine.hpp"
#include "core/Recognise.hpp"

#include <latch/Protect.hpp>

#include <gio/gio.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace tidy::core::ebooks {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
std::string squeeze(const std::string& s) {   // runs of spaces -> one
    std::string o;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) { if (!o.empty() && o.back() != ' ') o += ' '; }
        else o += c;
    }
    return trim(o);
}

// ── READ-ONLY file access: open(O_RDONLY) + pread ─────────────────────────────
struct File {
    int fd = -1;
    std::uint64_t size = 0;
    explicit File(const std::string& p) {
        fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            const off_t e = ::lseek(fd, 0, SEEK_END);
            size = e > 0 ? static_cast<std::uint64_t>(e) : 0;
        }
    }
    ~File() { if (fd >= 0) ::close(fd); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    std::string at(std::uint64_t off, std::size_t n) const {
        std::string out;
        if (fd < 0 || off >= size) return out;
        n = static_cast<std::size_t>(std::min<std::uint64_t>(n, size - off));
        out.resize(n);
        std::size_t got = 0;
        while (got < n) {
            const ssize_t r = ::pread(fd, out.data() + got, n - got, static_cast<off_t>(off + got));
            if (r <= 0) break;
            got += static_cast<std::size_t>(r);
        }
        out.resize(got);
        return out;
    }
};
std::uint32_t u16(const std::string& s, std::size_t i) {
    return i + 2 <= s.size() ? static_cast<std::uint32_t>(static_cast<unsigned char>(s[i]) |
                                                         (static_cast<unsigned char>(s[i + 1]) << 8)) : 0;
}
std::uint32_t u32(const std::string& s, std::size_t i) {
    return i + 4 <= s.size() ? u16(s, i) | (u16(s, i + 2) << 16) : 0;
}

// Raw deflate (zip method 8) through GIO's zlib -- no new dependency.
std::string inflate(const std::string& in, std::size_t want) {
    std::string out;
    if (want > (8u << 20)) return out;   // an OPF is small; refuse anything silly
    out.resize(want);
    GZlibDecompressor* z = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW);
    gsize read = 0, written = 0;
    GError* err = nullptr;
    const GConverterResult r = g_converter_convert(G_CONVERTER(z), in.data(), in.size(), out.data(), out.size(),
                                                   G_CONVERTER_INPUT_AT_END, &read, &written, &err);
    if (err) g_error_free(err);
    g_object_unref(z);
    if (r == G_CONVERTER_ERROR) return {};
    out.resize(written);
    return out;
}

// One member of a zip, by name (case-insensitive); "" if absent / unreadable.
std::string zip_member(const File& f, const std::string& want) {
    if (f.size < 22) return {};
    const std::uint64_t tail = std::min<std::uint64_t>(f.size, 65557);
    const std::string end = f.at(f.size - tail, static_cast<std::size_t>(tail));
    const auto eocd = end.rfind(std::string("PK\x05\x06", 4));
    if (eocd == std::string::npos) return {};
    const std::uint32_t cd_size = u32(end, eocd + 12), cd_off = u32(end, eocd + 16);
    if (cd_size > (16u << 20)) return {};
    const std::string cd = f.at(cd_off, cd_size);
    const std::string lw = lower(want);
    for (std::size_t i = 0; i + 46 <= cd.size() && u32(cd, i) == 0x02014b50;) {
        const std::uint32_t method = u16(cd, i + 10), csize = u32(cd, i + 20), usize = u32(cd, i + 24);
        const std::uint32_t nlen = u16(cd, i + 28), xlen = u16(cd, i + 30), clen = u16(cd, i + 32);
        const std::uint32_t loff = u32(cd, i + 42);
        const std::string name = cd.substr(i + 46, nlen);
        i += 46 + nlen + xlen + clen;
        if (lower(name) != lw) continue;
        const std::string lh = f.at(loff, 30);
        if (u32(lh, 0) != 0x04034b50) return {};
        const std::uint64_t data = loff + 30 + u16(lh, 26) + u16(lh, 28);
        if (csize > (8u << 20)) return {};
        const std::string raw = f.at(data, csize);
        if (method == 0) return raw;
        if (method == 8) return inflate(raw, usize);
        return {};
    }
    return {};
}

std::string unescape(std::string s) {   // XML entities, the common ones
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { o += s[i]; continue; }
        const auto semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { o += s[i]; continue; }
        const std::string e = s.substr(i + 1, semi - i - 1);
        if (e == "amp") o += '&';
        else if (e == "lt") o += '<';
        else if (e == "gt") o += '>';
        else if (e == "quot") o += '"';
        else if (e == "apos") o += '\'';
        else if (!e.empty() && e[0] == '#') {
            unsigned long cp = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16)
                                                                           : std::strtoul(e.c_str() + 1, nullptr, 10);
            if (cp < 0x80) o += static_cast<char>(cp);
            else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) {
                o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                o += static_cast<char>(0x80 | (cp & 0x3F));
            }
        } else { o += '&' + e + ';'; }
        i = semi;
    }
    return o;
}
// The first <tag ...>text</tag> (tag matched with or without a prefix: dc:title).
// `attrs` gets the opening tag's attributes.
std::string element(const std::string& xml, const std::string& tag, std::string* attrs = nullptr) {
    const std::string lx = lower(xml);
    for (std::size_t p = 0; (p = lx.find(tag, p)) != std::string::npos; ++p) {
        if (p == 0 || (lx[p - 1] != '<' && lx[p - 1] != ':')) continue;
        std::size_t lt = p - 1;
        while (lt > 0 && lx[lt] != '<') --lt;
        if (lx[lt] != '<' || lx[lt + 1] == '/') continue;
        const char after = p + tag.size() < lx.size() ? lx[p + tag.size()] : '\0';
        if (after != '>' && after != ' ' && after != '\t' && after != '\n' && after != '\r') continue;
        const auto gt = lx.find('>', p);
        if (gt == std::string::npos) return {};
        if (attrs) *attrs = xml.substr(p + tag.size(), gt - p - tag.size());
        const auto close = lx.find("</", gt);
        if (close == std::string::npos) return {};
        return squeeze(unescape(xml.substr(gt + 1, close - gt - 1)));
    }
    return {};
}
std::string attr(const std::string& attrs, const std::string& name) {
    const std::string la = lower(attrs);
    for (std::size_t p = 0; (p = la.find(name, p)) != std::string::npos; ++p) {
        if (p > 0 && la[p - 1] != ' ' && la[p - 1] != ':' && la[p - 1] != '\n' && la[p - 1] != '\t') continue;
        auto q = la.find_first_not_of(" \t\r\n", p + name.size());
        if (q == std::string::npos || la[q] != '=') continue;
        q = la.find_first_not_of(" \t\r\n", q + 1);
        if (q == std::string::npos || (la[q] != '"' && la[q] != '\'')) continue;
        const auto e = la.find(la[q], q + 1);
        if (e == std::string::npos) return {};
        return squeeze(unescape(attrs.substr(q + 1, e - q - 1)));
    }
    return {};
}

Meta read_epub(const std::string& path) {
    Meta m;
    File f(path);
    const std::string container = zip_member(f, "META-INF/container.xml");
    std::string opf_at;
    {
        const std::string lc = lower(container);
        std::size_t r = 0;   // <rootfile ...>, not <rootfiles>
        while ((r = lc.find("<rootfile", r)) != std::string::npos && r + 9 < lc.size() &&
               !std::isspace(static_cast<unsigned char>(lc[r + 9])))
            r += 9;
        if (r != std::string::npos) {
            const auto gt = lc.find('>', r);
            opf_at = attr(container.substr(r + 9, gt == std::string::npos ? std::string::npos : gt - r - 9), "full-path");
        }
    }
    if (opf_at.empty()) return m;
    const std::string opf = zip_member(f, opf_at);
    if (opf.empty()) return m;
    m.title = element(opf, "title");
    std::string a;
    m.author = element(opf, "creator", &a);
    if (const auto fa = attr(a, "file-as"); !fa.empty() && fa.find(',') != std::string::npos) m.author = fa;
    if (!m.title.empty() || !m.author.empty()) m.from = "epub";
    return m;
}

// A PDF string after "/Key": (literal) or <hex>, UTF-16BE with a BOM or bytes.
std::string pdf_value(const std::string& s, std::size_t p) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\r' || s[p] == '\n')) ++p;
    if (p >= s.size()) return {};
    std::string raw;
    if (s[p] == '(') {
        int depth = 1;
        for (++p; p < s.size() && depth > 0; ++p) {
            const char c = s[p];
            if (c == '\\' && p + 1 < s.size()) {
                const char n = s[++p];
                if (n == 'n') raw += '\n'; else if (n == 'r') raw += '\r'; else if (n == 't') raw += '\t';
                else if (n >= '0' && n <= '7') {
                    int v = n - '0';
                    for (int k = 0; k < 2 && p + 1 < s.size() && s[p + 1] >= '0' && s[p + 1] <= '7'; ++k) v = v * 8 + (s[++p] - '0');
                    raw += static_cast<char>(v);
                } else raw += n;
                continue;
            }
            if (c == '(') ++depth;
            if (c == ')' && --depth == 0) break;
            raw += c;
        }
    } else if (s[p] == '<' && p + 1 < s.size() && s[p + 1] != '<') {
        const auto e = s.find('>', p);
        if (e == std::string::npos) return {};
        std::string hex;
        for (std::size_t i = p + 1; i < e; ++i)
            if (std::isxdigit(static_cast<unsigned char>(s[i]))) hex += s[i];
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) raw += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
    } else {
        return {};
    }
    std::string out;
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE && static_cast<unsigned char>(raw[1]) == 0xFF) {
        for (std::size_t i = 2; i + 1 < raw.size(); i += 2) {
            const unsigned cp = (static_cast<unsigned char>(raw[i]) << 8) | static_cast<unsigned char>(raw[i + 1]);
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
            else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        }
    } else {
        for (unsigned char c : raw) {   // PDFDocEncoding ~ Latin-1 for these
            if (c < 0x80) out += static_cast<char>(c);
            else { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        }
    }
    return squeeze(out);
}

Meta read_pdf(const std::string& path) {
    Meta m;
    File f(path);
    if (f.size < 8 || f.at(0, 5) != "%PDF-") return m;
    // The Info dictionary is usually near the end (or the start); compressed
    // object streams hide it -- then the name is used.
    std::string hay = f.at(0, 65536);
    if (f.size > 65536) hay += f.at(f.size > 262144 ? f.size - 262144 : 65536, 262144);
    for (const char* key : {"/Title", "/Author"}) {
        for (std::size_t p = 0; (p = hay.find(key, p)) != std::string::npos; p += 1) {
            const std::string v = pdf_value(hay, p + std::strlen(key));
            if (v.empty()) continue;
            (std::strcmp(key, "/Title") == 0 ? m.title : m.author) = v;
            break;
        }
    }
    // Junk titles some makers write: "untitled", "Microsoft Word - x.doc".
    if (lower(m.title) == "untitled" || m.title.rfind("Microsoft Word - ", 0) == 0) m.title.clear();
    if (!m.title.empty() || !m.author.empty()) m.from = "pdf";
    return m;
}

}  // namespace

bool is_ebook(const std::string& file_name) {
    // s030e: comics (.cbz .cbr .cb7) are no ebooks -- they go to the Comics shelf by series.
    static const std::set<std::string> k = {".epub", ".mobi", ".azw", ".azw3", ".fb2", ".djvu", ".pdf"};
    return k.count(lower(fs::path(file_name).extension().string())) > 0;
}

Meta from_name(const std::string& file_name) {
    Meta m;
    std::string stem = fs::path(file_name).stem().string();
    for (auto& c : stem) if (c == '_') c = ' ';
    stem = squeeze(stem);
    if (stem.empty()) return m;
    m.from = "the name";
    const auto dash = stem.find(" - ");
    if (dash == std::string::npos) { m.title = stem; return m; }
    const std::string a = trim(stem.substr(0, dash)), b = trim(stem.substr(dash + 3));
    if (a.find(',') != std::string::npos && b.find(',') == std::string::npos) { m.author = a; m.title = b; }
    else { m.title = a; m.author = b; }   // Calibre's "Title - Author"
    return m;
}

Meta read(const std::string& path) {
    const std::string ext = lower(fs::path(path).extension().string());
    Meta m;
    if (ext == ".epub") m = read_epub(path);
    else if (ext == ".pdf") m = read_pdf(path);
    const Meta n = from_name(fs::path(path).filename().string());
    if (m.from.empty()) return n;
    if (m.title.empty()) m.title = n.title;
    if (m.author.empty()) m.author = n.author;
    return m;
}

std::string last_first(const std::string& raw) {
    std::string a = squeeze(raw);
    // "Carolyn Keene and Mildred Wirt" / "A; B" -> the first one
    for (const char* sep : {";", " & ", " and "}) {
        const auto p = a.find(sep);
        if (p != std::string::npos) a = trim(a.substr(0, p));
    }
    if (a.empty() || a.find(',') != std::string::npos) return a;
    std::vector<std::string> w;
    for (std::size_t p = 0; p < a.size();) {
        const auto q = a.find(' ', p);
        w.push_back(a.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    if (w.size() < 2) return a;
    std::string suffix;
    const std::string ls = lower(w.back());
    if (ls == "jr." || ls == "jr" || ls == "sr." || ls == "sr" || ls == "ii" || ls == "iii" || ls == "iv") {
        suffix = w.back();
        w.pop_back();
        if (w.size() < 2) return a;
    }
    std::size_t last = w.size() - 1;   // particles join the last name: Van Vogt, de la Mare
    static const std::set<std::string> particles = {"van", "von", "de", "der", "del", "della", "la", "le", "du", "da", "di", "st.", "ten", "ter"};
    while (last > 1 && particles.count(lower(w[last - 1]))) --last;
    std::string first, sur;
    for (std::size_t i = 0; i < w.size(); ++i) {
        std::string& to = i < last ? first : sur;
        if (!to.empty()) to += ' ';
        to += w[i];
    }
    return sur + ", " + first + (suffix.empty() ? "" : ", " + suffix);
}

std::string folder_name(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '/' || c == '\\') o += '-';
        else if (c == ':') o += " -";
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    o = squeeze(o);
    while (!o.empty() && (o[0] == '.' || o[0] == '-')) o.erase(0, 1);
    if (o.size() > 120) o = trim(o.substr(0, 120));
    return o;
}

std::string type_name(const std::string& genre) {
    std::string n = "ebook-";
    for (char c : lower(folder_name(genre)))
        if (std::isalnum(static_cast<unsigned char>(c))) n += c;
        else if (n.back() != '-') n += '-';
    while (n.back() == '-') n.pop_back();
    return n;
}

templates::Template type_for(const std::string& genre) {
    templates::Template t;
    t.name = type_name(genre);
    t.category = std::string(kRoot) + "/" + folder_name(genre);
    t.by_place = true;
    t.group = kGroup;
    for (const char* e : {"epub", "mobi", "azw", "azw3", "fb2", "djvu", "cbz", "cbr", "cb7", "pdf"})
        t.routes.push_back({std::string("*.") + e, templates::kTop});
    return t;
}

std::vector<Row> find(const std::string& home, const std::string& dir, const std::string& genre) {
    std::vector<Row> out;
    if (dir != home && dir.rfind(home + "/", 0) != 0) return out;
    const auto rel_of = [&](const std::string& abs) { return abs == home ? std::string() : abs.substr(home.size() + 1); };
    // The folder itself: never inside a protected place or a project.
    {
        const std::string r = rel_of(dir);
        std::string so_far;
        for (const auto& part : fs::path(r)) {
            so_far = so_far.empty() ? part.string() : so_far + "/" + part.string();
            if (latch::protect::here(so_far) || projects::is_whole_at(so_far) || quarantine::one(home + "/" + so_far))
                return out;
        }
    }
    const auto walk = [&](const fs::path& at, auto& self) -> void {
        std::error_code ec;
        std::vector<fs::path> kids;
        for (fs::directory_iterator it(at, ec), end; !ec && it != end; it.increment(ec)) kids.push_back(it->path());
        std::sort(kids.begin(), kids.end());
        for (const auto& p : kids) {
            const std::string fn = p.filename().string();
            if (fn.empty() || fn[0] == '.') continue;
            const auto st = fs::symlink_status(p, ec);
            if (ec || fs::is_symlink(st)) continue;
            const std::string r = rel_of(p.string());
            if (fs::is_directory(st)) {
                if (latch::protect::here(r) || projects::is_whole_at(r) || quarantine::one(p.string()) ||
                    projects::is_code(p) || r == "Archive")
                    continue;
                // Do no harm: the EBooks library's books (already in title folders -- Organize as...)
                // and anything that looks like a project (working folders: an old bookbinding's
                // Finals/*.epub) are never picked apart file by file.
                const std::string lib = recognise::kLibrary;
                if (r == lib || r.rfind(lib + "/", 0) == 0) continue;
                bool project_like = false;
                for (fs::directory_iterator k(p, ec), e2; !ec && k != e2 && !project_like; k.increment(ec))
                    project_like = k->is_directory(ec) && recognise::is_working_name(k->path().filename().string());
                if (project_like) continue;
                self(p, self);
                continue;
            }
            if (!fs::is_regular_file(st) || !is_ebook(fn)) continue;
            Row row;
            row.path = p.string();
            row.genre = genre;
            const Meta m = read(row.path);
            row.author = last_first(m.author);
            row.title = m.title;
            row.from = m.from;
            row.pick = lower(p.extension().string()) != ".pdf";
            out.push_back(std::move(row));
        }
    };
    walk(fs::path(dir), walk);
    return out;
}

std::string target_of(const Row& r) {
    return std::string(kRoot) + "/" + folder_name(r.genre) + "/" + folder_name(r.author) + "/" + folder_name(r.title);
}

std::string row_problem(const Row& r) {
    if (folder_name(r.genre).empty()) return "no genre";
    if (folder_name(r.author).empty()) return "no author -- type it (Last, First)";
    if (folder_name(r.title).empty()) return "no title";
    return "";
}

homing::Planned plan(const std::string& home, const std::vector<Row>& rows) {
    std::vector<homing::Pick> picks;
    for (const auto& r : rows) {
        if (!r.pick || !row_problem(r).empty()) continue;
        picks.push_back({r.path, target_of(r), "an ebook -> its title folder (" + type_name(r.genre) + ")"});
    }
    return homing::plan(home, picks);
}

}  // namespace tidy::core::ebooks
