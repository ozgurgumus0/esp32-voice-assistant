#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

enum class FaceState {
    NEUTRAL,
    HAPPY,
    SAD,
    THINKING,
    SURPRISED,
    CONFUSED,
    LISTENING,
    SPEAKING,
    ERROR_STATE
};

void display_init();
void display_set_face(FaceState state);
FaceState display_get_face();

// Call every loop() iteration; redraws animated states (blinking dots, mouth
// flap, recording pulse) at their own internal pace.
void display_tick();

// Re-paints the static portrait chrome (footer background) and forces a face
// redraw on the next display_tick() -- call after wifi_setup.cpp hands the
// panel back from its temporary landscape takeover.
void display_force_redraw();

// Drives the progress bar shown while LISTENING.
void display_set_record_progress(float seconds, float maxSeconds);

// One-line status text shown below the dino's face (e.g. "Transcribing...",
// or an error message). Pass "" to clear it.
void display_show_status(const char *text);

// True if (x, y) in screen coordinates falls within the dino's head -- the
// tap target for starting/stopping a recording.
bool display_head_hit(int16_t x, int16_t y);

// True if (x, y) falls within the WiFi button, top-right of the screen --
// the tap target for opening the WiFi setup portal.
bool display_wifi_button_hit(int16_t x, int16_t y);

// Maps an LLM emotion tag ("happy", "sad", ...) to a FaceState. Unknown/empty
// tags fall back to NEUTRAL.
FaceState display_face_from_emotion_tag(const String &tag);

// The single TFT_eSPI instance driving the physical panel -- shared with
// other full-screen UI (WiFi setup) so there's never more than one object
// talking to the display hardware at once.
TFT_eSPI &display_get_tft();
