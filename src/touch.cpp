#include "touch.h"
#include "display.h"
#include "pins.h"
#include <FT6336U.h>

static FT6336U ft6336u(PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_RST, PIN_TOUCH_INT);

static TouchOrientation g_orientation = TouchOrientation::PORTRAIT;

static bool g_headHeld = false;
static bool g_headJustTapped = false;
static bool g_wifiHeld = false;
static bool g_wifiJustTapped = false;
static bool g_anyHeld = false;
static bool g_anyJustTapped = false;
static int16_t g_tapX = 0, g_tapY = 0;

void touch_init() {
    ft6336u.begin();
}

void touch_set_orientation(TouchOrientation orientation) {
    g_orientation = orientation;
}

void touch_update() {
    g_headJustTapped = false;
    g_wifiJustTapped = false;
    g_anyJustTapped = false;

    FT6336U_TouchPointType tp = ft6336u.scan();
    bool touched = tp.touch_count > 0;
    bool onHead = false;
    bool onWifiButton = false;
    int16_t x = 0, y = 0;

    if (touched) {
        if (g_orientation == TouchOrientation::LANDSCAPE) {
            // Coordinate remap for the FNK0104AB 2.8" ILI9341 panel in
            // landscape (rotation 1), matching Freenove's own
            // Sketch_12.1_TFT_Touch_Draw.
            x = tp.tp[0].y;
            y = 240 - tp.tp[0].x;
        } else {
            // Portrait (rotation 0) is the panel's native frame -- the raw
            // touch controller frame already lines up with it directly, no
            // remap needed (the landscape formula above is derived FROM this
            // identity mapping by rotating it 90 degrees).
            x = tp.tp[0].x;
            y = tp.tp[0].y;
        }
        onHead = display_head_hit(x, y);
        onWifiButton = display_wifi_button_hit(x, y);
    }

    if (onHead && !g_headHeld) {
        g_headHeld = true;
        g_headJustTapped = true;
    } else if (!onHead && g_headHeld) {
        g_headHeld = false;
    }

    if (onWifiButton && !g_wifiHeld) {
        g_wifiHeld = true;
        g_wifiJustTapped = true;
    } else if (!onWifiButton && g_wifiHeld) {
        g_wifiHeld = false;
    }

    if (touched && !g_anyHeld) {
        g_anyHeld = true;
        g_anyJustTapped = true;
        g_tapX = x;
        g_tapY = y;
    } else if (!touched && g_anyHeld) {
        g_anyHeld = false;
    }
}

bool touch_head_just_tapped() { return g_headJustTapped; }
bool touch_wifi_button_just_tapped() { return g_wifiJustTapped; }

bool touch_get_tap(int16_t &x, int16_t &y) {
    if (!g_anyJustTapped) return false;
    x = g_tapX;
    y = g_tapY;
    return true;
}
