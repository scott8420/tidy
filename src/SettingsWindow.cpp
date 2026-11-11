#include "SettingsWindow.hpp"
#include "core/Protection.hpp"

#include <algorithm>
#include "core/Homing.hpp"

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/textview.h>

namespace tidy {

namespace st = core::settings;

namespace {
Gtk::Label* text(const std::string& s, const char* css = nullptr) {
    auto l = Gtk::make_managed<Gtk::Label>(s);
    l->set_xalign(0);
    l->set_wrap(true);
    if (css) l->add_css_class(css);
    return l;
}
}  // namespace

SettingsWindow::SettingsWindow(Gtk::Window& parent, const std::string& home_dir, SaveFn on_save)
    : m_save(std::move(on_save)), m_home_dir(home_dir) {
    set_name("settings.window");
    set_title("Tidy Settings");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_default_size(520, 700);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(20);

    box->append(*text("The Closet", "heading"));
    auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    auto pre = text("Propose aging out after");
    pre->set_wrap(false);
    row->append(*pre);
    m_days = Gtk::make_managed<Gtk::SpinButton>(
        Gtk::Adjustment::create(st::kAgingDefault, st::kAgingMin, st::kAgingMax, 1, 10));
    m_days->set_name("settings.aging_days");
    m_days->set_numeric(true);
    row->append(*m_days);
    auto post = text("days unread");
    post->set_wrap(false);
    row->append(*post);
    box->append(*row);
    box->append(*text("An undecided file born and unread this long is proposed as Ephemeral.", "dim"));
    {   // s019 the home sweep: the first sweep's archive
        auto r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto a = text("Propose Historical after");
        a->set_wrap(false);
        r->append(*a);
        m_old = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(st::kOldDefault, st::kOldMin, st::kOldMax, 1, 1));
        m_old->set_name("settings.old_years");
        m_old->set_numeric(true);
        r->append(*m_old);
        auto b = text("years untouched (0 = never)");
        b->set_wrap(false);
        r->append(*b);
        box->append(*r);
        box->append(*text("A loose file nobody has changed or read this long is a record: proposed Historical, "
                          "then Due archives it.", "dim"));
    }

    auto prj = text("Projects", "heading");
    prj->set_margin_top(14);
    box->append(*prj);
    auto row2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    auto pre2 = text("Look again every");
    pre2->set_wrap(false);
    row2->append(*pre2);
    m_rescan = Gtk::make_managed<Gtk::SpinButton>(
        Gtk::Adjustment::create(st::kRescanDefault, st::kRescanMin, st::kRescanMax, 1, 10));
    m_rescan->set_name("settings.rescan_minutes");
    m_rescan->set_numeric(true);
    row2->append(*m_rescan);
    auto post2 = text("minutes (0 = never)");
    post2->set_wrap(false);
    row2->append(*post2);
    box->append(*row2);
    box->append(*text("While Tidy runs, and once at launch: finds your projects again, read-only. "
                      "A moved one is followed by its id; one not found is marked gone, never forgotten.", "dim"));

    // s012a: the trigger's clocks.
    auto due = text("Due", "heading");
    due->set_margin_top(14);
    box->append(*due);
    const auto spin_row = [&](const char* pre, Gtk::SpinButton*& spin, const char* name, int def, int lo, int hi,
                              const char* post) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto a = text(pre);
        a->set_wrap(false);
        row->append(*a);
        spin = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(def, lo, hi, 1, 10));
        spin->set_name(name);
        spin->set_numeric(true);
        row->append(*spin);
        auto b = text(post);
        b->set_wrap(false);
        row->append(*b);
        box->append(*row);
    };
    spin_row("Held is due to remove after", m_held, "settings.held_years", st::kHeldDefault, st::kHeldMin,
             st::kHeldMax, "years");
    spin_row("Historical is due to archive after", m_archive, "settings.archive_days", st::kArchiveDefault,
             st::kArchiveMin, st::kArchiveMax, "days unread and unchanged");
    spin_row("Remove from ~/Archive with", m_proofs, "settings.proofs_needed", st::kProofsDefault, st::kProofsMin,
             st::kProofsMax, "proofs on media");
    box->append(*text("Ephemeral uses the aging days above. Due proposes; you check and Do it. Removal goes to the "
                      "Trash -- an archived file only when that many of its media copies read back the same, then.",
                      "dim"));

    auto junk = text("Junk list", "heading");
    junk->set_margin_top(14);
    box->append(*junk);
    box->append(*text("File-name patterns, one per line -- *.png, Screenshot*, *.tmp. A file in the "
                      "Closet that matches is proposed for the Junk Drawer: kept, no home. Your "
                      "list wins over Tidy's own birth rules; a decided file is never re-proposed.", "dim"));
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_vexpand(true);
    scroll->set_has_frame(true);
    m_junk = Gtk::make_managed<Gtk::TextView>();
    m_junk->set_name("settings.junk");
    m_junk->set_monospace(true);
    m_junk->set_top_margin(6);
    m_junk->set_left_margin(6);
    scroll->set_child(*m_junk);
    box->append(*scroll);

    // s011: the type table Files home uses -- "*.png -> Pictures", one per line.
    auto home = text("Files home: the type table", "heading");
    home->set_margin_top(14);
    box->append(*home);
    box->append(*text("One per line: a file-name pattern -> a home folder (one or two deep). The first "
                      "line a file fits names its folder; you pick the subfolder on the Files home page.", "dim"));
    auto hscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    hscroll->set_vexpand(true);
    hscroll->set_has_frame(true);
    hscroll->set_min_content_height(150);
    m_home = Gtk::make_managed<Gtk::TextView>();
    m_home->set_name("settings.home");
    m_home->set_monospace(true);
    m_home->set_top_margin(6);
    m_home->set_left_margin(6);
    hscroll->set_child(*m_home);
    box->append(*hscroll);
    auto reset = Gtk::make_managed<Gtk::Button>("Standard table");
    reset->set_name("settings.home.reset");
    reset->set_halign(Gtk::Align::START);
    reset->set_tooltip_text("Put the standard table back in the box (Save keeps it)");
    reset->signal_clicked().connect([this] {
        std::string lines;
        for (const auto& l : core::homing::lines_of(core::homing::defaults())) lines += l + "\n";
        m_home->get_buffer()->set_text(lines);
    });
    box->append(*reset);

    // s013b: the backup media -- the SSD's folder, the Pi's mount.
    auto media = text("Backup media", "heading");
    media->set_margin_top(14);
    box->append(*media);
    box->append(*text("One per line: a name | a folder on the disk (\"Big SSD | /run/media/scott/Big SSD/Tidy\"). "
                      "The Archive page copies ~/Archive there and proves each copy by reading it back. Tidy never "
                      "moves or overwrites anything there. The folder must be on its own disk -- an unplugged "
                      "disk's empty mount point is refused.", "dim"));
    auto mscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    mscroll->set_has_frame(true);
    mscroll->set_min_content_height(60);
    m_media = Gtk::make_managed<Gtk::TextView>();
    m_media->set_name("settings.media");
    m_media->set_monospace(true);
    m_media->set_top_margin(6);
    m_media->set_left_margin(6);
    mscroll->set_child(*m_media);
    box->append(*mscroll);
    auto addm = Gtk::make_managed<Gtk::Button>("Add folder...");
    addm->set_name("settings.media.add");
    addm->set_halign(Gtk::Align::START);
    addm->set_tooltip_text("Pick a folder on a backup disk; it is added as a line (Save keeps it)");
    addm->signal_clicked().connect([this] {
        auto dlg = Gtk::FileDialog::create();
        dlg->set_title("A folder on the backup disk");
        dlg->set_modal(true);
        dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
            try {
                const auto f = dlg->select_folder_finish(res);
                if (!f || f->get_path().empty()) return;
                st::Media m;
                if (!st::media_of(f->get_path(), m)) return;
                auto buf = m_media->get_buffer();
                std::string t = buf->get_text();
                if (!t.empty() && t.back() != '\n') t += "\n";
                buf->set_text(t + st::media_line(m) + "\n");
            } catch (const Glib::Error&) {}   // cancelled
        });
    });
    box->append(*addm);

    // s019a: folders Tidy never looks inside, never moves into or out of.
    auto hands = text("Hands-off folders", "heading");
    hands->set_margin_top(14);
    box->append(*hands);
    box->append(*text("One per line, in your home (\"~/Taxes\"). Tidy never looks inside them and never moves "
                      "anything into or out of them -- with everything inside, all the way down. Hidden folders "
                      "are always left alone.", "dim"));
    auto oscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    oscroll->set_has_frame(true);
    oscroll->set_min_content_height(60);
    m_hands = Gtk::make_managed<Gtk::TextView>();
    m_hands->set_name("settings.hands_off");
    m_hands->set_monospace(true);
    m_hands->set_top_margin(6);
    m_hands->set_left_margin(6);
    oscroll->set_child(*m_hands);
    box->append(*oscroll);
    m_samson = text("", "dim");
    m_samson->set_name("settings.hands_off.samson");
    box->append(*m_samson);
    auto addh = Gtk::make_managed<Gtk::Button>("Add folder...");
    addh->set_name("settings.hands_off.add");
    addh->set_halign(Gtk::Align::START);
    addh->set_tooltip_text("Pick a folder in your home to leave alone; it is added as a line (Save keeps it)");
    addh->signal_clicked().connect([this] {
        auto dlg = Gtk::FileDialog::create();
        dlg->set_title("A folder Tidy leaves alone");
        dlg->set_modal(true);
        dlg->set_initial_folder(Gio::File::create_for_path(m_home_dir));
        dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& res) {
            try {
                const auto f = dlg->select_folder_finish(res);
                if (!f || f->get_path().empty()) return;
                std::string rel;
                if (const auto why = core::protection::rel_of(m_home_dir, f->get_path(), rel); !why.empty()) {
                    m_status->set_text("Hands-off folders: " + why);
                    return;
                }
                auto buf = m_hands->get_buffer();
                std::string t = buf->get_text();
                if (!t.empty() && t.back() != '\n') t += "\n";
                buf->set_text(t + "~/" + rel + "\n");
            } catch (const Glib::Error&) {}   // cancelled
        });
    });
    box->append(*addh);

    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->set_margin_top(8);
    m_status = text("");
    m_status->set_name("settings.status");
    m_status->set_hexpand(true);
    bar->append(*m_status);
    auto cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel->signal_clicked().connect([this] { set_visible(false); });
    bar->append(*cancel);
    auto save = Gtk::make_managed<Gtk::Button>("Save");
    save->set_name("settings.save");
    save->add_css_class("suggested-action");
    save->signal_clicked().connect([this] { on_save_clicked(); });
    bar->append(*save);
    // s013b: the settings grew past a laptop screen -- they scroll; Save stays in view.
    auto outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    auto page = Gtk::make_managed<Gtk::ScrolledWindow>();
    page->set_name("settings.page");
    page->set_vexpand(true);
    page->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    page->set_child(*box);
    outer->append(*page);
    bar->set_margin_start(20);
    bar->set_margin_end(20);
    bar->set_margin_bottom(14);
    outer->append(*bar);
    set_child(*outer);
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::show(const st::Settings& current) {
    m_current = current;   // s012b: what this window does not show (bg minutes, notify at) is kept
    m_days->set_value(current.aging_days);
    m_old->set_value(current.old_years);
    m_rescan->set_value(current.rescan_minutes);
    m_held->set_value(current.held_years);
    m_archive->set_value(current.archive_days);
    m_proofs->set_value(current.proofs_needed);
    std::string lines;
    for (const auto& j : current.junk) lines += j + "\n";
    m_junk->get_buffer()->set_text(lines);
    lines.clear();
    for (const auto& l : core::homing::lines_of(core::homing::table(current))) lines += l + "\n";
    m_home->get_buffer()->set_text(lines);
    lines.clear();
    for (const auto& m : current.media) lines += st::media_line(m) + "\n";
    m_media->get_buffer()->set_text(lines);
    lines.clear();
    for (const auto& h : current.hands_off) lines += "~/" + h + "\n";
    m_hands->get_buffer()->set_text(lines);
    {   // s019a: Samson's own places -- shown, never edited here
        const auto sam = core::protection::samson(m_home_dir, core::protection::config_home(m_home_dir));
        std::string t;
        for (const auto& r : sam) t += (t.empty() ? "" : ", ") + std::string("~/") + r;
        m_samson->set_text(sam.empty() ? "" : "Samson's (always, while Samson is installed): " + t);
        m_samson->set_visible(!sam.empty());
    }
    m_status->set_text("");
    present();
}

void SettingsWindow::on_save_clicked() {
    st::Settings s = m_current;
    s.aging_days = m_days->get_value_as_int();
    s.old_years = m_old->get_value_as_int();
    s.rescan_minutes = m_rescan->get_value_as_int();
    s.held_years = m_held->get_value_as_int();
    s.archive_days = m_archive->get_value_as_int();
    s.proofs_needed = m_proofs->get_value_as_int();
    s.junk = st::patterns_of(m_junk->get_buffer()->get_text());
    // The type table: every line must read, or nothing is saved (says which).
    std::vector<core::homing::TypeRoute> table;
    int n = 0;
    for (const auto& l : st::patterns_of(m_home->get_buffer()->get_text())) {
        ++n;
        core::homing::TypeRoute r;
        if (!core::homing::parse_line(l, r)) {
            const auto arrow = l.find("->");
            std::string why = "write it \"*.png -> Pictures\"";
            if (arrow != std::string::npos) {
                r = {l.substr(0, arrow), l.substr(arrow + 2)};
                for (auto* x : {&r.pattern, &r.folder}) {
                    x->erase(0, x->find_first_not_of(' '));
                    x->erase(x->find_last_not_of(' ') + 1);
                }
                if (auto w = core::homing::route_refusal(r); !w.empty()) why = w;
            }
            m_status->set_text("Type table, line " + std::to_string(n) + ": " + why);
            return;
        }
        table.push_back(r);
    }
    // Still the standard table -> leave it unset, so a later standard table reaches you.
    s.home_set = false;
    s.home.clear();
    if (table != core::homing::defaults()) {
        s.home_set = true;
        s.home = core::homing::lines_of(table);
    }
    // s013b: the media -- every line must read, names unique.
    s.media.clear();
    n = 0;
    for (const auto& l : st::patterns_of(m_media->get_buffer()->get_text())) {
        ++n;
        st::Media m;
        if (!st::media_of(l, m)) {
            m_status->set_text("Backup media, line " + std::to_string(n) + ": write it \"name | /a/folder\"");
            return;
        }
        for (const auto& x : s.media)
            if (x.label == m.label) {
                m_status->set_text("Backup media, line " + std::to_string(n) + ": the name \"" + m.label +
                                   "\" is used twice");
                return;
            }
        s.media.push_back(m);
    }
    // s019a: every hands-off line must be a folder in the home.
    s.hands_off.clear();
    n = 0;
    for (const auto& l : st::patterns_of(m_hands->get_buffer()->get_text())) {
        ++n;
        std::string rel;
        if (const auto why = core::protection::rel_of(m_home_dir, l, rel); !why.empty()) {
            m_status->set_text("Hands-off folders, line " + std::to_string(n) + ": " + why);
            return;
        }
        if (std::find(s.hands_off.begin(), s.hands_off.end(), rel) == s.hands_off.end()) s.hands_off.push_back(rel);
    }
    const std::string err = m_save ? m_save(s) : std::string("nothing to save to");
    if (err.empty()) set_visible(false);
    else m_status->set_text("Not saved: " + err);
}

}  // namespace tidy
