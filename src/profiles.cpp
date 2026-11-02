#include "profiles.h"
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <math.h>

// Now doubling as the long-horizon memory (topics/questions, not just durable
// facts -- see the remember-tag instruction in net_llm.cpp), since these
// outlive the much shorter rolling history -- give it real headroom.
static const int FACTS_MAX = 30;
static const char *PROFILES_PATH = "/profiles.json";

struct ProfileData {
    String name;
    String facts[FACTS_MAX];
    int factCount = 0;
    HistEntry history[PROFILE_HISTORY_MAX];
    int historyCount = 0;
};

static ProfileData g_primary;
static ProfileData g_secondary;
// Default to the primary owner before the first utterance is ever
// classified -- profiles_classify_speaker() re-decides this every turn, so
// this only matters for the very first interaction after boot.
static ProfileId g_active = ProfileId::PRIMARY;
static bool g_fsReady = false;

static ProfileData &dataFor(ProfileId id) {
    return id == ProfileId::PRIMARY ? g_primary : g_secondary;
}

// ---- persistence --------------------------------------------------------

static void loadProfileFromJson(ProfileData &pd, JsonObjectConst obj) {
    if (obj.isNull()) return;

    pd.factCount = 0;
    for (JsonVariantConst f : obj["facts"].as<JsonArrayConst>()) {
        if (pd.factCount >= FACTS_MAX) break;
        pd.facts[pd.factCount++] = f.as<String>();
    }

    pd.historyCount = 0;
    for (JsonObjectConst h : obj["history"].as<JsonArrayConst>()) {
        if (pd.historyCount >= PROFILE_HISTORY_MAX) break;
        pd.history[pd.historyCount++] = {h["role"].as<String>(), h["content"].as<String>()};
    }
}

static void loadAll() {
    File f = LittleFS.open(PROFILES_PATH, "r");
    if (!f) {
        Serial.println("[profiles] no saved profiles.json yet, starting fresh");
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.print("[profiles] JSON parse error, starting fresh: ");
        Serial.println(err.c_str());
        return;
    }
    loadProfileFromJson(g_primary, doc["primary"]);
    loadProfileFromJson(g_secondary, doc["secondary"]);
    Serial.printf("[profiles] loaded -- primary: %d fact(s)/%d turn(s), secondary: %d fact(s)/%d turn(s)\n",
                  g_primary.factCount, g_primary.historyCount, g_secondary.factCount, g_secondary.historyCount);
}

static void saveProfileToJson(const ProfileData &pd, JsonObject obj) {
    JsonArray facts = obj["facts"].to<JsonArray>();
    for (int i = 0; i < pd.factCount; i++) facts.add(pd.facts[i]);

    JsonArray hist = obj["history"].to<JsonArray>();
    for (int i = 0; i < pd.historyCount; i++) {
        JsonObject h = hist.add<JsonObject>();
        h["role"] = pd.history[i].role;
        h["content"] = pd.history[i].content;
    }
}

void profiles_save() {
    if (!g_fsReady) return;
    JsonDocument doc;
    saveProfileToJson(g_primary, doc["primary"].to<JsonObject>());
    saveProfileToJson(g_secondary, doc["secondary"].to<JsonObject>());

    File f = LittleFS.open(PROFILES_PATH, "w");
    if (!f) {
        Serial.println("[profiles] failed to open profiles.json for writing");
        return;
    }
    serializeJson(doc, f);
    f.close();
}

void profiles_init() {
    g_fsReady = LittleFS.begin(true); // format on first-ever boot
    if (!g_fsReady) {
        Serial.println("[profiles] LittleFS mount failed -- memory won't persist this session");
    }
    g_primary.name = PROFILE_PRIMARY_NAME;
    g_secondary.name = PROFILE_SECONDARY_NAME;
    if (g_fsReady) loadAll();
}

void profiles_set_active(ProfileId id) { g_active = id; }
ProfileId profiles_get_active() { return g_active; }

// ---- voice-pitch speaker classification ---------------------------------

// Coarse normalized-autocorrelation F0 estimate per ~40ms frame, classifying
// the whole utterance by the median F0 across confidently-voiced frames.
// Only two known speakers with a real pitch-range gap between them, so a
// simple threshold split is all this needs -- no actual speaker-recognition
// model.
//
// Naive autocorrelation (raw sum, search for the single global peak across
// all lags) reliably locks onto a strong 2nd harmonic instead of the true
// fundamental for voices with a lot of harmonic energy -- a male voice at
// ~110Hz has a very strong ~220Hz component, and the raw sum at that shorter
// lag can outscore the sum at the true (longer) lag. Fix: normalize each
// lag's correlation by the energy of both windows it compares (so lags are
// actually comparable to each other), and search from the LONGEST lag (the
// lowest frequency) downward, committing to the first confidently-periodic
// lag found rather than whichever lag scores highest overall -- that's what
// keeps it from ever preferring a harmonic over the fundamental.
ProfileId profiles_classify_speaker(const AudioBuffer &wav) {
    if (!wav.data || wav.length <= 44) return g_active; // nothing to go on, keep whoever was active

    const int16_t *samples = (const int16_t *)(wav.data + 44);
    size_t sampleCount = (wav.length - 44) / sizeof(int16_t);
    const int sampleRate = AUDIO_SAMPLE_RATE;

    const int frameLen = sampleRate * 40 / 1000; // 40ms
    const int hop = frameLen / 2;                 // 50% overlap
    const int lagMin = sampleRate / 300;          // 300 Hz (highest expected pitch)
    const int lagMax = sampleRate / 70;           // 70 Hz (lowest expected pitch)

    static const int MAX_ESTIMATES = 128;
    float estimates[MAX_ESTIMATES];
    int estimateCount = 0;

    for (size_t start = 0; start + frameLen <= sampleCount && estimateCount < MAX_ESTIMATES; start += hop) {
        const int16_t *frame = samples + start;

        float energy = 0;
        for (int i = 0; i < frameLen; i++) energy += (float)frame[i] * frame[i];
        if (energy / frameLen < 2000.0f) continue; // skip silence/near-silence

        int foundLag = 0;
        for (int lag = lagMax; lag >= lagMin; lag--) { // low frequency first
            if (lag >= frameLen) continue;
            int n = frameLen - lag;
            float cross = 0, e1 = 0, e2 = 0;
            for (int i = 0; i < n; i++) {
                float a = frame[i], b = frame[i + lag];
                cross += a * b;
                e1 += a * a;
                e2 += b * b;
            }
            float denom = sqrtf(e1 * e2);
            float norm = denom > 1.0f ? cross / denom : 0.0f;
            if (norm > 0.45f) { foundLag = lag; break; } // first confident (lowest-frequency) match wins
        }
        if (foundLag == 0) continue; // no confidently-periodic lag in this frame -- not voiced enough to use

        estimates[estimateCount++] = (float)sampleRate / foundLag;
    }

    if (estimateCount < 3) {
        Serial.println("[profiles] too few voiced frames to classify speaker, keeping current profile");
        return g_active;
    }

    // insertion sort for the median -- more robust to a stray octave-error
    // outlier than a plain mean would be, and estimateCount is tiny (<=128)
    for (int i = 1; i < estimateCount; i++) {
        float key = estimates[i];
        int j = i - 1;
        while (j >= 0 && estimates[j] > key) { estimates[j + 1] = estimates[j]; j--; }
        estimates[j + 1] = key;
    }
    float medianHz = estimates[estimateCount / 2];

    ProfileId guess = (medianHz < VOICE_PITCH_THRESHOLD_HZ) ? ProfileId::SECONDARY : ProfileId::PRIMARY;
    Serial.printf("[profiles] voiced frames=%d median F0=%.0fHz -> %s\n",
                  estimateCount, medianHz, guess == ProfileId::PRIMARY ? "primary" : "secondary");
    return guess;
}

// ---- prompt context / facts digest --------------------------------------

String profiles_build_prompt_context() {
    ProfileData &pd = dataFor(g_active);
    String ctx = (g_active == ProfileId::PRIMARY) ? PROFILE_PRIMARY_CONTEXT : PROFILE_SECONDARY_CONTEXT;
    if (pd.factCount > 0) {
        ctx += " Some things you already know about them: ";
        for (int i = 0; i < pd.factCount; i++) {
            if (i > 0) ctx += "; ";
            ctx += pd.facts[i];
        }
        ctx += ".";
    }
    return ctx;
}

// ---- history -------------------------------------------------------------

int profiles_get_history(HistEntry *out, int maxCount) {
    ProfileData &pd = dataFor(g_active);
    int n = min(pd.historyCount, maxCount);
    for (int i = 0; i < n; i++) out[i] = pd.history[i];
    return n;
}

void profiles_push_history(const String &role, const String &content) {
    ProfileData &pd = dataFor(g_active);
    if (pd.historyCount < PROFILE_HISTORY_MAX) {
        pd.history[pd.historyCount++] = {role, content};
    } else {
        for (int i = 1; i < PROFILE_HISTORY_MAX; i++) pd.history[i - 1] = pd.history[i];
        pd.history[PROFILE_HISTORY_MAX - 1] = {role, content};
    }
}

// ---- remembering facts ----------------------------------------------------

void profiles_remember_fact(const String &fact) {
    if (fact.length() == 0) return;

    ProfileData &pd = dataFor(g_active);
    if (pd.factCount < FACTS_MAX) {
        pd.facts[pd.factCount++] = fact;
    } else {
        for (int i = 1; i < FACTS_MAX; i++) pd.facts[i - 1] = pd.facts[i];
        pd.facts[FACTS_MAX - 1] = fact;
    }
    Serial.printf("[profiles] remembered (%s): %s\n",
                  g_active == ProfileId::PRIMARY ? "primary" : "secondary", fact.c_str());
}

void profiles_forget_fact(const String &fragment) {
    if (fragment.length() == 0) return;
    ProfileData &pd = dataFor(g_active);

    String needle = fragment;
    needle.toLowerCase();
    for (int i = 0; i < pd.factCount; i++) {
        String hay = pd.facts[i];
        hay.toLowerCase();
        // Either direction: the model may echo back the stored fact
        // verbatim, or a shorter paraphrase of it -- match whichever way.
        if (hay.indexOf(needle) >= 0 || needle.indexOf(hay) >= 0) {
            Serial.printf("[profiles] forgot (%s): %s\n",
                          g_active == ProfileId::PRIMARY ? "primary" : "secondary", pd.facts[i].c_str());
            for (int j = i + 1; j < pd.factCount; j++) pd.facts[j - 1] = pd.facts[j];
            pd.factCount--;
            return; // first match only
        }
    }
    Serial.printf("[profiles] forget requested but no matching fact found (%s): %s\n",
                  g_active == ProfileId::PRIMARY ? "primary" : "secondary", fragment.c_str());
}

void profiles_forget_all() {
    ProfileData &pd = dataFor(g_active);
    Serial.printf("[profiles] wiped all memory for %s (%d fact(s), %d turn(s))\n",
                  g_active == ProfileId::PRIMARY ? "primary" : "secondary", pd.factCount, pd.historyCount);
    pd.factCount = 0;
    pd.historyCount = 0;
}
