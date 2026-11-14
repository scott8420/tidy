#include "core/Library.hpp"
#include "core/Ebooks.hpp"
#include "core/Homing.hpp"
#include "core/Projects.hpp"
#include "core/Protection.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;
namespace dr = latch::door;

namespace tidy::core::library {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Any ebook in it (3 levels at most)?
bool holds_ebook(const fs::path& dir) {
    std::error_code ec;
    for (fs::recursive_directory_iterator r(dir, fs::directory_options::skip_permission_denied, ec), e; !ec && r != e; r.increment(ec)) {
        if (r.depth() > 2) { r.disable_recursion_pending(); continue; }
        if (r->is_regular_file(ec) && ebooks::is_ebook(r->path().filename().string())) return true;
    }
    return false;
}
}  // namespace

std::string genre_of(const std::string& folder) {
    std::string k;
    for (char c : lower(folder)) if (c != '-' && c != ' ' && c != '_') k += c;
    if (k == "nonfiction") return "Non-Fiction";
    if (k == "fiction") return "Fiction";
    return ebooks::folder_name(folder);
}

std::vector<Item> find(const std::string& home, const recognise::Report& r) {
    std::vector<Item> out;
    const std::string lib = recognise::kLibrary;
    for (const auto& p : r.projects) {
        if (p.how != recognise::Project::How::Book || !p.maybe.empty() || !p.tpl.empty() || !p.shelf.empty()) continue;
        if (p.rel.rfind(lib + "/", 0) != 0 || protection::under(p.rel)) continue;
        const std::string rest = p.rel.substr(lib.size() + 1);   // <Genre>/<Author>/<Title>  or  <Genre>/<X>
        const auto parts = std::count(rest.begin(), rest.end(), '/');
        const fs::path abs = fs::path(home) / p.rel;
        if (!holds_ebook(abs)) continue;
        const std::string genre = genre_of(rest.substr(0, rest.find('/')));
        if (parts >= 2) {   // a title folder under its author: whole
            const fs::path rp(rest);
            const std::string author = ebooks::last_first(rp.parent_path().filename().string());
            const std::string title = ebooks::folder_name(rp.filename().string());
            if (author.empty() || title.empty()) continue;
            out.push_back({abs.string(), std::string(ebooks::kRoot) + "/" + genre + "/" + ebooks::folder_name(author) + "/" + title, true});
        } else {            // ebook files right in <Genre>/<X>: each by its own title + author
            for (const auto& row : ebooks::find(home, abs.string(), genre)) {
                if (!ebooks::row_problem(row).empty()) continue;
                out.push_back({row.path, ebooks::target_of(row), false});
            }
        }
    }
    return out;
}

Planned shelve(const std::string& home, const std::vector<Item>& items) {
    Planned out;
    std::vector<homing::Pick> picks;
    std::vector<dr::Action> wholes;
    std::set<std::string> gone, claimed;
    for (const auto& it : items) {
        gone.insert(it.from);
        if (!it.whole) {
            picks.push_back({it.from, it.to, "s030m: an ebook on its shelf"});
            out.lines.push_back(fs::path(it.from).filename().string() + "  ->  ~/" + it.to);
            continue;
        }
        fs::path dest = fs::path(home) / it.to;
        std::error_code ec;
        if (fs::exists(dest, ec) || claimed.count(dest.string())) {
            const std::string base = dest.filename().string();
            for (int n = 2; n < 100; ++n) {
                const fs::path d2 = dest.parent_path() / homing::numbered(base, n);
                if (!fs::exists(d2, ec) && !claimed.count(d2.string())) { dest = d2; break; }
            }
            out.renamed.push_back(base + " -> " + dest.filename().string());
        }
        claimed.insert(dest.string());
        dr::Action a;
        a.verb = dr::Verb::Move;
        a.from = it.from;
        a.to = dest.string();
        a.reason = "a book on its shelf";
        wholes.push_back(a);
        out.lines.push_back(fs::path(it.from).filename().string() + "/  ->  ~/" + dest.string().substr(home.size() + 1));
    }
    auto hp = homing::plan(home, picks);
    out.plan = std::move(hp.plan);
    out.renamed.insert(out.renamed.end(), hp.renamed.begin(), hp.renamed.end());
    // Parents of the whole moves, shallow first, after homing's own MakeDirs.
    std::set<std::string> have;
    for (const auto& a : out.plan.actions) if (a.verb == dr::Verb::MakeDir) have.insert(a.to);
    std::vector<std::string> dirs;
    for (const auto& w : wholes) {
        std::error_code ec;
        for (fs::path d = fs::path(w.to).parent_path(); d.string().size() > home.size() && !fs::exists(d, ec); d = d.parent_path())
            if (!have.count(d.string())) { dirs.push_back(d.string()); have.insert(d.string()); }
    }
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
    for (const auto& d : dirs) {
        dr::Action a;
        a.verb = dr::Verb::MakeDir;
        a.to = d;
        a.reason = "a shelf";
        out.plan.actions.push_back(a);
    }
    out.plan.actions.insert(out.plan.actions.end(), wholes.begin(), wholes.end());
    // Folders the moves leave empty -> the Trash (deepest first; never the library itself).
    const std::string lib = (fs::path(home) / recognise::kLibrary).string();
    std::set<std::string> cand;
    for (const auto& g : gone)
        for (fs::path d = fs::path(g).parent_path(); d.string().size() > lib.size() && d.string().rfind(lib + "/", 0) == 0; d = d.parent_path())
            cand.insert(d.string());
    std::vector<std::string> order(cand.begin(), cand.end());
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
    std::set<std::string> trashed;
    for (const auto& d : order) {
        bool empty = true;
        std::error_code ec;
        for (fs::directory_iterator it(d, ec), end; !ec && it != end && empty; it.increment(ec)) {
            const std::string p = it->path().string(), n = it->path().filename().string();
            if (gone.count(p) || trashed.count(p) || n == ".tidy-project" || n == ".tidy-files" || n == ".directory") continue;
            empty = false;
        }
        if (empty) trashed.insert(d);
    }
    for (const auto& d : trashed) {
        bool inner = false;
        for (const auto& t : trashed) if (t != d && d.rfind(t + "/", 0) == 0) inner = true;
        if (inner) continue;
        dr::Action a;
        a.verb = dr::Verb::Trash;
        a.from = d;
        a.reason = "left empty once its books went to the shelf";
        out.plan.actions.push_back(a);
        out.trashed.push_back(d.substr(home.size() + 1));
    }
    out.plan.label = "Library: " + std::to_string(items.size()) + (items.size() == 1 ? " book" : " books");
    return out;
}

}  // namespace tidy::core::library
