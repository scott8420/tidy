#include "core/Pages.hpp"

namespace tidy::core::pages {

const std::vector<Page>& all() {
    // Promises from REQUIREMENTS.md; milestones from PROGRESS.md's road.
    static const std::vector<Page> p = {
        {"closet", "The Closet",
         "The undecided wait here until someone decides. Decided files never visit.", "s006"},
        {"due", "Due",
         "What is due to archive or to go. Tidy proposes; it never acts on its own.", "s011"},
        {"projects", "Projects",
         "Each <name>-project is found, placed by category, and moved whole.", "s008"},
        {"templates", "Templates",
         "The upper levels of a project's tree, built here; drift shown against them.", "s007"},
        {"purposes", "Purposes",
         "One decision per purpose, not per file: open it, close it, and its inputs follow.", "s009"},
        {"home", "Files home",
         "Each file to its folder by type, or into the project it serves.", "s011"},
        {"archive", "Archive",
         "Archive and catalog first; back up to media; nothing goes without proof.", "s012"},
        {"activity", "Activity",
         "Every move Tidy made, through one door, and the way back.", "s004"},
    };
    return p;
}

const Page* find(const std::string& id) {
    for (const auto& p : all())
        if (p.id == id) return &p;
    return nullptr;
}

}  // namespace tidy::core::pages
