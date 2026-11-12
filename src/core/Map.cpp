#include "core/Map.hpp"

#include "core/Archive.hpp"
#include "core/Birth.hpp"
#include "core/Facts.hpp"
#include "core/Projects.hpp"
#include "core/Protection.hpp"

#include <latch/Door.hpp>
#include <latch/Package.hpp>
#include <latch/Protect.hpp>

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <set>

namespace tidy::core::map {

namespace fs = std::filesystem;

const char* kind_title(Kind k) {
    switch (k) {
        case Kind::Cataloged: return "cataloged";
        case Kind::Loose:     return "loose";
        case Kind::Untracked: return "untracked";
        case Kind::Whole:     return "in projects / repos";
        case Kind::Archived:  return "archived";
    }
    return "";
}

std::size_t Counts::total() const { return std::accumulate(n.begin(), n.end(), std::size_t{0}); }
void Counts::add(const Counts& o) {
    for (std::size_t i = 0; i < kKinds; ++i) n[i] += o.n[i];
}

namespace {

enum class Mode { Normal, Whole, Archived };

struct Ctx {
    std::string home;
    std::set<std::string> tops;          // a file directly in one of these is loose
    std::vector<std::string> whole;      // a file anywhere under one of these is loose
    std::set<std::string> catch_all;     // absolute
};

bool under(const std::string& p, const std::string& dir) { return p.size() > dir.size() && p.rfind(dir + "/", 0) == 0; }

Kind classify(const Ctx& cx, const std::string& path, const std::string& parent, Mode mode) {
    if (mode == Mode::Archived) return Kind::Archived;
    if (mode == Mode::Whole) return Kind::Whole;
    std::string id;
    if (latch::door::tag_of(path, facts::kId, id) && !id.empty()) return Kind::Cataloged;
    if (cx.tops.count(parent)) return Kind::Loose;
    for (const auto& w : cx.whole)
        if (under(path, w)) return Kind::Loose;
    return Kind::Untracked;
}

void walk(const Ctx& cx, const std::string& dir, const std::string& rel, Mode mode, Node& node) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;   // hidden: never
        std::error_code e2;
        const auto st = it->symlink_status(e2);
        if (e2 || fs::is_symlink(st)) continue;         // a symlink: never followed
        const std::string path = it->path().string();
        const std::string crel = rel.empty() ? name : rel + "/" + name;
        if (fs::is_directory(st)) {
            if (latch::protect::here(crel) || protection::under(crel)) {
                Node k;
                k.name = name;
                k.rel = crel;
                k.shut = true;
                k.what = "not looked inside -- Hands off, Samson's or never-touch";
                node.kids.push_back(std::move(k));
                continue;
            }
            if (latch::pkg::is_package_name(name)) {    // a package is one item
                const auto k = classify(cx, path, dir, mode);
                node.here.add(k);
                node.c.add(k);
                continue;
            }
            Node k;
            k.name = name;
            k.rel = crel;
            Mode m = mode;
            if (mode == Mode::Normal && rel.empty() && name == archive::kFolder) { m = Mode::Archived; k.what = "~/Archive"; }
            else if (mode == Mode::Normal && projects::is_project_name(name)) { m = Mode::Whole; k.what = "a -project, kept whole"; }
            else if (mode == Mode::Normal && projects::is_whole_at(crel)) { m = Mode::Whole; k.what = "a project by its place, kept whole"; }
            else if (mode == Mode::Normal && fs::exists(fs::symlink_status(it->path() / ".git", e2))) {
                m = Mode::Whole;
                k.what = "a git repo, kept whole";
            } else if (cx.catch_all.count(path)) k.what = "a catch-all";
            walk(cx, path, crel, m, k);
            node.c.add(k.c);
            node.kids.push_back(std::move(k));
            continue;
        }
        if (!fs::is_regular_file(st)) continue;
        const auto k = classify(cx, path, dir, mode);
        node.here.add(k);
        node.c.add(k);
    }
    std::stable_sort(node.kids.begin(), node.kids.end(),
                     [](const Node& a, const Node& b) { return a.weight() > b.weight(); });
}

}  // namespace

Node build(const std::string& home, const std::string& user_dirs, const std::vector<std::string>& catch_all) {
    Ctx cx;
    cx.home = home;
    for (const auto& pl : birth::sweep(home, user_dirs, catch_all)) {
        if (pl.top_only) cx.tops.insert(pl.path);
        else cx.whole.push_back(pl.path);
    }
    for (const auto& c : catch_all) cx.catch_all.insert(home + "/" + c);
    Node root;
    root.name = "~";
    walk(cx, home, "", Mode::Normal, root);
    return root;
}

const Node* find(const Node& root, const std::string& rel) {
    if (rel.empty()) return &root;
    const Node* at = &root;
    std::string so_far;
    for (const auto& part : fs::path(rel)) {
        so_far += (so_far.empty() ? "" : "/") + part.string();
        const Node* next = nullptr;
        for (const auto& k : at->kids)
            if (k.rel == so_far) { next = &k; break; }
        if (!next) return nullptr;
        at = next;
    }
    return at;
}

std::string line(const Counts& c) {
    const auto t = c.total();
    std::string s = std::to_string(t) + (t == 1 ? " file" : " files");
    std::string parts;
    for (std::size_t i = 0; i < kKinds; ++i)
        if (c.n[i]) parts += (parts.empty() ? "" : ", ") + std::to_string(c.n[i]) + " " + kind_title(static_cast<Kind>(i));
    return parts.empty() ? s : s + ": " + parts;
}

namespace {
double worst(const std::vector<double>& row, double side) {
    if (row.empty()) return 1e300;
    const double s = std::accumulate(row.begin(), row.end(), 0.0);
    const auto [mn, mx] = std::minmax_element(row.begin(), row.end());
    const double s2 = s * s, w2 = side * side;
    return std::max(w2 * *mx / s2, s2 / (w2 * *mn));
}
}  // namespace

std::vector<Rect> squarify(const std::vector<double>& sizes, Rect area) {
    std::vector<Rect> out(sizes.size());
    double total = 0;
    for (double s : sizes) total += std::max(0.0, s);
    if (total <= 0 || area.w <= 0 || area.h <= 0) return out;
    std::vector<std::size_t> order(sizes.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return sizes[a] > sizes[b]; });
    const double scale = area.w * area.h / total;
    Rect free = area;
    std::size_t i = 0;
    while (i < order.size()) {
        if (sizes[order[i]] <= 0) { ++i; continue; }
        const double side = std::min(free.w, free.h);
        std::vector<double> row{sizes[order[i]] * scale};
        std::size_t j = i + 1;
        while (j < order.size() && sizes[order[j]] > 0) {
            auto more = row;
            more.push_back(sizes[order[j]] * scale);
            if (worst(more, side) > worst(row, side)) break;
            row = std::move(more);
            ++j;
        }
        const double s = std::accumulate(row.begin(), row.end(), 0.0);
        if (free.w >= free.h) {   // a column at the left
            const double w = free.h > 0 ? s / free.h : 0;
            double y = free.y;
            for (std::size_t k = 0; k < row.size(); ++k) {
                const double h = w > 0 ? row[k] / w : 0;
                out[order[i + k]] = {free.x, y, w, h};
                y += h;
            }
            free.x += w;
            free.w -= w;
        } else {                  // a row along the top
            const double h = free.w > 0 ? s / free.w : 0;
            double x = free.x;
            for (std::size_t k = 0; k < row.size(); ++k) {
                const double w = h > 0 ? row[k] / h : 0;
                out[order[i + k]] = {x, free.y, w, h};
                x += w;
            }
            free.y += h;
            free.h -= h;
        }
        i = j;
    }
    return out;
}

}  // namespace tidy::core::map
