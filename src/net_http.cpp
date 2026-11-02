#include "net_http.h"
#include "certs.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

// A single persistent client/HTTPClient pair, reused across calls (STT, LLM,
// TTS all run one at a time, never concurrently). With setReuse(true), as
// long as consecutive requests target the same host (api.openai.com for all
// three when LLM_PROVIDER is OPENAI), HTTPClient keeps the TLS connection
// open instead of doing a fresh TCP+TLS handshake each time -- that handshake
// is slow on the ESP32's CPU, so this saves roughly a second or more per
// reused call.
static WiFiClientSecure g_client;
static HTTPClient g_http;
static bool g_ready = false;

static bool beginRequest(const char *host, const char *path, const char *bearerToken, const char *contentType) {
    if (!g_ready) {
        g_client.setCACert(ROOT_CA_BUNDLE);
        g_client.setTimeout(HTTP_TIMEOUT_MS);
        g_http.setTimeout(HTTP_TIMEOUT_MS);
        g_http.setReuse(true);
        g_ready = true;
    }

    String url = String("https://") + host + path;
    if (!g_http.begin(g_client, url)) {
        Serial.println("[http] begin() failed");
        return false;
    }
    g_http.addHeader("Authorization", String("Bearer ") + bearerToken);
    g_http.addHeader("Content-Type", contentType);
    return true;
}

HttpResult http_post_text(const char *host, const char *path, const char *bearerToken,
                           const char *contentType, const uint8_t *body, size_t bodyLen,
                           uint8_t *outBuf, size_t outCapacity) {
    HttpResult result;
    if (!beginRequest(host, path, bearerToken, contentType)) return result;

    result.statusCode = g_http.POST((uint8_t *)body, bodyLen);
    if (result.statusCode > 0) {
        String responseBody = g_http.getString();
        size_t n = min(responseBody.length(), outCapacity);
        memcpy(outBuf, responseBody.c_str(), n);
        result.bodyLen = n;
    } else {
        Serial.printf("[http] request failed: %s\n", g_http.errorToString(result.statusCode).c_str());
    }

    g_http.end();
    return result;
}

// A write-only sink that copies bytes straight into a caller-owned buffer,
// so HTTPClient::writeToStream() -- its own well-tested body-transfer path,
// the same one getString() uses internally -- can deliver a large binary
// response directly into our PSRAM buffer instead of a heap String.
// writeToStream() takes a Stream*, so this must be a Stream (not just a
// Print); the read-side methods are unused stubs.
class BufferSink : public Stream {
public:
    BufferSink(uint8_t *buf, size_t capacity) : buf_(buf), capacity_(capacity) {}
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *data, size_t len) override {
        size_t n = min(len, capacity_ - written_);
        if (n > 0) memcpy(buf_ + written_, data, n);
        written_ += n;
        return n;
    }
    size_t written() const { return written_; }

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

private:
    uint8_t *buf_;
    size_t capacity_;
    size_t written_ = 0;
};

HttpResult http_post_binary(const char *host, const char *path, const char *bearerToken,
                             const char *contentType, const uint8_t *body, size_t bodyLen,
                             uint8_t *outBuf, size_t outCapacity) {
    HttpResult result;
    if (!beginRequest(host, path, bearerToken, contentType)) return result;

    result.statusCode = g_http.POST((uint8_t *)body, bodyLen);
    if (result.statusCode > 0) {
        BufferSink sink(outBuf, outCapacity);
        g_http.writeToStream(&sink);
        result.bodyLen = sink.written();
    } else {
        Serial.printf("[http] request failed: %s\n", g_http.errorToString(result.statusCode).c_str());
    }

    g_http.end();
    return result;
}
