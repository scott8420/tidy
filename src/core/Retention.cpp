#include "core/Retention.hpp"

namespace tidy::core::retention {

const std::vector<Info>& all() {
    static const std::vector<Info> v = {
        {Level::Undecided,  "undecided",  "Undecided",  "In the Closet, waiting"},
        {Level::Ephemeral,  "ephemeral",  "Ephemeral",  "Days, then decays"},
        {Level::UntilDone,  "until-done", "Until done", "Lives while its purpose is open"},
        {Level::Held,       "held",       "Held",       "Kept N years after done, then decays"},
        {Level::Historical, "historical", "Historical", "Archived, kept, out of the way"},
        {Level::Keep,       "keep",       "Keep",       "Active and in place -- the product"},
        {Level::HandsOff,   "hands-off",  "Hands off",  "Tidy never touches it"},
    };
    return v;
}

const Info& info(Level l) {
    for (const auto& i : all())
        if (i.level == l) return i;
    return all().front();
}
const std::string& id_of(Level l) { return info(l).id; }
const std::string& title_of(Level l) { return info(l).title; }

bool parse(const std::string& id, Level& out) {
    for (const auto& i : all())
        if (i.id == id) { out = i.level; return true; }
    return false;
}

}  // namespace tidy::core::retention
