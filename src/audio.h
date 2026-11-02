#pragma once
#include <Arduino.h>

struct AudioBuffer {
    uint8_t *data = nullptr;   // complete WAV file (44-byte header + PCM16 mono)
    size_t   length = 0;       // bytes used
    size_t   capacity = 0;
};

void audio_init();

// Tap-to-toggle recording, driven from the main loop between the start/stop taps.
void audio_record_start();
void audio_record_feed();
bool audio_record_is_full();
float audio_record_seconds();
AudioBuffer audio_record_stop();   // transfers ownership to the caller; free with audio_free()
void audio_free(AudioBuffer &buf);

// Blocking playback of a WAV buffer (as returned by the TTS API). Writes in
// small chunks and calls onTick() between them (if provided) so the caller
// can animate the display (e.g. a talking mouth) while audio is still playing.
void audio_play(const uint8_t *wavData, size_t wavLen, void (*onTick)() = nullptr);
