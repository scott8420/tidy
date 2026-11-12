#include "Busy.hpp"
#include "Log.hpp"

#include <glibmm/dispatcher.h>
#include <glibmm/main.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/window.h>

#include <chrono>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <thread>

namespace tidy::busy {

namespace {

struct Job {
    Gtk::Window win;
    Gtk::Label* text = nullptr;
    Gtk::ProgressBar* bar = nullptr;
    std::string base;                             // the line under the title
    std::function<void()> work, done;
    std::function<double()> fraction;
    std::function<std::string()> detail;          // "120 of 4,000 steps"; "" = seconds only
    std::thread thread;
    Glib::Dispatcher finished;
    sigc::connection tick;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
};

void tick(Job* j) {
    const double f = j->fraction ? j->fraction() : -1.0;
    if (f < 0) j->bar->pulse();
    else j->bar->set_fraction(f);
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - j->t0).count();
    std::string d = j->detail ? j->detail() : "";
    j->text->set_text(j->base + "\n" + (d.empty() ? "" : d + " -- ") + std::to_string(secs) + " s");
}

void launch(Job* j, Gtk::Window& parent, const std::string& title) {
    j->win.set_title(title);
    j->win.set_transient_for(parent);
    j->win.set_modal(true);
    j->win.set_deletable(false);                  // never closed mid-run
    j->win.set_resizable(false);
    j->win.set_default_size(420, -1);
    j->win.set_name("busy.window");
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_margin(20);
    auto head = Gtk::make_managed<Gtk::Label>(title);
    head->set_xalign(0);
    head->add_css_class("heading");
    box->append(*head);
    j->text = Gtk::make_managed<Gtk::Label>(j->base);
    j->text->set_xalign(0);
    j->text->set_wrap(true);
    j->text->add_css_class("dim");
    j->text->set_name("busy.text");
    box->append(*j->text);
    j->bar = Gtk::make_managed<Gtk::ProgressBar>();
    j->bar->set_name("busy.bar");
    j->bar->set_pulse_step(0.08);
    box->append(*j->bar);
    j->win.set_child(*box);
    j->win.present();

    j->finished.connect([j] {
        j->tick.disconnect();
        if (j->thread.joinable()) j->thread.join();
        j->win.set_visible(false);
        if (j->done) j->done();
        Glib::signal_idle().connect_once([j] { delete j; });   // never inside its own dispatcher
    });
    j->tick = Glib::signal_timeout().connect([j] { tick(j); return true; }, 150);
    tick(j);
    j->thread = std::thread([j] {
        if (j->work) j->work();
        j->finished.emit();
    });
}

}  // namespace

void start(Gtk::Window& parent, const std::string& title, const std::string& text, std::function<void()> work,
           std::function<void()> done, std::function<double()> fraction) {
    auto* j = new Job;
    j->base = text;
    j->work = std::move(work);
    j->done = std::move(done);
    j->fraction = std::move(fraction);
    launch(j, parent, title);
}

void door(Gtk::Window& parent, latch::door::Door& d, latch::door::Plan plan, const std::string& title,
          std::function<void(const latch::door::Result&)> done, std::function<void(const latch::door::Result&)> after,
          std::function<void(latch::door::Plan&)> before) {
    struct Watch {                                // the manifest as it grows -- read on the window's thread
        std::string path;
        std::uintmax_t offset = 0;
        std::size_t lines = 0;
        void read() {   // read-only: open(O_RDONLY) + pread from where it was
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) return;
            char buf[65536];
            for (;;) {
                const ssize_t n = ::pread(fd, buf, sizeof buf, static_cast<off_t>(offset));
                if (n <= 0) break;
                for (ssize_t i = 0; i < n; ++i) lines += buf[i] == '\n';
                offset += static_cast<std::uintmax_t>(n);
            }
            ::close(fd);
        }
    };
    auto w = std::make_shared<Watch>();
    w->path = d.manifest_path();
    std::error_code ec;
    w->offset = std::filesystem::exists(w->path, ec) ? std::filesystem::file_size(w->path, ec) : 0;
    const std::size_t steps = plan.actions.size();
    auto result = std::make_shared<latch::door::Result>();
    auto* j = new Job;
    j->base = plan.label;
    j->fraction = [w, steps] { w->read(); return door_fraction(w->lines, steps); };
    j->detail = [w, steps] {
        const std::size_t n = w->lines <= 1 ? 0 : std::min(steps, (w->lines - 1) / 2);
        return std::to_string(n) + " of " + std::to_string(steps) + (steps == 1 ? " step" : " steps");
    };
    j->work = [&d, plan = std::move(plan), result, after = std::move(after), before = std::move(before)]() mutable {
        const auto t0 = std::chrono::steady_clock::now();
        if (before) before(plan);
        *result = d.run(plan);
        if (after) after(*result);
        if (auto lg = log::get(log::Area::Io))
            lg->info("busy: {} -- {} steps, {} done, {} ms", plan.label, plan.actions.size(), result->done,
                     std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    };
    j->done = [result, done = std::move(done)] { if (done) done(*result); };
    launch(j, parent, title);
}

}  // namespace tidy::busy
