// rossat-cli - command line front end for librossat.

#include "rossat/rossat.h"

#include <cctype>
#include <chrono>
#include <csignal>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char *g_prog = "rossat-cli";

void usage(FILE *f)
{
    std::fprintf(f,
        "Usage: %s [--device PATH] <command> [options]\n"
        "\n"
        "Commands:\n"
        "  list                         list supported devices\n"
        "  info                         device, firmware and active profile\n"
        "  profile [N]                  show or switch the active profile (1-5)\n"
        "  show [-p N]                  show settings of a profile (default: active)\n"
        "  set [-p N] KEY=VALUE...      change settings, keys:\n"
        "        dpiN=DPI | dpiN=XxY    DPI level N (1-5), %d-%d in steps of %d\n"
        "        dpi-active=N           selected DPI level (1-5)\n"
        "        dpi-enabled=1,2,3      enabled DPI levels\n"
        "        polling=125|250|500|1000\n"
        "        angle-snapping=on|off  debounce=MS\n"
        "        effect=NAME|NUM        off, fully-lit, blinking, breathing, heartbeat, color-cycle,\n"
        "                               snake, breathing-2, heartbeat-2, aimo, colorwave\n"
        "        render=engine|mouse    who draws the effect; heartbeat, *-2 and aimo are always\n"
        "                               engine-rendered. Engine rendering is needed for a custom\n"
        "                               Easy-Shift colour (the mouse's own one is fixed #059cfa)\n"
        "        theme=1-10             AIMO colour theme\n"
        "        speed=N  brightness=0-255\n"
        "        led=all|wheel|left|right|N[,N..]:#RRGGBB[@ALPHA]\n"
        "        led-enable=all|none|MASK\n"
        "  buttons [-p N]               show button assignment\n"
        "  bind [-p N] BUTTON TYPE [ARGS]  e.g. bind 9 shortcut ctrl+c\n"
        "                               BUTTON: 1-12 or shift+1..12, TYPE: name or number\n"
        "  reset-buttons [-p N]         restore the factory button layout\n"
        "  types                        list button function names\n"
        "  macro [-p N] -b BUTTON                 show the macro stored for a button\n"
        "  macro [-p N] -b BUTTON --text TEXT [--delay MS] [--name NAME]\n"
        "                               store TEXT as a macro and bind it to BUTTON\n"
        "  direct off|#RRGGBB|C1,..,C11 host-controlled LED colours (\"off\" restores effect)\n"
        "  dump [-p N]                  hex dump of the raw settings and button reports\n"
        "  shift-light [-p N] [on|off] [#RRGGBB[@ALPHA]] [solid|pulse]\n"
        "                               lighting while Easy-Shift[+] is held (needs the engine)\n"
        "  calibrate                    TDCU sensor calibration for your mousepad\n"
        "  tdcu                         show the stored sensor calibration\n"
        "  engine [--mode auto|aimo|heartbeat]\n"
        "                               run the lighting engine (AIMO, Heartbeat, Breathing/Heartbeat 2.0)\n"
        "                               in the foreground until Ctrl+C\n"
        "  backup FILE                  save settings + buttons of all 5 profiles\n"
        "  restore FILE                 write a backup back to the mouse\n"
        "\n"
        "Profiles, DPI levels and buttons are numbered from 1.\n",
        g_prog, ROSSAT_DPI_MIN, ROSSAT_DPI_MAX, ROSSAT_DPI_STEP);
}

[[noreturn]] void die(const char *what, int err)
{
    std::fprintf(stderr, "%s: %s: %s\n", g_prog, what, rossat_strerror(err));
    std::exit(1);
}

[[noreturn]] void fail(const char *fmt, const char *arg = "")
{
    std::fprintf(stderr, "%s: ", g_prog);
    std::fprintf(stderr, fmt, arg);
    std::fputc('\n', stderr);
    std::exit(2);
}

void check(int r, const char *what)
{
    if (r != ROSSAT_OK)
        die(what, r);
}

long parse_long(const std::string &s, const char *what)
{
    char *end = nullptr;
    long v = std::strtol(s.c_str(), &end, 0);
    if (s.empty() || *end)
        fail("invalid number for %s", what);
    return v;
}

bool parse_bool(const std::string &s)
{
    if (s == "on" || s == "1" || s == "true" || s == "yes")
        return true;
    if (s == "off" || s == "0" || s == "false" || s == "no")
        return false;
    fail("expected on/off, got '%s'", s.c_str());
}

bool parse_color(const std::string &s, rossat_color *c)
{
    std::string hex = s, alpha;
    auto at = s.find('@');
    if (at != std::string::npos) {
        hex = s.substr(0, at);
        alpha = s.substr(at + 1);
    }
    if (!hex.empty() && hex[0] == '#')
        hex.erase(0, 1);
    if (hex.size() != 6)
        return false;
    for (char ch : hex)
        if (!std::isxdigit(static_cast<unsigned char>(ch)))
            return false;
    unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
    c->r = (v >> 16) & 0xff;
    c->g = (v >> 8) & 0xff;
    c->b = v & 0xff;
    c->a = alpha.empty() ? 255 : static_cast<uint8_t>(parse_long(alpha, "alpha") & 0xff);
    return true;
}

std::vector<int> led_selection(const std::string &s)
{
    if (s == "all") {
        std::vector<int> v;
        for (int i = 0; i < ROSSAT_LED_COUNT; ++i)
            v.push_back(i);
        return v;
    }
    if (s == "wheel")
        return {0};
    if (s == "left")
        return {1, 2, 3, 4, 9};
    if (s == "right")
        return {5, 6, 7, 8, 10};
    std::vector<int> v;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        std::string part = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        long n = parse_long(part, "LED index");
        if (n < 1 || n > ROSSAT_LED_COUNT)
            fail("LED index must be 1-11");
        v.push_back(static_cast<int>(n - 1));
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return v;
}

void print_color(const rossat_color &c)
{
    std::printf("#%02x%02x%02x@%d", c.r, c.g, c.b, c.a);
}

uint16_t g_pid = ROSSAT_PID_KONE_AIMO_REMASTERED;

void print_settings(const rossat_settings &s)
{
    std::printf("Profile %d\n", s.profile + 1);
    std::printf("  DPI levels:\n");
    for (int i = 0; i < ROSSAT_DPI_LEVELS; ++i) {
        bool en = s.dpi_enabled_mask & (1 << i);
        std::printf("    %c %d: ", i == s.dpi_active ? '*' : ' ', i + 1);
        if (s.dpi_x[i] == s.dpi_y[i])
            std::printf("%5d", s.dpi_x[i]);
        else
            std::printf("%5dx%d", s.dpi_x[i], s.dpi_y[i]);
        std::printf("%s\n", en ? "" : "  (disabled)");
    }
    std::printf("  Polling rate:    %d Hz\n", rossat_polling_rate_hz(s.polling_rate));
    std::printf("  Angle snapping:  %s\n", s.angle_snapping ? "on" : "off");
    std::printf("  Debounce time:   %d ms\n", s.debounce_ms);
    const char *ename = rossat_effect_name(s.effect);
    uint8_t eff = 0;
    int by_engine = 0;
    rossat_effective_effect(g_pid, &s, &eff, &by_engine);
    ename = rossat_effect_name(eff);
    std::printf("  Lighting effect: %s (%d), %s\n", ename ? ename : "unknown", eff,
                by_engine ? "rendered by the rossat engine (custom Easy-Shift colour possible)"
                          : "rendered by the mouse (Easy-Shift shows the built-in light blue)");
    std::printf("  AIMO theme:      %d (raw 0x%02x)\n", (s.color_theme & 0x7f) % ROSSAT_AIMO_THEMES + 1, s.color_theme);
    std::printf("  Effect speed:    %d\n", s.effect_speed);
    std::printf("  Brightness:      %d\n", s.brightness);
    std::printf("  LEDs (enable mask 0x%03x):\n", s.led_enable_mask);
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        std::printf("    %2d %-12s ", i + 1, rossat_led_name(i));
        print_color(s.leds[i]);
        std::printf("%s\n", s.led_enable_mask & (1 << i) ? "" : "  (off)");
    }
}

void print_buttons(const rossat_buttons &b)
{
    std::printf("Profile %d buttons\n", b.profile + 1);
    for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i) {
        const rossat_button &bt = b.buttons[i];
        const char *tn = rossat_button_type_name(bt.type);
        std::printf("  %-9s %-34s %s",
                    (i < 12 ? std::to_string(i + 1) : "shift+" + std::to_string(i - 11)).c_str(),
                    rossat_button_name(i), tn ? tn : "unknown");
        if (bt.type == ROSSAT_BT_SHORTCUT) {
            char sc[64];
            rossat_format_shortcut(bt.param1, bt.param2, sc, sizeof sc);
            std::printf(" %s", sc);
        } else if (!tn || bt.param1 || bt.param2) {
            std::printf(" [type 0x%02x, 0x%02x, 0x%02x]", bt.type, bt.param1, bt.param2);
        }
        std::printf("\n");
    }
}

int parse_button(const std::string &s)
{
    std::string t = s;
    int base = 0;
    if (t.rfind("shift+", 0) == 0) {
        base = ROSSAT_BUTTON_COUNT / 2;
        t = t.substr(6);
    }
    long n = parse_long(t, "button");
    if (n < 1 || n > ROSSAT_BUTTON_COUNT / 2)
        fail("button must be 1-12 or shift+1..12");
    return base + static_cast<int>(n - 1);
}

void hexdump(const char *title, const uint8_t *buf, size_t len)
{
    std::printf("%s (%zu bytes)\n", title, len);
    for (size_t i = 0; i < len; i += 16) {
        std::printf("  %04zx:", i);
        for (size_t j = i; j < i + 16 && j < len; ++j)
            std::printf(" %02x", buf[j]);
        std::printf("\n");
    }
}

// US layout ASCII -> HID usage (+ shift flag).
bool ascii_to_hid(char ch, uint8_t *usage, bool *shift)
{
    static const char *row_lower = "1234567890";
    *shift = false;
    if (ch >= 'a' && ch <= 'z') { *usage = static_cast<uint8_t>(0x04 + ch - 'a'); return true; }
    if (ch >= 'A' && ch <= 'Z') { *usage = static_cast<uint8_t>(0x04 + ch - 'A'); *shift = true; return true; }
    if (const char *p = std::strchr(row_lower, ch); p && ch) { *usage = static_cast<uint8_t>(0x1e + (p - row_lower)); return true; }
    struct { char c; uint8_t u; bool s; } map[] = {
        {'\n', 0x28, false}, {'\t', 0x2b, false}, {' ', 0x2c, false}, {'-', 0x2d, false},
        {'=', 0x2e, false}, {'[', 0x2f, false}, {']', 0x30, false}, {'\\', 0x31, false},
        {';', 0x33, false}, {'\'', 0x34, false}, {'`', 0x35, false}, {',', 0x36, false},
        {'.', 0x37, false}, {'/', 0x38, false},
        {'!', 0x1e, true}, {'@', 0x1f, true}, {'#', 0x20, true}, {'$', 0x21, true},
        {'%', 0x22, true}, {'^', 0x23, true}, {'&', 0x24, true}, {'*', 0x25, true},
        {'(', 0x26, true}, {')', 0x27, true}, {'_', 0x2d, true}, {'+', 0x2e, true},
        {'{', 0x2f, true}, {'}', 0x30, true}, {'|', 0x31, true}, {':', 0x33, true},
        {'"', 0x34, true}, {'~', 0x35, true}, {'<', 0x36, true}, {'>', 0x37, true},
        {'?', 0x38, true},
    };
    for (const auto &m : map)
        if (m.c == ch) { *usage = m.u; *shift = m.s; return true; }
    return false;
}

struct options {
    const char *device = nullptr;
    int profile = -1; // 0-based, -1 = active
    std::vector<std::string> args;
};

rossat_device *open_device(const options &o)
{
    rossat_device *dev = nullptr;
    check(rossat_open(o.device, &dev), "open");
    return dev;
}

uint8_t resolve_profile(rossat_device *dev, const options &o)
{
    if (o.profile >= 0)
        return static_cast<uint8_t>(o.profile);
    uint8_t p = 0;
    check(rossat_get_active_profile(dev, &p, nullptr), "read active profile");
    return p;
}

int cmd_list()
{
    rossat_device_info infos[16];
    int n = rossat_enumerate(infos, 16);
    if (n < 0)
        die("enumerate", n);
    if (n == 0) {
        std::printf("No supported devices found.\n");
        return 1;
    }
    for (int i = 0; i < n && i < 16; ++i)
        std::printf("%s  %04x:%04x  if%d  %s%s%s\n", infos[i].path, infos[i].vendor_id,
                    infos[i].product_id, infos[i].interface_number, infos[i].name,
                    infos[i].serial[0] ? "  serial " : "", infos[i].serial);
    return 0;
}

int cmd_info(const options &o)
{
    rossat_device *dev = open_device(o);
    rossat_device_info info;
    rossat_get_info(dev, &info);
    char fw[16];
    rossat_get_firmware_version(dev, fw, sizeof fw);
    uint8_t p = 0, count = 0;
    check(rossat_get_active_profile(dev, &p, &count), "read active profile");
    std::printf("Device:         %s\n", info.name);
    std::printf("USB ID:         %04x:%04x (interface %d)\n", info.vendor_id, info.product_id, info.interface_number);
    std::printf("Path:           %s\n", info.path);
    std::printf("Firmware:       %s\n", fw);
    std::printf("Active profile: %d of %d\n", p + 1, count);
    rossat_close(dev);
    return 0;
}

int cmd_profile(const options &o)
{
    rossat_device *dev = open_device(o);
    if (o.args.empty()) {
        uint8_t p = 0, count = 0;
        check(rossat_get_active_profile(dev, &p, &count), "read active profile");
        std::printf("%d\n", p + 1);
    } else {
        long n = parse_long(o.args[0], "profile");
        if (n < 1 || n > ROSSAT_PROFILE_COUNT)
            fail("profile must be 1-5");
        check(rossat_set_active_profile(dev, static_cast<uint8_t>(n - 1)), "set active profile");
    }
    rossat_close(dev);
    return 0;
}

int cmd_show(const options &o)
{
    rossat_device *dev = open_device(o);
    rossat_settings s;
    check(rossat_read_settings(dev, resolve_profile(dev, o), &s), "read settings");
    rossat_device_info info;
    rossat_get_info(dev, &info);
    g_pid = info.product_id;
    print_settings(s);
    rossat_close(dev);
    return 0;
}

// effect/render are applied after all keys, see cmd_set
int g_new_effect = -1, g_new_render = -1;

void apply_setting(rossat_settings &s, const std::string &kv)
{
    auto eq = kv.find('=');
    if (eq == std::string::npos)
        fail("expected KEY=VALUE, got '%s'", kv.c_str());
    std::string key = kv.substr(0, eq), val = kv.substr(eq + 1);

    if (key.size() == 4 && key.rfind("dpi", 0) == 0 && key[3] >= '1' && key[3] <= '5') {
        int lvl = key[3] - '1';
        long x, y;
        auto sep = val.find_first_of("xX");
        if (sep != std::string::npos) {
            x = parse_long(val.substr(0, sep), "DPI");
            y = parse_long(val.substr(sep + 1), "DPI");
        } else {
            x = y = parse_long(val, "DPI");
        }
        for (long v : {x, y})
            if (v < ROSSAT_DPI_MIN || v > ROSSAT_DPI_MAX || v % ROSSAT_DPI_STEP)
                fail("DPI must be 100-16000 in steps of 50");
        s.dpi_x[lvl] = static_cast<uint16_t>(x);
        s.dpi_y[lvl] = static_cast<uint16_t>(y);
    } else if (key == "dpi-active") {
        long n = parse_long(val, key.c_str());
        if (n < 1 || n > 5)
            fail("dpi-active must be 1-5");
        s.dpi_active = static_cast<uint8_t>(n - 1);
        s.dpi_enabled_mask |= static_cast<uint8_t>(1 << (n - 1));
    } else if (key == "dpi-enabled") {
        uint8_t mask = 0;
        size_t pos = 0;
        while (pos <= val.size()) {
            size_t c = val.find(',', pos);
            long n = parse_long(val.substr(pos, c == std::string::npos ? std::string::npos : c - pos), key.c_str());
            if (n < 1 || n > 5)
                fail("DPI levels are 1-5");
            mask |= static_cast<uint8_t>(1 << (n - 1));
            if (c == std::string::npos)
                break;
            pos = c + 1;
        }
        if (!(mask & (1 << s.dpi_active)))
            fail("the active DPI level must stay enabled");
        s.dpi_enabled_mask = mask;
    } else if (key == "polling") {
        long hz = parse_long(val, key.c_str());
        int code = hz == 125 ? 0 : hz == 250 ? 1 : hz == 500 ? 2 : hz == 1000 ? 3 : -1;
        if (code < 0)
            fail("polling must be 125, 250, 500 or 1000");
        s.polling_rate = static_cast<uint8_t>(code);
    } else if (key == "angle-snapping") {
        s.angle_snapping = parse_bool(val) ? 1 : 0;
    } else if (key == "debounce") {
        long ms = parse_long(val, key.c_str());
        if (ms < 0 || ms > 255)
            fail("debounce must be 0-255 ms");
        s.debounce_ms = static_cast<uint8_t>(ms);
    } else if (key == "effect") {
        struct { const char *n; uint8_t v; } names[] = {
            {"off", ROSSAT_EFFECT_OFF}, {"fully-lit", ROSSAT_EFFECT_FULLY_LIT},
            {"static", ROSSAT_EFFECT_FULLY_LIT},
            {"blinking", ROSSAT_EFFECT_BLINKING}, {"breathing", ROSSAT_EFFECT_BREATHING},
            {"heartbeat", ROSSAT_EFFECT_HEARTBEAT}, {"color-cycle", ROSSAT_EFFECT_COLOR_CYCLE},
            {"snake", ROSSAT_EFFECT_SNAKE}, {"breathing-2", ROSSAT_EFFECT_BREATHING_2},
            {"heartbeat-2", ROSSAT_EFFECT_HEARTBEAT_2}, {"aimo", ROSSAT_EFFECT_AIMO},
            {"colorwave", ROSSAT_EFFECT_COLORWAVE}};
        bool found = false;
        for (const auto &n : names)
            if (val == n.n) {
                g_new_effect = n.v;
                found = true;
            }
        if (!found)
            g_new_effect = static_cast<int>(parse_long(val, key.c_str()) & 0xff);
    } else if (key == "render") {
        if (val != "engine" && val != "mouse")
            fail("render must be engine or mouse");
        g_new_render = val == "engine";
    } else if (key == "theme") {
        long th = parse_long(val, key.c_str());
        if (th < 1 || th > ROSSAT_AIMO_THEMES)
            fail("theme must be 1-10");
        s.color_theme = static_cast<uint8_t>(0x80 | (th - 1));
    } else if (key == "speed") {
        s.effect_speed = static_cast<uint8_t>(parse_long(val, key.c_str()) & 0xff);
    } else if (key == "brightness") {
        long b = parse_long(val, key.c_str());
        if (b < 0 || b > 255)
            fail("brightness must be 0-255");
        s.brightness = static_cast<uint8_t>(b);
    } else if (key == "led") {
        auto colon = val.rfind(':');
        if (colon == std::string::npos)
            fail("led expects SELECTION:#RRGGBB");
        rossat_color c;
        if (!parse_color(val.substr(colon + 1), &c))
            fail("invalid colour '%s'", val.substr(colon + 1).c_str());
        for (int i : led_selection(val.substr(0, colon)))
            s.leds[i] = c;
    } else if (key == "led-enable") {
        if (val == "all")
            s.led_enable_mask = 0x7ff;
        else if (val == "none")
            s.led_enable_mask = 0;
        else
            s.led_enable_mask = static_cast<uint16_t>(parse_long(val, key.c_str()) & 0x7ff);
    } else {
        fail("unknown setting '%s'", key.c_str());
    }
}

int cmd_set(const options &o)
{
    if (o.args.empty())
        fail("set needs at least one KEY=VALUE");
    rossat_device *dev = open_device(o);
    rossat_settings s;
    check(rossat_read_settings(dev, resolve_profile(dev, o), &s), "read settings");
    for (const auto &kv : o.args)
        apply_setting(s, kv);
    if (g_new_effect >= 0 || g_new_render >= 0) {
        rossat_device_info info;
        rossat_get_info(dev, &info);
        uint8_t eff = 0;
        int engine = 0;
        rossat_effective_effect(info.product_id, &s, &eff, &engine);
        if (g_new_effect >= 0)
            eff = static_cast<uint8_t>(g_new_effect);
        if (g_new_render >= 0)
            engine = g_new_render;
        if (g_new_render == 0 && rossat_effect_is_software(eff))
            fail("%s can only be rendered by the engine", rossat_effect_name(eff));
        check(rossat_select_effect(info.product_id, &s, eff, engine), "select effect");
    }
    check(rossat_write_settings(dev, &s), "write settings");
    rossat_close(dev);
    return 0;
}

int cmd_buttons(const options &o)
{
    rossat_device *dev = open_device(o);
    rossat_buttons b;
    check(rossat_read_buttons(dev, resolve_profile(dev, o), &b), "read buttons");
    print_buttons(b);
    rossat_close(dev);
    return 0;
}

int cmd_bind(const options &o)
{
    if (o.args.size() < 2)
        fail("bind needs BUTTON and TYPE");
    int btn = parse_button(o.args[0]);
    int type = rossat_button_type_from_string(o.args[1].c_str());
    if (type < 0)
        fail("unknown button type '%s' (see 'types')", o.args[1].c_str());
    rossat_device *dev = open_device(o);
    rossat_buttons b;
    check(rossat_read_buttons(dev, resolve_profile(dev, o), &b), "read buttons");
    b.buttons[btn].type = static_cast<uint8_t>(type);
    uint8_t mods = 0, key = 0;
    if (type == ROSSAT_BT_SHORTCUT && o.args.size() == 3 &&
        rossat_parse_shortcut(o.args[2].c_str(), &mods, &key) == ROSSAT_OK) {
        b.buttons[btn].param1 = mods;
        b.buttons[btn].param2 = key;
    } else {
        if (type == ROSSAT_BT_SHORTCUT && o.args.size() < 3)
            fail("shortcut needs a key combination, e.g. ctrl+c");
        b.buttons[btn].param1 = o.args.size() > 2 ? static_cast<uint8_t>(parse_long(o.args[2], "P1")) : 0;
        b.buttons[btn].param2 = o.args.size() > 3 ? static_cast<uint8_t>(parse_long(o.args[3], "P2")) : 0;
    }
    check(rossat_write_buttons(dev, &b), "write buttons");
    rossat_close(dev);
    return 0;
}

int cmd_reset_buttons(const options &o)
{
    rossat_device *dev = open_device(o);
    rossat_buttons b;
    rossat_buttons_default(resolve_profile(dev, o), &b);
    check(rossat_write_buttons(dev, &b), "write buttons");
    rossat_close(dev);
    return 0;
}

int cmd_types()
{
    for (int i = 0; i < rossat_button_type_count(); ++i) {
        uint8_t t = static_cast<uint8_t>(rossat_button_type_at(i));
        std::printf("  0x%02x  %-20s %s\n", t, rossat_button_type_key(t), rossat_button_type_name(t));
    }
    std::printf("\nshortcut takes a key combination: bind 9 shortcut ctrl+c / alt+f4 / shift+win+s\n"
                "(raw form: bind 9 shortcut MODMASK USAGE, mod bits 2 shift, 4 ctrl, 8 alt, 16 win).\n");
    return 0;
}

int cmd_macro(const options &o)
{
    int button = -1, delay = 20;
    std::string text, name = "rossat";
    bool have_text = false;
    for (size_t i = 0; i < o.args.size(); ++i) {
        const std::string &a = o.args[i];
        auto next = [&](const char *what) -> const std::string & {
            if (i + 1 >= o.args.size())
                fail("%s needs a value", what);
            return o.args[++i];
        };
        if (a == "-b" || a == "--button")
            button = parse_button(next("-b"));
        else if (a == "--text") {
            text = next("--text");
            have_text = true;
        } else if (a == "--delay")
            delay = static_cast<int>(parse_long(next("--delay"), "delay"));
        else if (a == "--name")
            name = next("--name");
        else
            fail("unknown macro option '%s'", a.c_str());
    }
    if (button < 0)
        fail("macro needs -b BUTTON");

    rossat_device *dev = open_device(o);
    uint8_t profile = resolve_profile(dev, o);
    if (!have_text) {
        rossat_macro m;
        check(rossat_read_macro(dev, profile, static_cast<uint8_t>(button), &m), "read macro");
        std::printf("Macro '%s' / '%s' (profile %d, button %s), mode %d, loop %d, %d events\n",
                    m.set_name, m.name, profile + 1, rossat_button_name(button), m.mode, m.loop, m.count);
        for (int i = 0; i < m.count; ++i)
            std::printf("  key 0x%02x %-7s then wait %d ms\n", m.keys[i].key,
                        m.keys[i].action == ROSSAT_KEY_PRESS ? "press" : "release", m.keys[i].delay_ms);
        rossat_close(dev);
        return 0;
    }

    static rossat_macro m;
    std::memset(&m, 0, sizeof m);
    m.profile = profile;
    m.button = static_cast<uint8_t>(button);
    m.loop = 1;
    m.mode = ROSSAT_MACRO_ON_PRESS;
    std::snprintf(m.set_name, sizeof m.set_name, "rossat");
    std::snprintf(m.name, sizeof m.name, "%s", name.c_str());
    auto push = [&](uint8_t key, uint8_t action) {
        if (m.count >= ROSSAT_MACRO_KEYSTROKES)
            fail("macro too long");
        m.keys[m.count++] = rossat_keystroke{key, action, static_cast<uint16_t>(delay)};
    };
    for (char ch : text) {
        uint8_t usage;
        bool shift;
        if (!ascii_to_hid(ch, &usage, &shift))
            fail("cannot type character '%s'", std::string(1, ch).c_str());
        if (shift)
            push(0xe1, ROSSAT_KEY_PRESS);
        push(usage, ROSSAT_KEY_PRESS);
        push(usage, ROSSAT_KEY_RELEASE);
        if (shift)
            push(0xe1, ROSSAT_KEY_RELEASE);
    }
    check(rossat_write_macro(dev, &m), "write macro");
    rossat_buttons b;
    check(rossat_read_buttons(dev, profile, &b), "read buttons");
    b.buttons[button] = rossat_button{ROSSAT_BT_MACRO, 0, 0};
    check(rossat_write_buttons(dev, &b), "bind macro");
    rossat_close(dev);
    return 0;
}

int cmd_direct(const options &o)
{
    if (o.args.empty())
        fail("direct needs off or colours");
    rossat_device *dev = open_device(o);
    if (o.args[0] == "off") {
        check(rossat_set_direct_lighting(dev, 0), "leave direct mode");
    } else {
        rossat_color colors[ROSSAT_LED_COUNT];
        std::string spec = o.args[0];
        std::vector<std::string> parts;
        size_t pos = 0;
        while (true) {
            size_t c = spec.find(',', pos);
            parts.push_back(spec.substr(pos, c == std::string::npos ? std::string::npos : c - pos));
            if (c == std::string::npos)
                break;
            pos = c + 1;
        }
        if (parts.size() != 1 && parts.size() != ROSSAT_LED_COUNT)
            fail("give one colour or 11 comma separated colours");
        for (int i = 0; i < ROSSAT_LED_COUNT; ++i)
            if (!parse_color(parts[parts.size() == 1 ? 0 : i], &colors[i]))
                fail("invalid colour '%s'", parts[parts.size() == 1 ? 0 : i].c_str());
        check(rossat_set_direct_lighting(dev, 1), "enter direct mode");
        check(rossat_set_direct_colors(dev, colors), "send colours");
    }
    rossat_close(dev);
    return 0;
}

int cmd_dump(const options &o)
{
    rossat_device *dev = open_device(o);
    uint8_t profile = resolve_profile(dev, o);
    uint8_t settings[ROSSAT_SETTINGS_SIZE];
    check(rossat_read_settings_raw(dev, profile, settings), "read settings");
    hexdump("Settings report 0x06", settings, sizeof settings);
    rossat_buttons b;
    check(rossat_read_buttons(dev, profile, &b), "read buttons");
    uint8_t buttons[3 * ROSSAT_BUTTON_COUNT];
    for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i) {
        buttons[3 * i] = b.buttons[i].type;
        buttons[3 * i + 1] = b.buttons[i].param1;
        buttons[3 * i + 2] = b.buttons[i].param2;
    }
    hexdump("Button entries (report 0x07 bytes 3..74)", buttons, sizeof buttons);
    rossat_close(dev);
    return 0;
}

volatile sig_atomic_t g_interrupted = 0;
void on_signal(int) { g_interrupted = 1; }

int cmd_shift_light(const options &o)
{
    rossat_device *dev = open_device(o);
    rossat_device_info info;
    rossat_get_info(dev, &info);
    uint8_t profile = resolve_profile(dev, o);
    rossat_settings st;
    check(rossat_read_settings(dev, profile, &st), "read settings");
    rossat_close(dev);
    int by_engine = 0;
    rossat_effective_effect(info.product_id, &st, nullptr, &by_engine);
    rossat_shift_lighting sl;
    check(rossat_shift_lighting_load(info.product_id, profile, &sl), "load");
    for (const auto &a : o.args) {
        rossat_color c;
        if (a == "on") sl.enabled = 1;
        else if (a == "off") sl.enabled = 0;
        else if (a == "solid") sl.style = ROSSAT_SHIFT_SOLID;
        else if (a == "pulse") sl.style = ROSSAT_SHIFT_PULSE;
        else if (parse_color(a, &c)) { sl.color = c; sl.enabled = 1; }
        else fail("unknown shift-light argument '%s'", a.c_str());
    }
    if (!o.args.empty())
        check(rossat_shift_lighting_save(info.product_id, profile, &sl), "save");
    std::printf("Profile %d Easy-Shift lighting: %s, %s, #%02x%02x%02x@%d\n", profile + 1,
                sl.enabled ? "on" : "off", sl.style == ROSSAT_SHIFT_PULSE ? "pulse" : "solid",
                sl.color.r, sl.color.g, sl.color.b, sl.color.a);
    if (sl.enabled && !by_engine)
        std::printf("Note: this profile's effect is rendered by the mouse, which always shows its own\n"
                    "light blue on Easy-Shift. Use `rossat-cli -p %d set render=engine` to use this colour.\n",
                    profile + 1);
    else if (sl.enabled)
        std::printf("Shown by the lighting engine (GUI switch, `rossat-cli engine` or the service).\n");
    return 0;
}

int cmd_tdcu(const options &o)
{
    rossat_device *dev = open_device(o);
    rossat_tdcu t;
    check(rossat_read_tdcu(dev, &t), "read TDCU");
    std::printf("TDCU mode %d (%s), state %d, value 0x%04x\n", t.mode,
                t.mode == 2 ? "custom calibration" : "factory", t.state, t.value);
    rossat_close(dev);
    return 0;
}

int cmd_calibrate(const options &o)
{
    rossat_device *dev = open_device(o);
    std::printf("TDCU calibration\n"
                "Put the mouse on the surface you use. After pressing Enter, keep moving\n"
                "it in large circles and figure-eights until calibration completes.\n"
                "Press Enter to start...");
    std::fflush(stdout);
    std::getchar();
    check(rossat_calibration_start(dev), "start calibration");
    std::printf("Calibrating - keep moving the mouse");
    std::fflush(stdout);
    int finished = 0;
    for (int i = 0; i < 600 && !finished; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (i < 10)
            continue; // give the sensor time to enter calibration
        check(rossat_calibration_poll(dev, &finished), "calibration");
        if (i % 10 == 0) {
            std::printf(".");
            std::fflush(stdout);
        }
    }
    std::printf("\n");
    if (!finished) {
        rossat_calibration_finish(dev, 0);
        fail("calibration did not finish within 60 s, discarded");
    }
    rossat_tdcu t;
    check(rossat_read_tdcu(dev, &t), "read TDCU");
    std::printf("Calibration finished (value 0x%04x). Save it? [Y/n] ", t.value);
    std::fflush(stdout);
    int c = std::getchar();
    if (c == '\n')
        c = std::getchar();
    bool save = !(c == 'n' || c == 'N');
    check(rossat_calibration_finish(dev, save ? 1 : 0), "finish calibration");
    std::printf(save ? "Saved.\n" : "Discarded.\n");
    rossat_close(dev);
    return 0;
}

int cmd_engine(const options &o)
{
    rossat_engine_mode mode = ROSSAT_ENGINE_AUTO;
    for (size_t i = 0; i < o.args.size(); ++i) {
        if (o.args[i] == "--mode" && i + 1 < o.args.size()) {
            const std::string &m = o.args[++i];
            mode = m == "aimo" ? ROSSAT_ENGINE_AIMO : m == "heartbeat" ? ROSSAT_ENGINE_HEARTBEAT
                 : m == "auto" ? ROSSAT_ENGINE_AUTO : (fail("unknown mode '%s'", m.c_str()), mode);
        } else {
            fail("unknown engine option '%s'", o.args[i].c_str());
        }
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    rossat_engine *e = nullptr;
    int r = rossat_engine_start(o.device, mode, &e);
    if (r == ROSSAT_ERR_BUSY)
        fail("another lighting engine is already running (GUI or service)");
    check(r, "start engine");
    std::printf("Lighting engine running (%s mode), Ctrl+C to stop.\n",
                mode == ROSSAT_ENGINE_AUTO ? "auto" : mode == ROSSAT_ENGINE_AIMO ? "aimo" : "heartbeat");
    int last_effect = -1, last_rendering = -1, last_error = 0;
    while (!g_interrupted) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        rossat_engine_status st;
        rossat_engine_get_status(e, &st);
        if (st.last_error != last_error && st.last_error != ROSSAT_OK)
            std::fprintf(stderr, "engine: %s (retrying)\n", rossat_strerror(st.last_error));
        last_error = st.last_error;
        if (st.profile != 0xff && (st.effect != last_effect || st.rendering != last_rendering)) {
            const char *n = rossat_effect_name(st.effect);
            std::printf("profile %d: %s - %s\n", st.profile + 1, n ? n : "unknown",
                        st.rendering ? "rendered by rossat" : "rendered by the mouse");
            std::fflush(stdout);
            last_effect = st.effect;
            last_rendering = st.rendering;
        }
    }
    rossat_engine_stop(e);
    std::printf("Lighting handed back to the mouse.\n");
    return 0;
}

// Backup format: "RSWB" v1, then per profile 126 settings bytes + 72 button bytes.
constexpr char BACKUP_MAGIC[4] = {'R', 'S', 'W', 'B'};
constexpr size_t BACKUP_PROFILE = ROSSAT_SETTINGS_SIZE + 3 * ROSSAT_BUTTON_COUNT;

int cmd_backup(const options &o)
{
    if (o.args.size() != 1)
        fail("backup needs a FILE");
    rossat_device *dev = open_device(o);
    std::vector<uint8_t> out = {'R', 'S', 'W', 'B', 1};
    for (uint8_t p = 0; p < ROSSAT_PROFILE_COUNT; ++p) {
        uint8_t raw[ROSSAT_SETTINGS_SIZE];
        check(rossat_read_settings_raw(dev, p, raw), "read settings");
        out.insert(out.end(), raw, raw + sizeof raw);
        rossat_buttons b;
        check(rossat_read_buttons(dev, p, &b), "read buttons");
        for (const auto &bt : b.buttons)
            out.insert(out.end(), {bt.type, bt.param1, bt.param2});
    }
    rossat_close(dev);
    FILE *f = std::fopen(o.args[0].c_str(), "wb");
    if (!f || std::fwrite(out.data(), 1, out.size(), f) != out.size())
        fail("cannot write '%s'", o.args[0].c_str());
    std::fclose(f);
    std::printf("Saved %d profiles to %s\n", ROSSAT_PROFILE_COUNT, o.args[0].c_str());
    return 0;
}

int cmd_restore(const options &o)
{
    if (o.args.size() != 1)
        fail("restore needs a FILE");
    FILE *f = std::fopen(o.args[0].c_str(), "rb");
    if (!f)
        fail("cannot open '%s'", o.args[0].c_str());
    std::vector<uint8_t> in(5 + ROSSAT_PROFILE_COUNT * BACKUP_PROFILE + 1);
    size_t n = std::fread(in.data(), 1, in.size(), f);
    std::fclose(f);
    if (n != in.size() - 1 || std::memcmp(in.data(), BACKUP_MAGIC, 4) != 0 || in[4] != 1)
        fail("'%s' is not a rossat backup", o.args[0].c_str());
    rossat_device *dev = open_device(o);
    for (uint8_t p = 0; p < ROSSAT_PROFILE_COUNT; ++p) {
        const uint8_t *blk = in.data() + 5 + p * BACKUP_PROFILE;
        uint8_t raw[ROSSAT_SETTINGS_SIZE];
        std::memcpy(raw, blk, sizeof raw);
        raw[2] = p;
        check(rossat_write_settings_raw(dev, raw), "write settings");
        rossat_buttons b;
        b.profile = p;
        for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i) {
            const uint8_t *e = blk + ROSSAT_SETTINGS_SIZE + 3 * i;
            b.buttons[i] = rossat_button{e[0], e[1], e[2]};
        }
        check(rossat_write_buttons(dev, &b), "write buttons");
    }
    rossat_close(dev);
    std::printf("Restored %d profiles from %s\n", ROSSAT_PROFILE_COUNT, o.args[0].c_str());
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    g_prog = argv[0] && std::strrchr(argv[0], '/') ? std::strrchr(argv[0], '/') + 1 : "rossat-cli";
    options o;
    std::string cmd;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(stdout);
            return 0;
        } else if (a == "--version") {
            std::printf("rossat-cli %s\n", rossat_version());
            return 0;
        } else if (a == "-d" || a == "--device") {
            if (++i >= argc)
                fail("--device needs a path");
            o.device = argv[i];
        } else if (a == "-p" || a == "--profile") {
            if (++i >= argc)
                fail("--profile needs a number");
            long n = parse_long(argv[i], "profile");
            if (n < 1 || n > ROSSAT_PROFILE_COUNT)
                fail("profile must be 1-5");
            o.profile = static_cast<int>(n - 1);
        } else if (cmd.empty()) {
            cmd = a;
        } else {
            o.args.push_back(a);
        }
    }

    int rc;
    if (cmd.empty()) {
        usage(stderr);
        return 2;
    } else if (cmd == "list") rc = cmd_list();
    else if (cmd == "info") rc = cmd_info(o);
    else if (cmd == "profile") rc = cmd_profile(o);
    else if (cmd == "show") rc = cmd_show(o);
    else if (cmd == "set") rc = cmd_set(o);
    else if (cmd == "buttons") rc = cmd_buttons(o);
    else if (cmd == "bind") rc = cmd_bind(o);
    else if (cmd == "reset-buttons") rc = cmd_reset_buttons(o);
    else if (cmd == "types") rc = cmd_types();
    else if (cmd == "macro") rc = cmd_macro(o);
    else if (cmd == "direct") rc = cmd_direct(o);
    else if (cmd == "dump") rc = cmd_dump(o);
    else if (cmd == "backup") rc = cmd_backup(o);
    else if (cmd == "calibrate") rc = cmd_calibrate(o);
    else if (cmd == "tdcu") rc = cmd_tdcu(o);
    else if (cmd == "shift-light") rc = cmd_shift_light(o);
    else if (cmd == "engine") rc = cmd_engine(o);
    else if (cmd == "restore") rc = cmd_restore(o);
    else {
        std::fprintf(stderr, "%s: unknown command '%s'\n\n", g_prog, cmd.c_str());
        usage(stderr);
        return 2;
    }
    rossat_exit();
    return rc;
}
