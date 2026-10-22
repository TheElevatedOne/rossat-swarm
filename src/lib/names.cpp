// Human readable names and factory defaults for the Kone AIMO Remastered.

#include "rossat/rossat.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct type_entry {
    uint8_t type;
    const char *key;   // CLI spelling
    const char *label; // UI spelling
};

constexpr type_entry TYPES[] = {
    {ROSSAT_BT_UNUSED,            "unused",            "Unassigned"},
    {ROSSAT_BT_CLICK,             "click",             "Click"},
    {ROSSAT_BT_MENU,              "menu",              "Menu"},
    {ROSSAT_BT_UNIVERSAL_SCROLL,  "universal-scroll",  "Universal scrolling"},
    {ROSSAT_BT_DOUBLE_CLICK,      "double-click",      "Double click"},
    {ROSSAT_BT_SHORTCUT,          "shortcut",          "Shortcut"},
    {ROSSAT_BT_DISABLED,          "disabled",          "Disabled"},
    {ROSSAT_BT_BROWSER_FORWARD,   "forward",           "Browser forward"},
    {ROSSAT_BT_BROWSER_BACKWARD,  "backward",          "Browser backward"},
    {ROSSAT_BT_TILT_LEFT,         "tilt-left",         "Tilt left"},
    {ROSSAT_BT_TILT_RIGHT,        "tilt-right",        "Tilt right"},
    {ROSSAT_BT_SCROLL_UP,         "scroll-up",         "Scroll up"},
    {ROSSAT_BT_SCROLL_DOWN,       "scroll-down",       "Scroll down"},
    {ROSSAT_BT_QUICKLAUNCH,       "quicklaunch",       "Quicklaunch"},
    {ROSSAT_BT_PROFILE_CYCLE,     "profile-cycle",     "Profile cycle"},
    {ROSSAT_BT_PROFILE_UP,        "profile-up",        "Profile up"},
    {ROSSAT_BT_PROFILE_DOWN,      "profile-down",      "Profile down"},
    {ROSSAT_BT_DPI_CYCLE,         "dpi-cycle",         "DPI cycle"},
    {ROSSAT_BT_DPI_UP,            "dpi-up",            "DPI up"},
    {ROSSAT_BT_DPI_DOWN,          "dpi-down",          "DPI down"},
    {ROSSAT_BT_WINDOWS_KEY,       "windows-key",       "Windows key"},
    {ROSSAT_BT_OPEN_DRIVER,       "open-driver",       "Open driver"},
    {ROSSAT_BT_OPEN_PLAYER,       "open-player",       "Open media player"},
    {ROSSAT_BT_PREV_TRACK,        "prev-track",        "Previous track"},
    {ROSSAT_BT_NEXT_TRACK,        "next-track",        "Next track"},
    {ROSSAT_BT_PLAY_PAUSE,        "play-pause",        "Play / pause"},
    {ROSSAT_BT_STOP,              "stop",              "Stop"},
    {ROSSAT_BT_MUTE,              "mute",              "Mute"},
    {ROSSAT_BT_VOLUME_UP,         "volume-up",         "Volume up"},
    {ROSSAT_BT_VOLUME_DOWN,       "volume-down",       "Volume down"},
    {ROSSAT_BT_MACRO,             "macro",             "Macro"},
    {ROSSAT_BT_TIMER,             "timer",             "Timer"},
    {ROSSAT_BT_TIMER_STOP,        "timer-stop",        "Timer stop"},
    {ROSSAT_BT_EASYAIM_1,         "easyaim-1",         "EasyAim DPI level 1"},
    {ROSSAT_BT_EASYAIM_2,         "easyaim-2",         "EasyAim DPI level 2"},
    {ROSSAT_BT_EASYAIM_3,         "easyaim-3",         "EasyAim DPI level 3"},
    {ROSSAT_BT_EASYAIM_4,         "easyaim-4",         "EasyAim DPI level 4"},
    {ROSSAT_BT_EASYAIM_5,         "easyaim-5",         "EasyAim DPI level 5"},
    {ROSSAT_BT_EASYSHIFT,         "easyshift",         "Easy-Shift[+]"},
    {ROSSAT_BT_EASYSHIFT_OTHER,   "easyshift-other",   "Easy-Shift[+] other devices"},
    {ROSSAT_BT_EASYSHIFT_LOCK,    "easyshift-lock",    "Easy-Shift[+] lock"},
    {ROSSAT_BT_EASYSHIFT_ALL,     "easyshift-all",     "Easy-Shift[+] all devices"},
    {ROSSAT_BT_EASYSHIFT_LOCK_ALL,"easyshift-lock-all","Easy-Shift[+] lock all devices"},
    {ROSSAT_BT_BRIGHTNESS_UP,     "brightness-up",     "Brightness step up (engine)"},
    {ROSSAT_BT_BRIGHTNESS_DOWN,   "brightness-down",   "Brightness step down (engine)"},
};

constexpr const char *BUTTON_NAMES[ROSSAT_BUTTON_COUNT / 2] = {
    "Left button",   "Right button",   "Middle button", "Wheel tilt left",
    "Wheel tilt right", "Wheel up",    "Wheel down",    "Thumb forward",
    "Thumb backward", "Thumb Easy-Shift[+]", "DPI up",  "DPI down",
};

// Factory layout from the Swarm module (FUN_6a44ea50).
constexpr uint8_t DEFAULT_LAYOUT[ROSSAT_BUTTON_COUNT] = {
    ROSSAT_BT_CLICK,      ROSSAT_BT_MENU,        ROSSAT_BT_UNIVERSAL_SCROLL,
    ROSSAT_BT_TILT_LEFT,  ROSSAT_BT_TILT_RIGHT,  ROSSAT_BT_SCROLL_UP,
    ROSSAT_BT_SCROLL_DOWN, ROSSAT_BT_BROWSER_FORWARD, ROSSAT_BT_BROWSER_BACKWARD,
    ROSSAT_BT_EASYSHIFT,  ROSSAT_BT_DPI_UP,      ROSSAT_BT_DPI_DOWN,
    // Easy-Shift[+] layer
    ROSSAT_BT_UNUSED,     ROSSAT_BT_UNUSED,      ROSSAT_BT_PLAY_PAUSE,
    ROSSAT_BT_PREV_TRACK, ROSSAT_BT_NEXT_TRACK,  ROSSAT_BT_VOLUME_UP,
    ROSSAT_BT_VOLUME_DOWN, ROSSAT_BT_UNUSED,     ROSSAT_BT_UNUSED,
    ROSSAT_BT_UNUSED,     ROSSAT_BT_PROFILE_UP,  ROSSAT_BT_PROFILE_DOWN,
};

struct key_entry {
    uint8_t usage;
    const char *name;
};

constexpr key_entry KEYS[] = {
    {0x28, "enter"}, {0x29, "esc"}, {0x2a, "backspace"}, {0x2b, "tab"}, {0x2c, "space"},
    {0x2d, "minus"}, {0x2e, "equal"}, {0x2f, "leftbrace"}, {0x30, "rightbrace"}, {0x31, "backslash"},
    {0x33, "semicolon"}, {0x34, "apostrophe"}, {0x35, "grave"}, {0x36, "comma"}, {0x37, "dot"},
    {0x38, "slash"}, {0x39, "capslock"}, {0x46, "printscreen"}, {0x47, "scrolllock"},
    {0x48, "pause"}, {0x49, "insert"}, {0x4a, "home"}, {0x4b, "pageup"}, {0x4c, "delete"},
    {0x4d, "end"}, {0x4e, "pagedown"}, {0x4f, "right"}, {0x50, "left"}, {0x51, "down"},
    {0x52, "up"}, {0x53, "numlock"}, {0x54, "kpslash"}, {0x55, "kpasterisk"}, {0x56, "kpminus"},
    {0x57, "kpplus"}, {0x58, "kpenter"}, {0x63, "kpdot"}, {0x65, "menu"},
};

std::string lower(const std::string &in)
{
    std::string out;
    for (char c : in)
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int key_from_name(const std::string &n)
{
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z')
        return 0x04 + (n[0] - 'a');
    if (n.size() == 1 && n[0] >= '1' && n[0] <= '9')
        return 0x1e + (n[0] - '1');
    if (n == "0")
        return 0x27;
    if (n.size() >= 2 && n[0] == 'f') {
        char *end = nullptr;
        long f = std::strtol(n.c_str() + 1, &end, 10);
        if (*end == '\0' && f >= 1 && f <= 12)
            return static_cast<int>(0x3a + f - 1);
        if (*end == '\0' && f >= 13 && f <= 24)
            return static_cast<int>(0x68 + f - 13);
    }
    if (n.size() == 3 && n[0] == 'k' && n[1] == 'p' && n[2] >= '0' && n[2] <= '9')
        return n[2] == '0' ? 0x62 : 0x59 + (n[2] - '1');
    for (const auto &k : KEYS)
        if (n == k.name)
            return k.usage;
    if (n.rfind("0x", 0) == 0) {
        char *end = nullptr;
        long v = std::strtol(n.c_str(), &end, 16);
        if (*end == '\0' && v > 0 && v <= 0xff)
            return static_cast<int>(v);
    }
    return -1;
}

} // namespace

extern "C" {

int rossat_parse_shortcut(const char *text, uint8_t *modifiers, uint8_t *key)
{
    if (!text || !modifiers || !key)
        return ROSSAT_ERR_ARGUMENT;
    std::string s = lower(text);
    uint8_t mods = 0;
    int k = -1;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t plus = s.find('+', pos);
        std::string part = s.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos);
        if (part.empty() && plus != std::string::npos) { // "ctrl++" -> plus key
            part = "equal";
        }
        if (part == "shift") mods |= ROSSAT_MOD_SHIFT;
        else if (part == "ctrl" || part == "control") mods |= ROSSAT_MOD_CTRL;
        else if (part == "alt") mods |= ROSSAT_MOD_ALT;
        else if (part == "win" || part == "super" || part == "meta") mods |= ROSSAT_MOD_WIN;
        else {
            if (k >= 0)
                return ROSSAT_ERR_ARGUMENT; // two non-modifier keys
            k = key_from_name(part);
            if (k < 0)
                return ROSSAT_ERR_ARGUMENT;
        }
        if (plus == std::string::npos)
            break;
        pos = plus + 1;
    }
    if (k < 0)
        return ROSSAT_ERR_ARGUMENT;
    *modifiers = mods;
    *key = static_cast<uint8_t>(k);
    return ROSSAT_OK;
}

void rossat_format_shortcut(uint8_t modifiers, uint8_t key, char *buf, size_t len)
{
    if (!buf || len == 0)
        return;
    std::string out;
    if (modifiers & ROSSAT_MOD_CTRL) out += "ctrl+";
    if (modifiers & ROSSAT_MOD_SHIFT) out += "shift+";
    if (modifiers & ROSSAT_MOD_ALT) out += "alt+";
    if (modifiers & ROSSAT_MOD_WIN) out += "win+";
    char tmp[8];
    if (key >= 0x04 && key <= 0x1d) out += static_cast<char>('a' + key - 0x04);
    else if (key >= 0x1e && key <= 0x26) out += static_cast<char>('1' + key - 0x1e);
    else if (key == 0x27) out += '0';
    else if (key >= 0x3a && key <= 0x45) { std::snprintf(tmp, sizeof tmp, "f%d", key - 0x3a + 1); out += tmp; }
    else if (key >= 0x68 && key <= 0x73) { std::snprintf(tmp, sizeof tmp, "f%d", key - 0x68 + 13); out += tmp; }
    else {
        const char *name = nullptr;
        for (const auto &k : KEYS)
            if (k.usage == key)
                name = k.name;
        if (name) out += name;
        else { std::snprintf(tmp, sizeof tmp, "0x%02x", key); out += tmp; }
    }
    std::snprintf(buf, len, "%s", out.c_str());
}


const char *rossat_button_name(int index)
{
    static std::string shifted[ROSSAT_BUTTON_COUNT / 2];
    if (index < 0 || index >= ROSSAT_BUTTON_COUNT)
        return "?";
    if (index < ROSSAT_BUTTON_COUNT / 2)
        return BUTTON_NAMES[index];
    int base = index - ROSSAT_BUTTON_COUNT / 2;
    if (shifted[base].empty())
        shifted[base] = std::string("Easy-Shift + ") + BUTTON_NAMES[base];
    return shifted[base].c_str();
}

const char *rossat_button_type_name(uint8_t type)
{
    for (const auto &t : TYPES)
        if (t.type == type)
            return t.label;
    return nullptr;
}

int rossat_button_type_from_string(const char *s)
{
    if (!s || !*s)
        return -1;
    std::string k;
    for (const char *p = s; *p; ++p)
        k += (*p == '_' || *p == ' ') ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    for (const auto &t : TYPES)
        if (k == t.key)
            return t.type;
    char *end = nullptr;
    long v = std::strtol(s, &end, 0);
    if (end && *end == '\0' && v >= 0 && v <= 0xff)
        return static_cast<int>(v);
    return -1;
}

void rossat_buttons_default(uint8_t profile, rossat_buttons *out)
{
    if (!out)
        return;
    std::memset(out, 0, sizeof *out);
    out->profile = profile;
    for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i)
        out->buttons[i].type = DEFAULT_LAYOUT[i];
}

int rossat_button_type_count(void)
{
    return static_cast<int>(sizeof TYPES / sizeof TYPES[0]);
}

int rossat_button_type_at(int i)
{
    return i >= 0 && i < rossat_button_type_count() ? TYPES[i].type : -1;
}

const char *rossat_button_type_key(uint8_t type)
{
    for (const auto &t : TYPES)
        if (t.type == type)
            return t.key;
    return nullptr;
}

} // extern "C"
