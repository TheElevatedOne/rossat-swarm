// rossat-swarm core library: HID transport and Kone AIMO Remastered protocol.
//
// Report flow mirrors ROCCAT_Swarm_Monitor.exe (kone_aimo_base_device_c):
//   * every SetFeature is followed by polling report 0x04 until the device
//     reports "ready" (status byte 1) / "invalid" (2) instead of "busy" (3);
//   * per-profile data is read by first writing a 0x04 "select" report and
//     then fetching the data report with GetFeature.

#include "internal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint8_t REPORT_CONTROL  = 0x04;
constexpr uint8_t REPORT_PROFILE  = 0x05;
constexpr uint8_t REPORT_SETTINGS = 0x06;
constexpr uint8_t REPORT_BUTTONS  = 0x07;
constexpr uint8_t REPORT_MACRO    = 0x08;
constexpr uint8_t REPORT_INFO     = 0x09;
constexpr uint8_t REPORT_LEDS     = 0x0d;
constexpr uint8_t REPORT_TDCU     = 0x0f;
constexpr uint8_t REPORT_LED_MODE = 0x0e;

constexpr uint8_t SELECT_SETTINGS = 0x80;
constexpr uint8_t SELECT_BUTTONS  = 0x90;
constexpr uint8_t SELECT_MACRO_1  = 0x10;
constexpr uint8_t SELECT_MACRO_2  = 0x20;

constexpr size_t MACRO_CHUNK_REPORT = 0x402; // id + chunk number + 1024 payload
constexpr size_t MACRO_CHUNK_DATA   = 0x400;
constexpr size_t MACRO_HEADER_ID    = 0x0e;  // first byte of the macro blob
constexpr size_t MACRO_KEYS_OFFSET  = 0x50;
constexpr size_t MACRO_CSUM_OFFSET  = 2000;

// Swarm sleeps 150 ms before the first status poll and gives up after 100
// busy answers.
constexpr int STATUS_FIRST_DELAY_MS = 150;
constexpr int STATUS_POLL_DELAY_MS  = 50;
constexpr int STATUS_MAX_POLLS      = 100;

struct supported_model {
    uint16_t pid;
    const char *name;
};

constexpr supported_model MODELS[] = {
    {ROSSAT_PID_KONE_AIMO_REMASTERED, "ROCCAT Kone AIMO Remastered"},
};

bool g_initialised = false;

const supported_model *find_model(uint16_t vid, uint16_t pid)
{
    if (vid != ROSSAT_VID)
        return nullptr;
    for (const auto &m : MODELS)
        if (m.pid == pid)
            return &m;
    return nullptr;
}

void sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void copy_wide(char *dst, size_t dst_len, const wchar_t *src)
{
    size_t i = 0;
    if (src)
        for (; src[i] && i + 1 < dst_len; ++i)
            dst[i] = src[i] < 0x80 ? static_cast<char>(src[i]) : '?';
    dst[i] = '\0';
}

// Returns true if the HID report descriptor declares the given report id.
// Global item "Report ID" is encoded as 0x85 <id>.
bool descriptor_has_report(const unsigned char *desc, int len, uint8_t id)
{
    int i = 0;
    while (i < len) {
        unsigned char prefix = desc[i];
        if (prefix == 0xfe) { // long item
            if (i + 1 >= len)
                break;
            i += 3 + desc[i + 1];
            continue;
        }
        int size = prefix & 0x03;
        if (size == 3)
            size = 4;
        if ((prefix & 0xfc) == 0x84 && size == 1 && i + 1 < len && desc[i + 1] == id)
            return true;
        i += 1 + size;
    }
    return false;
}

bool is_control_interface(const hid_device_info *d)
{
    hid_device *h = hid_open_path(d->path);
    if (!h)
        return false;
    unsigned char desc[HID_API_MAX_REPORT_DESCRIPTOR_SIZE];
    int len = hid_get_report_descriptor(h, desc, sizeof desc);
    hid_close(h);
    return len > 0 && descriptor_has_report(desc, len, REPORT_SETTINGS);
}

void fill_info(rossat_device_info *info, const hid_device_info *d, const supported_model *m)
{
    std::memset(info, 0, sizeof *info);
    std::snprintf(info->path, sizeof info->path, "%s", d->path);
    info->vendor_id = d->vendor_id;
    info->product_id = d->product_id;
    info->release = d->release_number;
    info->interface_number = d->interface_number;
    std::snprintf(info->name, sizeof info->name, "%s", m->name);
    copy_wide(info->serial, sizeof info->serial, d->serial_number);
}

// Collects the control interface of every supported device. When the report
// descriptor cannot be read the device's interface 0 is used as fallback.
std::vector<rossat_device_info> scan()
{
    std::vector<rossat_device_info> found;
    std::vector<const hid_device_info *> fallback;

    hid_device_info *all = hid_enumerate(ROSSAT_VID, 0);
    for (hid_device_info *d = all; d; d = d->next) {
        const supported_model *m = find_model(d->vendor_id, d->product_id);
        if (!m)
            continue;
        // hidraw lists one entry per top-level collection of the same node
        bool seen = false;
        for (const auto &i : found)
            seen = seen || std::strcmp(i.path, d->path) == 0;
        for (const hid_device_info *f : fallback)
            seen = seen || std::strcmp(f->path, d->path) == 0;
        if (seen)
            continue;
        if (is_control_interface(d)) {
            rossat_device_info info;
            fill_info(&info, d, m);
            found.push_back(info);
        } else if (d->interface_number == 0) {
            fallback.push_back(d);
        }
    }
    if (found.empty())
        for (const hid_device_info *d : fallback) {
            rossat_device_info info;
            fill_info(&info, d, find_model(d->vendor_id, d->product_id));
            found.push_back(info);
        }
    hid_free_enumeration(all);
    return found;
}

int get_raw(rossat_device *dev, uint8_t *buf, size_t len)
{
    if (!dev || !dev->hid || !buf || len == 0)
        return ROSSAT_ERR_ARGUMENT;
    uint8_t id = buf[0];
    int r = hid_get_feature_report(dev->hid, buf, len);
    if (r < 0)
        return ROSSAT_ERR_IO;
    if (buf[0] != id)
        return ROSSAT_ERR_PROTOCOL;
    return ROSSAT_OK;
}

int wait_ready(rossat_device *dev)
{
    sleep_ms(STATUS_FIRST_DELAY_MS);
    for (int i = 0; i < STATUS_MAX_POLLS; ++i) {
        uint8_t status[3] = {REPORT_CONTROL, 0, 0};
        if (hid_get_feature_report(dev->hid, status, sizeof status) < 0)
            return ROSSAT_ERR_IO;
        switch (status[1]) {
        case 3: // busy
            sleep_ms(STATUS_POLL_DELAY_MS);
            continue;
        case 2:
            return ROSSAT_ERR_REJECTED;
        default:
            return ROSSAT_OK;
        }
    }
    return ROSSAT_ERR_BUSY;
}

int set_raw(rossat_device *dev, const uint8_t *buf, size_t len, bool wait)
{
    if (!dev || !dev->hid || !buf || len == 0)
        return ROSSAT_ERR_ARGUMENT;
    if (hid_send_feature_report(dev->hid, buf, len) < 0)
        return ROSSAT_ERR_IO;
    return wait ? wait_ready(dev) : ROSSAT_OK;
}

int select_data(rossat_device *dev, uint8_t a, uint8_t b)
{
    const uint8_t sel[3] = {REPORT_CONTROL, a, b};
    return set_raw(dev, sel, sizeof sel, true);
}

void put_checksum(uint8_t *buf, size_t len)
{
    uint16_t sum = rossat_checksum(buf, len - 2);
    buf[len - 2] = sum & 0xff;
    buf[len - 1] = sum >> 8;
}

bool checksum_ok(const uint8_t *buf, size_t len)
{
    uint16_t stored = buf[len - 2] | (buf[len - 1] << 8);
    return stored == rossat_checksum(buf, len - 2);
}

uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xff; p[1] = v >> 8; }

int ensure_init()
{
    return g_initialised ? ROSSAT_OK : rossat_init();
}

} // namespace

extern "C" {

const char *rossat_version(void)
{
#define ROSSAT_STR2(x) #x
#define ROSSAT_STR(x) ROSSAT_STR2(x)
    return ROSSAT_STR(ROSSAT_VERSION_MAJOR) "." ROSSAT_STR(ROSSAT_VERSION_MINOR) "." ROSSAT_STR(ROSSAT_VERSION_PATCH);
}

const char *rossat_strerror(int error)
{
    switch (error) {
    case ROSSAT_OK:            return "success";
    case ROSSAT_ERR_INIT:      return "failed to initialise hidapi";
    case ROSSAT_ERR_NO_DEVICE: return "no supported ROCCAT device found";
    case ROSSAT_ERR_ACCESS:    return "cannot open device (check udev permissions)";
    case ROSSAT_ERR_IO:        return "HID feature report transfer failed";
    case ROSSAT_ERR_BUSY:      return "device stayed busy";
    case ROSSAT_ERR_REJECTED:  return "device rejected the data";
    case ROSSAT_ERR_CHECKSUM:  return "checksum mismatch in data read from device";
    case ROSSAT_ERR_ARGUMENT:  return "invalid argument";
    case ROSSAT_ERR_PROTOCOL:  return "unexpected answer from device";
    default:                   return "unknown error";
    }
}

int rossat_init(void)
{
    if (g_initialised)
        return ROSSAT_OK;
    if (hid_init() != 0)
        return ROSSAT_ERR_INIT;
    g_initialised = true;
    return ROSSAT_OK;
}

void rossat_exit(void)
{
    if (g_initialised)
        hid_exit();
    g_initialised = false;
}

int rossat_enumerate(rossat_device_info *out, int max)
{
    int r = ensure_init();
    if (r != ROSSAT_OK)
        return r;
    auto found = scan();
    for (int i = 0; i < static_cast<int>(found.size()) && i < max && out; ++i)
        out[i] = found[i];
    return static_cast<int>(found.size());
}

int rossat_open(const char *path, rossat_device **out)
{
    if (!out)
        return ROSSAT_ERR_ARGUMENT;
    *out = nullptr;
    int r = ensure_init();
    if (r != ROSSAT_OK)
        return r;

    auto found = scan();
    const rossat_device_info *chosen = nullptr;
    for (const auto &i : found)
        if (!path || std::strcmp(path, i.path) == 0) {
            chosen = &i;
            break;
        }
    if (!chosen)
        return ROSSAT_ERR_NO_DEVICE;

    hid_device *h = hid_open_path(chosen->path);
    if (!h)
        return ROSSAT_ERR_ACCESS;
    auto *dev = new rossat_device;
    dev->hid = h;
    dev->info = *chosen;
    *out = dev;
    return ROSSAT_OK;
}

void rossat_close(rossat_device *dev)
{
    if (!dev)
        return;
    if (dev->hid)
        hid_close(dev->hid);
    delete dev;
}

int rossat_get_info(rossat_device *dev, rossat_device_info *info)
{
    if (!dev || !info)
        return ROSSAT_ERR_ARGUMENT;
    *info = dev->info;
    return ROSSAT_OK;
}

int rossat_get_firmware_version(rossat_device *dev, char *buf, size_t len)
{
    if (!dev || !buf || len == 0)
        return ROSSAT_ERR_ARGUMENT;
    // Report 0x09 = [09 09 fw ...]; fw 105 = "1.05" (matches Swarm's
    // firmware_upgrade.ini). Fall back to the USB release number.
    uint8_t info[9] = {REPORT_INFO};
    if (get_raw(dev, info, sizeof info) == ROSSAT_OK && info[1] == sizeof info) {
        std::snprintf(buf, len, "%d.%02d", info[2] / 100, info[2] % 100);
        return ROSSAT_OK;
    }
    unsigned v = dev->info.release;
    std::snprintf(buf, len, "%x.%02x", v >> 8, v & 0xff);
    return ROSSAT_OK;
}

uint16_t rossat_checksum(const uint8_t *buf, size_t len)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum = static_cast<uint16_t>(sum + buf[i]);
    return sum;
}

int rossat_feature_get(rossat_device *dev, uint8_t *buf, size_t len)
{
    return get_raw(dev, buf, len);
}

int rossat_feature_set(rossat_device *dev, const uint8_t *buf, size_t len)
{
    return set_raw(dev, buf, len, true);
}

/* ---- profiles ---------------------------------------------------------- */

int rossat_get_active_profile(rossat_device *dev, uint8_t *profile, uint8_t *count)
{
    uint8_t buf[4] = {REPORT_PROFILE, 0, 0, 0};
    int r = get_raw(dev, buf, sizeof buf);
    if (r != ROSSAT_OK)
        return r;
    if (profile)
        *profile = buf[2];
    if (count)
        *count = buf[3];
    return ROSSAT_OK;
}

int rossat_set_active_profile(rossat_device *dev, uint8_t profile)
{
    uint8_t current = 0, count = ROSSAT_PROFILE_COUNT;
    int r = rossat_get_active_profile(dev, &current, &count);
    if (r != ROSSAT_OK)
        return r;
    if (count == 0 || count > ROSSAT_PROFILE_COUNT)
        count = ROSSAT_PROFILE_COUNT;
    if (profile >= count)
        return ROSSAT_ERR_ARGUMENT;
    const uint8_t buf[4] = {REPORT_PROFILE, 4, profile, count};
    return set_raw(dev, buf, sizeof buf, true);
}

/* ---- settings ---------------------------------------------------------- */

int rossat_polling_rate_hz(uint8_t rate)
{
    static const int hz[] = {125, 250, 500, 1000};
    return rate < 4 ? hz[rate] : 0;
}

const char *rossat_effect_name(uint8_t effect)
{
    switch (effect) {
    case ROSSAT_EFFECT_OFF:         return "Off";
    case ROSSAT_EFFECT_FULLY_LIT:   return "Fully lit";
    case ROSSAT_EFFECT_BLINKING:    return "Blinking";
    case ROSSAT_EFFECT_BREATHING:   return "Breathing";
    case ROSSAT_EFFECT_HEARTBEAT:   return "Heartbeat";
    case ROSSAT_EFFECT_COLOR_CYCLE: return "Colour cycle";
    case ROSSAT_EFFECT_SNAKE:       return "Snake";
    case ROSSAT_EFFECT_BREATHING_2: return "Breathing 2.0";
    case ROSSAT_EFFECT_HEARTBEAT_2: return "Heartbeat 2.0";
    case ROSSAT_EFFECT_AIMO:        return "AIMO";
    case ROSSAT_EFFECT_COLORWAVE:   return "Colorwave";
    default:                        return nullptr;
    }
}

int rossat_effect_is_software(uint8_t effect)
{
    return effect == ROSSAT_EFFECT_HEARTBEAT || effect == ROSSAT_EFFECT_BREATHING_2 ||
           effect == ROSSAT_EFFECT_HEARTBEAT_2 || effect == ROSSAT_EFFECT_AIMO;
}

const char *rossat_led_name(int led)
{
    static const char *names[ROSSAT_LED_COUNT] = {
        "Wheel",        "Left 1 (front)", "Left 2",       "Left 3",
        "Left 4",       "Right 1 (front)", "Right 2",     "Right 3",
        "Right 4",      "Left 5 (rear)",   "Right 5 (rear)",
    };
    return led >= 0 && led < ROSSAT_LED_COUNT ? names[led] : "?";
}

void rossat_settings_decode(const uint8_t raw[ROSSAT_SETTINGS_SIZE], rossat_settings *s)
{
    std::memset(s, 0, sizeof *s);
    std::memcpy(s->raw, raw, ROSSAT_SETTINGS_SIZE);
    s->profile = raw[0x02];
    s->dpi_enabled_mask = raw[0x04];
    s->dpi_active = raw[0x05];
    for (int i = 0; i < ROSSAT_DPI_LEVELS; ++i) {
        s->dpi_x[i] = static_cast<uint16_t>(get16(raw + 0x06 + 2 * i) * ROSSAT_DPI_STEP);
        s->dpi_y[i] = static_cast<uint16_t>(get16(raw + 0x10 + 2 * i) * ROSSAT_DPI_STEP);
    }
    s->polling_rate = raw[0x1a];
    s->angle_snapping = raw[0x1b];
    s->debounce_ms = raw[0x1d];
    s->led_enable_mask = get16(raw + 0x1e);
    s->effect = raw[0x21];
    s->effect_speed = raw[0x22];
    s->brightness = raw[0x23];
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        const uint8_t *p = raw + 0x3d + 5 * i;
        s->leds[i] = rossat_color{p[0], p[1], p[2], p[3]};
    }
    s->color_theme = raw[0x74];
}

void rossat_settings_encode(const rossat_settings *s, uint8_t raw[ROSSAT_SETTINGS_SIZE])
{
    std::memcpy(raw, s->raw, ROSSAT_SETTINGS_SIZE);
    raw[0x00] = REPORT_SETTINGS;
    raw[0x01] = ROSSAT_SETTINGS_SIZE;
    raw[0x02] = s->profile;
    raw[0x04] = s->dpi_enabled_mask & 0x1f;
    raw[0x05] = s->dpi_active;
    for (int i = 0; i < ROSSAT_DPI_LEVELS; ++i) {
        put16(raw + 0x06 + 2 * i, static_cast<uint16_t>(s->dpi_x[i] / ROSSAT_DPI_STEP));
        put16(raw + 0x10 + 2 * i, static_cast<uint16_t>(s->dpi_y[i] / ROSSAT_DPI_STEP));
    }
    raw[0x1a] = s->polling_rate;
    raw[0x1b] = s->angle_snapping;
    raw[0x1d] = s->debounce_ms;
    put16(raw + 0x1e, s->led_enable_mask & 0x7ff);
    raw[0x21] = s->effect;
    raw[0x22] = s->effect_speed;
    raw[0x23] = s->brightness;
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        uint8_t *p = raw + 0x3d + 5 * i;
        p[0] = s->leds[i].a;
        p[1] = s->leds[i].r;
        p[2] = s->leds[i].g;
        p[3] = s->leds[i].b;
        p[4] = 0;
    }
    raw[0x74] = s->color_theme;
    put_checksum(raw, ROSSAT_SETTINGS_SIZE);
}

int rossat_read_settings_raw(rossat_device *dev, uint8_t profile, uint8_t buf[ROSSAT_SETTINGS_SIZE])
{
    if (profile >= ROSSAT_PROFILE_COUNT || !buf)
        return ROSSAT_ERR_ARGUMENT;
    int r = select_data(dev, profile, SELECT_SETTINGS);
    if (r != ROSSAT_OK)
        return r;
    std::memset(buf, 0, ROSSAT_SETTINGS_SIZE);
    buf[0] = REPORT_SETTINGS;
    r = get_raw(dev, buf, ROSSAT_SETTINGS_SIZE);
    if (r != ROSSAT_OK)
        return r;
    if (buf[1] != ROSSAT_SETTINGS_SIZE || buf[2] != profile)
        return ROSSAT_ERR_PROTOCOL;
    return checksum_ok(buf, ROSSAT_SETTINGS_SIZE) ? ROSSAT_OK : ROSSAT_ERR_CHECKSUM;
}

int rossat_write_settings_raw(rossat_device *dev, uint8_t buf[ROSSAT_SETTINGS_SIZE])
{
    if (!buf || buf[2] >= ROSSAT_PROFILE_COUNT)
        return ROSSAT_ERR_ARGUMENT;
    buf[0] = REPORT_SETTINGS;
    buf[1] = ROSSAT_SETTINGS_SIZE;
    put_checksum(buf, ROSSAT_SETTINGS_SIZE);
    return set_raw(dev, buf, ROSSAT_SETTINGS_SIZE, true);
}

int rossat_read_settings(rossat_device *dev, uint8_t profile, rossat_settings *out)
{
    if (!out)
        return ROSSAT_ERR_ARGUMENT;
    uint8_t raw[ROSSAT_SETTINGS_SIZE];
    int r = rossat_read_settings_raw(dev, profile, raw);
    if (r != ROSSAT_OK)
        return r;
    rossat_settings_decode(raw, out);
    return ROSSAT_OK;
}

int rossat_write_settings(rossat_device *dev, const rossat_settings *in)
{
    if (!in || in->profile >= ROSSAT_PROFILE_COUNT || in->dpi_active >= ROSSAT_DPI_LEVELS ||
        in->polling_rate > ROSSAT_POLLING_1000HZ)
        return ROSSAT_ERR_ARGUMENT;
    for (int i = 0; i < ROSSAT_DPI_LEVELS; ++i)
        if (in->dpi_x[i] < ROSSAT_DPI_MIN || in->dpi_x[i] > ROSSAT_DPI_MAX ||
            in->dpi_y[i] < ROSSAT_DPI_MIN || in->dpi_y[i] > ROSSAT_DPI_MAX)
            return ROSSAT_ERR_ARGUMENT;
    uint8_t raw[ROSSAT_SETTINGS_SIZE];
    rossat_settings_encode(in, raw);
    return set_raw(dev, raw, sizeof raw, true);
}

/* ---- buttons ----------------------------------------------------------- */

int rossat_read_buttons(rossat_device *dev, uint8_t profile, rossat_buttons *out)
{
    if (!out || profile >= ROSSAT_PROFILE_COUNT)
        return ROSSAT_ERR_ARGUMENT;
    int r = select_data(dev, profile, SELECT_BUTTONS);
    if (r != ROSSAT_OK)
        return r;
    uint8_t raw[ROSSAT_BUTTONS_SIZE] = {REPORT_BUTTONS};
    r = get_raw(dev, raw, sizeof raw);
    if (r != ROSSAT_OK)
        return r;
    // unlike settings and macros, this report carries no checksum
    if (raw[1] != ROSSAT_BUTTONS_SIZE || raw[2] != profile)
        return ROSSAT_ERR_PROTOCOL;
    out->profile = raw[2];
    for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i) {
        const uint8_t *p = raw + 3 + 3 * i;
        out->buttons[i] = rossat_button{p[0], p[1], p[2]};
    }
    return ROSSAT_OK;
}

int rossat_write_buttons(rossat_device *dev, const rossat_buttons *in)
{
    if (!in || in->profile >= ROSSAT_PROFILE_COUNT)
        return ROSSAT_ERR_ARGUMENT;
    uint8_t raw[ROSSAT_BUTTONS_SIZE] = {REPORT_BUTTONS, ROSSAT_BUTTONS_SIZE, in->profile};
    for (int i = 0; i < ROSSAT_BUTTON_COUNT; ++i) {
        uint8_t *p = raw + 3 + 3 * i;
        p[0] = in->buttons[i].type;
        p[1] = in->buttons[i].param1;
        p[2] = in->buttons[i].param2;
    }
    return set_raw(dev, raw, sizeof raw, true);
}

/* ---- macros ------------------------------------------------------------ */

int rossat_read_macro(rossat_device *dev, uint8_t profile, uint8_t button, rossat_macro *out)
{
    if (!out || profile >= ROSSAT_PROFILE_COUNT || button >= ROSSAT_BUTTON_COUNT)
        return ROSSAT_ERR_ARGUMENT;

    std::vector<uint8_t> blob(ROSSAT_MACRO_SIZE, 0);
    const uint8_t selects[2] = {SELECT_MACRO_1, SELECT_MACRO_2};
    for (int chunk = 0; chunk < 2; ++chunk) {
        int r = select_data(dev, static_cast<uint8_t>(profile | selects[chunk]), button);
        if (r != ROSSAT_OK)
            return r;
        std::vector<uint8_t> rep(MACRO_CHUNK_REPORT, 0);
        rep[0] = REPORT_MACRO;
        r = get_raw(dev, rep.data(), rep.size());
        if (r != ROSSAT_OK)
            return r;
        size_t off = chunk * MACRO_CHUNK_DATA;
        size_t n = std::min(MACRO_CHUNK_DATA, ROSSAT_MACRO_SIZE - off);
        std::memcpy(blob.data() + off, rep.data() + 2, n);
    }

    std::memset(out, 0, sizeof *out);
    const uint8_t *b = blob.data();
    out->profile = b[3];
    out->button = b[4];
    out->loop = b[5];
    std::memcpy(out->set_name, b + 0x06, 40);
    std::memcpy(out->name, b + 0x2e, 32);
    uint16_t cm = get16(b + 0x4e);
    out->mode = static_cast<uint8_t>(cm >> 12);
    out->count = std::min<uint16_t>(cm & 0x0fff, ROSSAT_MACRO_KEYSTROKES);
    for (int i = 0; i < out->count; ++i) {
        const uint8_t *k = b + MACRO_KEYS_OFFSET + 4 * i;
        out->keys[i] = rossat_keystroke{k[0], k[1], get16(k + 2)};
    }
    // An unused slot reads back as zeros; only validate populated macros.
    if (b[0] == MACRO_HEADER_ID && get16(b + MACRO_CSUM_OFFSET) != rossat_checksum(b, MACRO_CSUM_OFFSET))
        return ROSSAT_ERR_CHECKSUM;
    return ROSSAT_OK;
}

int rossat_write_macro(rossat_device *dev, const rossat_macro *in)
{
    if (!in || in->profile >= ROSSAT_PROFILE_COUNT || in->button >= ROSSAT_BUTTON_COUNT ||
        in->count > ROSSAT_MACRO_KEYSTROKES || in->mode > ROSSAT_MACRO_INFINITE)
        return ROSSAT_ERR_ARGUMENT;

    std::vector<uint8_t> blob(ROSSAT_MACRO_SIZE, 0);
    uint8_t *b = blob.data();
    b[0] = MACRO_HEADER_ID;
    put16(b + 1, ROSSAT_MACRO_SIZE);
    b[3] = in->profile;
    b[4] = in->button;
    b[5] = in->loop;
    std::memcpy(b + 0x06, in->set_name, strnlen(in->set_name, 40));
    std::memcpy(b + 0x2e, in->name, strnlen(in->name, 32));
    put16(b + 0x4e, static_cast<uint16_t>((in->mode << 12) | (in->count & 0x0fff)));
    for (int i = 0; i < in->count; ++i) {
        uint8_t *k = b + MACRO_KEYS_OFFSET + 4 * i;
        k[0] = in->keys[i].key;
        k[1] = in->keys[i].action;
        put16(k + 2, in->keys[i].delay_ms);
    }
    put16(b + MACRO_CSUM_OFFSET, rossat_checksum(b, MACRO_CSUM_OFFSET));

    for (int chunk = 0; chunk < 2; ++chunk) {
        std::vector<uint8_t> rep(MACRO_CHUNK_REPORT, 0);
        rep[0] = REPORT_MACRO;
        rep[1] = static_cast<uint8_t>(chunk + 1);
        size_t off = chunk * MACRO_CHUNK_DATA;
        size_t n = std::min(MACRO_CHUNK_DATA, ROSSAT_MACRO_SIZE - off);
        std::memcpy(rep.data() + 2, b + off, n);
        int r = set_raw(dev, rep.data(), rep.size(), true);
        if (r != ROSSAT_OK)
            return r;
    }
    return ROSSAT_OK;
}

/* ---- sensor calibration (TDCU) -------------------------------------------- */

// Sequence from the Swarm module's dcu_calibration_fm (FUN_6a467480,
// FUN_6a467b80): [0f 06 02 ff] starts (no status wait), the control status
// stays "busy" while the sensor samples, [0f 06 02 01] keeps the result and
// [0f 06 02 00] discards it.

int rossat_read_tdcu(rossat_device *dev, rossat_tdcu *out)
{
    if (!out)
        return ROSSAT_ERR_ARGUMENT;
    uint8_t buf[ROSSAT_DCU_SIZE] = {REPORT_TDCU};
    int r = get_raw(dev, buf, sizeof buf);
    if (r != ROSSAT_OK)
        return r;
    out->mode = buf[2];
    out->state = buf[3];
    out->value = get16(buf + 4);
    return ROSSAT_OK;
}

int rossat_calibration_start(rossat_device *dev)
{
    const uint8_t buf[ROSSAT_DCU_SIZE] = {REPORT_TDCU, ROSSAT_DCU_SIZE, 2, 0xff, 0, 0};
    return set_raw(dev, buf, sizeof buf, false);
}

int rossat_calibration_poll(rossat_device *dev, int *finished)
{
    if (!dev || !dev->hid || !finished)
        return ROSSAT_ERR_ARGUMENT;
    uint8_t status[3] = {REPORT_CONTROL, 0, 0};
    if (hid_get_feature_report(dev->hid, status, sizeof status) < 0)
        return ROSSAT_ERR_IO;
    if (status[1] == 2)
        return ROSSAT_ERR_REJECTED;
    *finished = status[1] != 3;
    return ROSSAT_OK;
}

int rossat_calibration_finish(rossat_device *dev, int save)
{
    const uint8_t buf[ROSSAT_DCU_SIZE] = {REPORT_TDCU, ROSSAT_DCU_SIZE, 2, static_cast<uint8_t>(save ? 1 : 0), 0, 0};
    return set_raw(dev, buf, sizeof buf, true);
}

/* ---- direct lighting ----------------------------------------------------- */

int rossat_set_direct_lighting(rossat_device *dev, int enable)
{
    // byte 4 != 0 makes the firmware show host colours unscaled; otherwise
    // they are multiplied by the profile brightness (0 = invisible)
    const uint8_t buf[6] = {REPORT_LED_MODE, 6, 1, static_cast<uint8_t>(enable ? 1 : 0),
                            static_cast<uint8_t>(enable ? 0xff : 0), 0xff};
    return set_raw(dev, buf, sizeof buf, true);
}

int rossat_set_direct_colors(rossat_device *dev, const rossat_color colors[ROSSAT_LED_COUNT])
{
    if (!colors)
        return ROSSAT_ERR_ARGUMENT;
    uint8_t buf[2 + 4 * ROSSAT_LED_COUNT] = {REPORT_LEDS, sizeof buf};
    // The LEDs are R, G, B + amber. The firmware's own white (R=G=80 %,
    // B=100 %, amber on) looks mint; this neutral mix was calibrated by eye on
    // a Kone AIMO Remastered. The white part of each colour, min(R,G,B), is
    // drawn with that mix, the rest of the colour stays as given.
    constexpr int WHITE_R = 200, WHITE_G = 190, WHITE_B = 170, WHITE_A = 255;
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i) {
        const rossat_color &c = colors[i];
        int r = (c.r * c.a + 127) / 255;
        int g = (c.g * c.a + 127) / 255;
        int b = (c.b * c.a + 127) / 255;
        int w = std::min(r, std::min(g, b));
        r = std::min(255, r - w + w * WHITE_R / 255);
        g = std::min(255, g - w + w * WHITE_G / 255);
        b = std::min(255, b - w + w * WHITE_B / 255);
        uint8_t *p = buf + 2 + 4 * i;
        p[0] = static_cast<uint8_t>(r);
        p[1] = static_cast<uint8_t>(g);
        p[2] = static_cast<uint8_t>(b);
        p[3] = static_cast<uint8_t>(w * WHITE_A / 255);
    }
    return set_raw(dev, buf, sizeof buf, false);
}

} // extern "C"
