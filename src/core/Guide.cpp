#include "core/Guide.hpp"
#include "core/Own.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace tidy::core::guide {

namespace {

std::string plural(std::size_t n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}
bool under(const std::string& rel, const std::string& top) {
    return rel == top || rel.rfind(top + "/", 0) == 0;
}
std::string name_of(const std::string& rel) { return fs::path(rel).filename().string(); }
std::string more(std::size_t n, const std::string& one, const std::string& many) {
    return n ? plural(n, one, many) + " after this." : std::string();
}

}  // namespace

const char* state_text(State s) {
    switch (s) {
        case State::Done:     return "done";
        case State::Now:      return "do this now";
        case State::Todo:     return "to do";
        case State::NotReady: return "not ready yet";
        case State::Skipped:  return "skipped";
    }
    return "";
}

std::vector<Step> steps(const recognise::Report& r, const Inputs& in) {
    std::vector<Step> v(6);

    // 1 Basics -- what Tidy may touch and where loose files go.
    {
        auto& s = v[0];
        s.id = Id::Basics;
        s.title = "Basics";
        s.icon = "tidy-settings-symbolic";
        s.todo = in.held + (in.home_saved ? 0 : 1);
        if (in.held) {
            s.now = "Answer the " + plural(in.held, "folder", "folders") +
                    " Tidy is holding in Quarantine -- it won't touch them until you say what they are.";
            s.act = Act::Quarantine;
            s.details = "quarantine";
            if (!in.home_saved) s.after = "Then look over your Home design.";
        } else {
            s.now = "Look over your Home design -- where loose files go -- and Save it once. "
                    "Also check that Settings \u2192 Hands off lists every folder Tidy must never touch.";
            s.act = Act::HomeDesign;
            s.details = "templates";
        }
    }

    // 2 Bundles -- an app's file kept whole, sent to its kind's home.
    {
        auto& s = v[1];
        s.id = Id::Bundles;
        s.title = "Bundles";
        s.icon = "package-x-generic-symbolic";
        s.details = "sees";
        for (const auto& b : r.bundles) {
            if (b.held || !b.project.empty() || b.home.empty() || b.home == "=") continue;
            s.bundles.push_back(b.rel);
            s.homes.push_back(b.home);
        }
        s.todo = s.bundles.size();
        if (s.todo) {
            s.now = "Send " + plural(s.todo, "app file", "app files") + " to " + (s.todo == 1 ? "its home" : "their homes") +
                    ": " + name_of(s.bundles.front()) + " -> ~/" + s.homes.front() +
                    (s.todo > 1 ? " and " + plural(s.todo - 1, "more", "more") : std::string()) + ".";
            s.act = Act::SendHome;
        }
    }

    // 3 Projects -- each tree on its shelf; an old one made a project of its type.
    {
        auto& s = v[2];
        s.id = Id::Projects;
        s.title = "Projects";
        s.icon = "folder-symbolic";
        s.details = "projects";
        std::vector<const recognise::Project*> off, old;
        for (const auto& p : r.projects) {
            if (!p.shelf.empty() && !p.tpl.empty()) off.push_back(&p);
            else if (!p.maybe.empty()) old.push_back(&p);
        }
        s.todo = off.size() + old.size();
        for (const auto* p : off) s.items.emplace_back(p->rel, p->tpl);   // s030c: one after another
        for (const auto* p : old) s.items.emplace_back(p->rel, p->maybe);
        if (!off.empty()) {
            const auto* p = off.front();
            s.now = "Put " + name_of(p->rel) + " on its shelf: organize it as a " + p->tpl + ".";
            s.folder = p->rel;
            s.type = p->tpl;
            s.act = Act::OrganizeAs;
        } else if (!old.empty()) {
            const auto* p = old.front();
            s.now = "Make " + name_of(p->rel) + " a " + p->maybe + " project -- Tidy lays it out and puts it on its shelf." +
                    (old.size() > 1 ? " The next one follows by itself." : "");
            s.folder = p->rel;
            s.type = p->maybe;
            s.act = Act::OrganizeAs;
        }
        if (s.todo) s.after = more(s.todo - 1, "more", "more");
    }

    // 4 Sets -- series, comics, the EBooks library: not ready yet (s030b / s030c).
    {
        auto& s = v[3];
        s.id = Id::Sets;
        s.title = "Sets";
        s.icon = "view-grid-symbolic";
        s.details = "sees";
        s.todo = in.comics + in.movies + in.episodes + in.books;   // s030e comics; s030j movies + series; s030m books
        if (s.todo) {
            std::vector<std::string> parts;
            if (in.comics) parts.push_back(plural(in.comics, "comic", "comics") + " (" + plural(in.series, "series", "series") + ")");
            if (in.movies) parts.push_back(plural(in.movies, "movie", "movies"));
            if (in.episodes) parts.push_back(plural(in.episodes, "season or episode", "seasons and episodes"));
            if (in.books) parts.push_back(plural(in.books, "book", "books") + " from Documents/EBooks");
            std::string list;
            for (std::size_t i = 0; i < parts.size(); ++i) list += (i ? (i + 1 == parts.size() ? " and " : ", ") : "") + parts[i];
            std::vector<std::string> where;
            if (in.comics) where.push_back("Comics/<Series>");
            if (in.movies) where.push_back("Movies/<Title (Year)>");
            if (in.episodes) where.push_back("Series/<Show>/Season NN");
            if (in.books) where.push_back("EBooks/<Genre>/<Last, First>/<Title>");
            std::string shelves;
            for (std::size_t i = 0; i < where.size(); ++i) shelves += (i ? ", " : "") + where[i];
            s.now = "Put " + list + " on " + (parts.size() == 1 ? "its shelf" : "their shelves") + " in ~/Entertainment -- " + shelves + ".";
            s.act = Act::Reshelve;
            s.details = "sees";
        } else {
            s.now = "Comics, movies, series and books are on their shelves.";
        }
    }

    // 5 Loose files -- by kind, one folder at a time (Sort, for that folder).
    {
        auto& s = v[4];
        s.id = Id::Loose;
        s.title = "Loose files";
        s.icon = "text-x-generic-symbolic";
        s.details = "sort";
        std::map<std::string, std::size_t> by_folder;
        for (const auto& k : r.kinds) {
            if (k.kind.empty() || k.kind[0] == '(' || under(k.kind, "Entertainment")) continue;
            for (const auto& [f, n] : k.elsewhere) {
                // the Sets step's places, and Tidy's own Closet, are not this step's
                if (under(f, "Entertainment") || under(f, recognise::kLibrary) || under(f, "Closet")) continue;
                if (in.settled.count(f)) continue;   // s030f: Sort found nothing to move there
                by_folder[f] += n;
            }
        }
        std::string best;
        std::size_t best_n = 0;
        for (const auto& [f, n] : by_folder) {
            s.todo += n;
            if (n > best_n) { best = f; best_n = n; }
        }
        {
            std::vector<std::pair<std::size_t, std::string>> order;
            for (const auto& [f, n] : by_folder) order.emplace_back(n, f);
            std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (const auto& [n, f] : order) s.folders.push_back(f);
        }
        if (s.todo) {
            s.now = "Send the " + plural(best_n, "loose file", "loose files") + " in ~/" + (best.empty() ? "" : best) +
                    " home -- Sort shows where each goes first." +
                    (by_folder.size() > 1 ? " The next folder follows by itself." : "");
            s.folder = best;
            s.act = Act::SortFolder;
            for (const auto& p : r.projects) s.whole.push_back(p.rel);
            for (const auto& b : r.bundles) s.whole.push_back(b.rel);
            s.after = more(by_folder.size() - 1, "more folder", "more folders");
        }
    }

    // 6 Closet -- what nothing claims, until you decide.
    {
        auto& s = v[5];
        s.id = Id::Closet;
        s.title = "Closet";
        s.icon = "user-trash-symbolic";
        s.details = "closet";
        s.todo = in.closet >= 0 ? static_cast<std::size_t>(in.closet) : r.in_closet;
        if (s.todo) {
            s.now = "Decide on the " + plural(s.todo, "file", "files") + " nothing claims -- keep, move or let go.";
            s.act = Act::Closet;
        }
    }

    for (std::size_t i = 0; i < v.size(); ++i) {
        auto& s = v[i];
        if (in.skipped.count(static_cast<int>(i))) s.state = State::Skipped;
        else if (s.id == Id::Sets && !s.todo) s.state = State::NotReady;
        else s.state = s.todo ? State::Todo : State::Done;
    }
    for (auto& s : v)
        if (s.state == State::Todo) { s.state = State::Now; break; }
    return v;
}

int current(const std::vector<Step>& v) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].state == State::Now) return static_cast<int>(i);
    return -1;
}

std::set<std::string> settled(const std::string& data_dir) {
    std::set<std::string> out;
    std::istringstream in(own::read(data_dir, "settled.txt"));
    for (std::string line; std::getline(in, line);)
        if (!line.empty() && line[0] != '#') out.insert(line);
    return out;
}

std::string settle(const std::string& data_dir, const std::string& rel) {
    auto s = settled(data_dir);
    if (rel.empty() || !s.insert(rel).second) return "";
    std::string text = "# Tidy: folders Sort found nothing to move in -- Start's Loose files skips them\n";
    for (const auto& r : s) text += r + "\n";
    return own::write(data_dir, "settled.txt", text);
}

bool expert(const std::string& data_dir) {
    bool found = false;
    const std::string t = own::read(data_dir, "mode.txt", &found);
    return found && t.rfind("expert", 0) == 0;
}

std::string set_expert(const std::string& data_dir, bool on) {
    return own::write(data_dir, "mode.txt", on ? "expert\n" : "guided\n");
}

}  // namespace tidy::core::guide
