#include "core/Paths.hpp"

namespace tidy::core::paths {

std::string data_dir(const std::string& home, const char* xdg_data_home) {
    std::string base;
    if (xdg_data_home && xdg_data_home[0] == '/') base = xdg_data_home;
    else base = home + "/.local/share";
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    return base + "/tidy";
}

std::string templates_dir(const std::string& data_dir) { return data_dir + "/templates"; }

}  // namespace tidy::core::paths
