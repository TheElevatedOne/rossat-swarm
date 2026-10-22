// rossat-swarm - GTK3 front end.

#include "window.h"

#include "rossat/rossat.h"

#include <cstring>

namespace {

const char *g_start_page = nullptr;
gboolean g_minimized = FALSE;

void on_activate(GtkApplication *app, gpointer)
{
    GList *windows = gtk_application_get_windows(app);
    if (windows) {
        gtk_window_present(GTK_WINDOW(windows->data));
        return;
    }

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css, "/org/rossat/swarm/rossat.css");
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget *win = rossat_window_new(app, g_minimized);
    if (g_start_page)
        rossat_window_show_page(win, g_start_page);
}

} // namespace

int main(int argc, char **argv)
{
    // libayatana-appindicator warns on every start that it is deprecated
    g_log_set_handler("libayatana-appindicator", G_LOG_LEVEL_WARNING,
                      [](const gchar *domain, GLogLevelFlags level, const gchar *msg, gpointer) {
                          if (!std::strstr(msg, "deprecated"))
                              g_log_default_handler(domain, level, msg, nullptr);
                      }, nullptr);
    GtkApplication *app = gtk_application_new("org.rossat.Swarm", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    static gchar *page = nullptr;
    const GOptionEntry entries[] = {
        {"page", 0, 0, G_OPTION_ARG_STRING, &page,
         "Page to open: settings, buttons, illumination or advanced", "PAGE"},
        {"minimized", 'm', 0, G_OPTION_ARG_NONE, &g_minimized,
         "Start hidden in the system tray (enables the tray for this run)", nullptr},
        {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr}};
    g_application_add_main_option_entries(G_APPLICATION(app), entries);
    g_signal_connect(app, "handle-local-options", G_CALLBACK(+[](GApplication *, GVariantDict *, gpointer) -> gint {
        g_start_page = page;
        return -1;
    }), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    rossat_exit();
    return status;
}
