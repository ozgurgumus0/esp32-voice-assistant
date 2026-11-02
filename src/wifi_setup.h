#pragma once
#include <Arduino.h>

// Tries whatever network is already stored in the ESP32's NVS (WiFi.begin()
// with no args reuses it). Returns false on timeout -- there may be no
// stored network at all, e.g. a brand new board or a new location.
bool wifi_setup_try_stored(uint32_t timeoutMs);

// Full on-screen flow: scan -> pick a network from a touch list -> type its
// password on an on-screen keyboard (skipped for open networks) -> connect.
// A "Use phone instead" button falls back to the "Dino Setup" access point +
// captive portal, for networks the on-screen list/keyboard can't handle
// well (hidden SSIDs, awkward passwords). Blocking, like the rest of the
// pipeline; returns true once connected.
bool wifi_setup_run();
