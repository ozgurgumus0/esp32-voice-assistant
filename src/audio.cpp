#include "audio.h"
#include "config.h"
#include "pins.h"
#include "es8311.h"
#include <ESP_I2S.h>

static I2SClass i2s;

static uint8_t *g_recordBuf = nullptr;
static size_t g_recordCapacity = 0;
static size_t g_recordCursor = 0;
static bool g_recording = false;
static uint32_t g_recordStartMs = 0;

static void wavWriteHeader(uint8_t *buf, uint32_t dataBytes) {
    const uint32_t sampleRate = AUDIO_SAMPLE_RATE;
    const uint16_t numChannels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    const uint16_t blockAlign = numChannels * bitsPerSample / 8;
    const uint32_t riffChunkSize = 36 + dataBytes;

    memcpy(buf + 0, "RIFF", 4);
    memcpy(buf + 4, &riffChunkSize, 4);
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    const uint32_t fmtChunkSize = 16;
    memcpy(buf + 16, &fmtChunkSize, 4);
    const uint16_t audioFormat = 1; // PCM
    memcpy(buf + 20, &audioFormat, 2);
    memcpy(buf + 22, &numChannels, 2);
    memcpy(buf + 24, &sampleRate, 4);
    memcpy(buf + 28, &byteRate, 4);
    memcpy(buf + 32, &blockAlign, 2);
    memcpy(buf + 34, &bitsPerSample, 2);
    memcpy(buf + 36, "data", 4);
    memcpy(buf + 40, &dataBytes, 4);
}

void audio_init() {
    pinMode(PIN_PA_ENABLE, OUTPUT);
    digitalWrite(PIN_PA_ENABLE, LOW); // FM8002E amplifier enable is ACTIVE LOW

    // NOTE: Wire.begin() is NOT called here. touch_init() (FT6336U::begin())
    // already brings up the shared I2C bus (same SDA/SCL pins) before this
    // runs, and calling Wire.begin() a second time on the same bus caused an
    // intermittent watchdog reset during boot. touch_init() must run first.

    i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
    if (!i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
        Serial.println("[audio] Failed to start I2S!");
    }
    // I2SClass::available() is a stub (always reports a fixed chunk size, not
    // real DMA state), so readBytes() is our only real signal -- keep its
    // per-attempt timeout short so a stalled I2S read can never freeze loop().
    i2s.setTimeout(20);

    if (es8311_codec_init() != ESP_OK) {
        Serial.println("[audio] ES8311 codec init failed!");
    }
}

void audio_record_start() {
    g_recordCapacity = 44 + (size_t)MAX_RECORD_SECONDS * AUDIO_SAMPLE_RATE * sizeof(int16_t);
    g_recordBuf = (uint8_t *)ps_malloc(g_recordCapacity);
    if (!g_recordBuf) {
        Serial.println("[audio] Failed to allocate PSRAM recording buffer!");
        g_recording = false;
        return;
    }

    // Drain whatever is already sitting in the I2S RX ring buffer so playback
    // doesn't start with a fragment of audio from before the tap that started this.
    // NOTE: I2SClass::available() is a stub that always reports a fixed chunk
    // size (never 0), so it cannot be used as a loop-exit condition -- that
    // previously caused this to spin forever. Bound the drain by iteration
    // count instead; readBytes()'s own short timeout (see audio_init) caps
    // how long each attempt can take.
    uint8_t scratch[256];
    for (int i = 0; i < 8; i++) {
        i2s.readBytes((char *)scratch, sizeof(scratch));
    }

    g_recordCursor = 44; // reserve header space, patched in audio_record_stop()
    g_recording = true;
    g_recordStartMs = millis();
}

void audio_record_feed() {
    if (!g_recording || !g_recordBuf) return;

    size_t remaining = g_recordCapacity - g_recordCursor;
    if (remaining == 0) {
        g_recording = false; // buffer full, caller should treat this like a release
        return;
    }

    int avail = i2s.available();
    if (avail <= 0) return;

    size_t toRead = min((size_t)avail, remaining);
    size_t got = i2s.readBytes((char *)(g_recordBuf + g_recordCursor), toRead);
    g_recordCursor += got;
}

bool audio_record_is_full() {
    return g_recordBuf && g_recordCursor >= g_recordCapacity;
}

float audio_record_seconds() {
    if (!g_recording) return 0.0f;
    return (millis() - g_recordStartMs) / 1000.0f;
}

AudioBuffer audio_record_stop() {
    g_recording = false;

    AudioBuffer buf;
    if (!g_recordBuf) return buf;

    size_t dataBytes = (g_recordCursor > 44) ? (g_recordCursor - 44) : 0;
    wavWriteHeader(g_recordBuf, dataBytes);

    buf.data = g_recordBuf;
    buf.length = g_recordCursor;
    buf.capacity = g_recordCapacity;

    g_recordBuf = nullptr; // ownership transferred to caller
    g_recordCapacity = 0;
    g_recordCursor = 0;
    return buf;
}

void audio_free(AudioBuffer &buf) {
    if (buf.data) {
        free(buf.data);
        buf.data = nullptr;
    }
    buf.length = 0;
    buf.capacity = 0;
}

void audio_play(const uint8_t *wavData, size_t wavLen, void (*onTick)()) {
    if (!wavData || wavLen < 44) return;

    // Canonical WAV files put "data" right after the 44-byte header, but scan
    // for it properly in case an encoder inserted extra chunks before it.
    size_t pcmOffset = 44;
    size_t pcmLen = wavLen - 44;
    size_t offset = 12; // skip "RIFF" + size + "WAVE"
    while (offset + 8 <= wavLen) {
        uint32_t chunkSize;
        memcpy(&chunkSize, wavData + offset + 4, 4);
        if (memcmp(wavData + offset, "data", 4) == 0) {
            pcmOffset = offset + 8;
            pcmLen = min((size_t)chunkSize, wavLen - pcmOffset);
            break;
        }
        offset += 8 + chunkSize + (chunkSize & 1); // chunks are word-aligned
    }

    const size_t CHUNK = 2048;
    const uint8_t *p = wavData + pcmOffset;
    size_t remaining = pcmLen;
    while (remaining > 0) {
        size_t n = min(remaining, CHUNK);
        i2s.write(p, n);
        p += n;
        remaining -= n;
        if (onTick) onTick();
    }
}
