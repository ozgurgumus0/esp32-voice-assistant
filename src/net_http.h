#pragma once
#include <Arduino.h>

// Shared HTTPS POST helpers used by net_stt.cpp / net_llm.cpp / net_tts.cpp,
// built on Arduino-ESP32's HTTPClient rather than a hand-rolled socket parser
// (a hand-rolled chunked-transfer reader here previously desynced partway
// through and silently truncated responses).

struct HttpResult {
    int statusCode = -1;
    size_t bodyLen = 0;
};

// For small text/JSON responses (STT transcript, LLM chat completion): uses
// HTTPClient::getString(), which robustly handles chunked/content-length
// responses internally. Safe up to a few KB (uses the regular heap, not PSRAM).
HttpResult http_post_text(const char *host, const char *path, const char *bearerToken,
                           const char *contentType, const uint8_t *body, size_t bodyLen,
                           uint8_t *outBuf, size_t outCapacity);

// For large binary responses (TTS audio): streams directly into outBuf
// (a PSRAM buffer) instead of going through a heap-allocated String.
HttpResult http_post_binary(const char *host, const char *path, const char *bearerToken,
                             const char *contentType, const uint8_t *body, size_t bodyLen,
                             uint8_t *outBuf, size_t outCapacity);
