#include "Look.hpp"
#include "Log.hpp"

#include <gtkmm/cssprovider.h>
#include <gtkmm/settings.h>
#include <gtkmm/stylecontext.h>
#include <gdkmm/display.h>

#include <cstdlib>
#include <string_view>

namespace tidy::look {

bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("TIDY_LOOK");
        if (!e) return kLookDefault;
        const std::string_view v(e);
        if (v == "0") return false;
        if (v == "1") return true;
        return kLookDefault;   // anything else: the app's own choice
    }();
    return on;
}

void install() {
    if (!enabled()) return;

    // Dark, always: the skin's colours are literals on a dark ground. Asking
    // GTK for its dark variant makes the parts the skin does not reach (menus,
    // popovers, the file chooser) agree with it.
    if (auto settings = Gtk::Settings::get_default())
        settings->property_gtk_application_prefer_dark_theme() = true;

    if (auto display = Gdk::Display::get_default()) {
        auto css = Gtk::CssProvider::create();
        // A parse error in look.css is a visual-channel bug: say where, keep going.
        css->signal_parsing_error().connect(
            [](const Glib::RefPtr<const Gtk::CssSection>& sec, const Glib::Error& err) {
                if (auto lg = log::get(log::Area::App))
                    lg->warn("look.css {}: {}", sec ? std::string(sec->to_string()) : "?", err.what());
            });
        css->load_from_resource("/io/github/scott8420/Tidy/look.css");
        Gtk::StyleContext::add_provider_for_display(
            display, css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    if (auto lg = log::get(log::Area::App)) lg->info("the look: on");
}

}  // namespace tidy::look
