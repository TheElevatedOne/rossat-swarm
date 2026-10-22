/*
 * rossat-swarm - open re-implementation of ROCCAT Swarm device control.
 *
 * Public C API. Usable from C and C++ (link against librossat).
 *
 * Supported devices: ROCCAT Kone AIMO Remastered (USB 1e7d:2e2c).
 *
 * All protocol knowledge comes from reverse engineering ROCCAT Swarm 1.9482
 * (ROCCAT_Swarm_Monitor.exe and the KONE_AIMO 1.0009 module); see
 * docs/PROTOCOL.md for the report layouts.
 *
 * Every function that talks to the device returns ROSSAT_OK (0) on success or
 * a negative rossat_error code. Functions are not thread safe for the same
 * rossat_device handle.
 */
#ifndef ROSSAT_ROSSAT_H
#define ROSSAT_ROSSAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(ROSSAT_BUILDING_SHARED)
#  define ROSSAT_API __declspec(dllexport)
#elif defined(__GNUC__) && defined(ROSSAT_BUILDING_SHARED)
#  define ROSSAT_API __attribute__((visibility("default")))
#else
#  define ROSSAT_API
#endif

#define ROSSAT_VERSION_MAJOR 1
#define ROSSAT_VERSION_MINOR 0
#define ROSSAT_VERSION_PATCH 0

#define ROSSAT_VID                     0x1e7d
#define ROSSAT_PID_KONE_AIMO_REMASTERED 0x2e2c

#define ROSSAT_PROFILE_COUNT   5
#define ROSSAT_DPI_LEVELS      5
#define ROSSAT_DPI_MIN         100
#define ROSSAT_DPI_MAX         16000
#define ROSSAT_DPI_STEP        50
#define ROSSAT_LED_COUNT       11
#define ROSSAT_BUTTON_COUNT    24   /* 12 physical buttons + 12 Easy-Shift[+] layer */
#define ROSSAT_MACRO_KEYSTROKES 480

/* Raw report sizes (including report id and, where present, checksum). */
#define ROSSAT_SETTINGS_SIZE   0x7e  /* report 0x06 */
#define ROSSAT_BUTTONS_SIZE    0x4b  /* report 0x07, no checksum */
#define ROSSAT_MACRO_SIZE      0x7d2 /* report 0x08, sent as two chunks */
#define ROSSAT_DCU_SIZE        6     /* report 0x0f */

typedef enum rossat_error {
    ROSSAT_OK              = 0,
    ROSSAT_ERR_INIT        = -1,  /* hidapi could not be initialised        */
    ROSSAT_ERR_NO_DEVICE   = -2,  /* no supported device found / unplugged  */
    ROSSAT_ERR_ACCESS      = -3,  /* hidraw node could not be opened         */
    ROSSAT_ERR_IO          = -4,  /* feature report transfer failed          */
    ROSSAT_ERR_BUSY        = -5,  /* device stayed busy                      */
    ROSSAT_ERR_REJECTED    = -6,  /* device answered "invalid" (status 2)    */
    ROSSAT_ERR_CHECKSUM    = -7,  /* data read back had a bad checksum       */
    ROSSAT_ERR_ARGUMENT    = -8,  /* parameter out of range                  */
    ROSSAT_ERR_PROTOCOL    = -9   /* unexpected report id / size             */
} rossat_error;

/* ------------------------------------------------------------------------ */
/* Device discovery                                                          */
/* ------------------------------------------------------------------------ */

typedef struct rossat_device rossat_device;

typedef struct rossat_device_info {
    char     path[256];      /* hidraw path of the control interface */
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t release;        /* bcdDevice */
    int      interface_number;
    char     name[64];       /* human readable model name */
    char     serial[64];
} rossat_device_info;

ROSSAT_API const char *rossat_version(void);
ROSSAT_API const char *rossat_strerror(int error);

/* Optional: called automatically by rossat_enumerate()/rossat_open(). */
ROSSAT_API int  rossat_init(void);
ROSSAT_API void rossat_exit(void);

/* Fills up to `max` entries, returns the number of supported devices found
 * (may be larger than `max`) or a negative error. */
ROSSAT_API int rossat_enumerate(rossat_device_info *out, int max);

/* Opens a device. `path` may be NULL to open the first supported device. */
ROSSAT_API int  rossat_open(const char *path, rossat_device **out);
ROSSAT_API void rossat_close(rossat_device *dev);
ROSSAT_API int  rossat_get_info(rossat_device *dev, rossat_device_info *info);

/* Firmware version as reported by USB bcdDevice, e.g. 0x0105 -> "1.05". */
ROSSAT_API int rossat_get_firmware_version(rossat_device *dev, char *buf, size_t len);

/* ------------------------------------------------------------------------ */
/* Profiles (report 0x05)                                                    */
/* ------------------------------------------------------------------------ */

ROSSAT_API int rossat_get_active_profile(rossat_device *dev, uint8_t *profile, uint8_t *count);
ROSSAT_API int rossat_set_active_profile(rossat_device *dev, uint8_t profile);

/* ------------------------------------------------------------------------ */
/* Profile settings (report 0x06, 126 bytes)                                 */
/* ------------------------------------------------------------------------ */

typedef enum rossat_polling_rate {
    ROSSAT_POLLING_125HZ  = 0,
    ROSSAT_POLLING_250HZ  = 1,
    ROSSAT_POLLING_500HZ  = 2,
    ROSSAT_POLLING_1000HZ = 3
} rossat_polling_rate;

/* Lighting effect byte (values from Swarm's CMouse_Effect_Setting table,
 * behaviour verified on a Kone AIMO Remastered with firmware 1.05).
 * "Software" effects are not rendered by the firmware; Swarm - and the
 * rossat lighting engine below - draw them from the PC. */
typedef enum rossat_effect {
    ROSSAT_EFFECT_OFF          = 0,  /* firmware */
    ROSSAT_EFFECT_FULLY_LIT    = 1,  /* firmware: static colours */
    ROSSAT_EFFECT_BLINKING     = 2,  /* firmware */
    ROSSAT_EFFECT_BREATHING    = 3,  /* firmware */
    ROSSAT_EFFECT_HEARTBEAT    = 4,  /* software */
    ROSSAT_EFFECT_COLOR_CYCLE  = 5,  /* firmware only, not offered by Swarm */
    ROSSAT_EFFECT_SNAKE        = 6,  /* firmware */
    ROSSAT_EFFECT_BREATHING_2  = 7,  /* software (firmware shows a moving dot) */
    ROSSAT_EFFECT_HEARTBEAT_2  = 8,  /* software (firmware shows a moving dot) */
    ROSSAT_EFFECT_AIMO         = 9,  /* software, uses the colour theme */
    ROSSAT_EFFECT_COLORWAVE    = 10  /* firmware */
} rossat_effect;

/* LED index layout (verified on hardware, front to back):
 *   0              scroll wheel
 *   1,2,3,4,9      left side           5,6,7,8,10   right side          */
enum {
    ROSSAT_LED_WHEEL = 0,
    ROSSAT_LED_LEFT_FRONT = 1,
    ROSSAT_LED_RIGHT_FRONT = 5
};

typedef struct rossat_color {
    uint8_t a, r, g, b;     /* a = per-LED brightness (0..255) */
} rossat_color;

typedef struct rossat_settings {
    uint8_t      profile;            /* 0..4                                   */
    uint8_t      dpi_enabled_mask;   /* +0x04 bit n = DPI level n enabled      */
    uint8_t      dpi_active;         /* +0x05 0..4                             */
    uint16_t     dpi_x[ROSSAT_DPI_LEVELS]; /* +0x06 real DPI (stored /50)      */
    uint16_t     dpi_y[ROSSAT_DPI_LEVELS]; /* +0x10                            */
    uint8_t      polling_rate;       /* +0x1a rossat_polling_rate              */
    uint8_t      angle_snapping;     /* +0x1b 0 = off                          */
    uint8_t      debounce_ms;        /* +0x1d factory 8                        */
    uint16_t     led_enable_mask;    /* +0x1e bit n = LED n enabled (0x7ff)    */
    uint8_t      effect;             /* +0x21 rossat_effect                    */
    uint8_t      effect_speed;       /* +0x22 factory 6                        */
    uint8_t      brightness;         /* +0x23 0..255                           */
    rossat_color leds[ROSSAT_LED_COUNT]; /* +0x3d, 5 bytes each (a,r,g,b,0)    */
    uint8_t      color_theme;        /* +0x74 bit7 = flag, low bits = AIMO theme */
    /* Original report bytes. rossat_write_settings() starts from this buffer
     * and only overwrites the fields above, so unknown bytes survive a
     * read-modify-write cycle. Zero it to start from scratch. */
    uint8_t      raw[ROSSAT_SETTINGS_SIZE];
} rossat_settings;

ROSSAT_API int rossat_read_settings(rossat_device *dev, uint8_t profile, rossat_settings *out);
ROSSAT_API int rossat_write_settings(rossat_device *dev, const rossat_settings *in);

/* Raw access; the write variant fixes header and checksum. */
ROSSAT_API int rossat_read_settings_raw(rossat_device *dev, uint8_t profile, uint8_t buf[ROSSAT_SETTINGS_SIZE]);
ROSSAT_API int rossat_write_settings_raw(rossat_device *dev, uint8_t buf[ROSSAT_SETTINGS_SIZE]);

/* Pure helpers (no I/O). */
ROSSAT_API void rossat_settings_decode(const uint8_t raw[ROSSAT_SETTINGS_SIZE], rossat_settings *out);
ROSSAT_API void rossat_settings_encode(const rossat_settings *in, uint8_t raw[ROSSAT_SETTINGS_SIZE]);
ROSSAT_API int  rossat_polling_rate_hz(uint8_t polling_rate);
ROSSAT_API const char *rossat_effect_name(uint8_t effect);
/* Non-zero if the effect has to be drawn by the host (see rossat_engine). */
ROSSAT_API int rossat_effect_is_software(uint8_t effect);
ROSSAT_API const char *rossat_led_name(int led);

/* ------------------------------------------------------------------------ */
/* Button assignment (report 0x07, 75 bytes)                                 */
/* ------------------------------------------------------------------------ */

/* Physical button index; add ROSSAT_BUTTON_COUNT/2 for the Easy-Shift layer. */
enum {
    ROSSAT_BTN_LEFT = 0,
    ROSSAT_BTN_RIGHT = 1,
    ROSSAT_BTN_MIDDLE = 2,
    ROSSAT_BTN_TILT_LEFT = 3,
    ROSSAT_BTN_TILT_RIGHT = 4,
    ROSSAT_BTN_WHEEL_UP = 5,
    ROSSAT_BTN_WHEEL_DOWN = 6,
    ROSSAT_BTN_THUMB_FORWARD = 7,
    ROSSAT_BTN_THUMB_BACKWARD = 8,
    ROSSAT_BTN_THUMB_EASYSHIFT = 9,
    ROSSAT_BTN_DPI_UP = 10,
    ROSSAT_BTN_DPI_DOWN = 11,
    ROSSAT_BTN_SHIFT_BASE = 12
};

/* Function codes (ROCCAT's common button table, confirmed for the codes used
 * by the Kone AIMO factory layout). */
typedef enum rossat_button_type {
    ROSSAT_BT_UNUSED            = 0x00,
    ROSSAT_BT_CLICK             = 0x01,
    ROSSAT_BT_MENU              = 0x02,
    ROSSAT_BT_UNIVERSAL_SCROLL  = 0x03,
    ROSSAT_BT_DOUBLE_CLICK      = 0x04,
    ROSSAT_BT_SHORTCUT          = 0x05, /* p1 = modifier mask, p2 = HID key usage */
    ROSSAT_BT_DISABLED          = 0x06,
    ROSSAT_BT_BROWSER_FORWARD   = 0x07,
    ROSSAT_BT_BROWSER_BACKWARD  = 0x08,
    ROSSAT_BT_TILT_LEFT         = 0x09,
    ROSSAT_BT_TILT_RIGHT        = 0x0a,
    ROSSAT_BT_SCROLL_UP         = 0x0d,
    ROSSAT_BT_SCROLL_DOWN       = 0x0e,
    ROSSAT_BT_QUICKLAUNCH       = 0x0f,
    ROSSAT_BT_PROFILE_CYCLE     = 0x10,
    ROSSAT_BT_PROFILE_UP        = 0x11,
    ROSSAT_BT_PROFILE_DOWN      = 0x12,
    ROSSAT_BT_DPI_CYCLE         = 0x14,
    ROSSAT_BT_DPI_UP            = 0x15,
    ROSSAT_BT_DPI_DOWN          = 0x16,
    ROSSAT_BT_SENSITIVITY_CYCLE = 0x17,
    ROSSAT_BT_SENSITIVITY_UP    = 0x18,
    ROSSAT_BT_SENSITIVITY_DOWN  = 0x19,
    ROSSAT_BT_WINDOWS_KEY       = 0x1a,
    ROSSAT_BT_OPEN_DRIVER       = 0x1b,
    ROSSAT_BT_OPEN_PLAYER       = 0x20,
    ROSSAT_BT_PREV_TRACK        = 0x21,
    ROSSAT_BT_NEXT_TRACK        = 0x22,
    ROSSAT_BT_PLAY_PAUSE        = 0x23,
    ROSSAT_BT_STOP              = 0x24,
    ROSSAT_BT_MUTE              = 0x25,
    ROSSAT_BT_VOLUME_UP         = 0x26,
    ROSSAT_BT_VOLUME_DOWN       = 0x27,
    ROSSAT_BT_MACRO             = 0x30,
    ROSSAT_BT_TIMER             = 0x31,
    ROSSAT_BT_TIMER_STOP        = 0x32,
    ROSSAT_BT_EASYAIM_1         = 0x33, /* temporarily switch to DPI level 1..5 */
    ROSSAT_BT_EASYAIM_2         = 0x34,
    ROSSAT_BT_EASYAIM_3         = 0x35,
    ROSSAT_BT_EASYAIM_4         = 0x36,
    ROSSAT_BT_EASYAIM_5         = 0x37,
    ROSSAT_BT_EASYSHIFT         = 0x41,
    ROSSAT_BT_EASYSHIFT_OTHER   = 0x51,
    ROSSAT_BT_EASYSHIFT_LOCK    = 0x52,
    ROSSAT_BT_EASYSHIFT_ALL     = 0x53,
    ROSSAT_BT_EASYSHIFT_LOCK_ALL = 0x54
} rossat_button_type;

/* Modifier bits for ROSSAT_BT_SHORTCUT param1 (verified on hardware). */
enum {
    ROSSAT_MOD_SHIFT = 0x02,
    ROSSAT_MOD_CTRL  = 0x04,
    ROSSAT_MOD_ALT   = 0x08,
    ROSSAT_MOD_WIN   = 0x10
};

typedef struct rossat_button {
    uint8_t type;   /* rossat_button_type */
    uint8_t param1;
    uint8_t param2;
} rossat_button;

typedef struct rossat_buttons {
    uint8_t       profile;
    rossat_button buttons[ROSSAT_BUTTON_COUNT];
} rossat_buttons;

ROSSAT_API int rossat_read_buttons(rossat_device *dev, uint8_t profile, rossat_buttons *out);
ROSSAT_API int rossat_write_buttons(rossat_device *dev, const rossat_buttons *in);
ROSSAT_API void rossat_buttons_default(uint8_t profile, rossat_buttons *out);
ROSSAT_API const char *rossat_button_name(int index);
ROSSAT_API const char *rossat_button_type_name(uint8_t type);
/* Parses a symbolic type name ("click", "dpi-up", ...) or a number. Returns
 * the type or -1. */
ROSSAT_API int rossat_button_type_from_string(const char *s);
/* Iterate the known function codes: for i in [0, count) -> type code. */
ROSSAT_API int rossat_button_type_count(void);
ROSSAT_API int rossat_button_type_at(int i);
/* CLI spelling of a type ("dpi-up"), or NULL if unknown. */
ROSSAT_API const char *rossat_button_type_key(uint8_t type);
/* "ctrl+shift+c", "alt+f4", "volumeup"... -> modifier mask + HID key usage.
 * Returns ROSSAT_OK or ROSSAT_ERR_ARGUMENT. */
ROSSAT_API int rossat_parse_shortcut(const char *text, uint8_t *modifiers, uint8_t *key);
/* Inverse of rossat_parse_shortcut; writes e.g. "ctrl+c". */
ROSSAT_API void rossat_format_shortcut(uint8_t modifiers, uint8_t key, char *buf, size_t len);

/* ------------------------------------------------------------------------ */
/* Macros (report 0x08, 2002 bytes split into two 1026 byte transfers)       */
/* ------------------------------------------------------------------------ */

enum { ROSSAT_KEY_PRESS = 1, ROSSAT_KEY_RELEASE = 2 };

typedef enum rossat_macro_mode {
    ROSSAT_MACRO_ON_PRESS      = 0, /* play once per press              */
    ROSSAT_MACRO_WHILE_PRESSED = 1, /* repeat while the button is held  */
    ROSSAT_MACRO_INFINITE      = 2  /* toggle endless repeat            */
} rossat_macro_mode;

typedef struct rossat_keystroke {
    uint8_t  key;       /* HID keyboard usage                                */
    uint8_t  action;    /* ROSSAT_KEY_PRESS / ROSSAT_KEY_RELEASE              */
    uint16_t delay_ms;  /* wait after this event                              */
} rossat_keystroke;

typedef struct rossat_macro {
    uint8_t  profile;
    uint8_t  button;
    uint8_t  loop;                 /* repeat count                            */
    uint8_t  mode;                 /* rossat_macro_mode                       */
    char     set_name[41];         /* 40 bytes on the device                  */
    char     name[33];             /* 32 bytes on the device                  */
    uint16_t count;
    rossat_keystroke keys[ROSSAT_MACRO_KEYSTROKES];
} rossat_macro;

ROSSAT_API int rossat_read_macro(rossat_device *dev, uint8_t profile, uint8_t button, rossat_macro *out);
/* Also remember to assign ROSSAT_BT_MACRO to the button. */
ROSSAT_API int rossat_write_macro(rossat_device *dev, const rossat_macro *in);

/* ------------------------------------------------------------------------ */
/* Direct ("software") lighting, as used by Swarm's AIMO engine             */
/* ------------------------------------------------------------------------ */

/* enable != 0 hands the LEDs to the host (report 0x0e); 0 returns control to
 * the profile's lighting effect. */
ROSSAT_API int rossat_set_direct_lighting(rossat_device *dev, int enable);
/* Sends 11 colours (alpha is applied by the library) via report 0x0d. */
ROSSAT_API int rossat_set_direct_colors(rossat_device *dev, const rossat_color colors[ROSSAT_LED_COUNT]);

/* ------------------------------------------------------------------------ */
/* Sensor calibration (TDCU, report 0x0f)                                    */
/* ------------------------------------------------------------------------ */

typedef struct rossat_tdcu {
    uint8_t  mode;   /* 0 = factory, 2 = custom (calibrated)                  */
    uint8_t  state;
    uint16_t value;  /* calibration result reported by the sensor             */
} rossat_tdcu;

ROSSAT_API int rossat_read_tdcu(rossat_device *dev, rossat_tdcu *out);
/* Starts calibration: keep moving the mouse on the mousepad until
 * rossat_calibration_poll() reports completion, then call
 * rossat_calibration_finish(dev, 1) to store the result (0 discards it). */
ROSSAT_API int rossat_calibration_start(rossat_device *dev);
/* *finished = 1 once the sensor is done (the device leaves its busy state). */
ROSSAT_API int rossat_calibration_poll(rossat_device *dev, int *finished);
ROSSAT_API int rossat_calibration_finish(rossat_device *dev, int save);

/* ------------------------------------------------------------------------ */
/* Lighting engine: AIMO and other software effects                          */
/* ------------------------------------------------------------------------ */

#define ROSSAT_AIMO_THEMES 10

typedef enum rossat_engine_mode {
    ROSSAT_ENGINE_AUTO      = 0, /* follow the active profile's effect        */
    ROSSAT_ENGINE_AIMO      = 1, /* force AIMO with the profile's theme       */
    ROSSAT_ENGINE_HEARTBEAT = 2  /* force heartbeat with the profile colours  */
} rossat_engine_mode;

typedef struct rossat_engine rossat_engine;

typedef struct rossat_engine_status {
    int     running;          /* worker thread alive                         */
    int     rendering;        /* currently driving the LEDs                  */
    int     shift_held;       /* Easy-Shift[+] is pressed                    */
    uint8_t profile;          /* active profile it follows, 0xff = unknown   */
    uint8_t effect;           /* effect being rendered                       */
    int     last_error;       /* last rossat_error from the worker           */
    rossat_color leds[ROSSAT_LED_COUNT]; /* last frame sent                  */
} rossat_engine_status;

/* Starts a background thread with its own device handle. Only one engine per
 * user can run at a time (lock in $XDG_RUNTIME_DIR); a second start returns
 * ROSSAT_ERR_BUSY. Software effects are rendered at ~30 fps and react to
 * mouse activity; firmware effects are left to the mouse. */
ROSSAT_API int  rossat_engine_start(const char *path, rossat_engine_mode mode, rossat_engine **out);
/* Stops the thread and hands the LEDs back to the firmware. */
ROSSAT_API void rossat_engine_stop(rossat_engine *engine);
ROSSAT_API void rossat_engine_get_status(rossat_engine *engine, rossat_engine_status *out);
/* Ask the engine to re-read the active profile (e.g. after writing settings). */
ROSSAT_API void rossat_engine_reload(rossat_engine *engine);

/* Easy-Shift[+] lighting: while the Easy-Shift button is held, the engine
 * shows this instead of the profile's lighting. Stored on the PC in
 * $XDG_CONFIG_HOME/rossat/<pid>.ini (the mouse has no slot for it). */
typedef enum rossat_shift_style {
    ROSSAT_SHIFT_SOLID = 0,
    ROSSAT_SHIFT_PULSE = 1
} rossat_shift_style;

typedef struct rossat_shift_lighting {
    int          enabled;
    uint8_t      style;   /* rossat_shift_style                          */
    rossat_color color;   /* a = brightness, independent of the profile  */
} rossat_shift_lighting;

/* Effects rendered by rossat. The firmware paints its own, hard-coded
 * Easy-Shift[+] indicator (#059cfa, see docs/PROTOCOL.md) for every effect
 * except AIMO (9). So when a profile is "rendered by rossat", the mouse
 * stores effect 9 and the effect the user picked is kept on the PC; the
 * engine draws it and can show a custom Easy-Shift[+] colour.
 * rossat_engine_effect_load() returns ROSSAT_EFFECT_AIMO when nothing is
 * stored. Software-only effects (rossat_effect_is_software) are always
 * rendered this way. */
ROSSAT_API int rossat_engine_effect_load(uint16_t pid, uint8_t profile, uint8_t *effect);
ROSSAT_API int rossat_engine_effect_save(uint16_t pid, uint8_t profile, uint8_t effect);
/* Effect the user sees for a profile: the device byte, or - if the device
 * stores 9 - the effect kept on the PC. *by_engine tells which renders it. */
ROSSAT_API void rossat_effective_effect(uint16_t pid, const rossat_settings *s, uint8_t *effect, int *by_engine);
/* Selects an effect for s->profile: updates s->effect (to write with
 * rossat_write_settings) and the PC-side choice. by_engine is forced on for
 * software-only effects. */
ROSSAT_API int rossat_select_effect(uint16_t pid, rossat_settings *s, uint8_t effect, int by_engine);

ROSSAT_API int rossat_shift_lighting_load(uint16_t pid, uint8_t profile, rossat_shift_lighting *out);
ROSSAT_API int rossat_shift_lighting_save(uint16_t pid, uint8_t profile, const rossat_shift_lighting *in);

/* Pure renderers, usable for previews. t = seconds, activity = 0..1. */
ROSSAT_API void rossat_render_frame(const rossat_settings *s, uint8_t effect, double t, double activity,
                                    rossat_color out[ROSSAT_LED_COUNT]);
ROSSAT_API void rossat_aimo_theme(int theme, rossat_color out[3]);

/* ------------------------------------------------------------------------ */
/* Low level                                                                 */
/* ------------------------------------------------------------------------ */

/* Raw feature report helpers. buf[0] must be the report id. */
ROSSAT_API int rossat_feature_get(rossat_device *dev, uint8_t *buf, size_t len);
/* Sends a report and waits for the device's ready status (report 0x04). */
ROSSAT_API int rossat_feature_set(rossat_device *dev, const uint8_t *buf, size_t len);
ROSSAT_API uint16_t rossat_checksum(const uint8_t *buf, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROSSAT_ROSSAT_H */
