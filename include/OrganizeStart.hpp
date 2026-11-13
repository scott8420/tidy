#pragma once
// OrganizeStart (s027) -- Organize's landing page: the first real process to
// organization, so it gently says what to do, in order. Five steps, each a
// card with one line of why and one button; the first step not done yet is
// lit "Start here". Read-only: it counts your types (core::templates) and
// what Quarantine holds (core::quarantine::current), nothing more. The
// buttons only go to the page (or window) that does the work.
#include <gtkmm/box.h>

#include <functional>
#include <string>
#include <vector>

namespace Gtk { class Label; class Button; }

namespace tidy {

class OrganizeStart : public Gtk::Box {
public:
    using Go = std::function<void(const std::string& page)>;
    using Act = std::function<void()>;
    OrganizeStart(std::string data_dir, Go go, Act organize);

    void refresh();   // re-count: called each time the page shows

    // The step to light, from the counts (0-based; pure, the selftest's).
    // Types first if you have none of your own AND the built-in is off;
    // Quarantine while anything is held; else the Map.
    static int next_step(int types, int held);

private:
    struct Step { Gtk::Box* card = nullptr; Gtk::Label* state = nullptr; Gtk::Label* line = nullptr; };
    Gtk::Box* step(int n, const std::string& title, const std::string& why,
                   const std::vector<std::pair<std::string, Act>>& buttons, const std::string& name);

    std::string m_data_dir;
    Go  m_go;
    Act m_organize;
    std::vector<Step> m_steps;
    Gtk::Label* m_summary = nullptr;
};

}  // namespace tidy
