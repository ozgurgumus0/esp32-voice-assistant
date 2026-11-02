#pragma once
#include <Arduino.h>
#include "audio.h"

// Synthesizes `text` via OpenAI TTS as a 24kHz mono WAV file into a freshly
// PSRAM-allocated AudioBuffer (free with audio_free() when done playing it).
bool tts_synthesize(const String &text, AudioBuffer &outWav);
