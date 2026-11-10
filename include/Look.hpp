#pragma once
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Look -- THE LOOK stone's GTK half (Cairn s001, lifted from Rouster s005+).
//
// Contents:
//   enabled()  -- does this app wear the look? THE one switch.
//   install()  -- load resources/look.css from the gresource + ask GTK for its
//                 dark variant (menus, popovers, the file chooser match).
//
// The look is OPT-IN. An app turns it on by setting kLookDefault below (or,
// for a look at the other shell without rebuilding, TIDY_LOOK=0 / =1 in the
// environment). With it on, the Shell builds the custom sidebar (brand +
// section headers + icon rows, core::look::NavEntry), tints the window per
// page, and pages may use the start / running / results states. With it off,
// none of that is built and look.css is never loaded: the stock StackSidebar,
// the theme's own colours, and Appearance following the desktop's light/dark.
//
// The look is DARK, always (as Rouster): its colours are literals designed on
// a dark ground, so with the look on the app does not follow the desktop's
// light preference. That is the price of the skin; a plain app keeps it.
//
// The page: docs/the-look.md -- what to copy, what to rename, how to leave it off.
// ─────────────────────────────────────────────────────────────────────────────
namespace tidy::look {

// Tidy wears the look (emerald is its colour, from Rouster's Tidy page).
inline constexpr bool kLookDefault = true;

// kLookDefault, unless TIDY_LOOK is set to 0 or 1 (read once).
bool enabled();

// Call once in App::on_activate, after the gresource is registered and before
// the first window. Does nothing when !enabled().
void install();

}  // namespace tidy::look
