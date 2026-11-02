#pragma once
#include <Arduino.h>

enum class TouchOrientation { PORTRAIT, LANDSCAPE };

void touch_init();

// The main app runs portrait (rotation 0); wifi_setup.cpp temporarily flips
// the panel to landscape (rotation 1) for its own screens and must flip the
// touch remap to match, then flip both back on the way out.
void touch_set_orientation(TouchOrientation orientation);

// Call every loop() iteration before reading touch_head_just_tapped().
void touch_update();

// True for exactly one touch_update() call when a new touch lands on the
// dino's head -- each tap toggles recording on/off (no need to hold).
bool touch_head_just_tapped();

// True for exactly one touch_update() call when a new touch lands on the
// WiFi button, top-right of the screen.
bool touch_wifi_button_just_tapped();

// True for exactly one touch_update() call when a new touch lands anywhere
// on the panel, with its remapped screen coordinates -- used by full-screen
// flows (the WiFi network list/keyboard) that need arbitrary hit-testing
// rather than the two fixed zones above.
bool touch_get_tap(int16_t &x, int16_t &y);
