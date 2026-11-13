#include "core/Recognise.hpp"
#include "core/Suspects.hpp"
#include "App.hpp"
#include "core/Due.hpp"

#include <gio/gio.h>

#include <cstring>
#include <ctime>
#include <iostream>

namespace {
// s012b: a desktop notification through org.freedesktop.Notifications. The
// desktop-entry hint names Tidy's .desktop file: clicking it opens Tidy.
// No actions -- the scan has exited by the time anyone clicks.
bool notify(const tidy::core::due::Notice& n) {
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (!bus) {
        std::cerr << "tidy --scan: no session bus: " << (err ? err->message : "?") << "\n";
        if (err) g_error_free(err);
        return false;
    }
    GVariantBuilder actions, hints;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string("io.github.scott8420.Tidy"));
    GVariant* r = g_dbus_connection_call_sync(
        bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
        "Notify",
        g_variant_new("(susssasa{sv}i)", "Tidy", 0u, "io.github.scott8420.Tidy", n.summary.c_str(), n.body.c_str(),
                      &actions, &hints, -1),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &err);
    const bool ok = r != nullptr;
    if (r) g_variant_unref(r);
    if (err) {
        std::cerr << "tidy --scan: not notified: " << err->message << "\n";
        g_error_free(err);
    }
    g_object_unref(bus);
    return ok;
}
}  // namespace

// Thin bootstrap (CANON). Everything real is in App / Shell / the core.
// s012a: `tidy --scan` -- the headless look (no window, no GTK): what needs
// attention, saved to <data>/attention.ini for the tile. READ-ONLY on the home.
// s012b: `tidy --scan --notify` -- what the timer runs: at N, a notification.
// s025: `tidy --suspects [folder]` -- the suspect scan, a report. READ-ONLY.
// s026: `tidy --recognise [--deep] [folder]` -- the recognition report. READ-ONLY.
int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--suspects") == 0) return tidy::core::suspects::main_cli(argc, argv);
    if (argc >= 2 && (std::strcmp(argv[1], "--recognise") == 0 || std::strcmp(argv[1], "--recognize") == 0))
        return tidy::core::recognise::main_cli(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "--scan") == 0) {
        const bool want = argc == 3 && std::strcmp(argv[2], "--notify") == 0;
        if (argc > 3 || (argc == 3 && !want)) { std::cerr << "usage: tidy --scan [--notify]\n"; return 2; }
        tidy::core::due::Notice n;
        const int rc = tidy::core::due::scan_main(static_cast<std::int64_t>(std::time(nullptr)), want, &n);
        if (want && n.send) {
            std::cout << "notify: " << n.summary << " -- " << n.body << "\n";
            notify(n);
        }
        return rc;
    }
    return tidy::App::create()->run(argc, argv);
}
