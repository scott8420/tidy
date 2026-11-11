#include "App.hpp"
#include "core/Due.hpp"

#include <cstring>
#include <ctime>

// Thin bootstrap (CANON). Everything real is in App / Shell / the core.
// s012a: `tidy --scan` -- the headless look (no window, no GTK): what needs
// attention, saved to <data>/attention.ini for the tile. READ-ONLY on the home.
int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--scan") == 0)
        return tidy::core::due::scan_main(static_cast<std::int64_t>(std::time(nullptr)));
    return tidy::App::create()->run(argc, argv);
}
