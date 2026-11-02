#include "display.h"
#include <TFT_eSPI.h>
#include <math.h>

static TFT_eSPI tft;
static TFT_eSprite spr(&tft); // whole face area is drawn off-screen, then pushed in one go -- avoids
                               // visible flicker now that the scene has several moving parts.

// Portrait (rotation 0, the panel's native frame) on the 2.8" 240x320 ILI9341 panel.
static const int16_t SCREEN_W = 240;
static const int16_t SCREEN_H = 320;

// Face/body sprite covers most of the screen now that there's a full body
// below the head -- status text keeps a footer strip at the very bottom.
static const int16_t FACE_AREA_H = 260; // face+body sprite: y in [0, FACE_AREA_H)
static const int16_t GROUND_Y = FACE_AREA_H - 28; // top edge of the grass strip
// Spans the full dead zone from the bottom of the sprite down to the bottom
// of the screen, with no gap on either edge -- display_show_status()'s clear
// has to repaint every row here or whatever was drawn there last (e.g. the
// WiFi setup screens' full-screen background) is stuck showing as a stray bar.
static const int16_t STATUS_Y = FACE_AREA_H;
static const int16_t STATUS_H = SCREEN_H - FACE_AREA_H;

static const int16_t CX = SCREEN_W / 2;
static const int16_t HEAD_CY = 90;
static const int16_t HEAD_R = 58;

// Torso sits below the head, overlapping its lower half slightly so the head
// (drawn last) paints a seamless neck join rather than leaving a gap.
static const int16_t TORSO_DY = 90; // torso centre, relative to headCy
static const int16_t TORSO_RX = 54;
static const int16_t TORSO_RY = 46;
static const int16_t LEG_TOP_DY = 118; // leg top, relative to headCy

static const int16_t WIFI_BTN_CX = SCREEN_W - 22;
static const int16_t WIFI_BTN_CY = 24;
static const int16_t WIFI_BTN_R = 17; // hit-test is a little larger than the drawn badge, easier to tap
static const int16_t EYE_DY = -8;   // eye/mouth centres are relative to the head centre so the
static const int16_t MOUTH_DY = 28; // idle bob animation can shift the whole face as one unit.
static const int16_t EYE_DX = 26;   // horizontal offset of each eye from center

static FaceState g_state = FaceState::NEUTRAL;
static bool g_dirty = true;
static uint32_t g_lastAnimMs = 0;
static float g_recordSeconds = 0, g_recordMax = 1;
static char g_status[48] = "";
static uint32_t g_nextBlinkMs = 0;

// ---- palette -----------------------------------------------------------

static uint16_t dinoGreen()     { return tft.color565(110, 190, 120); }
static uint16_t dinoGreenDark() { return tft.color565(70, 140, 90); }
static uint16_t dinoHighlight() { return tft.color565(170, 225, 170); }
static uint16_t dinoBelly()     { return tft.color565(205, 235, 195); }
static uint16_t blushColor()    { return tft.color565(255, 170, 165); }
static uint16_t skyTop()        { return tft.color565(140, 200, 235); }
static uint16_t skyBottom()     { return tft.color565(215, 236, 246); }
static uint16_t sunColor()      { return tft.color565(255, 221, 130); }
static uint16_t sunGlow()       { return tft.color565(255, 236, 185); }
static uint16_t cloudColor()    { return tft.color565(255, 255, 255); }
static uint16_t grassColor()    { return tft.color565(130, 205, 120); }
static uint16_t grassDark()     { return tft.color565(95, 165, 95); }
static uint16_t groundShadow()  { return tft.color565(55, 115, 60); }
static uint16_t heartColor()    { return tft.color565(255, 110, 130); }
static uint16_t listenRing()    { return tft.color565(120, 230, 235); }

// ---- background scene ---------------------------------------------------

static void drawCloud(float x, int16_t y) {
    uint16_t c = cloudColor();
    spr.fillEllipse((int16_t)x, y, 22, 10, c);
    spr.fillEllipse((int16_t)x + 15, y - 5, 16, 10, c);
    spr.fillEllipse((int16_t)x - 15, y - 3, 14, 9, c);
}

static void drawSkyScene(uint32_t now) {
    spr.fillRectVGradient(0, 0, SCREEN_W, GROUND_Y, skyTop(), skyBottom());

    // top-left, clear of the WiFi badge (top-right) and the head/spikes (centre)
    spr.fillSmoothCircle(40, 26, 20, sunGlow(), skyTop());
    spr.fillSmoothCircle(40, 26, 13, sunColor(), sunGlow());

    // three clouds drifting right-to-left at different speeds/heights, spread
    // down the taller portrait sky for a little parallax.
    const float span = SCREEN_W + 70.0f;
    drawCloud(fmodf(now * 0.018f + 10.0f, span) - 70.0f, 24);
    drawCloud(fmodf(now * 0.011f + 160.0f, span) - 70.0f, 66);
    drawCloud(fmodf(now * 0.025f + 260.0f, span) - 70.0f, 108);

    spr.fillRect(0, GROUND_Y, SCREEN_W, FACE_AREA_H - GROUND_Y, grassDark());
    for (int i = -1; i < 6; i++) {
        spr.fillCircle(i * 48 + 20, GROUND_Y, 16, grassColor());
    }
}

// ---- eyes / mouths (parameterised by centre-y so the idle bob can move them) ----

// Big round "cute" eyes: white sclera, black pupil, small glint. ry < rx gives a
// sleepy/squinting look, pupilOffsetY nudges the pupil for gaze. When closed (mid-blink,
// or forced by a caller) draws a simple curved lash instead.
static void drawCuteEye(int16_t ex, int16_t ey, int16_t rx, int16_t ry, int16_t pupilOffsetY, bool closed) {
    if (closed) {
        spr.drawLine(ex - rx, ey, ex, ey - 4, dinoGreenDark());
        spr.drawLine(ex, ey - 4, ex + rx, ey, dinoGreenDark());
        return;
    }
    spr.fillEllipse(ex, ey, rx, ry, TFT_WHITE);
    int16_t pupilR = (rx < ry ? rx : ry) - 5;
    if (pupilR < 3) pupilR = 3;
    spr.fillCircle(ex, ey + pupilOffsetY, pupilR, TFT_BLACK);
    spr.fillCircle(ex - pupilR / 3, ey + pupilOffsetY - pupilR / 3, 2, TFT_WHITE);
}

static void drawCuteEyes(int16_t eyeCy, int16_t rx, int16_t ry, int16_t pupilOffsetY = 0, bool closed = false) {
    drawCuteEye(CX - EYE_DX, eyeCy, rx, ry, pupilOffsetY, closed);
    drawCuteEye(CX + EYE_DX, eyeCy, rx, ry, pupilOffsetY, closed);
}

static void drawOneCrossEye(int16_t eyeCy, int16_t dx, uint32_t color) {
    spr.drawLine(CX + dx - 12, eyeCy - 12, CX + dx + 12, eyeCy + 12, color);
    spr.drawLine(CX + dx - 12, eyeCy + 12, CX + dx + 12, eyeCy - 12, color);
    spr.drawLine(CX + dx - 11, eyeCy - 12, CX + dx + 11, eyeCy + 12, color);
    spr.drawLine(CX + dx - 11, eyeCy + 12, CX + dx + 11, eyeCy - 12, color);
}

static void drawEyesCross(int16_t eyeCy, uint32_t color) {
    drawOneCrossEye(eyeCy, -EYE_DX, color);
    drawOneCrossEye(eyeCy, EYE_DX, color);
}

// Draws a "smile" (bottom half-circle) or "frown" (top half-circle) by filling a
// circle and then painting over the unwanted half -- avoids relying on any
// particular arc-angle convention.
static void drawHalfCircleMouth(int16_t mouthCy, bool smile, uint32_t color) {
    const int16_t r = 17;
    if (smile) {
        spr.fillCircle(CX, mouthCy - r / 2 + 7, r, color);
        spr.fillRect(CX - r - 2, mouthCy - r / 2 + 7 - r - 2, 2 * r + 4, r + 2, dinoBelly());
    } else {
        spr.fillCircle(CX, mouthCy + r / 2 + 7, r, color);
        spr.fillRect(CX - r - 2, mouthCy + r / 2 + 7, 2 * r + 4, r + 2, dinoBelly());
    }
}

static void drawFlatMouth(int16_t mouthCy, uint32_t color) {
    spr.fillRoundRect(CX - 20, mouthCy + 3, 40, 6, 3, color);
}

static void drawZigzagMouth(int16_t mouthCy, uint32_t color) {
    int16_t y = mouthCy + 7, x = CX - 25, step = 10;
    for (int i = 0; i < 5; i++) {
        int16_t y0 = y + ((i % 2 == 0) ? -5 : 5);
        int16_t y1 = y + (((i + 1) % 2 == 0) ? -5 : 5);
        spr.drawLine(x + i * step, y0, x + (i + 1) * step, y1, color);
    }
}

static void drawThinkingDots(int16_t mouthCy, uint32_t color) {
    uint32_t phase = (millis() / 300) % 3;
    for (int i = 0; i < 3; i++) {
        uint32_t c = (i == phase) ? color : TFT_DARKGREY;
        spr.fillCircle(CX - 15 + i * 15, mouthCy + 28, 4, c);
    }
}

// ---- the dino itself ------------------------------------------------------

// Head/torso/legs/tail/arms/shadow are the same for every mood -- only the
// eyes and mouth (drawn by the caller) change to express the emotion.
static void drawDinoBody(int16_t headCy, int16_t mouthCy) {
    int16_t torsoCy = headCy + TORSO_DY;

    // ---- ground shadow + a lone grass blade, drawn first so the body
    // overlaps them cleanly.
    spr.fillEllipse(CX, GROUND_Y + 10, 52, 8, groundShadow());
    spr.fillTriangle(18, GROUND_Y + 4, 40, GROUND_Y + 4, 24, GROUND_Y - 16, dinoGreenDark());

    // ---- tail: two tapering segments sweeping from the torso's back out to
    // the side and down -- anchored to the (static) ground/CX like the feet,
    // so it doesn't bob with the head/torso.
    int16_t tailBaseX = CX + TORSO_RX - 10, tailBaseY = GROUND_Y - 46;
    spr.fillTriangle(tailBaseX, tailBaseY, tailBaseX + 40, tailBaseY - 34, tailBaseX + 18, tailBaseY + 18, dinoGreenDark());
    spr.fillTriangle(tailBaseX + 18, tailBaseY + 18, tailBaseX + 40, tailBaseY - 34, tailBaseX + 60, GROUND_Y - 6, dinoGreenDark());
    spr.fillTriangle(tailBaseX, tailBaseY + 4, tailBaseX + 30, tailBaseY - 18, tailBaseX + 14, tailBaseY + 22, dinoGreen());

    // ---- legs: thick capsules with a hip bulge and small clawed feet -- the
    // top bobs with the body, the bottom always lands exactly on GROUND_Y.
    int16_t legTop = headCy + LEG_TOP_DY;
    int16_t legH = GROUND_Y - legTop + 8;
    int16_t legLX = CX - 34, legRX = CX + 16;
    spr.fillCircle(legLX + 9, legTop, 15, dinoGreen());
    spr.fillCircle(legRX + 9, legTop, 15, dinoGreen());
    spr.fillRoundRect(legLX, legTop, 18, legH, 8, dinoGreen());
    spr.fillRoundRect(legRX, legTop, 18, legH, 8, dinoGreen());
    spr.fillRoundRect(legLX + 2, legTop, 4, legH, 2, dinoGreenDark());
    spr.fillRoundRect(legRX + 2, legTop, 4, legH, 2, dinoGreenDark());
    for (int f = -1; f <= 1; f++) {
        spr.fillTriangle(legLX + 9 + f * 7, GROUND_Y + 6, legLX + 9 + f * 7 - 3, GROUND_Y - 2, legLX + 9 + f * 7 + 3, GROUND_Y - 2, dinoGreenDark());
        spr.fillTriangle(legRX + 9 + f * 7, GROUND_Y + 6, legRX + 9 + f * 7 - 3, GROUND_Y - 2, legRX + 9 + f * 7 + 3, GROUND_Y - 2, dinoGreenDark());
    }

    // ---- torso: rounder body below the head, with a lighter belly patch.
    spr.fillEllipse(CX, torsoCy, TORSO_RX, TORSO_RY, dinoGreen());
    spr.fillSmoothCircle(CX - 16, torsoCy - 18, 14, dinoHighlight(), dinoGreen());
    spr.fillEllipse(CX, torsoCy + 6, TORSO_RX - 20, TORSO_RY - 10, dinoBelly());

    // ---- small T-rex-style arms, attached high on the torso sides.
    int16_t armY = torsoCy - TORSO_RY + 14;
    spr.fillRoundRect(CX - TORSO_RX - 6, armY, 18, 10, 5, dinoGreen());
    spr.fillCircle(CX - TORSO_RX - 12, armY + 12, 9, dinoGreen());
    spr.fillRoundRect(CX + TORSO_RX - 12, armY, 18, 10, 5, dinoGreen());
    spr.fillCircle(CX + TORSO_RX + 12, armY + 12, 9, dinoGreen());

    // ---- shoulder plates, just below the head silhouette so they stay
    // visible once the head is drawn on top.
    uint16_t plate = dinoGreenDark();
    int16_t plateBaseY = headCy + HEAD_R - 6;
    spr.fillTriangle(CX - 40, plateBaseY + 10, CX - 26, plateBaseY + 10, CX - 33, plateBaseY - 8, plate);
    spr.fillTriangle(CX + 26, plateBaseY + 10, CX + 40, plateBaseY + 10, CX + 33, plateBaseY - 8, plate);

    // ---- head, drawn last so it paints a seamless neck join over the torso.
    spr.fillCircle(CX, headCy, HEAD_R, dinoGreen());
    spr.fillSmoothCircle(CX - 16, headCy - 18, 16, dinoHighlight(), dinoGreen()); // soft shine, for volume

    uint16_t spike = dinoGreenDark();
    int16_t baseY = headCy - HEAD_R + 11;
    for (int i = -1; i <= 1; i++) {
        int16_t sx = CX + i * 17;
        spr.fillTriangle(sx - 8, baseY + 4, sx + 8, baseY + 4, sx, baseY - 15, spike);
    }

    // chin/muzzle patch -- narrower and taller than a plain belly ellipse so it
    // reads as a jaw jutting past the round head outline, not just shading
    spr.fillEllipse(CX, mouthCy + 4, 32, 30, dinoBelly());
    spr.fillEllipse(CX - 11, mouthCy - 11, 3, 2, dinoGreenDark());
    spr.fillEllipse(CX + 11, mouthCy - 11, 3, 2, dinoGreenDark());

    spr.fillEllipse(CX - EYE_DX - 5, mouthCy - 18, 6, 3, blushColor());
    spr.fillEllipse(CX + EYE_DX + 5, mouthCy - 18, 6, 3, blushColor());
}

// ---- per-emotion accents --------------------------------------------------

static void drawFloatingHearts(uint32_t now) {
    for (int i = 0; i < 2; i++) {
        uint32_t phase = (now + (uint32_t)i * 900) % 1800;
        float t = phase / 1800.0f;
        int16_t x = CX + (i == 0 ? -(HEAD_R + 16) : (HEAD_R + 16));
        int16_t y = 34 - (int16_t)(t * 24);
        int16_t s = 7;
        uint16_t c = heartColor();
        spr.fillCircle(x - s / 2, y, s / 2, c);
        spr.fillCircle(x + s / 2, y, s / 2, c);
        spr.fillTriangle(x - s, y + s / 3, x + s, y + s / 3, x, y + s, c);
    }
}

// Floating "!" / "?" pop next to the head, with a small bounce.
static void drawPopSymbol(const char *sym, uint16_t color) {
    int16_t bounce = (int16_t)(sinf(millis() * 0.012f) * 3.0f);
    spr.setTextColor(color); // single-arg form == transparent background
    spr.setTextDatum(MC_DATUM);
    spr.drawString(sym, CX + HEAD_R + 18, 22 + bounce, 4);
}

static void drawTear(int16_t eyeCy, uint32_t now) {
    uint32_t t = now % 1400;
    int16_t dy = (int16_t)((t / 1400.0f) * 26.0f);
    int16_t ex = CX + EYE_DX;
    spr.fillCircle(ex, eyeCy + 16 + dy, 3, TFT_CYAN);
    spr.fillTriangle(ex - 3, eyeCy + 16 + dy, ex + 3, eyeCy + 16 + dy, ex, eyeCy + 10 + dy, TFT_CYAN);
}

static void drawListenPulse(int16_t headCy, uint32_t now) {
    float t = (now % 1200) / 1200.0f;
    int16_t r = HEAD_R + 6 + (int16_t)(t * 16.0f);
    spr.drawCircle(CX, headCy, r, listenRing());
}

static void drawSoundWaves(int16_t mouthCy) {
    if ((millis() / 150) % 2 != 0) return;
    uint16_t c = TFT_GREEN;
    spr.drawLine(CX + HEAD_R - 2, mouthCy - 10, CX + HEAD_R + 8, mouthCy - 2, c);
    spr.drawLine(CX + HEAD_R + 2, mouthCy + 4, CX + HEAD_R + 10, mouthCy + 4, c);
    spr.drawLine(CX - HEAD_R + 2, mouthCy - 10, CX - HEAD_R - 8, mouthCy - 2, c);
    spr.drawLine(CX - HEAD_R - 2, mouthCy + 4, CX - HEAD_R - 10, mouthCy + 4, c);
}

// Small HUD chip, top-left: recording dot + elapsed/max progress bar.
static void drawRecordingIndicator() {
    uint16_t panelBg = tft.color565(20, 24, 32);
    int16_t panelW = SCREEN_W - 50;
    spr.fillSmoothRoundRect(8, 6, panelW, 46, 10, panelBg, skyTop());

    bool on = (millis() / 500) % 2 == 0;
    spr.fillCircle(20, 20, 7, on ? TFT_RED : dinoGreenDark());

    float frac = constrain(g_recordSeconds / g_recordMax, 0.0f, 1.0f);
    int16_t barW = SCREEN_W - 90;
    spr.fillSmoothRoundRect(36, 16, barW, 8, 4, tft.color565(50, 55, 65), panelBg);
    if (frac > 0.01f) {
        spr.fillSmoothRoundRect(36, 16, (int16_t)(barW * frac), 8, 4, TFT_YELLOW, panelBg);
    }
}

// Small round badge, top-right, always on screen -- signal-strength bars
// read as "connectivity" without needing exact TFT_eSPI arc-angle math.
static void drawWifiButton() {
    uint16_t panelBg = tft.color565(20, 24, 32);
    spr.fillSmoothCircle(WIFI_BTN_CX, WIFI_BTN_CY, WIFI_BTN_R - 2, panelBg, skyTop());
    spr.drawSmoothCircle(WIFI_BTN_CX, WIFI_BTN_CY, WIFI_BTN_R - 2, tft.color565(90, 150, 170), panelBg);

    uint16_t bar = tft.color565(150, 220, 230);
    int16_t baseY = WIFI_BTN_CY + 8;
    spr.fillRect(WIFI_BTN_CX - 9, baseY - 3, 3, 3, bar);
    spr.fillRect(WIFI_BTN_CX - 3, baseY - 6, 3, 6, bar);
    spr.fillRect(WIFI_BTN_CX + 3, baseY - 9, 3, 9, bar);
}

// ---- idle liveliness -------------------------------------------------------

// Every render, a coin-flip-free schedule: blink for ~120ms, then wait a
// pseudo-random 2.2-4.8s before the next one. millis()-seeded, not truly
// random, but plenty varied enough that it doesn't look mechanical.
static bool isBlinking(uint32_t now) {
    if (g_nextBlinkMs == 0) g_nextBlinkMs = now + 2000;
    if (now < g_nextBlinkMs) return false;
    if (now < g_nextBlinkMs + 120) return true;
    g_nextBlinkMs = now + 2200 + (now % 2600);
    return false;
}

static void renderFace() {
    uint32_t now = millis();
    int16_t bob = (int16_t)lroundf(sinf(now * 0.0016f) * 3.0f); // gentle idle bob, always on
    int16_t headCy = HEAD_CY + bob;
    int16_t eyeCy = headCy + EYE_DY;
    int16_t mouthCy = headCy + MOUTH_DY;
    bool blink = isBlinking(now);

    drawSkyScene(now);
    drawDinoBody(headCy, mouthCy);

    switch (g_state) {
        case FaceState::NEUTRAL:
            drawCuteEyes(eyeCy, 15, 15, 0, blink);
            drawFlatMouth(mouthCy, dinoGreenDark());
            break;
        case FaceState::HAPPY:
            drawCuteEyes(eyeCy, 15, 15, 0, blink);
            drawHalfCircleMouth(mouthCy, true, TFT_GREEN);
            drawFloatingHearts(now);
            break;
        case FaceState::SAD:
            drawCuteEyes(eyeCy, 15, 9, 3);
            drawHalfCircleMouth(mouthCy, false, TFT_BLUE);
            drawTear(eyeCy, now);
            break;
        case FaceState::THINKING:
            drawCuteEyes(eyeCy, 13, 6, 0, blink);
            drawFlatMouth(mouthCy, TFT_YELLOW);
            drawThinkingDots(mouthCy, TFT_YELLOW);
            break;
        case FaceState::SURPRISED:
            drawCuteEyes(eyeCy, 20, 20);
            spr.fillCircle(CX, mouthCy + 10, 10, TFT_MAGENTA);
            drawPopSymbol("!", TFT_MAGENTA);
            break;
        case FaceState::CONFUSED:
            drawCuteEyes(eyeCy, 15, 15, 0, blink);
            drawZigzagMouth(mouthCy, TFT_ORANGE);
            drawPopSymbol("?", TFT_ORANGE);
            break;
        case FaceState::LISTENING:
            drawCuteEyes(eyeCy, 15, 15, 0, blink);
            drawFlatMouth(mouthCy, TFT_CYAN);
            drawListenPulse(headCy, now);
            drawRecordingIndicator();
            break;
        case FaceState::SPEAKING: {
            drawCuteEyes(eyeCy, 15, 15, 0, blink);
            bool open = (millis() / 150) % 2 == 0;
            if (open) spr.fillEllipse(CX, mouthCy + 8, 16, 12, TFT_GREEN);
            else drawFlatMouth(mouthCy, TFT_GREEN);
            drawSoundWaves(mouthCy);
            break;
        }
        case FaceState::ERROR_STATE:
            drawEyesCross(eyeCy, TFT_RED);
            drawHalfCircleMouth(mouthCy, false, TFT_RED);
            break;
    }

    drawWifiButton();
    spr.pushSprite(0, 0);
}

// ---- public API -------------------------------------------------------

// Static bottom-of-screen background (status footer) -- painted once at init
// and again after wifi_setup.cpp hands the panel back from its temporary
// landscape takeover, since that overwrites the whole physical screen.
static void paintStaticChrome() {
    tft.fillRect(0, STATUS_Y, SCREEN_W, SCREEN_H - STATUS_Y, tft.color565(70, 130, 80));
    tft.fillRect(0, STATUS_Y, SCREEN_W, 3, tft.color565(140, 210, 140));
}

void display_init() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    paintStaticChrome();

    spr.setColorDepth(16);
    if (!spr.createSprite(SCREEN_W, FACE_AREA_H)) {
        Serial.println("[display] Failed to allocate face sprite!");
    }
}

void display_force_redraw() {
    paintStaticChrome();
    g_dirty = true;
}

FaceState display_get_face() { return g_state; }

void display_set_face(FaceState state) {
    if (state != g_state) {
        g_state = state;
        g_dirty = true;
    }
}

void display_set_record_progress(float seconds, float maxSeconds) {
    g_recordSeconds = seconds;
    g_recordMax = maxSeconds > 0 ? maxSeconds : 1;
}

void display_show_status(const char *text) {
    strncpy(g_status, text, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';

    uint16_t pillBg = tft.color565(32, 36, 46);
    uint16_t footerGreen = tft.color565(70, 130, 80);
    // Clear back to the same green the footer background is painted in
    // (paintStaticChrome()), not black -- this whole band sits on that green, so
    // clearing to black here punched a flat black gap in the middle of it.
    tft.fillRect(0, STATUS_Y, SCREEN_W, STATUS_H, footerGreen);
    int16_t pillH = 28;
    int16_t pillY = STATUS_Y + (STATUS_H - pillH) / 2;
    int16_t pillX = 14, pillW = SCREEN_W - 28;
    tft.fillSmoothRoundRect(pillX, pillY, pillW, pillH, 10, pillBg, footerGreen);
    tft.drawFastHLine(pillX + 10, pillY + 4, pillW - 20, tft.color565(60, 66, 80));
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_SILVER, pillBg);
    tft.drawString(g_status, CX, pillY + pillH / 2, 2);
}

bool display_head_hit(int16_t x, int16_t y) {
    // Deliberately tests against the static HEAD_CY, not the animated bob offset --
    // the tap target should stay put even while the face gently bobs.
    int32_t dx = x - CX, dy = y - HEAD_CY;
    return dx * dx + dy * dy <= (int32_t)HEAD_R * HEAD_R;
}

bool display_wifi_button_hit(int16_t x, int16_t y) {
    int32_t dx = x - WIFI_BTN_CX, dy = y - WIFI_BTN_CY;
    return dx * dx + dy * dy <= (int32_t)WIFI_BTN_R * WIFI_BTN_R;
}

TFT_eSPI &display_get_tft() { return tft; }

FaceState display_face_from_emotion_tag(const String &tagIn) {
    String tag = tagIn;
    tag.toLowerCase();
    if (tag == "happy") return FaceState::HAPPY;
    if (tag == "sad") return FaceState::SAD;
    if (tag == "thinking") return FaceState::THINKING;
    if (tag == "surprised") return FaceState::SURPRISED;
    if (tag == "confused") return FaceState::CONFUSED;
    return FaceState::NEUTRAL;
}

void display_tick() {
    uint32_t now = millis();
    // Always redraw at ~10fps, not just on state changes -- that's what drives the
    // idle bob/blink/drifting clouds even while the dino is just sitting there.
    if (g_dirty || (now - g_lastAnimMs) >= 100) {
        renderFace();
        g_dirty = false;
        g_lastAnimMs = now;
    }
}
