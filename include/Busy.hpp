#pragma once
// Busy (s023a) -- a long operation runs OFF the window's thread while a small
// modal window shows a progress bar (Scott, s023: adopting Documents froze the
// window long enough for GNOME's "Force quit" dialog).
//
//   Busy::start   any work: the bar pulses (or follows `fraction` when given),
//                 with the seconds gone by; `done` runs on the window's thread.
//   Busy::door    a door plan: door.run() off the thread; the bar follows the
//                 manifest as it grows (an intent + a done line per step), so it
//                 reads "N of M steps". `after` (optional) also runs off the
//                 thread with the result -- the catalog's record(), say.
//
// The window cannot be closed while the work runs, and it is modal: nothing
// else in Tidy touches the door until it is done. A Busy deletes itself.
#include <latch/Door.hpp>

#include <cstddef>
#include <functional>
#include <string>

namespace Gtk { class Window; }

namespace tidy::busy {

void start(Gtk::Window& parent, const std::string& title, const std::string& text,
           std::function<void()> work, std::function<void()> done,
           std::function<double()> fraction = {});   // 0..1, or < 0 to pulse

void door(Gtk::Window& parent, latch::door::Door& d, latch::door::Plan plan, const std::string& title,
          std::function<void(const latch::door::Result&)> done,
          std::function<void(const latch::door::Result&)> after = {},
          std::function<void(latch::door::Plan&)> before = {});   // off the thread, before run (drop refusals, say)

// The progress of a run from how far the manifest grew: lines written since
// `start_size`, over 2 per step plus the batch line. Pure; the selftest checks it.
inline double door_fraction(std::size_t new_lines, std::size_t steps) {
    if (steps == 0) return 1.0;
    const double f = new_lines <= 1 ? 0.0 : static_cast<double>(new_lines - 1) / (2.0 * static_cast<double>(steps));
    return f > 1.0 ? 1.0 : f;
}

}  // namespace tidy::busy
