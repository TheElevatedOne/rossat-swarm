// Offline tests for the report codecs; no device required.

#include "rossat/rossat.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

// Factory profile 1 of the original Kone AIMO (report 0x06, 0x69 bytes), as
// stored in the Swarm module's default.dat. Used to validate the checksum.
static const uint8_t KONE_AIMO_DEFAULT[0x69] = {
    0x06, 0x69, 0x00, 0x06, 0x1f, 0x08, 0x10, 0x18, 0x20, 0x40, 0x01, 0x03, 0x00, 0x00, 0x08, 0xff,
    0x07, 0x00, 0x09, 0x06, 0xff, 0x1d, 0x13, 0xff, 0x00, 0xff, 0x59, 0xff, 0x00, 0x00, 0xff, 0xfd,
    0xfd, 0x00, 0x00, 0xff, 0xf4, 0x64, 0x00, 0x00, 0xff, 0xf4, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x50, 0x00, 0x00, 0xff, 0xff, 0x50,
    0x00, 0x00, 0xff, 0xe6, 0x8c, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x50, 0x00,
    0x00, 0xff, 0xff, 0x50, 0x00, 0x00, 0xff, 0xe6, 0x8c, 0x00, 0x00, 0xff, 0xe6, 0x8c, 0x00, 0x00,
    0xff, 0xe6, 0x8c, 0x00, 0x00, 0x80, 0x01, 0x83, 0x28,
};

int main()
{
    CHECK(rossat_checksum(KONE_AIMO_DEFAULT, sizeof KONE_AIMO_DEFAULT - 2) == 0x2883);

    rossat_settings s;
    std::memset(&s, 0, sizeof s);
    s.profile = 2;
    s.raw[0x03] = 6; // unused by the firmware, must be preserved
    s.dpi_enabled_mask = 0x1f;
    s.dpi_active = 1;
    const uint16_t dpi[5] = {400, 800, 1200, 1600, 16000};
    for (int i = 0; i < 5; ++i) {
        s.dpi_x[i] = dpi[i];
        s.dpi_y[i] = static_cast<uint16_t>(dpi[i] / 2 / 50 * 50 + 100);
    }
    s.polling_rate = ROSSAT_POLLING_1000HZ;
    s.debounce_ms = 8;
    s.led_enable_mask = 0x7ff;
    s.effect = ROSSAT_EFFECT_AIMO;
    s.effect_speed = 6;
    s.brightness = 0xff;
    for (int i = 0; i < ROSSAT_LED_COUNT; ++i)
        s.leds[i] = rossat_color{0xff, static_cast<uint8_t>(i), 0x50, 0x00};
    s.color_theme = 0x86;
    s.raw[0x7a] = 0x5a; // an unknown byte that must survive encoding

    uint8_t raw[ROSSAT_SETTINGS_SIZE];
    rossat_settings_encode(&s, raw);
    CHECK(raw[0] == 0x06 && raw[1] == 0x7e && raw[2] == 2);
    CHECK(raw[0x03] == 6 && raw[0x05] == 1);
    CHECK(raw[0x06] == 8 && raw[0x07] == 0);      // 400 / 50
    CHECK(raw[0x0e] == 0x40 && raw[0x0f] == 0x01); // 16000 / 50 = 320
    CHECK(raw[0x1a] == 3 && raw[0x1d] == 8);
    CHECK(raw[0x1e] == 0xff && raw[0x1f] == 0x07);
    CHECK(raw[0x21] == 9 && raw[0x22] == 6 && raw[0x23] == 0xff);
    CHECK(raw[0x3d] == 0xff && raw[0x3e] == 0 && raw[0x3f] == 0x50);
    CHECK(raw[0x74] == 0x86 && raw[0x7a] == 0x5a);
    uint16_t sum = rossat_checksum(raw, ROSSAT_SETTINGS_SIZE - 2);
    CHECK(raw[0x7c] == (sum & 0xff) && raw[0x7d] == (sum >> 8));

    rossat_settings back;
    rossat_settings_decode(raw, &back);
    CHECK(back.profile == 2 && back.dpi_active == 1);
    for (int i = 0; i < 5; ++i)
        CHECK(back.dpi_x[i] == s.dpi_x[i] && back.dpi_y[i] == s.dpi_y[i]);
    CHECK(back.effect == 9 && back.led_enable_mask == 0x7ff);
    CHECK(back.leds[10].r == 10 && back.leds[10].g == 0x50);

    rossat_buttons b;
    rossat_buttons_default(0, &b);
    CHECK(b.buttons[ROSSAT_BTN_LEFT].type == ROSSAT_BT_CLICK);
    CHECK(b.buttons[ROSSAT_BTN_THUMB_EASYSHIFT].type == ROSSAT_BT_EASYSHIFT);
    CHECK(b.buttons[ROSSAT_BTN_SHIFT_BASE + 2].type == ROSSAT_BT_PLAY_PAUSE);

    CHECK(rossat_button_type_from_string("dpi-up") == ROSSAT_BT_DPI_UP);
    CHECK(rossat_button_type_from_string("DPI_UP") == ROSSAT_BT_DPI_UP);
    CHECK(rossat_button_type_from_string("0x30") == ROSSAT_BT_MACRO);
    CHECK(rossat_button_type_from_string("nonsense") == -1);
    CHECK(rossat_polling_rate_hz(ROSSAT_POLLING_500HZ) == 500);

    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("all codec checks passed\n");
    return failures ? 1 : 0;
}
