#include "OrganizeStart.hpp"
#include "core/Quarantine.hpp"
#include "core/Templates.hpp"

#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

namespace tidy {

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr, float x = 0.0f) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(x);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
}  // namespace

int OrganizeStart::next_step(int types, int held) {
    (void)types;            // bookbinding is built in: there is always one type to organize by
    if (held > 0) return 1; // answer the Quarantine first -- held folders are skipped whole
    return 2;               // pick a folder: What Tidy sees
}

OrganizeStart::OrganizeStart(std::string data_dir, Go go, Act organize)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_data_dir(std::move(data_dir)), m_go(std::move(go)),
      m_organize(std::move(organize)) {
    set_name("organize.start");
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    auto col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    col->set_margin_start(40);
    col->set_margin_end(40);
    col->set_margin_top(28);
    col->set_margin_bottom(28);
    col->set_halign(Gtk::Align::CENTER);
    col->set_size_request(560, -1);

    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
    auto emblem = Gtk::make_managed<Gtk::Image>();
    emblem->set_from_icon_name("tidy-hero-organize");
    emblem->set_pixel_size(64);
    head->append(*emblem);
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->set_valign(Gtk::Align::CENTER);
    titles->append(*text("Organize", "hero-title"));
    titles->append(*text("Put a folder in order, one step at a time. Nothing moves until you have seen it, "
                         "and every Do it is one Undo in Activity.", "hero-sub"));
    head->append(*titles);
    col->append(*head);
    m_summary = text("", "dim");
    m_summary->set_name("organize.start.summary");
    m_summary->set_margin_bottom(4);
    col->append(*m_summary);

    auto to = [this](const char* page) { return Act([this, page] { m_go(page); }); };
    col->append(*step(1, "Design your types",
                      "A type is a tree: the folders a project has and what goes in each. Tidy organizes by "
                      "your design -- it never guesses one.",
                      {{"Open Types", to("templates")}}, "organize.step.types"));
    col->append(*step(2, "Answer what Tidy is holding",
                      "Folders that may be one thing (an app's bundle) are held whole in Quarantine. Tidy will "
                      "not go inside them until you say what they are.",
                      {{"Open Quarantine", to("quarantine")}}, "organize.step.quarantine"));
    col->append(*step(3, "Pick a folder and see what Tidy sees",
                      "Tidy reads it top down: bundles, projects, sets, kinds, the Closet -- each with what you can "
                      "do. Not sure where the mess is? The Map shows your home as boxes; red is not tracked yet.",
                      {{"Look at a folder\u2026", to("sees")}, {"Open the Map", to("map")}}, "organize.step.map"));
    col->append(*step(4, "Organize it as a type",
                      "Pick its type; Tidy lays it out and moves it where that type lives. You see the whole "
                      "plan first, then Do it.",
                      {{"Organize a folder…", [this] { if (m_organize) m_organize(); }}}, "organize.step.organize"));
    col->append(*step(5, "Then the loose files",
                      "What is left goes by kind: Sort sends loose files home; the Closet keeps what nothing "
                      "claims until you decide.",
                      {{"Sort", to("sort")}, {"The Closet", to("closet")}}, "organize.step.loose"));

    scroll->set_child(*col);
    append(*scroll);
}

Gtk::Box* OrganizeStart::step(int n, const std::string& title, const std::string& why,
                              const std::vector<std::pair<std::string, Act>>& buttons, const std::string& name) {
    auto card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    card->set_name(name);
    card->add_css_class("card");
    auto num = text(std::to_string(n), "card-big", 0.5f);
    num->set_size_request(32, -1);
    num->set_valign(Gtk::Align::START);
    card->append(*num);
    auto mid = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    mid->set_hexpand(true);
    auto top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    auto t = text(title, "card-title");
    t->set_hexpand(true);
    top->append(*t);
    auto state = text("", "card-sub", 1.0f);
    state->set_name(name + ".state");
    top->append(*state);
    mid->append(*top);
    mid->append(*text(why, "card-sub"));
    auto line = text("", "dim");
    line->set_name(name + ".line");
    line->set_visible(false);
    mid->append(*line);
    auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row->set_margin_top(6);
    for (const auto& [label, act] : buttons) {
        auto b = Gtk::make_managed<Gtk::Button>(label);
        b->add_css_class("ghost");
        b->add_css_class("small");
        b->signal_clicked().connect(act);
        row->append(*b);
    }
    mid->append(*row);
    card->append(*mid);
    m_steps.push_back({card, state, line});
    return card;
}

void OrganizeStart::refresh() {
    const int mine = static_cast<int>(core::templates::list(m_data_dir).size());
    const int held = static_cast<int>(core::quarantine::current().held.size());
    const int next = next_step(mine, held);

    auto say = [](Step& s, const std::string& l) { s.line->set_text(l); s.line->set_visible(!l.empty()); };
    say(m_steps[0], "You have bookbinding (built in)" +
                        (mine ? " and " + std::to_string(mine) + " of your own." : std::string(" -- none of your own yet.")));
    say(m_steps[1], held ? std::to_string(held) + (held == 1 ? " folder is" : " folders are") + " held, waiting for an answer."
                         : "Nothing is held right now.");
    for (std::size_t i = 0; i < m_steps.size(); ++i) {
        const bool lit = static_cast<int>(i) == next;
        if (lit) m_steps[i].card->add_css_class("selected");
        else m_steps[i].card->remove_css_class("selected");
        m_steps[i].state->set_text(lit ? "Start here" : "");
    }
    m_summary->set_text(held ? "Start with the Quarantine: the folders it holds are skipped until you answer."
                             : "Your types are ready and nothing is held -- pick a folder and see what Tidy sees.");
}

}  // namespace tidy
