#include "core/Rescan.hpp"

#include <set>

namespace tidy::core::rescan {

Result record(const std::vector<projects::Project>& found, const std::vector<templates::Template>& ts,
              catalog::Catalog& cat, std::int64_t now) {
    Result r;
    r.found = found.size();
    std::set<std::string> seen;
    for (const auto& p : found) {
        if (p.id.empty()) continue;
        ++r.with_id;
        if (!seen.insert(p.id).second) { r.twins.push_back(p.path); continue; }
        const auto was = cat.project(p.id);
        if (!was) ++r.added;
        else {
            if (was->gone) ++r.back;
            else if (was->path != p.path) ++r.moved;
        }
        const auto* t = projects::template_for(p, ts);
        cat.seen_project({p.id, p.path, p.name, t ? t->name : "", "", "", now, false});
    }
    for (const auto& row : cat.projects())
        if (!row.gone && !seen.count(row.id) && cat.mark_project_gone(row.id, now)) ++r.gone;
    return r;
}

std::string summary(const Result& r) {
    std::string s;
    const auto add = [&](std::size_t n, const char* what) {
        if (!n) return;
        s += (s.empty() ? "" : ", ") + std::to_string(n) + " " + what;
    };
    add(r.added, "new");
    add(r.moved, "moved");
    add(r.back, "back");
    add(r.gone, "gone");
    add(r.twins.size(), r.twins.size() == 1 ? "copy (an id seen twice)" : "copies (ids seen twice)");
    return s.empty() ? "no change" : s;
}

}  // namespace tidy::core::rescan
