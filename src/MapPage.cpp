#include "MapPage.hpp"
#include "core/Projects.hpp"
#include "Busy.hpp"
#include "Log.hpp"
#include "core/Pages.hpp"

#include <giomm/appinfo.h>
#include <giomm/file.h>
#include <glibmm/markup.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/popover.h>
#include <gtkmm/tooltip.h>
#include <gtkmm/window.h>

#include <chrono>
#include <cmath>
#include <cstdio>

namespace tidy {

namespace mp = core::map;

namespace {
// Muted on purpose (Scott's colour-wheel rule): the page's tint is orange, so the
// blue (its complement) is toned well down; red / amber / green kept soft.
struct Rgb { double r, g, b; const char* hex; };
constexpr Rgb kColour[mp::kKinds] = {
    {0.33, 0.65, 0.45, "#54a673"},   // cataloged
    {0.86, 0.65, 0.28, "#dba647"},   // loose
    {0.80, 0.38, 0.33, "#cc6154"},   // untracked
    {0.55, 0.57, 0.60, "#8c9199"},   // whole
    {0.38, 0.47, 0.62, "#61789e"},   // archived
};
constexpr Rgb kShut = {0.25, 0.25, 0.28, "#40404a"};

Gtk::Label* label(const std::string& text, const char* css, float x = 0.5f) {
    auto l = Gtk::make_managed<Gtk::Label>(text);
    if (css) l->add_css_class(css);
    l->set_xalign(x);
    l->set_wrap(true);
    l->set_justify(x == 0.5f ? Gtk::Justification::CENTER : Gtk::Justification::LEFT);
    return l;
}
Gtk::Image* hero(int px) {
    auto i = Gtk::make_managed<Gtk::Image>();
    i->set_from_icon_name("tidy-hero-map");
    i->set_pixel_size(px);
    return i;
}
void rounded(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double w, double h, double r) {
    r = std::min({r, w / 2, h / 2});
    cr->begin_new_sub_path();
    cr->arc(x + w - r, y + r, r, -M_PI / 2, 0);
    cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
    cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
    cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cr->close_path();
}
}  // namespace

MapPage::MapPage(std::string home, bool fixture, Text user_dirs, CatchAll catch_all, Adopt adopt)
    : m_home(std::move(home)), m_fixture(fixture), m_user_dirs(std::move(user_dirs)),
      m_catch_all(std::move(catch_all)), m_adopt(std::move(adopt)) {
    set_name("map.states");
    const auto* p = core::pages::find("map");

    auto start = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 14);
    start->set_name("map.start");
    start->set_valign(Gtk::Align::CENTER);
    start->set_halign(Gtk::Align::CENTER);
    start->set_margin(40);
    start->append(*hero(128));
    start->append(*label(p ? p->title : "Map", "hero-title"));
    auto sub = label(p ? p->promise : "", "hero-sub");
    sub->set_max_width_chars(52);
    start->append(*sub);
    auto note = label(std::string("Each box is a folder, as big as its files. Green: cataloged. Amber: loose -- Sort "
                                  "sees them. Red: untracked -- adopt the folder, or make it a catch-all. Grey: inside "
                                  "a project or a repo. Blue: archived. Look reads only.") +
                          (m_fixture ? "\n(a FIXTURE home: " + m_home + ")" : ""),
                      "dim");
    note->set_max_width_chars(60);
    start->append(*note);
    auto orb = Gtk::make_managed<Gtk::Button>("Look");
    orb->set_name("map.look");
    orb->add_css_class("orb");
    orb->set_halign(Gtk::Align::CENTER);
    orb->set_margin_top(18);
    orb->signal_clicked().connect([this] { fill(); });
    start->append(*orb);
    add(*start, "start");

    auto results = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    results->set_name("map.results");
    auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    head->set_margin_start(32);
    head->set_margin_end(32);
    head->set_margin_top(24);
    head->set_margin_bottom(8);
    head->append(*hero(48));
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    titles->append(*label(p ? p->title : "Map", "hero-title", 0.0f));
    m_summary = label("", "dim", 0.0f);
    m_summary->set_name("map.summary");
    m_summary->set_use_markup(true);
    titles->append(*m_summary);
    titles->set_hexpand(true);
    head->append(*titles);
    results->append(*head);

    auto crumbs = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    crumbs->set_margin_start(32);
    crumbs->set_margin_end(32);
    crumbs->set_margin_bottom(8);
    m_up = Gtk::make_managed<Gtk::Button>();
    m_up->set_name("map.up");
    m_up->set_icon_name("go-up-symbolic");
    m_up->set_tooltip_text("Up a folder (Backspace)");
    m_up->signal_clicked().connect([this] {
        const auto cut = m_rel.find_last_of('/');
        go(cut == std::string::npos ? std::string() : m_rel.substr(0, cut));
    });
    crumbs->append(*m_up);
    m_where = label("", nullptr, 0.0f);
    m_where->set_name("map.where");
    m_where->set_hexpand(true);
    crumbs->append(*m_where);
    results->append(*crumbs);

    m_area = Gtk::make_managed<Gtk::DrawingArea>();
    m_area->set_name("map.area");
    m_area->set_vexpand(true);
    m_area->set_hexpand(true);
    m_area->set_margin_start(32);
    m_area->set_margin_end(32);
    m_area->set_margin_bottom(12);
    m_area->set_focusable(true);
    m_area->set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) { draw(cr, w, h); });
    m_area->set_has_tooltip(true);
    m_area->signal_query_tooltip().connect(
        [this](int x, int y, bool, const Glib::RefPtr<Gtk::Tooltip>& tip) {
            const auto* b = at(x, y);
            if (!b) return false;
            std::string t;
            if (!b->node) {
                const auto* n = mp::find(*m_root, m_rel);
                t = "Files directly in " + shown(m_rel) + "\n" + (n ? mp::line(n->here) : std::string());
            } else {
                t = shown(b->node->rel) + "\n" + (b->node->shut ? std::string("") : mp::line(b->node->c));
                if (!b->node->what.empty()) t += (b->node->shut ? "" : "\n") + b->node->what;
            }
            tip->set_text(t);
            return true;
        },
        false);
    auto click = Gtk::GestureClick::create();
    click->set_button(0);
    click->signal_pressed().connect([this, click](int, double x, double y) {
        const auto* b = at(x, y);
        if (!b) return;
        if (click->get_current_button() == 3) { menu_for(*b, x, y); return; }
        if (b->node && !b->node->shut && (b->node->c.total() > 0 || !b->node->kids.empty())) go(b->node->rel);
    });
    m_area->add_controller(click);
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType) {
        if (key != GDK_KEY_BackSpace || m_rel.empty()) return false;
        m_up->activate();
        return true;
    }, false);
    m_area->add_controller(keys);
    results->append(*m_area);

    m_menu = Gtk::make_managed<Gtk::Popover>();
    m_menu->set_has_arrow(false);
    m_menu_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    m_menu->set_child(*m_menu_box);
    m_menu->set_parent(*m_area);
    m_menu->signal_closed().connect([this] { m_area->set_has_tooltip(true); });   // no tooltip over the menu

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->add_css_class("act-bar");
    m_status = label("Click a box to go in; right-click it to adopt it, make it a catch-all, or open it. Sizes are "
                     "squeezed so small folders show -- the counts are real.", "dim", 0.0f);
    m_status->set_name("map.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto again = Gtk::make_managed<Gtk::Button>("Look again");
    again->set_name("map.again");
    again->signal_clicked().connect([this] { fill(); });
    bar->append(*again);
    results->append(*bar);
    add(*results, "results");
    set_visible_child("start");
}

void MapPage::refresh_if_shown() {
    if (get_visible_child_name() == "results") fill();
}

std::string MapPage::shown(const std::string& rel) const { return rel.empty() ? "~" : "~/" + rel; }

void MapPage::fill() {
    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    if (!win || m_busy) return;
    m_busy = true;
    auto out = std::make_shared<mp::Node>();
    const std::string home = m_home, dirs = m_user_dirs ? m_user_dirs() : "";
    const auto catch_all = m_catch_all ? m_catch_all() : std::vector<std::string>{};
    auto ms = std::make_shared<long long>(0);
    busy::start(*win, "Map -- reading the home", "Every folder: names, and each file's id (nothing is written)",
        [out, home, dirs, catch_all, ms] {
            const auto t0 = std::chrono::steady_clock::now();
            *out = mp::build(home, dirs, catch_all);
            *ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        },
        [this, out, ms] {
            m_busy = false;
            m_root = out;
            if (auto lg = log::get(log::Area::Shell)) lg->info("map look: {} ({} ms)", mp::line(m_root->c), *ms);
            const std::string keep = m_rel;
            set_visible_child("results");
            go(mp::find(*m_root, keep) ? keep : std::string());
        });
}

void MapPage::go(const std::string& rel) {
    if (!m_root) return;
    const auto* n = mp::find(*m_root, rel);
    if (!n) return;
    m_rel = rel;
    m_up->set_sensitive(!rel.empty());
    m_where->set_text(shown(rel) + (n->what.empty() ? "" : "   (" + n->what + ")"));
    std::string legend = Glib::Markup::escape_text(mp::line(n->c)) + "\n";
    for (std::size_t i = 0; i < mp::kKinds; ++i) {
        legend += std::string("<span foreground=\"") + kColour[i].hex + "\">■</span> " +
                  mp::kind_title(static_cast<mp::Kind>(i)) + "   ";
    }
    legend += std::string("<span foreground=\"") + kShut.hex + "\">■</span> not looked inside";
    m_summary->set_markup(legend);
    m_w = m_h = 0;   // lay out again at the next draw
    m_area->queue_draw();
    m_area->grab_focus();
}

void MapPage::layout(int w, int h) {
    m_boxes.clear();
    m_w = w;
    m_h = h;
    if (!m_root) return;
    const auto* n = mp::find(*m_root, m_rel);
    if (!n) return;
    std::vector<double> sizes;
    std::vector<const mp::Node*> nodes;
    for (const auto& k : n->kids) {
        if (k.weight() == 0) continue;
        sizes.push_back(static_cast<double>(k.weight()));
        nodes.push_back(&k);
    }
    if (n->here.total()) {
        sizes.push_back(static_cast<double>(n->here.total()));
        nodes.push_back(nullptr);
    }
    // Squeezed so small folders still show (one huge folder would leave the rest as
    // slivers): the square root of the count, and never under 1.5% of the whole.
    // The real counts are in each box's label and tooltip.
    double sum = 0;
    for (auto& s : sizes) { s = std::sqrt(s); sum += s; }
    for (auto& s : sizes) s = std::max(s, sum * 0.015);
    const auto rs = mp::squarify(sizes, {0, 0, static_cast<double>(w), static_cast<double>(h)});
    for (std::size_t i = 0; i < rs.size(); ++i) m_boxes.push_back({rs[i], nodes[i]});
}

const MapPage::Box* MapPage::at(double x, double y) const {
    for (const auto& b : m_boxes)
        if (x >= b.r.x && x < b.r.x + b.r.w && y >= b.r.y && y < b.r.y + b.r.h) return &b;
    return nullptr;
}

void MapPage::draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    if (w != m_w || h != m_h) layout(w, h);
    const auto* here = m_root ? mp::find(*m_root, m_rel) : nullptr;
    if (!here || m_boxes.empty()) {
        auto layout = m_area->create_pango_layout(m_root ? "Nothing here." : "");
        cr->set_source_rgba(1, 1, 1, 0.6);
        cr->move_to(8, 8);
        layout->show_in_cairo_context(cr);
        return;
    }
    const double gap = 3;
    for (const auto& b : m_boxes) {
        const double x = b.r.x + gap / 2, y = b.r.y + gap / 2, bw = b.r.w - gap, bh = b.r.h - gap;
        if (bw < 1 || bh < 1) continue;
        cr->save();
        rounded(cr, x, y, bw, bh, 5);
        cr->clip();
        const bool shut = b.node && b.node->shut;
        const mp::Counts& c = b.node ? b.node->c : here->here;
        if (shut || c.total() == 0) {
            cr->set_source_rgb(kShut.r, kShut.g, kShut.b);
            cr->paint();
        } else {   // stacked bands, bottom up, each kind its share of the box
            double yy = y + bh;
            for (std::size_t i = 0; i < mp::kKinds; ++i) {
                if (!c.n[i]) continue;
                const double part = bh * static_cast<double>(c.n[i]) / static_cast<double>(c.total());
                cr->set_source_rgb(kColour[i].r, kColour[i].g, kColour[i].b);
                cr->rectangle(x, yy - part, bw, part);
                cr->fill();
                yy -= part;
            }
        }
        cr->restore();
        // The name + count, on a dark strip, when the box has room for it.
        if (bw > 46 && bh > 22) {
            const std::string name = b.node ? b.node->name : std::string("(files here)");
            const std::string count = shut ? std::string("not looked inside") : std::to_string(c.total());
            auto lay = m_area->create_pango_layout(name + "  " + count);
            lay->set_width(static_cast<int>((bw - 10) * PANGO_SCALE));
            lay->set_ellipsize(Pango::EllipsizeMode::END);
            int lw = 0, lh = 0;
            lay->get_pixel_size(lw, lh);
            cr->save();
            rounded(cr, x, y, bw, bh, 5);
            cr->clip();
            cr->set_source_rgba(0, 0, 0, 0.38);
            cr->rectangle(x, y, bw, lh + 6);
            cr->fill();
            cr->set_source_rgb(1, 1, 1);
            cr->move_to(x + 5, y + 3);
            lay->show_in_cairo_context(cr);
            cr->restore();
        }
    }
}

void MapPage::menu_for(const Box& b, double x, double y) {
    if (!b.node || b.node->shut) return;
    while (auto* c = m_menu_box->get_first_child()) m_menu_box->remove(*c);
    const std::string rel = b.node->rel;
    const auto item = [this](const std::string& text, const std::string& name, std::function<void()> f) {
        auto btn = Gtk::make_managed<Gtk::Button>(text);
        btn->set_name(name);
        btn->set_has_frame(false);
        btn->signal_clicked().connect([this, f = std::move(f)] { m_menu->popdown(); f(); });
        m_menu_box->append(*btn);
    };
    const bool whole = b.node->what.find("kept whole") != std::string::npos || b.node->what == "~/Archive";
    if (!whole) {
        item("Adopt in place...", "map.menu.adopt", [this, rel] { if (m_adopt) m_adopt(rel, false); });
        item("Make it a catch-all...", "map.menu.catchall", [this, rel] { if (m_adopt) m_adopt(rel, true); });
    }
    if (!whole && m_conform)
        item("Conform to a template...", "map.menu.conform", [this, rel] { m_conform(m_home + "/" + rel); });   // s023h
    if (!whole && m_book) {   // s024b
        item("Make it a book...", "map.menu.book", [this, rel] { m_book(m_home + "/" + rel); });
        item("Its sub-folders are books...", "map.menu.books", [this, rel] { m_book(m_home + "/" + rel + "/"); });
    }
    if (m_layout && core::projects::is_whole_at(rel) && !core::projects::is_project_name(std::filesystem::path(rel).filename().string()))
        item("Put in the layout...", "map.menu.layout", [this, rel] { m_layout(m_home + "/" + rel); });   // s024b
    if (!whole && m_scan)
        item("Make a template from a scan...", "map.menu.scan", [this, rel] { m_scan(m_home + "/" + rel); });   // s023i
    item("Open in Files", "map.menu.open", [this, rel] {
        try {
            Gio::AppInfo::launch_default_for_uri(Gio::File::create_for_path(m_home + "/" + rel)->get_uri());
        } catch (const Glib::Error& e) {
            m_status->set_text("Could not open it: " + std::string(e.what()));
        }
    });
    Gdk::Rectangle at{static_cast<int>(x), static_cast<int>(y), 1, 1};
    m_menu->set_pointing_to(at);
    m_area->set_has_tooltip(false);
    m_menu->popup();
}

}  // namespace tidy
