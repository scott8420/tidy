#include "core/Tagging.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace tidy::core::tagging {

namespace {
door::Action tag(const std::string& path, const char* attr, const std::string& value, const std::string& why) {
    door::Action a;
    a.verb = door::Verb::Tag;
    a.from = path;
    a.attr = attr;
    a.content = value;
    a.reason = why;
    return a;
}
void set_tags(std::vector<door::Action>& out, const std::string& path, const facts::Tags& have,
              const facts::Tags& want, const std::string& why) {
    const std::pair<const char*, std::pair<const std::string*, const std::string*>> rows[] = {
        {facts::kRetention, {&have.retention, &want.retention}},
        {facts::kPurpose, {&have.purpose, &want.purpose}},
        {facts::kRole, {&have.role, &want.role}},
    };
    for (const auto& [attr, hv] : rows)
        if (!hv.second->empty() && *hv.first != *hv.second) out.push_back(tag(path, attr, *hv.second, why));
}
}  // namespace

door::Plan stamp(const std::vector<facts::Facts>& files, const facts::Tags& set, const std::string& label) {
    door::Plan p;
    p.label = label;
    for (const auto& f : files) {
        if (f.tags.id.empty()) p.actions.push_back(tag(f.path, facts::kId, facts::new_id(), "a stable id for the catalog"));
        set_tags(p.actions, f.path, f.tags, set, label);
    }
    return p;
}

Findings reconcile(const catalog::Catalog& cat, const std::vector<facts::Facts>& scanned) {
    Findings out;
    std::map<std::string, const facts::Facts*> at_path;
    std::map<std::string, std::vector<const facts::Facts*>> with_id;
    for (const auto& f : scanned) {
        at_path[f.path] = &f;
        if (!f.tags.id.empty()) with_id[f.tags.id].push_back(&f);
    }
    std::set<std::string> carried_paths;
    for (const auto& e : cat.all()) {
        if (e.gone) continue;
        const auto idit = with_id.find(e.id);
        if (idit != with_id.end()) continue;   // its id is still on a file somewhere (moved, or in place)
        const auto pit = at_path.find(e.path);
        if (pit != at_path.end() && pit->second->tags.id.empty() &&
            !(pit->second->dev == e.dev && pit->second->ino == e.ino)) {
            out.carries.push_back({e, *pit->second});   // new inode, no tags, same name: an atomic save
            carried_paths.insert(e.path);
            continue;
        }
        if (pit == at_path.end()) out.gone.push_back(e.id);   // a path Tidy didn't scan stays as it was
    }
    for (const auto& [id, fs] : with_id) {
        if (fs.size() < 2) continue;
        const auto known = cat.by_id(id);
        // The keeper: the inode the catalog knows; else the oldest born.
        const facts::Facts* keep = nullptr;
        for (const auto* f : fs)
            if (known && f->dev == known->dev && f->ino == known->ino) keep = f;
        if (!keep)
            keep = *std::min_element(fs.begin(), fs.end(), [](const auto* a, const auto* b) {
                return a->btime != b->btime ? a->btime < b->btime : a->path < b->path;
            });
        for (const auto* f : fs)
            if (f != keep) out.copies.push_back({*f, facts::new_id()});
    }
    return out;
}

door::Plan plan_for(const Findings& f, const std::string& label) {
    door::Plan p;
    p.label = label;
    for (const auto& c : f.carries) {
        const std::string why = "an app saved over it; its tags carried to the new file";
        p.actions.push_back(tag(c.now.path, facts::kId, c.was.id, why));
        set_tags(p.actions, c.now.path, c.now.tags, {"", c.was.retention, c.was.purpose, c.was.role}, why);
    }
    for (const auto& c : f.copies)
        p.actions.push_back(tag(c.file.path, facts::kId, c.new_id, "a copy carrying another file's id gets its own"));
    return p;
}

std::size_t record(catalog::Catalog& cat, const std::vector<std::string>& paths, std::int64_t now) {
    std::size_t n = 0;
    for (const auto& p : paths) {
        facts::Facts f;
        if (!facts::of(p, f)) continue;
        if (f.tags.id.empty()) continue;   // untagged: after_undo() decides what the catalog keeps
        // A copy carrying a known id (cp -a) must not take the original's
        // entry: the catalog's inode still holds that id -> reconcile's business.
        if (auto e = cat.by_id(f.tags.id); e && !(e->dev == f.dev && e->ino == f.ino) && e->ino != 0) {
            facts::Facts o;
            if (facts::of(e->path, o) && o.dev == e->dev && o.ino == e->ino && o.tags.id == f.tags.id) continue;
        }
        if (cat.put(catalog::from_facts(f, now))) ++n;
    }
    return n;
}

std::size_t after_undo(catalog::Catalog& cat, const door::Door& d, std::uint64_t batch, std::int64_t now) {
    std::size_t n = 0;
    std::vector<std::string> rest;
    const auto given_before = [&](const std::string& id) {
        for (const auto& b : d.batches()) {
            if (b.id >= batch) continue;
            for (auto rid : b.records)
                if (const auto* r = d.record(rid); r && r->verb == door::Verb::Tag && r->attr == facts::kId &&
                                                   !r->untag && r->after == id && r->state == door::State::Done)
                    return true;
        }
        return false;
    };
    for (const auto& b : d.batches()) {
        if (b.id != batch) continue;
        for (auto rid : b.records) {
            const auto* r = d.record(rid);
            if (!r || r->verb != door::Verb::Tag) continue;
            if (r->attr != facts::kId || r->untag || r->state != door::State::Undone) { rest.push_back(r->from); continue; }
            auto e = cat.by_id(r->after);
            if (!e) continue;
            if (given_before(r->after)) {
                e->dev = e->ino = 0;   // known id, inode unknown: reconcile carries it again
                e->seen = now;
                if (cat.put(*e)) ++n;
            } else if (cat.drop(e->id)) ++n;
        }
    }
    std::sort(rest.begin(), rest.end());
    rest.erase(std::unique(rest.begin(), rest.end()), rest.end());
    return n + record(cat, rest, now);
}

std::vector<std::string> paths_of(const door::Door& d, std::uint64_t batch) {
    std::vector<std::string> out;
    for (const auto& b : d.batches()) {
        if (b.id != batch) continue;
        for (auto id : b.records)
            if (const auto* r = d.record(id); r && r->verb == door::Verb::Tag &&
                                              std::find(out.begin(), out.end(), r->from) == out.end())
                out.push_back(r->from);
    }
    return out;
}

}  // namespace tidy::core::tagging
