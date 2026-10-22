// Lighting engine: renders the effects the Kone AIMO firmware cannot draw
// itself (AIMO, Heartbeat, Breathing 2.0, Heartbeat 2.0) and streams them to
// the mouse, the way Swarm's aimo.dll does.
//
// Protocol (verified on hardware): [0e 06 01 01 00 ff] hands the LEDs to the
// host, then report 0x0d = [0d 2e] + 11 x [R G B 0] sets them;
// [0e 06 01 00 00 ff] gives control back to the firmware effect.

#include "internal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {

constexpr double PI = 3.14159265358979323846;

// AIMO colour themes from Swarm's data/Swarm/aimo_color_theme.dat.
constexpr uint8_t THEMES[ROSSAT_AIMO_THEMES][3][3] = {
    {{0xff, 0x00, 0x00}, {0xff, 0x50, 0x00}, {0xe6, 0x8c, 0x00}},
    {{0x00, 0x00, 0xff}, {0x4b, 0x4b, 0xff}, {0x96, 0x96, 0xff}},
    {{0x5a, 0x13, 0xff}, {0x9d, 0x14, 0xff}, {0xaf, 0x0d, 0xdb}},
    {{0xc5, 0x0b, 0xdc}, {0xf1, 0x7f, 0xff}, {0xd0, 0x01, 0x57}},
    {{0xf6, 0xff, 0x39}, {0xf9, 0xc2, 0x00}, {0xf5, 0x68, 0x00}},
    {{0xc8, 0xff, 0x00}, {0x64, 0xff, 0x05}, {0x00, 0xff, 0x0a}},
    {{0x00, 0xff, 0x00}, {0x00, 0xff, 0x32}, {0x00, 0xff, 0x64}},
    {{0xf5, 0x4c, 0x6c}, {0xf6, 0x7e, 0x47}, {0xf9, 0xc2, 0x00}},
    {{0x00, 0x00, 0xff}, {0x00, 0x7f, 0x98}, {0x00, 0xff, 0x32}},
    {{0xf4, 0x00, 0x6b}, {0xf4, 0x00, 0x00}, {0xf6, 0x78, 0xa5}},
};

// Physical LED positions: side (-1 left, 0 wheel, 1 right) and depth
// (0 = front, 1 = rear), plus which theme colour Swarm assigns to it.
struct led_pos {
    double side, depth;
    int theme_slot;
};
constexpr led_pos LEDS[ROSSAT_LED_COUNT] = {
    {0, -0.2, 0},                                                  // wheel
    {-1, 0.0, 0}, {-1, 0.25, 1}, {-1, 0.5, 1}, {-1, 0.75, 2},      // left 1-4
    {1, 0.0, 0},  {1, 0.25, 1},  {1, 0.5, 1},  {1, 0.75, 2},       // right 1-4
    {-1, 1.0, 2}, {1, 1.0, 2},                                     // left/right 5
};

struct rgb {
    double r, g, b;
};

rgb mix(rgb a, rgb b, double t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

rgb theme_color(int theme, int slot)
{
    const uint8_t *c = THEMES[theme][slot];
    return {c[0] / 255.0, c[1] / 255.0, c[2] / 255.0};
}

double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

double gauss(double x, double mu, double sigma)
{
    double d = (x - mu) / sigma;
    return std::exp(-0.5 * d * d);
}

// One heartbeat: a strong beat followed by a weaker one, then rest.
double heartbeat(double phase)
{
    phase -= std::floor(phase);
    return clamp01(gauss(phase, 0.10, 0.045) + 0.65 * gauss(phase, 0.32, 0.05));
}

// Speed byte 1..11 (Swarm slider) -> rate multiplier.
double rate(uint8_t speed)
{
    int s = speed < 1 ? 1 : speed > 11 ? 11 : speed;
    return 0.35 + 0.13 * s;
}

int theme_index(uint8_t color_theme)
{
    return (color_theme & 0x7f) % ROSSAT_AIMO_THEMES;
}

rgb led_color(const rossat_settings &s, int i)
{
    const rossat_color &c = s.leds[i];
    double a = c.a / 255.0;
    return {c.r / 255.0 * a, c.g / 255.0 * a, c.b / 255.0 * a};
}

uint8_t to8(double v) { return static_cast<uint8_t>(std::lround(clamp01(v) * 255)); }

rgb hue(double h)
{
    h = (h - std::floor(h)) * 6.0;
    double x = 1 - std::fabs(std::fmod(h, 2.0) - 1);
    switch (static_cast<int>(h)) {
    case 0: return {1, x, 0};
    case 1: return {x, 1, 0};
    case 2: return {0, 1, x};
    case 3: return {0, x, 1};
    case 4: return {x, 0, 1};
    default: return {1, 0, x};
    }
}

// Order of the LEDs around the mouse for running-light effects:
// wheel, left side front to back, right side back to front.
constexpr int RING[ROSSAT_LED_COUNT] = {0, 1, 2, 3, 4, 9, 10, 8, 7, 6, 5};

int ring_pos(int led)
{
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i)
        if (RING[i] == led)
            return i;
    return 0;
}

// LED mode switch without the status wait, so Easy-Shift feedback is instant.
int set_led_mode_fast(rossat_device *dev, bool host)
{
    const uint8_t buf[6] = {0x0e, 6, 1, static_cast<uint8_t>(host ? 1 : 0), static_cast<uint8_t>(host ? 0xff : 0), 0xff};
    return hid_send_feature_report(dev->hid, buf, sizeof buf) < 0 ? ROSSAT_ERR_IO : ROSSAT_OK;
}

} // namespace

extern "C" void rossat_aimo_theme(int theme, rossat_color out[3])
{
    if (theme < 0 || theme >= ROSSAT_AIMO_THEMES)
        theme = 0;
    for (int i = 0; i < 3; ++i)
        out[i] = rossat_color{255, THEMES[theme][i][0], THEMES[theme][i][1], THEMES[theme][i][2]};
}

extern "C" void rossat_render_frame(const rossat_settings *s, uint8_t effect, double t, double activity,
                                    rossat_color out[ROSSAT_LED_COUNT])
{
    double level = s->brightness / 255.0;
    double f = rate(s->effect_speed);
    activity = clamp01(activity);
    int theme = theme_index(s->color_theme);

    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        const led_pos &p = LEDS[i];
        rgb c{0, 0, 0};
        switch (effect) {
        case ROSSAT_EFFECT_OFF:
            break;
        case ROSSAT_EFFECT_FULLY_LIT:
            c = led_color(*s, i);
            break;
        case ROSSAT_EFFECT_BLINKING:
            c = std::fmod(t * f * 0.8, 1.0) < 0.5 ? led_color(*s, i) : rgb{0, 0, 0};
            break;
        case ROSSAT_EFFECT_BREATHING: {
            double v = 0.5 - 0.5 * std::cos(2 * PI * t * f * 0.3);
            c = mix({0, 0, 0}, led_color(*s, i), v * v);
            break;
        }
        case ROSSAT_EFFECT_COLOR_CYCLE:
            c = hue(t * f * 0.08);
            break;
        case ROSSAT_EFFECT_SNAKE: {
            // a bright head with a fading tail running around the mouse
            double head = std::fmod(t * f * 4.0, static_cast<double>(ROSSAT_LED_COUNT));
            double d = head - ring_pos(i);
            if (d < 0)
                d += ROSSAT_LED_COUNT;
            c = mix({0, 0, 0}, led_color(*s, i), clamp01(1.0 - d / 4.0));
            break;
        }
        case ROSSAT_EFFECT_COLORWAVE:
            c = hue(t * f * 0.12 - p.depth * 0.35 - (p.side > 0 ? 0.05 : 0));
            break;
        case ROSSAT_EFFECT_HEARTBEAT:
            c = led_color(*s, i);
            c = mix({0, 0, 0}, c, heartbeat(t * f * 0.75));
            break;
        case ROSSAT_EFFECT_HEARTBEAT_2:
            // the beat travels from the front of the mouse to the back
            c = mix({0, 0, 0}, led_color(*s, i), heartbeat(t * f * 0.75 - 0.18 * (p.depth + 0.2)));
            break;
        case ROSSAT_EFFECT_BREATHING_2: {
            double v = 0.5 - 0.5 * std::cos(2 * PI * (t * f * 0.3 - 0.45 * (p.depth + 0.2)));
            c = mix({0, 0, 0}, led_color(*s, i), v * v);
            break;
        }
        case ROSSAT_EFFECT_AIMO:
        default: {
            // Slowly drifting colour field over the three theme colours. Each
            // LED starts at its own theme slot; activity brightens and
            // lightens the colours.
            double drift = std::sin(t * f * 0.55 + p.depth * 2.4 + p.side * 0.9) *
                           std::cos(t * f * 0.31 - p.depth * 1.3);
            double pos = p.theme_slot + 1.2 * drift;          // -1.2 .. 3.2
            pos = std::fmod(std::fmod(pos, 3.0) + 3.0, 3.0);  // wrap 0..3
            int a = static_cast<int>(pos) % 3, b = (a + 1) % 3;
            double w = pos - std::floor(pos);
            w = w * w * (3 - 2 * w);
            c = mix(theme_color(theme, a), theme_color(theme, b), w);
            double glow = 0.62 + 0.38 * (0.5 + 0.5 * std::sin(t * f * 0.9 + p.depth * 3.1 + p.side));
            glow = glow + (1.0 - glow) * activity;
            c = mix(c, {1, 1, 1}, 0.25 * activity * activity);
            c = {c.r * glow, c.g * glow, c.b * glow};
            break;
        }
        }
        bool on = (s->led_enable_mask >> i) & 1;
        out[i] = on ? rossat_color{255, to8(c.r * level), to8(c.g * level), to8(c.b * level)}
                    : rossat_color{255, 0, 0, 0};
    }
}

/* ---- engine thread ----------------------------------------------------- */

struct rossat_engine {
    std::string path;
    rossat_engine_mode mode = ROSSAT_ENGINE_AUTO;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<bool> reload{true};
    int lock_fd = -1;
    std::mutex status_mutex;
    rossat_engine_status status{};
};

namespace {

using clock_type = std::chrono::steady_clock;

int acquire_lock()
{
    const char *dir = std::getenv("XDG_RUNTIME_DIR");
    std::string path = std::string(dir && *dir ? dir : "/tmp") + "/rossat-engine.lock";
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void set_status(rossat_engine *e, const rossat_engine_status &s)
{
    std::lock_guard<std::mutex> g(e->status_mutex);
    e->status = s;
}

void run(rossat_engine *e)
{
    rossat_engine_status st{};
    st.running = 1;
    st.profile = 0xff; // unknown until the first read
    rossat_device *dev = nullptr;
    rossat_settings settings{};
    rossat_shift_lighting shift{};
    uint8_t engine_effect = ROSSAT_EFFECT_AIMO;
    // brightness changed by a button, written to the mouse once presses stop
    bool brightness_dirty = false;
    auto brightness_changed_at = clock_type::now();
    auto last_mode_check = clock_type::now();
    bool shift_held = false;
    uint8_t profile = 0xff;
    bool rendering = false;
    double t = 0, activity = 0;
    uint8_t last_buttons = 0;
    rossat_color last[ROSSAT_LED_COUNT];
    std::memset(last, 0xff, sizeof last);
    auto last_profile_check = clock_type::now() - std::chrono::seconds(10);
    auto prev = clock_type::now();

    auto fail = [&](int err) {
        st.last_error = err;
        st.rendering = 0;
        rendering = false;
        if (dev)
            rossat_close(dev);
        dev = nullptr;
        set_status(e, st);
    };

    while (!e->stop) {
        if (!dev) {
            int r = rossat_open(e->path.empty() ? nullptr : e->path.c_str(), &dev);
            if (r != ROSSAT_OK) {
                fail(r);
                for (int i = 0; i < 20 && !e->stop; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            profile = 0xff;
            e->reload = true;
            std::memset(last, 0xff, sizeof last);
        }

        auto now = clock_type::now();
        if (e->reload || now - last_profile_check > std::chrono::seconds(2)) {
            last_profile_check = now;
            uint8_t active = 0;
            int r = rossat_get_active_profile(dev, &active, nullptr);
            if (r != ROSSAT_OK) {
                fail(r);
                continue;
            }
            if (active != profile || e->reload.exchange(false)) {
                r = rossat_read_settings(dev, active, &settings);
                if (r != ROSSAT_OK) {
                    fail(r);
                    continue;
                }
                profile = active;
                rossat_shift_lighting_load(dev->info.product_id, profile, &shift);
                rossat_engine_effect_load(dev->info.product_id, profile, &engine_effect);
                std::memset(last, 0xff, sizeof last);
            }
        }

        uint8_t effect = settings.effect;
        bool engine_rendered = rossat_effect_is_software(effect);
        if (effect == ROSSAT_EFFECT_AIMO)
            effect = engine_effect; // AIMO on the mouse = "rendered by rossat"
        if (e->mode == ROSSAT_ENGINE_AIMO)
            effect = ROSSAT_EFFECT_AIMO;
        else if (e->mode == ROSSAT_ENGINE_HEARTBEAT)
            effect = ROSSAT_EFFECT_HEARTBEAT;
        bool shift_light = shift_held && shift.enabled;
        bool software = engine_rendered || e->mode != ROSSAT_ENGINE_AUTO || shift_light;
        if (software != rendering) {
            // Easy-Shift transitions must be immediate; profile changes can wait
            // for the device's acknowledgement.
            int r = (shift_light || (!software && shift.enabled)) ? set_led_mode_fast(dev, software)
                                                                  : rossat_set_direct_lighting(dev, software ? 1 : 0);
            if (r != ROSSAT_OK) {
                fail(r);
                continue;
            }
            rendering = software;
            std::memset(last, 0xff, sizeof last);
        }

        // Pace the loop by reading input reports for ~33 ms; mouse motion and
        // clicks feed the AIMO activity level, report 0x03 events (profile /
        // DPI changes) trigger a settings reload.
        auto frame_end = clock_type::now() + std::chrono::milliseconds(33);
        while (!e->stop) {
            int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            frame_end - clock_type::now()).count());
            if (left <= 0)
                break;
            unsigned char in[64];
            int n = hid_read_timeout(dev->hid, in, sizeof in, left);
            if (n < 0) {
                fail(ROSSAT_ERR_IO);
                break;
            }
            if (n == 0)
                break;
            if (in[0] == 0x01 && n > 1) {
                activity += 0.004;
                if ((in[1] & ~last_buttons) != 0)
                    activity += 0.35;
                last_buttons = in[1];
            } else if (in[0] == 0x03 && n >= 4 && in[2] == 0xee) {
                // Easy-Shift[+]: 03 00 ee 02 00 = pressed, 03 00 ee 00 00 = released
                bool held = in[3] != 0;
                // the firmware paints its own Easy-Shift indicator in host
                // mode; always redraw over it
                std::memset(last, 0xff, sizeof last);
                if (held != shift_held) {
                    shift_held = held;
                    if (shift.enabled)
                        break; // react this frame
                }
            } else if (in[0] == 0x03 && n >= 5 && in[2] == 0xf2) {
                // host action buttons: 03 00 f2 N 00 = press, ... 01 = release
                if (in[4] == 0 && profile != 0xff && (in[3] == 1 || in[3] == 2)) {
                    // logarithmic steps: snap to the nearest level, then move one
                    static constexpr int levels[ROSSAT_BRIGHTNESS_LEVELS] = ROSSAT_BRIGHTNESS_LEVEL_TABLE;
                    int idx = 0;
                    for (int i = 1; i < ROSSAT_BRIGHTNESS_LEVELS; ++i)
                        if (std::abs(levels[i] - settings.brightness) < std::abs(levels[idx] - settings.brightness))
                            idx = i;
                    idx = std::clamp(idx + (in[3] == 1 ? 1 : -1), 0, ROSSAT_BRIGHTNESS_LEVELS - 1);
                    settings.brightness = static_cast<uint8_t>(levels[idx]);
                    brightness_dirty = true;
                    brightness_changed_at = clock_type::now();
                    std::memset(last, 0xff, sizeof last);
                }
            } else if (in[0] == 0x03 && n >= 3 && in[2] != 0xef) {
                e->reload = true; // profile / DPI / other device events
            }
        }
        if (!dev)
            continue;

        // Persist button brightness changes once the presses stop (the
        // firmware effects only see the value stored in the profile).
        if (brightness_dirty && clock_type::now() - brightness_changed_at > std::chrono::milliseconds(400)) {
            brightness_dirty = false;
            int r = rossat_write_settings(dev, &settings);
            if (r != ROSSAT_OK) {
                fail(r);
                continue;
            }
            // storing a profile makes the firmware take the LEDs back
            if (rendering && set_led_mode_fast(dev, true) != ROSSAT_OK) {
                fail(ROSSAT_ERR_IO);
                continue;
            }
            std::memset(last, 0xff, sizeof last);
        }

        // Any profile write (GUI, CLI, Swarm) returns the LEDs to the
        // firmware; check once a second and take them back.
        if (rendering && clock_type::now() - last_mode_check > std::chrono::seconds(1)) {
            last_mode_check = clock_type::now();
            uint8_t mode[6] = {0x0e};
            if (hid_get_feature_report(dev->hid, mode, sizeof mode) == static_cast<int>(sizeof mode) && mode[3] == 0) {
                if (set_led_mode_fast(dev, true) != ROSSAT_OK) {
                    fail(ROSSAT_ERR_IO);
                    continue;
                }
                std::memset(last, 0xff, sizeof last);
            }
        }

        now = clock_type::now();
        double dt = std::chrono::duration<double>(now - prev).count();
        prev = now;
        activity = std::min(1.0, activity) * std::exp(-dt * 1.2);
        t += dt * (1.0 + 1.5 * activity);

        st.profile = profile;
        st.effect = effect;
        st.rendering = rendering;
        st.last_error = ROSSAT_OK;
        st.shift_held = shift_held;
        if (rendering) {
            rossat_color frame[ROSSAT_LED_COUNT];
            if (shift_light) {
                double k = shift.color.a / 255.0;
                if (shift.style == ROSSAT_SHIFT_PULSE)
                    k *= 0.55 + 0.45 * std::sin(t * 6.0);
                for (int i = 0; i < ROSSAT_LED_COUNT; ++i)
                    frame[i] = rossat_color{255, static_cast<uint8_t>(shift.color.r * k),
                                            static_cast<uint8_t>(shift.color.g * k),
                                            static_cast<uint8_t>(shift.color.b * k)};
            } else {
                rossat_render_frame(&settings, effect, t, activity, frame);
            }
            if (std::memcmp(frame, last, sizeof frame) != 0) {
                int r = rossat_set_direct_colors(dev, frame);
                if (r != ROSSAT_OK) {
                    fail(r);
                    continue;
                }
                std::memcpy(last, frame, sizeof frame);
            }
            std::memcpy(st.leds, frame, sizeof frame);
        }
        set_status(e, st);
    }

    if (dev) {
        if (rendering)
            rossat_set_direct_lighting(dev, 0);
        rossat_close(dev);
    }
    st.running = 0;
    st.rendering = 0;
    set_status(e, st);
}

} // namespace

extern "C" int rossat_engine_start(const char *path, rossat_engine_mode mode, rossat_engine **out)
{
    if (!out)
        return ROSSAT_ERR_ARGUMENT;
    *out = nullptr;
    int r = rossat_init();
    if (r != ROSSAT_OK)
        return r;
    int fd = acquire_lock();
    if (fd < 0)
        return ROSSAT_ERR_BUSY;
    auto *e = new rossat_engine;
    e->path = path ? path : "";
    e->mode = mode;
    e->lock_fd = fd;
    e->status.running = 1;
    e->status.profile = 0xff;
    e->worker = std::thread(run, e);
    *out = e;
    return ROSSAT_OK;
}

extern "C" void rossat_engine_stop(rossat_engine *e)
{
    if (!e)
        return;
    e->stop = true;
    if (e->worker.joinable())
        e->worker.join();
    if (e->lock_fd >= 0)
        ::close(e->lock_fd);
    delete e;
}

extern "C" void rossat_engine_get_status(rossat_engine *e, rossat_engine_status *out)
{
    if (!e || !out)
        return;
    std::lock_guard<std::mutex> g(e->status_mutex);
    *out = e->status;
}

extern "C" void rossat_engine_reload(rossat_engine *e)
{
    if (e)
        e->reload = true;
}
