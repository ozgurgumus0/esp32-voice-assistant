#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "display.h"
#include "touch.h"
#include "audio.h"
#include "net_stt.h"
#include "net_llm.h"
#include "net_tts.h"
#include "wifi_setup.h"
#include "profiles.h"

enum class AppState { IDLE, LISTENING, ERROR_ST };

static AppState g_appState = AppState::IDLE;
static uint32_t g_errorUntilMs = 0;

static void connectWiFi() {
    display_set_face(FaceState::THINKING);
    display_show_status("Waking up...");
    display_tick();

    if (wifi_setup_try_stored(12000)) {
        Serial.print("[wifi] connected, IP: ");
        Serial.println(WiFi.localIP());
        return;
    }

    // Hands off to the on-screen network list + keyboard (with a "use phone
    // instead" escape hatch) until the board is actually online -- there's
    // nothing else useful to do at boot without a network.
    while (!wifi_setup_run()) {}

    Serial.print("[wifi] connected, IP: ");
    Serial.println(WiFi.localIP());
}

static void goError(const char *message) {
    Serial.print("[error] ");
    Serial.println(message);
    display_set_face(FaceState::ERROR_STATE);
    display_show_status(message);
    display_tick();
    g_appState = AppState::ERROR_ST;
    g_errorUntilMs = millis() + 3000;
}

// STT -> LLM -> TTS -> playback for one press-and-release cycle. Blocking by
// design: the board has nothing else useful to do while waiting on the cloud.
static void runProcessing() {
    uint32_t t0 = millis();
    AudioBuffer wav = audio_record_stop();

    if (wav.length <= 44) {
        audio_free(wav);
        goError("Didn't hear anything, try again");
        return;
    }

    // Voice-pitch speaker guess runs on the raw recording, before STT even
    // touches it -- picks which of the two known profiles' history/facts
    // this turn reads from and appends to.
    profiles_set_active(profiles_classify_speaker(wav));

    display_set_face(FaceState::THINKING);
    display_show_status("Listening close...");
    display_tick();

    String transcript;
    bool sttOk = stt_transcribe(wav, transcript);
    audio_free(wav);

    if (!sttOk || transcript.length() == 0) {
        goError("Didn't quite catch that...");
        return;
    }
    Serial.printf("[stt] (+%ums) ", (unsigned)(millis() - t0));
    Serial.println(transcript);

    display_show_status("Thinking...");
    display_tick();

    String reply, emotion;
    bool llmOk = llm_chat(transcript, reply, emotion);
    if (!llmOk || reply.length() == 0) {
        goError("Lost my train of thought...");
        return;
    }
    Serial.printf("[llm] (+%ums) (%s) ", (unsigned)(millis() - t0), emotion.c_str());
    Serial.println(reply);

    // Show the LLM's chosen emotion for a beat before switching to the
    // talking-mouth animation that runs for the actual playback.
    display_set_face(display_face_from_emotion_tag(emotion));
    display_show_status("Clearing my throat...");
    display_tick();

    AudioBuffer ttsWav;
    bool ttsOk = tts_synthesize(reply, ttsWav);
    if (!ttsOk) {
        goError("Voice got stuck, try again");
        return;
    }
    Serial.printf("[tts] (+%ums) ready to speak\n", (unsigned)(millis() - t0));
    delay(400); // let the emotion face be visible briefly before speaking starts

    display_set_face(FaceState::SPEAKING);
    display_show_status("...");
    display_tick();
    audio_play(ttsWav.data, ttsWav.length, display_tick);
    audio_free(ttsWav);

    display_show_status("");
    display_set_face(FaceState::NEUTRAL);
    g_appState = AppState::IDLE;
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[starting] ESP32-S3 CYD voice assistant");

    display_init();
    display_set_face(FaceState::NEUTRAL);
    display_tick();

    touch_init();
    audio_init();
    profiles_init();

    connectWiFi();

    display_set_face(FaceState::HAPPY);
    display_show_status("Ready! Tap the dino to talk.");
    display_tick();
    delay(800);
    display_set_face(FaceState::NEUTRAL);
    display_show_status("");

    g_appState = AppState::IDLE;
}

void loop() {
    touch_update();

    switch (g_appState) {
        case AppState::IDLE:
            if (touch_wifi_button_just_tapped()) {
                Serial.println("[touch] tap -> WiFi setup");
                wifi_setup_run();
                if (WiFi.status() == WL_CONNECTED) {
                    display_show_status("");
                    display_set_face(FaceState::NEUTRAL);
                } else {
                    goError("Still no WiFi");
                }
                break;
            }
            if (touch_head_just_tapped()) {
                Serial.println("[touch] tap -> LISTENING");
                audio_record_start();
                display_set_face(FaceState::LISTENING);
                g_appState = AppState::LISTENING;
            }
            break;

        case AppState::LISTENING:
            audio_record_feed();
            display_set_record_progress(audio_record_seconds(), MAX_RECORD_SECONDS);
            if (touch_head_just_tapped() || audio_record_is_full()) {
                Serial.printf("[touch] tap -> stop after %.2fs -> processing\n", audio_record_seconds());
                runProcessing(); // blocking; leaves us in IDLE or ERROR_ST
            }
            break;

        case AppState::ERROR_ST:
            if (millis() > g_errorUntilMs) {
                display_show_status("");
                display_set_face(FaceState::NEUTRAL);
                g_appState = AppState::IDLE;
            }
            break;
    }

    display_tick();
    delay(10); // paces touch/I2C polling; I2S DMA easily absorbs this while LISTENING
}
