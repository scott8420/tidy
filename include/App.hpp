#pragma once
#include <gtkmm/application.h>
#include <memory>

// App -- the Gtk::Application. Thin: stands up logging in create(), builds the
// Shell on activate. (CANON: thin bootstrap -- main -> App::create() -> Shell.)
namespace tidy {

class Shell;

class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> create();

protected:
    App();
    void on_activate() override;
    void on_startup() override;
    int on_command_line(const Glib::RefPtr<Gio::ApplicationCommandLine>& cl) override;   // s012c: --page / --new-project / --background   // accelerators, driven from core::shortcut_registry()

private:
    Shell* m_shell = nullptr;   // owned by the application via add_window
};

}  // namespace tidy
