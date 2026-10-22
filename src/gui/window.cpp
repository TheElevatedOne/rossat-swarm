// rossat-swarm GTK3 main window, laid out after ROCCAT Swarm's Kone AIMO
// module: header, tab pages (Settings / Button assignment / Illumination /
// Advanced settings) and a profile bar with Apply/Reload in the footer.

#include "window.h"

#include "rossat/rossat.h"

#ifdef ROSSAT_HAVE_TRAY
#include <libayatana-appindicator/app-indicator.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr int HALF = ROSSAT_BUTTON_COUNT / 2;

struct dpi_row {
    GtkWidget *enabled;
    GtkWidget *active;
    GtkWidget *x;
    GtkWidget *y;
};

struct button_row {
    GtkWidget *combo;
    GtkWidget *p1;
    GtkWidget *p2;
    GtkWidget *text; // macro text entry
};

struct app_state {
    GtkWidget *window = nullptr;
    GtkWidget *device_label = nullptr;
    GtkWidget *status_label = nullptr;
    GtkWidget *notebook = nullptr;
    GtkWidget *profile_buttons[ROSSAT_PROFILE_COUNT] = {};
    GtkWidget *apply_button = nullptr;
    GtkWidget *activate_button = nullptr;
    GtkWidget *reload_button = nullptr;

    // settings page
    dpi_row dpi[ROSSAT_DPI_LEVELS] = {};
    GtkWidget *dpi_lock = nullptr;
    GtkWidget *polling[4] = {};

    // buttons page
    button_row buttons[ROSSAT_BUTTON_COUNT] = {};

    // illumination page
    GtkWidget *effect = nullptr;
    GtkWidget *speed = nullptr;
    GtkWidget *brightness = nullptr;
    GtkWidget *led_color[ROSSAT_LED_COUNT] = {};
    GtkWidget *led_enable[ROSSAT_LED_COUNT] = {};
    GtkWidget *group_color = nullptr;
    GtkWidget *theme = nullptr;
    GtkWidget *render_engine = nullptr;
    GtkWidget *shift_enable = nullptr;
    GtkWidget *shift_color = nullptr;
    GtkWidget *shift_style = nullptr;
    GtkWidget *shift_hint = nullptr;
    GtkWidget *theme_swatches[3] = {};
    GtkWidget *engine_label = nullptr;
    bool engine_busy = false;
    int engine_retry = 0;
    guint engine_source = 0;
    GtkWidget *colors_panel = nullptr;
    rossat_engine *engine = nullptr;
    guint preview_source = 0;
    double preview_t = 0;
    GtkWidget *mouse_light = nullptr;
    GtkWidget *mouse_buttons = nullptr;
    int highlighted_button = -1;

    // advanced page
    GtkWidget *angle_snapping = nullptr;
    GtkWidget *debounce = nullptr;
    GtkWidget *raw_view = nullptr;
    GtkWidget *tdcu_label = nullptr;
    GtkWidget *calib_button = nullptr;

    // application preferences (~/.config/rossat/gui.ini)
    GtkWidget *pref_tray = nullptr;
    GtkWidget *pref_start_min = nullptr;
    GtkWidget *dpi_panel = nullptr;
    GtkWidget *polling_panel = nullptr;
    bool tray_enabled = false;     // effective for this run (pref or --minimized)
    GtkWidget *auto_apply = nullptr;
    guint auto_apply_source = 0;
    bool quitting = false;
#ifdef ROSSAT_HAVE_TRAY
    AppIndicator *indicator = nullptr;
    GtkWidget *tray_show_item = nullptr;
#endif

    rossat_device *dev = nullptr;
    rossat_device_info info{};
    int edit_profile = 0;
    int active_profile = 0;
    bool loaded = false;
    bool updating = false; // suppress signal feedback while filling widgets
    rossat_settings settings{};
    rossat_buttons button_data{};
    guint hotplug_source = 0;
};

/* ---- small helpers ---------------------------------------------------- */

GtkWidget *label(const char *text, const char *cls = nullptr, float xalign = 0.0f)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cls)
        gtk_style_context_add_class(gtk_widget_get_style_context(l), cls);
    return l;
}

void add_class(GtkWidget *w, const char *cls)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cls);
}

GtkWidget *box_panel(const char *title, GtkWidget *content)
{
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    add_class(outer, "rs-box");
    gtk_box_pack_start(GTK_BOX(outer), label(title, "rs-box-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), content, TRUE, TRUE, 0);
    return outer;
}

void set_status(app_state *s, const char *text, const char *cls)
{
    gtk_label_set_text(GTK_LABEL(s->status_label), text);
    GtkStyleContext *ctx = gtk_widget_get_style_context(s->status_label);
    gtk_style_context_remove_class(ctx, "connected");
    gtk_style_context_remove_class(ctx, "error");
    if (cls)
        gtk_style_context_add_class(ctx, cls);
}

void report_error(app_state *s, const char *what, int err)
{
    char msg[256];
    std::snprintf(msg, sizeof msg, "%s: %s", what, rossat_strerror(err));
    set_status(s, msg, "error");
}

GdkRGBA to_rgba(const rossat_color &c)
{
    return GdkRGBA{c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0};
}

rossat_color from_rgba(const GdkRGBA &c)
{
    auto cv = [](double v) { return static_cast<uint8_t>(std::lround(std::fmin(std::fmax(v, 0.0), 1.0) * 255)); };
    return rossat_color{cv(c.alpha), cv(c.red), cv(c.green), cv(c.blue)};
}

/* ---- application preferences ----------------------------------------- */

struct gui_prefs {
    bool tray = false;
    bool start_minimized = false;
    bool auto_apply = false;
};

std::string prefs_path()
{
    return std::string(g_get_user_config_dir()) + "/rossat/gui.ini";
}

gui_prefs load_prefs()
{
    gui_prefs p;
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, prefs_path().c_str(), G_KEY_FILE_NONE, nullptr)) {
        p.tray = g_key_file_get_boolean(kf, "gui", "tray", nullptr);
        p.start_minimized = g_key_file_get_boolean(kf, "gui", "start_minimized", nullptr);
        p.auto_apply = g_key_file_get_boolean(kf, "gui", "auto_apply", nullptr);
    }
    g_key_file_unref(kf);
    return p;
}

void save_prefs(const gui_prefs &p)
{
    GKeyFile *kf = g_key_file_new();
    g_key_file_load_from_file(kf, prefs_path().c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);
    g_key_file_set_boolean(kf, "gui", "tray", p.tray);
    g_key_file_set_boolean(kf, "gui", "start_minimized", p.start_minimized);
    g_key_file_remove_key(kf, "gui", "engine", nullptr); // pre-1.0.1 setting
    g_key_file_set_boolean(kf, "gui", "auto_apply", p.auto_apply);
    std::string dir = std::string(g_get_user_config_dir()) + "/rossat";
    g_mkdir_with_parents(dir.c_str(), 0755);
    g_key_file_save_to_file(kf, prefs_path().c_str(), nullptr);
    g_key_file_unref(kf);
}

void tray_sync(app_state *s);
void store_prefs(app_state *s);
void schedule_auto_apply(app_state *s);

// defined further down, used by the drawing and fill code
uint8_t current_effect(app_state *s);
void preview_settings(app_state *s, rossat_settings *out);
void update_effect_ui(app_state *s);
void update_tdcu_label(app_state *s);
int combo_type(GtkWidget *combo);
bool engine_rendered(app_state *s);

/* ---- mouse drawing ------------------------------------------------------ */

// Shape of a Kone-style mouse seen from the top, in a 200x300 box.
void mouse_path(cairo_t *cr)
{
    cairo_new_path(cr);
    cairo_move_to(cr, 100, 6);
    cairo_curve_to(cr, 150, 6, 176, 40, 180, 95);
    cairo_curve_to(cr, 184, 140, 196, 170, 192, 215);
    cairo_curve_to(cr, 188, 268, 150, 296, 100, 296);
    cairo_curve_to(cr, 50, 296, 12, 268, 8, 215);
    cairo_curve_to(cr, 4, 170, 16, 140, 20, 95);
    cairo_curve_to(cr, 24, 40, 50, 6, 100, 6);
    cairo_close_path(cr);
}

void set_led_source(cairo_t *cr, const rossat_color &c, bool on, double extra_alpha = 1.0)
{
    if (!on) {
        cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 1.0);
        return;
    }
    cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0,
                          (0.25 + 0.75 * c.a / 255.0) * extra_alpha);
}

struct led_shape {
    double x, y, w, h;
};

// LED zones, see rossat_led_name(): wheel, left/right front, two bars.
constexpr led_shape LED_SHAPES[ROSSAT_LED_COUNT] = {
    {92, 52, 16, 46},   // 0 wheel
    {28, 70, 10, 30},   // 1 left front
    {20, 108, 10, 26},  // 2 left bar
    {18, 140, 10, 26},  // 3
    {18, 172, 10, 26},  // 4
    {162, 70, 10, 30},  // 5 right front
    {170, 108, 10, 26}, // 6 right bar
    {172, 140, 10, 26}, // 7
    {172, 172, 10, 26}, // 8
    {20, 204, 10, 26},  // 9 left bar
    {170, 204, 10, 26}, // 10 right bar
};

void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

void draw_mouse_body(cairo_t *cr)
{
    cairo_pattern_t *body = cairo_pattern_create_linear(0, 0, 200, 300);
    cairo_pattern_add_color_stop_rgb(body, 0, 0.23, 0.23, 0.23);
    cairo_pattern_add_color_stop_rgb(body, 1, 0.08, 0.08, 0.08);
    mouse_path(cr);
    cairo_set_source(cr, body);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(body);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);

    // button split and thumb area
    cairo_set_source_rgba(cr, 0, 0, 0, 0.8);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, 100, 8);
    cairo_line_to(cr, 100, 46);
    cairo_move_to(cr, 100, 104);
    cairo_line_to(cr, 100, 128);
    cairo_move_to(cr, 24, 120);
    cairo_curve_to(cr, 60, 130, 140, 130, 176, 120);
    cairo_stroke(cr);

    // DPI buttons behind the wheel
    for (int i = 0; i < 2; ++i) {
        rounded_rect(cr, 92, 106 + i * 12, 16, 10, 2);
        cairo_set_source_rgb(cr, 0.16, 0.16, 0.16);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_stroke(cr);
    }
}

void scale_to_widget(GtkWidget *w, cairo_t *cr)
{
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    double s = std::fmin(W / 210.0, H / 310.0);
    cairo_translate(cr, (W - 200 * s) / 2, (H - 300 * s) / 2);
    cairo_scale(cr, s, s);
}

gboolean on_draw_lighting(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    scale_to_widget(w, cr);
    draw_mouse_body(cr);
    rossat_color frame[ROSSAT_LED_COUNT];
    uint8_t effect = current_effect(s);
    bool software = engine_rendered(s);
    if (software) {
        rossat_settings ps;
        preview_settings(s, &ps);
        ps.brightness = 255; // preview shape of the effect, brightness is applied by alpha below
        rossat_render_frame(&ps, effect, s->preview_t, 0.0, frame);
    }
    double level = gtk_range_get_value(GTK_RANGE(s->brightness)) / 255.0;
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        const led_shape &l = LED_SHAPES[i];
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->led_color[i]), &rgba);
        rossat_color c = software ? frame[i] : from_rgba(rgba);
        c.a = static_cast<uint8_t>(c.a * (0.15 + 0.85 * level));
        bool on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->led_enable[i])) && effect != ROSSAT_EFFECT_OFF;
        // glow
        if (on) {
            cairo_pattern_t *glow = cairo_pattern_create_radial(l.x + l.w / 2, l.y + l.h / 2, 1,
                                                                l.x + l.w / 2, l.y + l.h / 2, l.h);
            cairo_pattern_add_color_stop_rgba(glow, 0, c.r / 255.0, c.g / 255.0, c.b / 255.0, 0.45 * c.a / 255.0);
            cairo_pattern_add_color_stop_rgba(glow, 1, c.r / 255.0, c.g / 255.0, c.b / 255.0, 0);
            cairo_set_source(cr, glow);
            cairo_arc(cr, l.x + l.w / 2, l.y + l.h / 2, l.h, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_pattern_destroy(glow);
        }
        rounded_rect(cr, l.x, l.y, l.w, l.h, 3);
        set_led_source(cr, c, on);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.9);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        char num[4];
        std::snprintf(num, sizeof num, "%d", i + 1);
        cairo_set_font_size(cr, 8);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
        double tx = i == 0 ? l.x + 4 : (l.x < 100 ? l.x - 12 : l.x + l.w + 3);
        cairo_move_to(cr, tx, l.y + l.h / 2 + 3);
        cairo_show_text(cr, num);
    }
    return FALSE;
}

// Anchor points for the physical buttons, used to highlight a selection.
constexpr double BUTTON_ANCHORS[HALF][2] = {
    {55, 45},   {145, 45},  {100, 75},  {84, 75},  {116, 75}, {100, 56},
    {100, 94},  {14, 150},  {14, 185},  {14, 118}, {100, 111}, {100, 123},
};

gboolean on_draw_buttons(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    scale_to_widget(w, cr);
    draw_mouse_body(cr);
    rounded_rect(cr, 92, 52, 16, 46, 6);
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
    cairo_fill(cr);
    // thumb buttons
    for (int i = 0; i < 3; ++i) {
        rounded_rect(cr, 6, 108 + i * 34, 10, 24, 3);
        cairo_set_source_rgb(cr, 0.14, 0.14, 0.14);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_stroke(cr);
    }
    for (int i = 0; i < HALF; ++i) {
        bool hl = s->highlighted_button == i;
        cairo_arc(cr, BUTTON_ANCHORS[i][0], BUTTON_ANCHORS[i][1], hl ? 8 : 6, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.40, 0.76, 0.97, hl ? 0.95 : 0.55);
        cairo_fill(cr);
        char num[4];
        std::snprintf(num, sizeof num, "%d", i + 1);
        cairo_set_font_size(cr, 7);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, num, &ext);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_move_to(cr, BUTTON_ANCHORS[i][0] - ext.width / 2 - ext.x_bearing,
                      BUTTON_ANCHORS[i][1] + ext.height / 2);
        cairo_show_text(cr, num);
    }
    return FALSE;
}

/* ---- widgets -> data -------------------------------------------------- */

int combo_type(GtkWidget *combo)
{
    const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
    return id ? static_cast<int>(std::strtol(id, nullptr, 0)) : -1;
}

void collect(app_state *s)
{
    rossat_settings &st = s->settings;
    st.profile = static_cast<uint8_t>(s->edit_profile);
    st.dpi_enabled_mask = 0;
    for (int i = 0; i < ROSSAT_DPI_LEVELS; ++i) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->dpi[i].enabled)))
            st.dpi_enabled_mask |= static_cast<uint8_t>(1 << i);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->dpi[i].active)))
            st.dpi_active = static_cast<uint8_t>(i);
        st.dpi_x[i] = static_cast<uint16_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->dpi[i].x)));
        st.dpi_y[i] = static_cast<uint16_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->dpi[i].y)));
    }
    st.dpi_enabled_mask |= static_cast<uint8_t>(1 << st.dpi_active);
    for (int i = 0; i < 4; ++i)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->polling[i])))
            st.polling_rate = static_cast<uint8_t>(i);

    int eff = combo_type(s->effect);
    if (eff >= 0)
        rossat_select_effect(s->info.product_id, &st, static_cast<uint8_t>(eff),
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->render_engine)));
    rossat_shift_lighting sl;
    sl.enabled = gtk_switch_get_active(GTK_SWITCH(s->shift_enable));
    sl.style = static_cast<uint8_t>(gtk_combo_box_get_active(GTK_COMBO_BOX(s->shift_style)) == 1 ? ROSSAT_SHIFT_PULSE
                                                                                                 : ROSSAT_SHIFT_SOLID);
    GdkRGBA srgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->shift_color), &srgba);
    sl.color = from_rgba(srgba);
    rossat_shift_lighting_save(s->info.product_id, st.profile, &sl);
    st.effect_speed = static_cast<uint8_t>(gtk_range_get_value(GTK_RANGE(s->speed)));
    int th = combo_type(s->theme);
    if (th >= 0 && (th != (st.color_theme & 0x7f) % ROSSAT_AIMO_THEMES || !(st.color_theme & 0x80)))
        st.color_theme = static_cast<uint8_t>(0x80 | th);
    st.brightness = static_cast<uint8_t>(gtk_range_get_value(GTK_RANGE(s->brightness)));
    st.led_enable_mask = 0;
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->led_color[i]), &rgba);
        st.leds[i] = from_rgba(rgba);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->led_enable[i])))
            st.led_enable_mask |= static_cast<uint16_t>(1 << i);
    }

    st.angle_snapping = gtk_switch_get_active(GTK_SWITCH(s->angle_snapping)) ? 1 : 0;
    st.debounce_ms = static_cast<uint8_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->debounce)));

    rossat_buttons &b = s->button_data;
    b.profile = static_cast<uint8_t>(s->edit_profile);
    for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i) {
        int t = combo_type(s->buttons[i].combo);
        if (t >= 0)
            b.buttons[i].type = static_cast<uint8_t>(t);
        b.buttons[i].param1 = static_cast<uint8_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->buttons[i].p1)));
        b.buttons[i].param2 = static_cast<uint8_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s->buttons[i].p2)));
        if (t == ROSSAT_BT_SHORTCUT) {
            uint8_t mods = 0, key = 0;
            if (rossat_parse_shortcut(gtk_entry_get_text(GTK_ENTRY(s->buttons[i].text)), &mods, &key) == ROSSAT_OK) {
                b.buttons[i].param1 = mods;
                b.buttons[i].param2 = key;
            }
        } else if (t != ROSSAT_BT_MACRO) {
            b.buttons[i].param1 = b.buttons[i].param2 = 0;
        }
    }
}

/* ---- data -> widgets -------------------------------------------------- */

void ensure_combo_id(GtkWidget *combo, int value)
{
    char id[8];
    std::snprintf(id, sizeof id, "%d", value);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), id)) {
        char text[32];
        std::snprintf(text, sizeof text, "Unknown (0x%02x)", value);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), id, text);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), id);
    }
}

void update_button_row_sensitivity(app_state *s, int i)
{
    int t = combo_type(s->buttons[i].combo);
    bool shortcut = t == ROSSAT_BT_SHORTCUT;
    GtkEntry *e = GTK_ENTRY(s->buttons[i].text);
    gtk_entry_set_placeholder_text(e, shortcut ? "e.g. ctrl+c" : "macro text (optional)");
    gtk_widget_set_tooltip_text(s->buttons[i].text,
                                shortcut ? "Key combination: ctrl, shift, alt, win + a key (a-z, 0-9, f1-f24, enter, esc, tab, space, up, ...)"
                                         : "Typed as a keystroke macro when you press APPLY");
    gtk_widget_set_visible(s->buttons[i].text, shortcut || t == ROSSAT_BT_MACRO);
}

void update_raw_view(app_state *s)
{
    std::string text;
    char line[96];
    const uint8_t *raw = s->settings.raw;
    for (int i = 0; i < ROSSAT_SETTINGS_SIZE; i += 16) {
        int n = std::snprintf(line, sizeof line, "%04x:", i);
        for (int j = i; j < i + 16 && j < ROSSAT_SETTINGS_SIZE; ++j)
            n += std::snprintf(line + n, sizeof line - n, " %02x", raw[j]);
        text += line;
        text += '\n';
    }
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->raw_view)), text.c_str(), -1);
}

void fill(app_state *s)
{
    s->updating = true;
    const rossat_settings &st = s->settings;
    for (int i = 0; i < ROSSAT_DPI_LEVELS; ++i) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->dpi[i].enabled), (st.dpi_enabled_mask >> i) & 1);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->dpi[i].active), st.dpi_active == i);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->dpi[i].x), st.dpi_x[i]);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->dpi[i].y), st.dpi_y[i]);
    }
    bool same = true;
    for (int i = 0; i < ROSSAT_DPI_LEVELS; ++i)
        same = same && st.dpi_x[i] == st.dpi_y[i];
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->dpi_lock), same);
    if (st.polling_rate < 4)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->polling[st.polling_rate]), TRUE);

    uint8_t eff = 0;
    int by_engine = 0;
    rossat_effective_effect(s->info.product_id, &st, &eff, &by_engine);
    ensure_combo_id(s->effect, eff);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->render_engine), by_engine);
    rossat_shift_lighting sl;
    rossat_shift_lighting_load(s->info.product_id, st.profile, &sl);
    gtk_switch_set_active(GTK_SWITCH(s->shift_enable), sl.enabled);
    gtk_combo_box_set_active(GTK_COMBO_BOX(s->shift_style), sl.style == ROSSAT_SHIFT_PULSE ? 1 : 0);
    GdkRGBA srgba = to_rgba(sl.color);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->shift_color), &srgba);
    gtk_range_set_value(GTK_RANGE(s->speed), st.effect_speed);
    char thid[8];
    std::snprintf(thid, sizeof thid, "%d", (st.color_theme & 0x7f) % ROSSAT_AIMO_THEMES);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(s->theme), thid);
    gtk_range_set_value(GTK_RANGE(s->brightness), st.brightness);
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        GdkRGBA rgba = to_rgba(st.leds[i]);
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->led_color[i]), &rgba);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->led_enable[i]), (st.led_enable_mask >> i) & 1);
    }

    gtk_switch_set_active(GTK_SWITCH(s->angle_snapping), st.angle_snapping != 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->debounce), st.debounce_ms);

    for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i) {
        const rossat_button &b = s->button_data.buttons[i];
        ensure_combo_id(s->buttons[i].combo, b.type);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->buttons[i].p1), b.param1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->buttons[i].p2), b.param2);
        char sc[64] = "";
        if (b.type == ROSSAT_BT_SHORTCUT)
            rossat_format_shortcut(b.param1, b.param2, sc, sizeof sc);
        gtk_entry_set_text(GTK_ENTRY(s->buttons[i].text), sc);
        update_button_row_sensitivity(s, i);
    }
    update_raw_view(s);
    s->updating = false;
    update_effect_ui(s);
    gtk_widget_queue_draw(s->mouse_light);
}

void set_controls_sensitive(app_state *s, bool on)
{
    // every page needs the mouse except the Application panel on page 1
    for (int i = 1; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(s->notebook)); ++i)
        gtk_widget_set_sensitive(gtk_notebook_get_nth_page(GTK_NOTEBOOK(s->notebook), i), on);
    gtk_widget_set_sensitive(s->dpi_panel, on);
    gtk_widget_set_sensitive(s->polling_panel, on);
    gtk_widget_set_sensitive(s->apply_button,
                             on && !(s->auto_apply && gtk_switch_get_active(GTK_SWITCH(s->auto_apply))));
    gtk_widget_set_sensitive(s->activate_button, on);
    for (GtkWidget *b : s->profile_buttons)
        gtk_widget_set_sensitive(b, on);
}

void update_profile_buttons(app_state *s)
{
    s->updating = true;
    for (int i = 0; i < ROSSAT_PROFILE_COUNT; ++i) {
        char text[32];
        std::snprintf(text, sizeof text, i == s->active_profile ? "%d ●" : "%d", i + 1);
        gtk_button_set_label(GTK_BUTTON(s->profile_buttons[i]), text);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->profile_buttons[i]), i == s->edit_profile);
    }
    s->updating = false;
}

/* ---- device handling ------------------------------------------------- */

void disconnect(app_state *s, const char *why);

bool load_profile(app_state *s)
{
    if (!s->dev)
        return false;
    GdkWindow *gw = gtk_widget_get_window(s->window);
    GdkCursor *busy = gw ? gdk_cursor_new_from_name(gdk_window_get_display(gw), "wait") : nullptr;
    if (gw)
        gdk_window_set_cursor(gw, busy);
    while (gtk_events_pending())
        gtk_main_iteration();

    int r = rossat_read_settings(s->dev, static_cast<uint8_t>(s->edit_profile), &s->settings);
    if (r == ROSSAT_OK)
        r = rossat_read_buttons(s->dev, static_cast<uint8_t>(s->edit_profile), &s->button_data);

    if (gw)
        gdk_window_set_cursor(gw, nullptr);
    if (busy)
        g_object_unref(busy);

    if (r != ROSSAT_OK) {
        if (r == ROSSAT_ERR_IO)
            disconnect(s, "Device disconnected");
        else
            report_error(s, "Reading profile failed", r);
        return false;
    }
    s->loaded = true;
    fill(s);
    update_tdcu_label(s);
    char msg[96];
    std::snprintf(msg, sizeof msg, "Connected — editing profile %d", s->edit_profile + 1);
    set_status(s, msg, "connected");
    return true;
}

gboolean hotplug_tick(gpointer data);

void connect_device(app_state *s)
{
    if (s->dev)
        return;
    int r = rossat_open(nullptr, &s->dev);
    if (r != ROSSAT_OK) {
        s->dev = nullptr;
        gtk_label_set_text(GTK_LABEL(s->device_label), "NO DEVICE");
        set_status(s, r == ROSSAT_ERR_NO_DEVICE ? "Plug in a ROCCAT Kone AIMO Remastered"
                                                : rossat_strerror(r),
                   r == ROSSAT_ERR_NO_DEVICE ? nullptr : "error");
        set_controls_sensitive(s, false);
        if (!s->hotplug_source)
            s->hotplug_source = g_timeout_add_seconds(2, hotplug_tick, s);
        return;
    }
    rossat_get_info(s->dev, &s->info);
    char fw[16], title[128];
    rossat_get_firmware_version(s->dev, fw, sizeof fw);
    std::snprintf(title, sizeof title, "%s   ·   FW %s", s->info.name, fw);
    gtk_label_set_text(GTK_LABEL(s->device_label), title);

    uint8_t active = 0;
    if (rossat_get_active_profile(s->dev, &active, nullptr) == ROSSAT_OK && active < ROSSAT_PROFILE_COUNT)
        s->active_profile = active;
    s->edit_profile = s->active_profile;
    update_profile_buttons(s);
    set_controls_sensitive(s, true);
    if (s->hotplug_source) {
        g_source_remove(s->hotplug_source);
        s->hotplug_source = 0;
    }
    load_profile(s);
}

void disconnect(app_state *s, const char *why)
{
    if (s->dev)
        rossat_close(s->dev);
    s->dev = nullptr;
    s->loaded = false;
    gtk_label_set_text(GTK_LABEL(s->device_label), "NO DEVICE");
    set_status(s, why, "error");
    set_controls_sensitive(s, false);
    if (!s->hotplug_source)
        s->hotplug_source = g_timeout_add_seconds(2, hotplug_tick, s);
}

gboolean hotplug_tick(gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->dev) {
        s->hotplug_source = 0;
        return G_SOURCE_REMOVE;
    }
    if (rossat_enumerate(nullptr, 0) > 0) {
        s->hotplug_source = 0;
        connect_device(s);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void write_text_macros(app_state *s)
{
    for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i) {
        if (combo_type(s->buttons[i].combo) != ROSSAT_BT_MACRO)
            continue;
        const char *text = gtk_entry_get_text(GTK_ENTRY(s->buttons[i].text));
        if (!text || !*text)
            continue; // keep whatever macro is stored
        static rossat_macro m;
        std::memset(&m, 0, sizeof m);
        m.profile = static_cast<uint8_t>(s->edit_profile);
        m.button = static_cast<uint8_t>(i);
        m.loop = 1;
        std::snprintf(m.set_name, sizeof m.set_name, "rossat");
        std::snprintf(m.name, sizeof m.name, "button %d", i + 1);
        for (const char *p = text; *p && m.count + 4 <= ROSSAT_MACRO_KEYSTROKES; ++p) {
            uint8_t usage = 0;
            bool shift = false;
            char ch = *p;
            if (ch >= 'a' && ch <= 'z') usage = static_cast<uint8_t>(0x04 + ch - 'a');
            else if (ch >= 'A' && ch <= 'Z') { usage = static_cast<uint8_t>(0x04 + ch - 'A'); shift = true; }
            else if (ch >= '1' && ch <= '9') usage = static_cast<uint8_t>(0x1e + ch - '1');
            else if (ch == '0') usage = 0x27;
            else if (ch == ' ') usage = 0x2c;
            else if (ch == '.') usage = 0x37;
            else if (ch == ',') usage = 0x36;
            else if (ch == '-') usage = 0x2d;
            else continue;
            if (shift) m.keys[m.count++] = {0xe1, ROSSAT_KEY_PRESS, 20};
            m.keys[m.count++] = {usage, ROSSAT_KEY_PRESS, 20};
            m.keys[m.count++] = {usage, ROSSAT_KEY_RELEASE, 20};
            if (shift) m.keys[m.count++] = {0xe1, ROSSAT_KEY_RELEASE, 20};
        }
        int r = rossat_write_macro(s->dev, &m);
        if (r != ROSSAT_OK)
            report_error(s, "Writing macro failed", r);
    }
}

void on_apply(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev || !s->loaded)
        return;
    collect(s);
    set_status(s, "Writing to device…", nullptr);
    while (gtk_events_pending())
        gtk_main_iteration();
    int r = rossat_write_settings(s->dev, &s->settings);
    if (r == ROSSAT_OK)
        r = rossat_write_buttons(s->dev, &s->button_data);
    if (r != ROSSAT_OK) {
        if (r == ROSSAT_ERR_IO)
            disconnect(s, "Device disconnected");
        else
            report_error(s, "Writing failed", r);
        return;
    }
    write_text_macros(s);
    if (s->engine)
        rossat_engine_reload(s->engine);
    char msg[64];
    std::snprintf(msg, sizeof msg, "Profile %d saved to the mouse", s->edit_profile + 1);
    set_status(s, msg, "connected");
    // re-read so the raw view and unknown bytes reflect the device state
    load_profile(s);
}

void on_reload(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev)
        connect_device(s);
    else
        load_profile(s);
}

void on_activate_profile(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev)
        return;
    int r = rossat_set_active_profile(s->dev, static_cast<uint8_t>(s->edit_profile));
    if (r != ROSSAT_OK) {
        report_error(s, "Switching profile failed", r);
        return;
    }
    s->active_profile = s->edit_profile;
    update_profile_buttons(s);
    if (s->engine)
        rossat_engine_reload(s->engine);
    char msg[64];
    std::snprintf(msg, sizeof msg, "Profile %d is now active", s->active_profile + 1);
    set_status(s, msg, "connected");
}

void on_profile_toggled(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    if (!gtk_toggle_button_get_active(b)) {
        // keep exactly one profile selected
        int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "profile"));
        if (idx == s->edit_profile) {
            s->updating = true;
            gtk_toggle_button_set_active(b, TRUE);
            s->updating = false;
        }
        return;
    }
    s->edit_profile = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "profile"));
    update_profile_buttons(s);
    load_profile(s);
}

/* ---- page builders ---------------------------------------------------- */

void on_dpi_x_changed(GtkSpinButton *spin, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->dpi_lock)))
        return;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "level"));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->dpi[idx].y), gtk_spin_button_get_value(spin));
}

void on_dpi_lock_toggled(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    bool locked = gtk_toggle_button_get_active(b);
    for (auto &row : s->dpi) {
        gtk_widget_set_sensitive(row.y, !locked);
        if (locked && !s->updating)
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(row.y), gtk_spin_button_get_value(GTK_SPIN_BUTTON(row.x)));
    }
}

GtkWidget *dpi_spin(void)
{
    GtkWidget *sp = gtk_spin_button_new_with_range(ROSSAT_DPI_MIN, ROSSAT_DPI_MAX, ROSSAT_DPI_STEP);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(sp), TRUE);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(sp), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(sp), 6);
    return sp;
}

GtkWidget *build_settings_page(app_state *s)
{
    GtkWidget *page = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(page), 14);
    gtk_grid_set_row_spacing(GTK_GRID(page), 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 16);

    // DPI
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    const char *heads[] = {"LEVEL", "ON", "ACTIVE", "DPI X", "DPI Y"};
    for (int c = 0; c < 5; ++c)
        gtk_grid_attach(GTK_GRID(grid), label(heads[c], "rs-hint"), c, 0, 1, 1);
    GSList *group = nullptr;
    for (int i = 0; i < ROSSAT_DPI_LEVELS; ++i) {
        char t[8];
        std::snprintf(t, sizeof t, "%d", i + 1);
        dpi_row &r = s->dpi[i];
        r.enabled = gtk_check_button_new();
        r.active = gtk_radio_button_new(group);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(r.active));
        r.x = dpi_spin();
        r.y = dpi_spin();
        g_object_set_data(G_OBJECT(r.x), "level", GINT_TO_POINTER(i));
        g_signal_connect(r.x, "value-changed", G_CALLBACK(on_dpi_x_changed), s);
        gtk_grid_attach(GTK_GRID(grid), label(t, "rs-value"), 0, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), r.enabled, 1, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), r.active, 2, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), r.x, 3, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), r.y, 4, i + 1, 1, 1);
    }
    s->dpi_lock = gtk_check_button_new_with_label("Same DPI for X and Y");
    g_signal_connect(s->dpi_lock, "toggled", G_CALLBACK(on_dpi_lock_toggled), s);
    gtk_grid_attach(GTK_GRID(grid), s->dpi_lock, 0, ROSSAT_DPI_LEVELS + 1, 5, 1);
    gtk_grid_attach(GTK_GRID(grid),
                    label("100 – 16000 DPI in steps of 50. The DPI up/down buttons cycle through enabled levels.", "rs-hint"),
                    0, ROSSAT_DPI_LEVELS + 2, 5, 1);
    s->dpi_panel = box_panel("DPI SWITCHER", grid);
    gtk_grid_attach(GTK_GRID(page), s->dpi_panel, 0, 0, 1, 2);

    // polling rate
    GtkWidget *pbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    const char *rates[] = {"125 Hz", "250 Hz", "500 Hz", "1000 Hz"};
    group = nullptr;
    for (int i = 0; i < 4; ++i) {
        s->polling[i] = gtk_radio_button_new_with_label(group, rates[i]);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(s->polling[i]));
        gtk_box_pack_start(GTK_BOX(pbox), s->polling[i], FALSE, FALSE, 0);
    }
    s->polling_panel = box_panel("POLLING RATE", pbox);
    gtk_grid_attach(GTK_GRID(page), s->polling_panel, 1, 0, 1, 1);

    // application behaviour (does not need the mouse)
    GtkWidget *abox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    s->pref_tray = gtk_check_button_new_with_label("Keep running in the system tray");
    gtk_widget_set_tooltip_text(s->pref_tray, "Closing the window hides it to the tray; the lighting engine keeps running");
    s->pref_start_min = gtk_check_button_new_with_label("Start minimized to the tray");
    gtk_widget_set_margin_start(s->pref_start_min, 24);
    gtk_widget_set_tooltip_text(s->pref_start_min, "Start without opening the window (same as --minimized)");
    gtk_box_pack_start(GTK_BOX(abox), s->pref_tray, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(abox), s->pref_start_min, FALSE, FALSE, 0);
#ifndef ROSSAT_HAVE_TRAY
    gtk_widget_set_sensitive(abox, FALSE);
    gtk_box_pack_start(GTK_BOX(abox), label("Built without system tray support (libayatana-appindicator3)", "rs-hint"),
                       FALSE, FALSE, 0);
#endif
    gtk_grid_attach(GTK_GRID(page), box_panel("APPLICATION", abox), 1, 1, 1, 1);

    return page;
}

void on_button_combo_changed(GtkComboBox *combo, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(combo), "button"));
    update_button_row_sensitivity(s, idx);
}

gboolean on_button_row_focus(GtkWidget *w, GdkEvent *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->highlighted_button = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "button")) % HALF;
    gtk_widget_queue_draw(s->mouse_buttons);
    return FALSE;
}

GtkWidget *button_combo(app_state *s, int idx)
{
    GtkWidget *combo = gtk_combo_box_text_new();
    for (int i = 0; i < rossat_button_type_count(); ++i) {
        int t = rossat_button_type_at(i);
        char id[8];
        std::snprintf(id, sizeof id, "%d", t);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), id, rossat_button_type_name(static_cast<uint8_t>(t)));
    }
    g_object_set_data(G_OBJECT(combo), "button", GINT_TO_POINTER(idx));
    g_signal_connect(combo, "changed", G_CALLBACK(on_button_combo_changed), s);
    g_signal_connect(combo, "focus-in-event", G_CALLBACK(on_button_row_focus), s);
    g_signal_connect(combo, "enter-notify-event", G_CALLBACK(on_button_row_focus), s);
    gtk_widget_add_events(combo, GDK_ENTER_NOTIFY_MASK);
    return combo;
}

void on_reset_buttons(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    rossat_buttons_default(static_cast<uint8_t>(s->edit_profile), &s->button_data);
    s->updating = true;
    for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i) {
        ensure_combo_id(s->buttons[i].combo, s->button_data.buttons[i].type);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->buttons[i].p1), 0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s->buttons[i].p2), 0);
        update_button_row_sensitivity(s, i);
    }
    s->updating = false;
    set_status(s, "Factory layout loaded — press APPLY to write it", nullptr);
    schedule_auto_apply(s);
}

GtkWidget *build_buttons_page(app_state *s)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 16);

    s->mouse_buttons = gtk_drawing_area_new();
    gtk_widget_set_size_request(s->mouse_buttons, 230, 340);
    g_signal_connect(s->mouse_buttons, "draw", G_CALLBACK(on_draw_buttons), s);
    gtk_box_pack_start(GTK_BOX(page), s->mouse_buttons, FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_attach(GTK_GRID(grid), label("BUTTON", "rs-hint"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label("STANDARD FUNCTION", "rs-hint"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label("EASY-SHIFT[+] FUNCTION", "rs-hint"), 2, 0, 1, 1);

    for (int i = 0; i < HALF; ++i) {
        char t[64];
        std::snprintf(t, sizeof t, "%d  %s", i + 1, rossat_button_name(i));
        gtk_grid_attach(GTK_GRID(grid), label(t), 0, i + 1, 1, 1);
        for (int layer = 0; layer < 2; ++layer) {
            int idx = i + layer * HALF;
            button_row &r = s->buttons[idx];
            r.combo = button_combo(s, idx);
            r.p1 = gtk_spin_button_new_with_range(0, 255, 1);
            r.p2 = gtk_spin_button_new_with_range(0, 255, 1);
            // raw parameter holders; shortcuts are edited as text
            gtk_widget_set_no_show_all(r.p1, TRUE);
            gtk_widget_set_no_show_all(r.p2, TRUE);
            r.text = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(r.text), "macro text (optional)");
            gtk_entry_set_width_chars(GTK_ENTRY(r.text), 14);
            GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            gtk_box_pack_start(GTK_BOX(cell), r.combo, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(cell), r.p1, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(cell), r.p2, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(cell), r.text, FALSE, FALSE, 0);
            gtk_widget_set_no_show_all(r.p1, TRUE);
            gtk_widget_set_no_show_all(r.p2, TRUE);
            gtk_widget_set_no_show_all(r.text, TRUE);
            gtk_grid_attach(GTK_GRID(grid), cell, 1 + layer, i + 1, 1, 1);
        }
    }
    GtkWidget *reset = gtk_button_new_with_label("RESTORE FACTORY LAYOUT");
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset_buttons), s);
    GtkWidget *hint = label("Shortcut: type a key combination such as ctrl+c or alt+f4. "
                            "Macro: type text to store it as a keystroke macro on APPLY. "
                            "The Easy-Shift[+] column is active while the thumb Easy-Shift button is held.", "rs-hint");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_grid_attach(GTK_GRID(grid), hint, 0, HALF + 1, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), reset, 0, HALF + 2, 1, 1);

    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), grid);
    gtk_box_pack_start(GTK_BOX(page), box_panel("BUTTON ASSIGNMENT", scroll), TRUE, TRUE, 0);
    return page;
}

void on_light_changed(GtkWidget *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    gtk_widget_queue_draw(s->mouse_light);
}

void apply_group_color(app_state *s, std::initializer_list<int> leds)
{
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->group_color), &rgba);
    for (int i : leds)
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->led_color[i]), &rgba);
    gtk_widget_queue_draw(s->mouse_light);
    schedule_auto_apply(s);
}

void on_group_all(GtkButton *, gpointer d) { apply_group_color(static_cast<app_state *>(d), {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}); }
void on_group_wheel(GtkButton *, gpointer d) { apply_group_color(static_cast<app_state *>(d), {0}); }
void on_group_left(GtkButton *, gpointer d) { apply_group_color(static_cast<app_state *>(d), {1, 2, 3, 4, 9}); }
void on_group_right(GtkButton *, gpointer d) { apply_group_color(static_cast<app_state *>(d), {5, 6, 7, 8, 10}); }

uint8_t current_effect(app_state *s)
{
    int e = combo_type(s->effect);
    return e < 0 ? 0 : static_cast<uint8_t>(e);
}

void preview_settings(app_state *s, rossat_settings *out)
{
    *out = s->settings;
    out->effect_speed = static_cast<uint8_t>(gtk_range_get_value(GTK_RANGE(s->speed)));
    out->brightness = static_cast<uint8_t>(gtk_range_get_value(GTK_RANGE(s->brightness)));
    int th = combo_type(s->theme);
    out->color_theme = static_cast<uint8_t>(0x80 | (th < 0 ? 0 : th));
    out->led_enable_mask = 0;
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(s->led_color[i]), &rgba);
        out->leds[i] = from_rgba(rgba);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->led_enable[i])))
            out->led_enable_mask |= static_cast<uint16_t>(1 << i);
    }
}

gboolean preview_tick(gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->preview_t += 0.04;
    gtk_widget_queue_draw(s->mouse_light);
    return G_SOURCE_CONTINUE;
}

bool engine_rendered(app_state *s)
{
    return rossat_effect_is_software(current_effect(s)) ||
           gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->render_engine));
}

void update_effect_ui(app_state *s)
{
    bool only_engine = rossat_effect_is_software(current_effect(s));
    if (only_engine && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->render_engine)))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->render_engine), TRUE);
    gtk_widget_set_sensitive(s->render_engine, !only_engine);
    bool software = engine_rendered(s);
    bool aimo = current_effect(s) == ROSSAT_EFFECT_AIMO;
    gtk_label_set_text(GTK_LABEL(s->shift_hint),
                       software ? "Shown by the rossat engine while Easy-Shift[+] is held."
                                : "This effect is drawn by the mouse, which always shows its built-in light blue "
                                  "(#059cfa, hard-coded in the firmware). Turning this on switches the profile to "
                                  "\"Render with rossat engine\".");
    gtk_widget_set_sensitive(s->theme, aimo);
    gtk_widget_set_sensitive(s->colors_panel, !aimo);
    if (software && !s->preview_source)
        s->preview_source = g_timeout_add(40, preview_tick, s);
    else if (!software && s->preview_source) {
        g_source_remove(s->preview_source);
        s->preview_source = 0;
    }
    gtk_widget_queue_draw(s->mouse_light);
}

void on_effect_changed(GtkComboBox *, gpointer data)
{
    update_effect_ui(static_cast<app_state *>(data));
}

void on_theme_changed(GtkComboBox *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    for (GtkWidget *w : s->theme_swatches)
        gtk_widget_queue_draw(w);
    gtk_widget_queue_draw(s->mouse_light);
}

gboolean on_draw_swatch(GtkWidget *w, cairo_t *cr, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    int slot = 0;
    for (int i = 0; i < 3; ++i)
        if (s->theme_swatches[i] == w)
            slot = i;
    rossat_color c[3];
    int th = combo_type(s->theme);
    rossat_aimo_theme(th < 0 ? 0 : th, c);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    rounded_rect(cr, 1, 1, W - 2, H - 2, 3);
    cairo_set_source_rgb(cr, c[slot].r / 255.0, c[slot].g / 255.0, c[slot].b / 255.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_stroke(cr);
    return FALSE;
}

gboolean engine_tick(gpointer data);

// Status line under the effect: who is drawing the active profile's lighting.
void set_engine_label(app_state *s)
{
    if (!s->engine) {
        gtk_label_set_text(GTK_LABEL(s->engine_label),
                           s->engine_busy ? "Lighting engine: handled by `rossat-cli engine` / the rossat-engine service"
                                          : "Lighting engine: not running");
        return;
    }
    rossat_engine_status st;
    rossat_engine_get_status(s->engine, &st);
    char msg[160];
    if (st.last_error != ROSSAT_OK)
        std::snprintf(msg, sizeof msg, "Lighting engine: %s", rossat_strerror(st.last_error));
    else if (st.profile == 0xff)
        std::snprintf(msg, sizeof msg, "Lighting engine: starting\u2026");
    else {
        const char *n = rossat_effect_name(st.effect);
        std::snprintf(msg, sizeof msg, "Active profile %d: %s, %s", st.profile + 1, n ? n : "?",
                      st.rendering ? "drawn by rossat" : "drawn by the mouse");
    }
    gtk_label_set_text(GTK_LABEL(s->engine_label), msg);
}

// The GUI runs the engine for as long as it is open (window or tray); it idles
// while the active profile is drawn by the mouse. If another engine (CLI or
// the user service) holds the lock, retry every few seconds.
void start_engine(app_state *s)
{
    if (s->engine)
        return;
    int r = rossat_engine_start(nullptr, ROSSAT_ENGINE_AUTO, &s->engine);
    if (r != ROSSAT_OK)
        s->engine = nullptr;
    s->engine_busy = r == ROSSAT_ERR_BUSY;
    set_engine_label(s);
}

gboolean engine_tick(gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->engine && ++s->engine_retry % 10 == 0)
        start_engine(s);
    set_engine_label(s);
    return G_SOURCE_CONTINUE;
}

void on_render_toggled(GtkToggleButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    update_effect_ui(s);
}

void on_shift_toggled(GObject *, GParamSpec *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->updating)
        return;
    // a custom Easy-Shift colour needs the effect to be engine-rendered
    if (gtk_switch_get_active(GTK_SWITCH(s->shift_enable)))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->render_engine), TRUE);
    update_effect_ui(s);
}

GtkWidget *build_illumination_page(app_state *s)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 16);

    s->mouse_light = gtk_drawing_area_new();
    gtk_widget_set_size_request(s->mouse_light, 230, 340);
    g_signal_connect(s->mouse_light, "draw", G_CALLBACK(on_draw_lighting), s);
    gtk_box_pack_start(GTK_BOX(page), s->mouse_light, FALSE, FALSE, 0);

    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);

    // effect
    GtkWidget *eg = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(eg), 12);
    gtk_grid_set_row_spacing(GTK_GRID(eg), 8);
    s->effect = gtk_combo_box_text_new();
    for (int e = 0; e < 256; ++e) {
        const char *name = rossat_effect_name(static_cast<uint8_t>(e));
        if (!name)
            continue;
        char id[8], text[64];
        std::snprintf(id, sizeof id, "%d", e);
        std::snprintf(text, sizeof text, rossat_effect_is_software(static_cast<uint8_t>(e)) ? "%s  (engine)" : "%s", name);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->effect), id, text);
    }
    g_signal_connect(s->effect, "changed", G_CALLBACK(on_effect_changed), s);
    s->speed = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 11, 1);
    s->brightness = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 255, 1);
    gtk_widget_set_hexpand(s->speed, TRUE);
    g_signal_connect(s->brightness, "value-changed", G_CALLBACK(on_light_changed), s);
    s->render_engine = gtk_check_button_new_with_label("Render with rossat engine");
    gtk_widget_set_tooltip_text(s->render_engine,
                                "The PC draws the effect (the mouse stores AIMO). Needed for a custom "
                                "Easy-Shift[+] colour; requires the engine to run.");
    g_signal_connect(s->render_engine, "toggled", G_CALLBACK(on_render_toggled), s);
    GtkWidget *effbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(effbox), s->effect, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(effbox), s->render_engine, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(eg), label("EFFECT"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(eg), effbox, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(eg), label("SPEED"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(eg), s->speed, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(eg), label("BRIGHTNESS"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(eg), s->brightness, 1, 2, 1, 1);
    s->theme = gtk_combo_box_text_new();
    for (int i = 0; i < ROSSAT_AIMO_THEMES; ++i) {
        char id[8], text[32];
        std::snprintf(id, sizeof id, "%d", i);
        std::snprintf(text, sizeof text, "Theme %d", i + 1);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(s->theme), id, text);
    }
    g_signal_connect(s->theme, "changed", G_CALLBACK(on_theme_changed), s);
    GtkWidget *tb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(tb), s->theme, FALSE, FALSE, 0);
    for (auto &sw : s->theme_swatches) {
        sw = gtk_drawing_area_new();
        gtk_widget_set_size_request(sw, 22, 22);
        g_signal_connect(sw, "draw", G_CALLBACK(on_draw_swatch), s);
        gtk_box_pack_start(GTK_BOX(tb), sw, FALSE, FALSE, 0);
    }
    gtk_grid_attach(GTK_GRID(eg), label("AIMO THEME"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(eg), tb, 1, 3, 1, 1);

    s->engine_label = label("", "rs-hint");
    gtk_label_set_line_wrap(GTK_LABEL(s->engine_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(s->engine_label), 60);
    gtk_grid_attach(GTK_GRID(eg), s->engine_label, 1, 4, 1, 1);
    GtkWidget *ehint = label("\"Render with rossat engine\" lets the PC draw the effect (always on for effects "
                             "marked engine). rossat-swarm does this while it is open or in the tray; without "
                             "the GUI use `rossat-cli engine` or the rossat-engine user service. "
                             "AIMO uses the colour theme instead of the per-LED colours.", "rs-hint");
    gtk_label_set_line_wrap(GTK_LABEL(ehint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(ehint), 60);
    gtk_grid_attach(GTK_GRID(eg), ehint, 0, 5, 2, 1);
    gtk_box_pack_start(GTK_BOX(right), box_panel("LIGHTING EFFECT", eg), FALSE, FALSE, 0);

    // colours
    GtkWidget *cg = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(cg), 10);
    gtk_grid_set_row_spacing(GTK_GRID(cg), 4);
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        int col = (i % 2) * 3, row = i / 2;
        char t[40];
        std::snprintf(t, sizeof t, "%d  %s", i + 1, rossat_led_name(i));
        s->led_enable[i] = gtk_check_button_new();
        s->led_color[i] = gtk_color_button_new();
        gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(s->led_color[i]), TRUE);
        add_class(s->led_color[i], "rs-swatch");
        g_signal_connect(s->led_color[i], "color-set", G_CALLBACK(on_light_changed), s);
        g_signal_connect(s->led_enable[i], "toggled", G_CALLBACK(on_light_changed), s);
        gtk_grid_attach(GTK_GRID(cg), s->led_enable[i], col, row, 1, 1);
        gtk_grid_attach(GTK_GRID(cg), s->led_color[i], col + 1, row, 1, 1);
        gtk_grid_attach(GTK_GRID(cg), label(t), col + 2, row, 1, 1);
    }
    GtkWidget *gb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    s->group_color = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(s->group_color), TRUE);
    GdkRGBA def{0.40, 0.76, 0.97, 1.0};
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(s->group_color), &def);
    gtk_box_pack_start(GTK_BOX(gb), label("Set colour for:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gb), s->group_color, FALSE, FALSE, 0);
    struct { const char *t; GCallback cb; } groups[] = {
        {"ALL", G_CALLBACK(on_group_all)}, {"WHEEL", G_CALLBACK(on_group_wheel)},
        {"LEFT SIDE", G_CALLBACK(on_group_left)}, {"RIGHT SIDE", G_CALLBACK(on_group_right)}};
    for (const auto &g : groups) {
        GtkWidget *b = gtk_button_new_with_label(g.t);
        g_signal_connect(b, "clicked", g.cb, s);
        gtk_box_pack_start(GTK_BOX(gb), b, FALSE, FALSE, 0);
    }
    gtk_grid_attach(GTK_GRID(cg), gb, 0, 6, 6, 1);
    s->colors_panel = box_panel("COLOURS (checkbox = LED enabled, alpha = LED brightness)", cg);
    gtk_box_pack_start(GTK_BOX(right), s->colors_panel, FALSE, FALSE, 0);

    GtkWidget *sg = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(sg), 12);
    gtk_grid_set_row_spacing(GTK_GRID(sg), 8);
    s->shift_enable = gtk_switch_new();
    gtk_widget_set_valign(s->shift_enable, GTK_ALIGN_CENTER);
    g_signal_connect(s->shift_enable, "notify::active", G_CALLBACK(on_shift_toggled), s);
    s->shift_color = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(s->shift_color), TRUE);
    add_class(s->shift_color, "rs-swatch");
    s->shift_style = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->shift_style), "Solid");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->shift_style), "Pulse");
    gtk_combo_box_set_active(GTK_COMBO_BOX(s->shift_style), 0);
    GtkWidget *srow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(srow), s->shift_enable, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(srow), s->shift_color, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(srow), s->shift_style, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(sg), srow, 0, 0, 1, 1);
    s->shift_hint = label("", "rs-hint");
    gtk_label_set_line_wrap(GTK_LABEL(s->shift_hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(s->shift_hint), 60);
    gtk_grid_attach(GTK_GRID(sg), s->shift_hint, 0, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(right), box_panel("EASY-SHIFT[+] LIGHTING", sg), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), right, TRUE, TRUE, 0);
    return page;
}

void update_tdcu_label(app_state *s)
{
    if (!s->dev)
        return;
    rossat_tdcu t;
    char msg[128];
    if (rossat_read_tdcu(s->dev, &t) == ROSSAT_OK)
        std::snprintf(msg, sizeof msg, t.mode == 2 ? "Calibrated for your surface (value 0x%04x)"
                                                   : "Factory calibration (value 0x%04x)", t.value);
    else
        std::snprintf(msg, sizeof msg, "Could not read the sensor calibration");
    gtk_label_set_text(GTK_LABEL(s->tdcu_label), msg);
}

struct calib_state {
    app_state *app;
    GtkWidget *dialog, *text, *progress, *start, *save, *discard;
    guint timer = 0;
    int ticks = 0;
    bool running = false;
};

void calib_set_text(calib_state *c, const char *t)
{
    gtk_label_set_text(GTK_LABEL(c->text), t);
}

gboolean calib_tick(gpointer data)
{
    auto *c = static_cast<calib_state *>(data);
    ++c->ticks;
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(c->progress));
    if (c->ticks < 10) // give the sensor a moment to enter calibration
        return G_SOURCE_CONTINUE;
    int finished = 0;
    int r = rossat_calibration_poll(c->app->dev, &finished);
    if (r != ROSSAT_OK || c->ticks > 600) {
        c->timer = 0;
        c->running = false;
        rossat_calibration_finish(c->app->dev, 0);
        calib_set_text(c, r != ROSSAT_OK ? "Calibration failed. Try again on a clean, even surface."
                                         : "Calibration timed out and was discarded.");
        gtk_widget_set_sensitive(c->start, TRUE);
        return G_SOURCE_REMOVE;
    }
    if (!finished)
        return G_SOURCE_CONTINUE;
    c->timer = 0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->progress), 1.0);
    calib_set_text(c, "Calibration complete. Save it to the mouse, or discard it to keep the previous calibration.");
    gtk_widget_set_sensitive(c->save, TRUE);
    gtk_widget_set_sensitive(c->discard, TRUE);
    return G_SOURCE_REMOVE;
}

void on_calib_start(GtkButton *, gpointer data)
{
    auto *c = static_cast<calib_state *>(data);
    int r = rossat_calibration_start(c->app->dev);
    if (r != ROSSAT_OK) {
        calib_set_text(c, rossat_strerror(r));
        return;
    }
    c->running = true;
    c->ticks = 0;
    gtk_widget_set_sensitive(c->start, FALSE);
    calib_set_text(c, "Calibrating\u2026 keep moving the mouse in large circles and figure-eights.");
    c->timer = g_timeout_add(100, calib_tick, c);
}

void calib_finish(calib_state *c, bool save)
{
    int r = rossat_calibration_finish(c->app->dev, save ? 1 : 0);
    c->running = false;
    if (r != ROSSAT_OK)
        report_error(c->app, "Calibration", r);
    else
        set_status(c->app, save ? "Sensor calibration saved" : "Sensor calibration discarded", "connected");
    gtk_widget_destroy(c->dialog);
}

void on_calib_save(GtkButton *, gpointer data) { calib_finish(static_cast<calib_state *>(data), true); }
void on_calib_discard(GtkButton *, gpointer data) { calib_finish(static_cast<calib_state *>(data), false); }

void on_calib_destroy(GtkWidget *, gpointer data)
{
    auto *c = static_cast<calib_state *>(data);
    if (c->timer)
        g_source_remove(c->timer);
    if (c->running && c->app->dev) // closed mid-way: keep the old calibration
        rossat_calibration_finish(c->app->dev, 0);
    update_tdcu_label(c->app);
    delete c;
}

void on_calibrate(GtkButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->dev)
        return;
    auto *c = new calib_state;
    c->app = s;
    c->dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(c->dialog), "Sensor calibration");
    gtk_window_set_transient_for(GTK_WINDOW(c->dialog), GTK_WINDOW(s->window));
    gtk_window_set_modal(GTK_WINDOW(c->dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(c->dialog), 460, -1);
    add_class(c->dialog, "rossat");
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(c->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(area), 16);
    gtk_box_set_spacing(GTK_BOX(area), 12);
    gtk_box_pack_start(GTK_BOX(area), label("TRACKING & DISTANCE CONTROL UNIT", "rs-box-title"), FALSE, FALSE, 0);
    c->text = label("Put the mouse on the surface you play on. Press START, then keep moving the mouse "
                    "in large circles and figure-eights until calibration completes (about 10\u201320 s).");
    gtk_label_set_line_wrap(GTK_LABEL(c->text), TRUE);
    gtk_box_pack_start(GTK_BOX(area), c->text, FALSE, FALSE, 0);
    c->progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(area), c->progress, FALSE, FALSE, 0);
    GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    c->start = gtk_button_new_with_label("START");
    add_class(c->start, "suggested-action");
    c->save = gtk_button_new_with_label("SAVE");
    c->discard = gtk_button_new_with_label("DISCARD");
    gtk_widget_set_sensitive(c->save, FALSE);
    gtk_widget_set_sensitive(c->discard, FALSE);
    gtk_box_pack_start(GTK_BOX(bb), c->start, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(bb), c->save, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(bb), c->discard, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), bb, FALSE, FALSE, 0);
    g_signal_connect(c->start, "clicked", G_CALLBACK(on_calib_start), c);
    g_signal_connect(c->save, "clicked", G_CALLBACK(on_calib_save), c);
    g_signal_connect(c->discard, "clicked", G_CALLBACK(on_calib_discard), c);
    g_signal_connect(c->dialog, "destroy", G_CALLBACK(on_calib_destroy), c);
    gtk_widget_show_all(c->dialog);
}

GtkWidget *build_advanced_page(app_state *s)
{
    GtkWidget *page = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(page), 14);
    gtk_grid_set_row_spacing(GTK_GRID(page), 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 16);

    GtkWidget *ab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    s->angle_snapping = gtk_switch_new();
    gtk_box_pack_start(GTK_BOX(ab), s->angle_snapping, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ab), label("Straighten nearly horizontal / vertical movements", "rs-hint"), FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(page), box_panel("ANGLE SNAPPING", ab), 0, 0, 1, 1);

    GtkWidget *db = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    s->debounce = gtk_spin_button_new_with_range(0, 30, 1);
    gtk_box_pack_start(GTK_BOX(db), s->debounce, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(db), label("ms  (factory: 8)", "rs-hint"), FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(page), box_panel("DEBOUNCE TIME", db), 1, 0, 1, 1);

    GtkWidget *cb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    s->calib_button = gtk_button_new_with_label("CALIBRATE SENSOR\u2026");
    g_signal_connect(s->calib_button, "clicked", G_CALLBACK(on_calibrate), s);
    s->tdcu_label = label("", "rs-hint");
    gtk_box_pack_start(GTK_BOX(cb), s->calib_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cb), s->tdcu_label, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(page), box_panel("SENSOR CALIBRATION (TDCU)", cb), 0, 2, 2, 1);

    s->raw_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(s->raw_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(s->raw_view), TRUE);
    gtk_widget_set_hexpand(s->raw_view, TRUE);
    gtk_grid_attach(GTK_GRID(page), box_panel("RAW SETTINGS REPORT 0x06 (as last read)", s->raw_view), 0, 1, 2, 1);
    return page;
}

/* ---- system tray ------------------------------------------------------ */

void tray_sync(app_state *s);

void show_window(app_state *s)
{
    gtk_widget_show(s->window);
    gtk_window_present(GTK_WINDOW(s->window));
    tray_sync(s);
}

void hide_window(app_state *s)
{
    gtk_widget_hide(s->window);
    tray_sync(s);
}

#ifdef ROSSAT_HAVE_TRAY
void on_tray_show(GtkMenuItem *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (gtk_widget_get_visible(s->window))
        hide_window(s);
    else
        show_window(s);
}

void on_tray_quit(GtkMenuItem *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->quitting = true;
    gtk_widget_destroy(s->window);
}
#endif

void tray_sync(app_state *s)
{
#ifdef ROSSAT_HAVE_TRAY
    if (!s->tray_enabled) {
        if (s->indicator)
            app_indicator_set_status(s->indicator, APP_INDICATOR_STATUS_PASSIVE);
        return;
    }
    if (!s->indicator) {
        // The tray host loads icons by name from a directory, so put the
        // bundled SVG where it can find it (works without installation too).
        std::string icon_dir = std::string(g_get_user_cache_dir()) + "/rossat/icons";
        g_mkdir_with_parents(icon_dir.c_str(), 0755);
        if (GBytes *svg = g_resources_lookup_data("/org/rossat/swarm/rossat-swarm.svg", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr)) {
            gsize len = 0;
            const char *data = static_cast<const char *>(g_bytes_get_data(svg, &len));
            g_file_set_contents((icon_dir + "/rossat-swarm-tray.svg").c_str(), data, static_cast<gssize>(len), nullptr);
            g_bytes_unref(svg);
        }
        s->indicator = app_indicator_new_with_path("rossat-swarm", "rossat-swarm-tray", APP_INDICATOR_CATEGORY_HARDWARE,
                                                   icon_dir.c_str());
        app_indicator_set_title(s->indicator, "Rossat Swarm");
        GtkWidget *menu = gtk_menu_new();
        s->tray_show_item = gtk_menu_item_new_with_label("Show Rossat Swarm");
        g_signal_connect(s->tray_show_item, "activate", G_CALLBACK(on_tray_show), s);
        GtkWidget *quit = gtk_menu_item_new_with_label("Quit");
        g_signal_connect(quit, "activate", G_CALLBACK(on_tray_quit), s);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), s->tray_show_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
        gtk_widget_show_all(menu);
        app_indicator_set_menu(s->indicator, GTK_MENU(menu));
        // middle click on the icon toggles the window
        app_indicator_set_secondary_activate_target(s->indicator, s->tray_show_item);
    }
    app_indicator_set_status(s->indicator, APP_INDICATOR_STATUS_ACTIVE);
    gtk_menu_item_set_label(GTK_MENU_ITEM(s->tray_show_item),
                            gtk_widget_get_visible(s->window) ? "Hide Rossat Swarm" : "Show Rossat Swarm");
#else
    (void)s;
#endif
}

gboolean on_delete(GtkWidget *, GdkEvent *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->tray_enabled && !s->quitting) {
        hide_window(s);
        return TRUE; // keep running in the tray
    }
    return FALSE;
}

void store_prefs(app_state *s)
{
    gui_prefs p;
    p.tray = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_tray));
    p.start_minimized = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->pref_start_min));
    p.auto_apply = gtk_switch_get_active(GTK_SWITCH(s->auto_apply));
    save_prefs(p);
}

void on_pref_tray(GtkToggleButton *b, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    bool on = gtk_toggle_button_get_active(b);
    gtk_widget_set_sensitive(s->pref_start_min, on);
    if (s->updating)
        return;
    s->tray_enabled = on;
    tray_sync(s);
    store_prefs(s);
}

void on_pref_start_min(GtkToggleButton *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (!s->updating)
        store_prefs(s);
}

/* ---- auto apply ------------------------------------------------------- */

gboolean auto_apply_fire(gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    s->auto_apply_source = 0;
    if (s->dev && s->loaded)
        on_apply(nullptr, s);
    return G_SOURCE_REMOVE;
}

void schedule_auto_apply(app_state *s)
{
    if (s->updating || !s->loaded || !s->auto_apply ||
        !gtk_switch_get_active(GTK_SWITCH(s->auto_apply)))
        return;
    if (s->auto_apply_source)
        g_source_remove(s->auto_apply_source);
    if (s->engine_source)
        g_source_remove(s->engine_source);
    // debounce: one write after the user stops dragging / typing
    s->auto_apply_source = g_timeout_add(700, auto_apply_fire, s);
}

void on_any_change(GtkWidget *, gpointer data)
{
    schedule_auto_apply(static_cast<app_state *>(data));
}

void on_any_notify(GObject *, GParamSpec *, gpointer data)
{
    schedule_auto_apply(static_cast<app_state *>(data));
}

// Connects every editing widget below `w` to the auto-apply debouncer.
// Widgets tagged "rs-no-autoapply" (app preferences, engine switch) are skipped.
void hook_auto_apply(app_state *s, GtkWidget *w)
{
    if (g_object_get_data(G_OBJECT(w), "rs-no-autoapply"))
        return;
    if (GTK_IS_SPIN_BUTTON(w))
        g_signal_connect(w, "value-changed", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_ENTRY(w))
        g_signal_connect(w, "changed", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_COLOR_BUTTON(w))
        g_signal_connect(w, "color-set", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_TOGGLE_BUTTON(w))
        g_signal_connect(w, "toggled", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_COMBO_BOX(w))
        g_signal_connect(w, "changed", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_RANGE(w))
        g_signal_connect(w, "value-changed", G_CALLBACK(on_any_change), s);
    else if (GTK_IS_SWITCH(w))
        g_signal_connect(w, "notify::active", G_CALLBACK(on_any_notify), s);
    if (GTK_IS_CONTAINER(w) && !GTK_IS_COMBO_BOX(w) && !GTK_IS_SPIN_BUTTON(w)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *c = children; c; c = c->next)
            hook_auto_apply(s, GTK_WIDGET(c->data));
        g_list_free(children);
    }
}

void on_auto_apply_toggled(GObject *sw, GParamSpec *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    bool on = gtk_switch_get_active(GTK_SWITCH(sw));
    gtk_widget_set_sensitive(s->apply_button, !on && s->dev);
    if (s->updating)
        return;
    store_prefs(s);
    if (on)
        set_status(s, "Auto apply on \u2014 changes are written to the mouse automatically", "connected");
}

void on_destroy(GtkWidget *, gpointer data)
{
    auto *s = static_cast<app_state *>(data);
    if (s->hotplug_source)
        g_source_remove(s->hotplug_source);
    if (s->preview_source)
        g_source_remove(s->preview_source);
    if (s->auto_apply_source)
        g_source_remove(s->auto_apply_source);
    if (s->engine)
        rossat_engine_stop(s->engine);
    if (s->dev)
        rossat_close(s->dev);
#ifdef ROSSAT_HAVE_TRAY
    if (s->indicator)
        g_object_unref(s->indicator);
#endif
    delete s;
}

} // namespace

GtkWidget *rossat_window_new(GtkApplication *app, bool force_minimized)
{
    auto *s = new app_state;
    s->window = gtk_application_window_new(app);
    g_signal_connect(s->window, "delete-event", G_CALLBACK(on_delete), s);
    gtk_window_set_title(GTK_WINDOW(s->window), "Rossat Swarm");
    if (GdkPixbuf *icon = gdk_pixbuf_new_from_resource_at_scale("/org/rossat/swarm/rossat-swarm.svg", 256, 256, TRUE, nullptr)) {
        gtk_window_set_icon(GTK_WINDOW(s->window), icon);
        g_object_unref(icon);
    }
    gtk_window_set_default_size(GTK_WINDOW(s->window), 1180, 760);
    add_class(s->window, "rossat");
    g_signal_connect(s->window, "destroy", G_CALLBACK(on_destroy), s);
    g_object_set_data(G_OBJECT(s->window), "rossat-state", s);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(s->window), root);

    // header
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    add_class(header, "rs-header");
    GtkWidget *logo_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GdkPixbuf *logo = gdk_pixbuf_new_from_resource_at_scale("/org/rossat/swarm/rossat-swarm.svg", 44, 44, TRUE, nullptr);
    if (logo) {
        GtkWidget *img = gtk_image_new_from_pixbuf(logo);
        gtk_widget_set_valign(img, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(logo_box), img, FALSE, FALSE, 0);
        g_object_unref(logo);
    }
    GtkWidget *brand = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(brand), label("ROSSAT", "rs-brand"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(brand), label("SWARM", "rs-brand-sub"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(logo_box), brand, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), logo_box, FALSE, FALSE, 0);
    GtkWidget *dev = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    s->device_label = label("NO DEVICE", "rs-device-name");
    gtk_label_set_ellipsize(GTK_LABEL(s->device_label), PANGO_ELLIPSIZE_END);
    s->status_label = label("", "rs-status");
    gtk_box_pack_start(GTK_BOX(dev), s->device_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dev), s->status_label, FALSE, FALSE, 0);
    gtk_widget_set_valign(dev, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(header), dev, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    // pages
    s->notebook = gtk_notebook_new();
    add_class(s->notebook, "rs-tabs");
    // pages scroll, so the window can be made smaller than its content
    auto scrolled = [](GtkWidget *page) {
        GtkWidget *sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(sw), page);
        return sw;
    };
    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), scrolled(build_settings_page(s)), label("SETTINGS"));
    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), scrolled(build_buttons_page(s)), label("BUTTON ASSIGNMENT"));
    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), scrolled(build_illumination_page(s)), label("ILLUMINATION"));
    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), scrolled(build_advanced_page(s)), label("ADVANCED SETTINGS"));
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(s->notebook), TRUE);
    gtk_box_pack_start(GTK_BOX(root), s->notebook, TRUE, TRUE, 0);

    // footer: profiles + actions
    // Two groups in a flow box: side by side when there is room, stacked on
    // two rows in narrow windows.
    GtkWidget *footer = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(footer), GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(footer), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(footer), 2);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(footer), 6);
    add_class(footer, "rs-footer");
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(left, GTK_ALIGN_START);
    gtk_widget_set_halign(right, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(left), label("PROFILE", "rs-box-title"), FALSE, FALSE, 4);
    for (int i = 0; i < ROSSAT_PROFILE_COUNT; ++i) {
        GtkWidget *b = gtk_toggle_button_new_with_label("");
        add_class(b, "rs-profile");
        g_object_set_data(G_OBJECT(b), "profile", GINT_TO_POINTER(i));
        g_signal_connect(b, "toggled", G_CALLBACK(on_profile_toggled), s);
        s->profile_buttons[i] = b;
        gtk_box_pack_start(GTK_BOX(left), b, FALSE, FALSE, 0);
    }
    s->activate_button = gtk_button_new_with_label("ACTIVATE");
    gtk_widget_set_tooltip_text(s->activate_button, "Make the profile being edited the mouse's active profile");
    g_signal_connect(s->activate_button, "clicked", G_CALLBACK(on_activate_profile), s);
    gtk_box_pack_start(GTK_BOX(left), s->activate_button, FALSE, FALSE, 12);

    s->apply_button = gtk_button_new_with_label("APPLY");
    add_class(s->apply_button, "suggested-action");
    g_signal_connect(s->apply_button, "clicked", G_CALLBACK(on_apply), s);
    s->reload_button = gtk_button_new_with_label("RELOAD");
    gtk_widget_set_tooltip_text(s->reload_button, "Discard changes and read the profile from the mouse again");
    g_signal_connect(s->reload_button, "clicked", G_CALLBACK(on_reload), s);
    s->auto_apply = gtk_switch_new();
    gtk_widget_set_valign(s->auto_apply, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(s->auto_apply, "Write every change to the mouse automatically");
    GtkWidget *aa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(aa), label("AUTO APPLY", "rs-hint"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(aa), s->auto_apply, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(right), s->apply_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(right), s->reload_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(right), aa, FALSE, FALSE, 8);
    gtk_container_add(GTK_CONTAINER(footer), left);
    gtk_container_add(GTK_CONTAINER(footer), right);
    gtk_box_pack_start(GTK_BOX(root), footer, FALSE, FALSE, 0);

    update_profile_buttons(s);
    set_engine_label(s);
    g_signal_connect_swapped(s->window, "map", G_CALLBACK(connect_device), s);

    // auto apply: hook every editing widget except app-level switches
    for (GtkWidget *w : {s->pref_tray, s->pref_start_min, s->group_color})
        g_object_set_data(G_OBJECT(w), "rs-no-autoapply", GINT_TO_POINTER(1));
    hook_auto_apply(s, s->notebook);

    // restore application preferences
    gui_prefs prefs = load_prefs();
    s->updating = true;
    g_signal_connect(s->auto_apply, "notify::active", G_CALLBACK(on_auto_apply_toggled), s);
    gtk_switch_set_active(GTK_SWITCH(s->auto_apply), prefs.auto_apply);
    gtk_widget_set_sensitive(s->apply_button, !prefs.auto_apply);
    g_signal_connect(s->pref_tray, "toggled", G_CALLBACK(on_pref_tray), s);
    g_signal_connect(s->pref_start_min, "toggled", G_CALLBACK(on_pref_start_min), s);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->pref_tray), prefs.tray);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->pref_start_min), prefs.start_minimized);
    gtk_widget_set_sensitive(s->pref_start_min, prefs.tray);
#ifdef ROSSAT_HAVE_TRAY
    s->tray_enabled = prefs.tray || force_minimized;
#endif
    s->updating = false;

    start_engine(s);
    s->engine_source = g_timeout_add(500, engine_tick, s);

    bool start_hidden = s->tray_enabled && (force_minimized || prefs.start_minimized);
    gtk_widget_show_all(root);
    if (!start_hidden)
        show_window(s);
    else
        tray_sync(s);
    g_object_set_data(G_OBJECT(s->window), "rossat-hidden", GINT_TO_POINTER(start_hidden));
    return s->window;
}

void rossat_window_show_page(GtkWidget *window, const char *page)
{
    auto *s = static_cast<app_state *>(g_object_get_data(G_OBJECT(window), "rossat-state"));
    if (!s || !page)
        return;
    const char *names[] = {"settings", "buttons", "illumination", "advanced"};
    for (int i = 0; i < 4; ++i)
        if (std::strcmp(page, names[i]) == 0)
            gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), i);
}
