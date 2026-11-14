#include "core/Comics.hpp"
#include "core/Homing.hpp"
#include "core/Projects.hpp"
#include "core/Protection.hpp"

#include <latch/Package.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
namespace dr = latch::door;

namespace tidy::core::comics {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(std::string s) {
    const auto junk = [](char c) { return c == ' ' || c == '-' || c == ',' || c == '.' || c == '_' || c == '\t'; };
    while (!s.empty() && junk(s.back())) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && junk(s[i])) ++i;
    return s.substr(i);
}
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> w;
    std::istringstream in(s);
    for (std::string x; in >> x;) w.push_back(x);
    return w;
}
std::string join(const std::vector<std::string>& w, std::size_t from, std::size_t to) {
    std::string s;
    for (std::size_t i = from; i < to && i < w.size(); ++i) s += (s.empty() ? "" : " ") + w[i];
    return s;
}
bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}
// An issue / volume marker: 06, #12, 1942-08, v2, vol, vol., volume, no, issue,
// book, part, S045 / s01e02, a year.
bool issue_word(const std::string& raw) {
    const std::string w = lower(raw);
    if (all_digits(w)) return true;
    if (w.size() > 1 && w[0] == '#' && all_digits(w.substr(1))) return true;
    if (std::isdigit(static_cast<unsigned char>(w[0])) && w.find('-') != std::string::npos) return true;   // 1942-08
    static const std::set<std::string> marks = {"vol", "vol.", "volume", "no", "no.", "issue", "book", "part", "ch", "chapter"};
    if (marks.count(w)) return true;
    if (w.size() > 1 && (w[0] == 'v' || w[0] == 's') && std::isdigit(static_cast<unsigned char>(w[1]))) return true;   // v2, S045, s01e02
    return false;
}
bool site_word(const std::string& raw) {
    const std::string w = lower(raw);
    return w == "libgen.li" || w.rfind("www.", 0) == 0 || w == "libgen" ||
           (w.size() > 4 && (w.ends_with(".org") || w.ends_with(".com") || w.ends_with(".net")));
}
// Words equal, ignoring case and a trailing 's (Raymond's).
bool same_word(const std::string& a, const std::string& b) { return lower(a) == lower(b); }
// Does `hay` hold `needle` as a run of whole words?
bool holds(const std::vector<std::string>& hay, const std::vector<std::string>& needle) {
    if (needle.empty() || needle.size() > hay.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t k = 0; k < needle.size() && ok; ++k) ok = same_word(hay[i + k], needle[k]);
        if (ok) return true;
    }
    return false;
}

}  // namespace

bool is_comic(const std::string& file_name) {
    static const std::set<std::string> k = {".cbz", ".cbr", ".cb7", ".cbt"};
    return k.count(lower(fs::path(file_name).extension().string())) > 0;
}

std::string clean(const std::string& file_name) {
    std::string s = is_comic(file_name) ? fs::path(file_name).stem().string() : file_name;
    std::replace(s.begin(), s.end(), '_', ' ');
    if (s.find(' ') == std::string::npos) std::replace(s.begin(), s.end(), '.', ' ');   // Flash.Gordon.S045
    // Brackets out -- an unclosed one to the end ("(Kitchen").
    std::string out;
    int depth = 0;
    for (char c : s) {
        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
        if (c == ')' || c == ']' || c == '}') { if (depth) --depth; continue; }
        if (!depth) out += c;
    }
    std::vector<std::string> keep;
    for (const auto& w : words(out))
        if (!site_word(w)) keep.push_back(w);
    return trim(join(keep, 0, keep.size()));
}

std::string series_of(const std::string& file_name, const std::vector<std::string>& known, bool* cut) {
    if (cut) *cut = false;
    const std::string c = clean(file_name);
    auto w = words(c);
    // A series already on the shelf wins (longest first).
    std::vector<std::string> ks = known;
    std::stable_sort(ks.begin(), ks.end(), [](const auto& a, const auto& b) { return words(a).size() > words(b).size(); });
    for (const auto& k : ks)
        if (holds(w, words(k))) { if (cut) *cut = true; return k; }
    // " - " ends the series ("Donald Duck - The Lost Peg Leg").
    if (const auto d = c.find(" - "); d != std::string::npos && words(c.substr(0, d)).size() >= 2) {
        if (cut) *cut = true;
        w = words(trim(c.substr(0, d)));
    }
    // Cut at the issue (never the first word).
    for (std::size_t i = 1; i < w.size(); ++i)
        if (issue_word(w[i])) {
            if (cut) *cut = true;
            w.resize(i);
            break;
        }
    // "Alex Raymond's Flash Gordon" -> "Flash Gordon"; "The Complete ..." -> "Complete ...".
    for (std::size_t i = 0; i < w.size(); ++i)
        if (w[i].size() > 2 && lower(w[i]).ends_with("'s") && w.size() >= i + 3) { w.erase(w.begin(), w.begin() + i + 1); break; }
    if (w.size() > 1 && lower(w[0]) == "the") w.erase(w.begin());
    std::string s = trim(join(w, 0, w.size()));
    for (auto& ch : s) if (ch == '/') ch = '-';
    return s.empty() ? std::string("Unsorted") : s;
}

std::vector<std::string> series_all(const std::vector<std::string>& names, const std::vector<std::string>& known) {
    std::vector<std::string> out(names.size());
    std::vector<bool> was_cut(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        bool c = false;
        out[i] = series_of(names[i], known, &c);
        was_cut[i] = c;
    }
    // An uncut name that starts with another's series joins it ("Donald Duck Finds Pirate Gold" + "Donald Duck").
    std::set<std::string> cut_series;
    for (std::size_t i = 0; i < names.size(); ++i)
        if (was_cut[i] && words(out[i]).size() >= 2) cut_series.insert(out[i]);
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (was_cut[i]) continue;
        const auto w = words(out[i]);
        std::string best;
        for (const auto& s : cut_series) {
            const auto sw = words(s);
            if (sw.size() < w.size() && std::equal(sw.begin(), sw.end(), w.begin(), same_word) && sw.size() > words(best).size()) best = s;
        }
        if (!best.empty()) { out[i] = best; was_cut[i] = true; }
    }
    // Uncut siblings sharing 2+ leading words are one series (the longest such prefix).
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (was_cut[i]) continue;
        const auto wi = words(out[i]);
        std::size_t best = 0;
        for (std::size_t j = 0; j < names.size(); ++j) {
            if (j == i) continue;
            const auto wj = words(was_cut[j] ? out[j] : clean(names[j]));
            std::size_t k = 0;
            while (k < wi.size() && k < wj.size() && same_word(wi[k], wj[k])) ++k;
            if (k >= 2 && k < wi.size() && k > best) best = k;
        }
        if (best) {
            static const std::set<std::string> small = {"the", "a", "an", "of", "and", "in", "on", "to"};
            while (best > 2 && small.count(lower(wi[best - 1]))) --best;   // "Uncle Scrooge The ..." -> "Uncle Scrooge"
            if (!small.count(lower(wi[best - 1]))) out[i] = join(wi, 0, best);
        }
    }
    return out;
}

std::vector<std::string> shelf(const std::string& home) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(home) / kShelf, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.empty() || n[0] == '.' || !it->is_directory(ec)) continue;
        out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Item> find(const std::string& home) {
    std::vector<std::string> paths;
    const auto rel_of = [&](const fs::path& p) { return p.string().substr(home.size() + 1); };
    std::function<void(const fs::path&, int)> walk = [&](const fs::path& dir, int depth) {
        if (depth > 8) return;
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto& p = it->path();
            const std::string n = p.filename().string();
            if (n.empty() || n[0] == '.' || it->is_symlink(ec)) continue;
            const std::string rel = rel_of(p);
            if (it->is_directory(ec)) {
                if (rel == kShelf || rel == "Documents/Bookbinding" || projects::is_project_name(n) ||
                    latch::pkg::is_package_name(n) || protection::under(rel))
                    continue;
                walk(p, depth + 1);
            } else if (is_comic(n) && !protection::under(rel)) {
                paths.push_back(p.string());
            }
        }
    };
    for (const char* top : {"Entertainment", "Documents/EBooks", "Desktop"}) {
        std::error_code ec;
        const fs::path t = fs::path(home) / top;
        if (fs::is_directory(t, ec) && !protection::under(top)) walk(t, 1);
    }
    std::vector<std::string> names;
    for (const auto& p : paths) names.push_back(fs::path(p).filename().string());
    const auto ss = series_all(names, shelf(home));
    std::vector<Item> out;
    for (std::size_t i = 0; i < paths.size(); ++i) out.push_back({paths[i], ss[i]});
    return out;
}

Planned reshelve(const std::string& home, const std::vector<Item>& items) {
    Planned out;
    std::vector<homing::Pick> picks;
    std::set<std::string> gone;
    for (const auto& it : items) {
        picks.push_back({it.path, std::string(kShelf) + "/" + it.series, "a comic joins its series: " + it.series});
        gone.insert(it.path);
    }
    auto hp = homing::plan(home, picks);
    out.plan = std::move(hp.plan);
    out.renamed = std::move(hp.renamed);
    out.plan.label = "Re-shelve: " + std::to_string(items.size()) + (items.size() == 1 ? " comic" : " comics");
    // Folders left empty -> the Trash, deepest first. Never a top, the library, a genre.
    static const std::set<std::string> keep = {"Entertainment", "Entertainment/EBooks", "Documents", "Documents/EBooks",
                                               "Desktop", kShelf};
    std::set<std::string> dirs;
    for (const auto& p : gone)
        for (fs::path d = fs::path(p).parent_path(); d.string().size() > home.size() + 1; d = d.parent_path()) {
            const std::string rel = d.string().substr(home.size() + 1);
            const auto slashes = std::count(rel.begin(), rel.end(), '/');
            const bool library = rel.rfind("Entertainment/EBooks/", 0) == 0 || rel.rfind("Documents/EBooks/", 0) == 0;
            if (keep.count(rel) || slashes < 1 || (library && slashes < 3)) break;   // a genre (EBooks/Fiction) stays
            dirs.insert(d.string());
        }
    std::vector<std::string> order(dirs.begin(), dirs.end());
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    std::set<std::string> trashed;
    for (const auto& d : order) {
        bool empty = true;
        std::error_code ec;
        for (fs::directory_iterator it(d, ec), end; !ec && it != end && empty; it.increment(ec)) {
            const std::string p = it->path().string();
            const std::string n = it->path().filename().string();
            if (gone.count(p) || trashed.count(p)) continue;
            if (n == ".tidy-project" || n == ".tidy-files" || n == ".directory") continue;   // Tidy's / the desktop's own marks
            empty = false;
        }
        if (!empty) continue;
        trashed.insert(d);
        dr::Action a;
        a.verb = dr::Verb::Trash;
        a.from = d;
        a.reason = "left empty once its comics joined their series";
        out.plan.actions.push_back(a);
        out.trashed.push_back(d.substr(home.size() + 1));
    }
    // Only the outermost trashed folder needs its own step: an inner one goes with it.
    std::vector<dr::Action> kept;
    for (auto& a : out.plan.actions) {
        if (a.verb == dr::Verb::Trash) {
            bool inner = false;
            for (const auto& t : trashed)
                if (t != a.from && a.from.rfind(t + "/", 0) == 0) inner = true;
            if (inner) continue;
        }
        kept.push_back(std::move(a));
    }
    out.plan.actions = std::move(kept);
    return out;
}

}  // namespace tidy::core::comics
