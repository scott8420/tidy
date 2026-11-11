#pragma once
#include <gtkmm/window.h>

#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// HelpWindow (s017) -- "How to use Tidy": the process, step by step, from
// core::help. A step that happens on a page carries a button that takes you
// there. Hide-on-close singleton, like About. Non-modal: keep it open beside
// Tidy while you work.
// ─────────────────────────────────────────────────────────────────────────────
namespace tidy {

class HelpWindow : public Gtk::Window {
public:
    using Go = std::function<void(const std::string& page)>;
    HelpWindow(Gtk::Window& parent, Go go);
};

}  // namespace tidy
