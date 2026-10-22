// Host-side settings the mouse cannot store (Easy-Shift[+] lighting).
// Format: $XDG_CONFIG_HOME/rossat/<pid>.ini with one [profileN] section per
// profile and keys shift_light=on|off, shift_style=solid|pulse,
// shift_color=#rrggbb, shift_brightness=0-255.

#include "internal.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <sys/stat.h>

namespace {

using ini = std::map<std::string, std::map<std::string, std::string>>;

std::string config_dir()
{
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        return std::string(xdg) + "/rossat";
    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/rossat";
}

std::string config_path(uint16_t pid)
{
    char name[32];
    std::snprintf(name, sizeof name, "/%04x.ini", pid);
    return config_dir() + name;
}

ini read_ini(const std::string &path)
{
    ini out;
    std::ifstream f(path);
    std::string line, section;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line[0] == '[') {
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        auto eq = line.find('=');
        if (eq != std::string::npos)
            out[section][line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

bool write_ini(const std::string &path, const ini &data)
{
    ::mkdir(config_dir().c_str(), 0755);
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f)
            return false;
        f << "# rossat-swarm host-side settings\n";
        for (const auto &sec : data) {
            f << "[" << sec.first << "]\n";
            for (const auto &kv : sec.second)
                f << kv.first << "=" << kv.second << "\n";
            f << "\n";
        }
        if (!f)
            return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::string section_name(uint8_t profile)
{
    return "profile" + std::to_string(profile + 1);
}

} // namespace

extern "C" int rossat_shift_lighting_load(uint16_t pid, uint8_t profile, rossat_shift_lighting *out)
{
    if (!out || profile >= ROSSAT_PROFILE_COUNT)
        return ROSSAT_ERR_ARGUMENT;
    // default: off, solid ROCCAT blue
    *out = rossat_shift_lighting{0, ROSSAT_SHIFT_SOLID, rossat_color{255, 0x65, 0xc3, 0xf7}};
    ini data = read_ini(config_path(pid));
    auto sec = data.find(section_name(profile));
    if (sec == data.end())
        return ROSSAT_OK;
    auto get = [&](const char *k) -> std::string {
        auto it = sec->second.find(k);
        return it == sec->second.end() ? "" : it->second;
    };
    out->enabled = get("shift_light") == "on";
    out->style = get("shift_style") == "pulse" ? ROSSAT_SHIFT_PULSE : ROSSAT_SHIFT_SOLID;
    std::string c = get("shift_color");
    if (c.size() == 7 && c[0] == '#') {
        unsigned long v = std::strtoul(c.c_str() + 1, nullptr, 16);
        out->color.r = (v >> 16) & 0xff;
        out->color.g = (v >> 8) & 0xff;
        out->color.b = v & 0xff;
    }
    std::string b = get("shift_brightness");
    if (!b.empty())
        out->color.a = static_cast<uint8_t>(std::strtoul(b.c_str(), nullptr, 10) & 0xff);
    return ROSSAT_OK;
}

extern "C" int rossat_shift_lighting_save(uint16_t pid, uint8_t profile, const rossat_shift_lighting *in)
{
    if (!in || profile >= ROSSAT_PROFILE_COUNT)
        return ROSSAT_ERR_ARGUMENT;
    std::string path = config_path(pid);
    ini data = read_ini(path);
    auto &sec = data[section_name(profile)];
    char color[8];
    std::snprintf(color, sizeof color, "#%02x%02x%02x", in->color.r, in->color.g, in->color.b);
    sec["shift_light"] = in->enabled ? "on" : "off";
    sec["shift_style"] = in->style == ROSSAT_SHIFT_PULSE ? "pulse" : "solid";
    sec["shift_color"] = color;
    sec["shift_brightness"] = std::to_string(in->color.a);
    return write_ini(path, data) ? ROSSAT_OK : ROSSAT_ERR_IO;
}

extern "C" int rossat_engine_effect_load(uint16_t pid, uint8_t profile, uint8_t *effect)
{
    if (!effect || profile >= ROSSAT_PROFILE_COUNT)
        return ROSSAT_ERR_ARGUMENT;
    *effect = ROSSAT_EFFECT_AIMO;
    ini data = read_ini(config_path(pid));
    auto sec = data.find(section_name(profile));
    if (sec != data.end()) {
        auto it = sec->second.find("engine_effect");
        if (it != sec->second.end() && !it->second.empty())
            *effect = static_cast<uint8_t>(std::strtoul(it->second.c_str(), nullptr, 10) & 0xff);
    }
    return ROSSAT_OK;
}

extern "C" int rossat_engine_effect_save(uint16_t pid, uint8_t profile, uint8_t effect)
{
    if (profile >= ROSSAT_PROFILE_COUNT)
        return ROSSAT_ERR_ARGUMENT;
    std::string path = config_path(pid);
    ini data = read_ini(path);
    data[section_name(profile)]["engine_effect"] = std::to_string(effect);
    return write_ini(path, data) ? ROSSAT_OK : ROSSAT_ERR_IO;
}

extern "C" void rossat_effective_effect(uint16_t pid, const rossat_settings *s, uint8_t *effect, int *by_engine)
{
    uint8_t e = s->effect;
    int engine = rossat_effect_is_software(e);
    if (e == ROSSAT_EFFECT_AIMO)
        rossat_engine_effect_load(pid, s->profile, &e);
    if (effect)
        *effect = e;
    if (by_engine)
        *by_engine = engine;
}

extern "C" int rossat_select_effect(uint16_t pid, rossat_settings *s, uint8_t effect, int by_engine)
{
    if (!s)
        return ROSSAT_ERR_ARGUMENT;
    if (rossat_effect_is_software(effect))
        by_engine = 1;
    if (!by_engine) {
        s->effect = effect;
        return ROSSAT_OK;
    }
    s->effect = ROSSAT_EFFECT_AIMO;
    return rossat_engine_effect_save(pid, s->profile, effect);
}
