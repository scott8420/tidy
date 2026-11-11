#include "SelectList.hpp"

#include <gtkmm/bitset.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/label.h>
#include <gtkmm/listitem.h>
#include <gtkmm/listview.h>
#include <gtkmm/multiselection.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/stringlist.h>
#include <giomm/menu.h>
#include <giomm/simpleactiongroup.h>

namespace tidy {

namespace {

// One row's data, as the model holds it.
class RowObj : public Glib::Object {
public:
    SelectList::Item item;
    static Glib::RefPtr<RowObj> create(SelectList::Item i) {
        auto r = Glib::make_refptr_for_instance<RowObj>(new RowObj());
        r->item = std::move(i);
        return r;
    }
protected:
    RowObj() : Glib::ObjectBase(typeid(RowObj)) {}
};

Gtk::Label* text(const char* css) {
    auto l = Gtk::make_managed<Gtk::Label>();
    l->set_xalign(0);
    l->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    if (css) l->add_css_class(css);
    return l;
}

std::string count_text(std::size_t n, std::size_t of) {
    if (of == 0) return "";
    if (n == 0) return "Nothing selected";
    return std::to_string(n) + " selected";
}

}  // namespace

SelectList::SelectList(const std::string& name) : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
    set_name(name);
    m_store = Gio::ListStore<Glib::Object>::create();
    m_sel = Gtk::MultiSelection::create(m_store);

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([this](const Glib::RefPtr<Gtk::ListItem>& li) {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        box->set_margin_start(10);
        box->set_margin_end(10);
        box->set_margin_top(3);
        box->set_margin_bottom(3);
        auto head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        head->set_margin_top(12);
        head->set_margin_bottom(6);
        auto h = text("heading");
        auto hs = text("dim");
        hs->set_ellipsize(Pango::EllipsizeMode::END);
        head->append(*h);
        head->append(*hs);
        box->append(*head);
        box->append(*text(nullptr));
        box->append(*text("dim"));
        li->set_child(*box);
        // Right-click: on an unselected row, select it alone first (Nautilus),
        // then the same choices as the setter.
        auto click = Gtk::GestureClick::create();
        click->set_button(3);
        Gtk::ListItem* raw = li.get();
        click->signal_pressed().connect([this, raw, box](int, double x, double y) {
            const auto pos = raw->get_position();
            if (pos == GTK_INVALID_LIST_POSITION) return;
            if (!m_sel->is_selected(pos)) m_sel->select_item(pos, true);
            double ox = x, oy = y;
            box->translate_coordinates(*this, x, y, ox, oy);
            popup_at(ox, oy);
        });
        box->add_controller(click);
    });
    factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& li) {
        auto row = std::dynamic_pointer_cast<RowObj>(li->get_item());
        auto* box = dynamic_cast<Gtk::Box*>(li->get_child());
        if (!row || !box) return;
        const auto& it = row->item;
        auto* head = dynamic_cast<Gtk::Box*>(box->get_first_child());
        auto* title = dynamic_cast<Gtk::Label*>(head->get_next_sibling());
        auto* line = dynamic_cast<Gtk::Label*>(title->get_next_sibling());
        auto* h = dynamic_cast<Gtk::Label*>(head->get_first_child());
        auto* hs = dynamic_cast<Gtk::Label*>(h->get_next_sibling());
        head->set_visible(!it.header.empty());
        h->set_text(it.header);
        hs->set_text(it.header_sub);
        hs->set_visible(!it.header_sub.empty());
        title->set_text(it.title);
        line->set_text(it.line);
        line->set_visible(!it.line.empty());
        if (it.tooltip.empty()) box->set_has_tooltip(false);
        else box->set_tooltip_text(it.tooltip);
    });

    m_view = Gtk::make_managed<Gtk::ListView>(m_sel, factory);
    m_view->set_name(name + ".view");
    m_view->set_enable_rubberband(true);
    m_view->set_show_separators(false);
    m_view->add_css_class("select-list");
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_child(*m_view);
    append(*scroll);

    // The setter: "N selected   Set to [choose... | ...]".
    m_setter = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    m_setter->set_name(name + ".setter");
    m_setter->set_hexpand(true);
    m_count = Gtk::make_managed<Gtk::Label>();
    m_count->set_name(name + ".count");
    m_count->set_xalign(0);
    m_count->add_css_class("dim");
    m_count->set_hexpand(true);
    m_setter->append(*m_count);
    auto to_l = Gtk::make_managed<Gtk::Label>("Set to");
    m_setter->append(*to_l);
    m_to = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({"choose..."}));
    m_to->set_name(name + ".to");
    m_to->set_tooltip_text("Picking one shows the preview at once -- nothing changes until Do it");
    m_to->property_selected().signal_changed().connect([this] {
        if (m_resetting) return;
        const auto k = m_to->get_selected();
        if (k == 0 || k == GTK_INVALID_LIST_POSITION) return;
        m_resetting = true;
        m_to->set_selected(0);
        m_resetting = false;
        choose(k - 1);
    });
    m_setter->append(*m_to);

    // The right-click menu: the same choices, as sel.set(i).
    auto group = Gio::SimpleActionGroup::create();
    auto act = Gio::SimpleAction::create("set", Glib::VARIANT_TYPE_INT32);
    act->signal_activate().connect([this](const Glib::VariantBase& v) {
        const int i = Glib::VariantBase::cast_dynamic<Glib::Variant<int>>(v).get();
        if (i >= 0) choose(static_cast<std::size_t>(i));
    });
    group->add_action(act);
    insert_action_group("sel", group);
    m_menu = Gtk::make_managed<Gtk::PopoverMenu>(Gio::Menu::create());
    m_menu->set_has_arrow(false);
    m_menu->set_halign(Gtk::Align::START);
    m_menu->set_parent(*this);

    m_sel->signal_selection_changed().connect([this](guint, guint) { selection_changed(); });
    selection_changed();
}

SelectList::~SelectList() {
    if (m_menu) m_menu->unparent();
}

void SelectList::set_items(const std::vector<Item>& items) {
    std::vector<Glib::RefPtr<Glib::Object>> objs;
    objs.reserve(items.size());
    for (const auto& i : items) objs.push_back(RowObj::create(i));
    m_store->splice(0, m_store->get_n_items(), objs);
    m_sel->unselect_all();
    selection_changed();
}

void SelectList::set_choices(const std::vector<std::string>& choices) {
    m_choices = choices;
    auto model = Gtk::StringList::create({"choose..."});
    auto menu = Gio::Menu::create();
    for (std::size_t i = 0; i < choices.size(); ++i) {
        model->append(choices[i]);
        auto item = Gio::MenuItem::create(choices[i], "");
        item->set_action_and_target("sel.set", Glib::Variant<int>::create(static_cast<int>(i)));
        menu->append_item(item);
    }
    m_resetting = true;
    m_to->set_model(model);
    m_to->set_selected(0);
    m_resetting = false;
    m_menu->set_menu_model(menu);
}

void SelectList::select(const std::vector<std::size_t>& rows) {
    const auto n = m_store->get_n_items();
    auto want = Gtk::Bitset::create();
    for (auto r : rows) if (r < n) want->add(static_cast<guint>(r));
    auto mask = Gtk::Bitset::create();
    if (n) mask->add_range(0, n);
    m_sel->set_selection(want, mask);
    selection_changed();
}

void SelectList::select_all() { m_sel->select_all(); }

std::vector<std::size_t> SelectList::selected() const {
    std::vector<std::size_t> out;
    const auto n = m_store->get_n_items();
    for (guint i = 0; i < n; ++i)
        if (m_sel->is_selected(i)) out.push_back(i);
    return out;
}

std::size_t SelectList::size() const { return m_store->get_n_items(); }

void SelectList::choose(std::size_t i) {
    if (i >= m_choices.size() || !m_choose) return;
    m_choose(i);
}

void SelectList::selection_changed() {
    const auto n = selected().size();
    m_count->set_text(count_text(n, size()));
    m_to->set_sensitive(n > 0);
    if (m_changed) m_changed(n);
}

void SelectList::popup_at(double x, double y) {
    if (m_choices.empty() || selected().empty()) return;
    Gdk::Rectangle r(static_cast<int>(x), static_cast<int>(y), 1, 1);
    m_menu->set_pointing_to(r);
    m_menu->popup();
}

}  // namespace tidy
