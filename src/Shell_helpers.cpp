#include "Shell.hpp"
#include "core/Pages.hpp"

// Shell_helpers.cpp -- HELPERS. Shared orchestration for zones and handlers.

namespace tidy {

// The line under a page's promise. The spine says when the page comes alive;
// Activity also says where Tidy's records will live (resolved, not created).
std::string Shell::page_note(const std::string& id) const {  // helper
    const auto* p = core::pages::find(id);
    std::string s = p ? "Comes with " + p->milestone + "." : "";
    if (id == "activity")
        s += "\nTidy's records will live in " + m_data_dir + " -- nothing written yet.";
    return s;
}

}  // namespace tidy
