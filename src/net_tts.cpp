#include "net_tts.h"
#include "net_http.h"
#include "config.h"
#include <ArduinoJson.h>

// Upper bound on a synthesized reply's length so one PSRAM buffer can hold
// the whole response (OpenAI TTS's native output is 24kHz mono 16-bit). The
// persona now caps even a "story" reply at one short paragraph (well under
// this), so this is a safety ceiling, not a target -- kept tight enough that
// a runaway reply hits an audible/logged cutoff quickly instead of silently
// ballooning into a full minute of audio.
static const size_t TTS_MAX_SECONDS = 30;

// HTTPClient::writeToStream() occasionally (seen ~1 in 5-15 calls, regardless
// of language/content) stops right after the first network chunk instead of
// the full streamed response -- always the same suspiciously-exact byte
// count, so it reads like a timing race in its internal chunked-transfer
// handling rather than anything content-dependent. A fresh retry reliably
// avoids hitting the same race twice, so that's the mitigation here rather
// than chasing the exact race inside a third-party library.
static bool fetchOnce(const String &body, uint8_t *buf, size_t capacity, HttpResult &resp) {
    resp = http_post_binary("api.openai.com", "/v1/audio/speech", OPENAI_API_KEY,
                             "application/json", (const uint8_t *)body.c_str(), body.length(),
                             buf, capacity);
    return resp.statusCode == 200 && resp.bodyLen >= 44;
}

bool tts_synthesize(const String &text, AudioBuffer &outWav) {
    outWav = AudioBuffer{};
    if (text.length() == 0) return false;

    JsonDocument reqDoc;
    reqDoc["model"] = TTS_MODEL;
    reqDoc["voice"] = TTS_VOICE;
    reqDoc["input"] = text;
    reqDoc["response_format"] = "wav";
#ifdef TTS_INSTRUCTIONS
    reqDoc["instructions"] = TTS_INSTRUCTIONS;
#endif

    String body;
    serializeJson(reqDoc, body);

    const size_t capacity = TTS_MAX_SECONDS * AUDIO_SAMPLE_RATE * sizeof(int16_t) + 44;
    uint8_t *buf = (uint8_t *)ps_malloc(capacity);
    if (!buf) {
        Serial.println("[tts] failed to allocate PSRAM buffer");
        return false;
    }

    // Even very fast TTS delivery doesn't exceed ~25 characters/second of
    // audio, so anything shorter than that is almost certainly a truncated
    // transfer, not a legitimately terse reply -- worth one retry.
    size_t minPlausibleBytes = text.length() * (AUDIO_SAMPLE_RATE * 2 / 25);

    HttpResult resp;
    bool ok = fetchOnce(body, buf, capacity, resp);
    if (ok && resp.bodyLen < minPlausibleBytes) {
        Serial.printf("[tts] received only %u bytes for %u chars of text -- likely truncated, retrying\n",
                      (unsigned)resp.bodyLen, (unsigned)text.length());
        ok = fetchOnce(body, buf, capacity, resp);
    }

    if (!ok) {
        if (resp.statusCode != 200) {
            Serial.printf("[tts] HTTP %d: %.*s\n", resp.statusCode, (int)min(resp.bodyLen, (size_t)300), (char *)buf);
        } else {
            Serial.println("[tts] invalid audio response (shorter than 44 bytes)");
        }
        free(buf);
        return false;
    }

    Serial.printf("[tts] received=%u bytes (~%.2fs of audio)\n",
                  (unsigned)resp.bodyLen, (resp.bodyLen - 44) / (float)(AUDIO_SAMPLE_RATE * 2));

    outWav.data = buf;
    outWav.length = resp.bodyLen;
    outWav.capacity = capacity;
    return true;
}
