#pragma once
#include <Arduino.h>
#include "audio.h"

// Uploads a WAV buffer to OpenAI Whisper for transcription.
// Returns true and fills outText on success.
bool stt_transcribe(const AudioBuffer &wav, String &outText);
