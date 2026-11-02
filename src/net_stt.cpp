#include "net_stt.h"
#include "net_http.h"
#include "config.h"
#include <ArduinoJson.h>

bool stt_transcribe(const AudioBuffer &wav, String &outText) {
    if (!wav.data || wav.length == 0) return false;

    static const char *BOUNDARY = "ESP32FormBoundary7MA4YWxkTrZu0gW";
    String head = String("--") + BOUNDARY + "\r\n" +
                  "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
                  STT_MODEL + "\r\n--" + BOUNDARY + "\r\n" +
                  "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n" +
                  "Content-Type: audio/wav\r\n\r\n";
    String tail = String("\r\n--") + BOUNDARY + "--\r\n";

    size_t bodyLen = head.length() + wav.length + tail.length();
    uint8_t *bodyBuf = (uint8_t *)ps_malloc(bodyLen);
    if (!bodyBuf) {
        Serial.println("[stt] failed to allocate multipart body buffer");
        return false;
    }
    size_t offset = 0;
    memcpy(bodyBuf + offset, head.c_str(), head.length());
    offset += head.length();
    memcpy(bodyBuf + offset, wav.data, wav.length);
    offset += wav.length;
    memcpy(bodyBuf + offset, tail.c_str(), tail.length());

    static uint8_t respBuf[4096];
    HttpResult resp = http_post_text("api.openai.com", "/v1/audio/transcriptions", OPENAI_API_KEY,
                                      (String("multipart/form-data; boundary=") + BOUNDARY).c_str(),
                                      bodyBuf, bodyLen, respBuf, sizeof(respBuf) - 1);
    free(bodyBuf);

    respBuf[resp.bodyLen] = 0;

    if (resp.statusCode != 200) {
        Serial.printf("[stt] HTTP %d: %s\n", resp.statusCode, (char *)respBuf);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (char *)respBuf, resp.bodyLen);
    if (err) {
        Serial.print("[stt] JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }

    outText = doc["text"].as<String>();
    return outText.length() > 0;
}
