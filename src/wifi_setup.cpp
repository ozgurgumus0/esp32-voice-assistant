#include "wifi_setup.h"
// WiFi/WiFiManager (which drags in WebServer.h -> FS.h) must be included
// before display.h pulls in TFT_eSPI.h -- TFT_eSPI's own transitive FS.h
// include otherwise leaves the unqualified `FS` symbol WebServer.h expects
// unresolved, breaking the build.
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
#include "touch.h"
#include "config.h"
#include "display.h"

// Reuses display.cpp's single TFT_eSPI instance rather than constructing a
// second one -- two TFT_eSPI objects driving the same physical SPI display
// caused exactly the kind of lockup/unresponsive screen this module used to
// produce (a second instance doesn't share the first's transaction/DMA
// state, even though both talk to the same real hardware). g_tft is bound
// lazily in wifi_setup_run() so there's no static-initialization-order
// dependency on display.cpp's own tft being constructed first.
static TFT_eSPI *g_tft = nullptr;
#define tft (*g_tft)

static const int16_t SCREEN_W = 320;
static const int16_t SCREEN_H = 240;

static uint16_t bgColor()    { return tft.color565(20, 28, 38); }
static uint16_t panelColor() { return tft.color565(38, 50, 64); }
static uint16_t textColor()  { return TFT_WHITE; }
static uint16_t dimText()    { return tft.color565(150, 160, 170); }
static uint16_t accent()     { return tft.color565(120, 220, 230); }
static uint16_t warnColor()  { return tft.color565(255, 150, 110); }

struct Rect {
    int16_t x, y, w, h;
    bool hit(int16_t px, int16_t py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// ---- network scan + list -----------------------------------------------

struct WifiNet { String ssid; int32_t rssi; bool open; };
static WifiNet g_nets[24];
static int g_netCount = 0;
static int g_page = 0;

// Rows/buttons sized generously -- a first pass at these was too small to
// tap reliably, so height/width here matters more than visual density.
static const int16_t LIST_TOP = 34;
static const int16_t ROW_H = 42;
static const int ROWS_PER_PAGE = 4;

static const Rect BTN_RESCAN = {8, 206, 96, 30};
static const Rect BTN_MORE   = {112, 206, 96, 30};
static const Rect BTN_PHONE  = {216, 206, 96, 30};
static const Rect BTN_BACK   = {280, 4, 34, 34};

static Rect rowRect(int rowOnPage) {
    return { 8, (int16_t)(LIST_TOP + rowOnPage * ROW_H), (int16_t)(SCREEN_W - 16), (int16_t)(ROW_H - 6) };
}

static void drawSignalBars(int16_t cx, int16_t cy, int32_t rssi) {
    int bars = rssi > -55 ? 3 : (rssi > -75 ? 2 : 1);
    uint16_t on = accent(), off = tft.color565(70, 80, 90);
    for (int i = 0; i < 3; i++) {
        int16_t h = 5 + i * 4;
        tft.fillRect(cx + i * 6, cy + 9 - h, 4, h, i < bars ? on : off);
    }
}

static void drawScanningScreen() {
    tft.fillScreen(bgColor());
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(textColor(), bgColor());
    tft.drawString("Scanning for WiFi...", SCREEN_W / 2, SCREEN_H / 2, 4);
}

// Synchronous scan (blocks a couple of seconds) -- dedups repeated SSIDs
// (multiple APs/bands of the same network) keeping the strongest signal,
// then sorts strongest-first.
static void scanNetworks() {
    drawScanningScreen();
    WiFi.mode(WIFI_STA);
    // A still-settling connection attempt from wifi_setup_try_stored() can
    // make scanNetworks() fail outright (WIFI_SCAN_FAILED) rather than just
    // return zero results -- force a clean STA state first, and retry a
    // couple of times since the failure can be transient (driver not fully
    // back up yet right after a disconnect).
    WiFi.disconnect();
    delay(200);

    int n = WiFi.scanNetworks();
    for (int attempt = 0; n == WIFI_SCAN_FAILED && attempt < 3; attempt++) {
        Serial.printf("[wifi-setup] scan failed, retrying (%d/3)...\n", attempt + 1);
        delay(500);
        n = WiFi.scanNetworks();
    }
    Serial.printf("[wifi-setup] WiFi.scanNetworks() returned %d\n", n);

    int found = n > 0 ? n : 0;
    g_netCount = 0;
    for (int i = 0; i < found && g_netCount < 24; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        int existing = -1;
        for (int j = 0; j < g_netCount; j++) {
            if (g_nets[j].ssid == ssid) { existing = j; break; }
        }
        int32_t rssi = WiFi.RSSI(i);
        bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        if (existing >= 0) {
            if (rssi > g_nets[existing].rssi) g_nets[existing].rssi = rssi;
        } else {
            g_nets[g_netCount++] = { ssid, rssi, open };
        }
    }
    for (int i = 1; i < g_netCount; i++) {
        WifiNet key = g_nets[i];
        int j = i - 1;
        while (j >= 0 && g_nets[j].rssi < key.rssi) {
            g_nets[j + 1] = g_nets[j];
            j--;
        }
        g_nets[j + 1] = key;
    }
    WiFi.scanDelete();
    g_page = 0;
}

static void drawFooterButtons() {
    tft.fillRoundRect(BTN_RESCAN.x, BTN_RESCAN.y, BTN_RESCAN.w, BTN_RESCAN.h, 6, panelColor());
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(textColor(), panelColor());
    tft.drawString("Rescan", BTN_RESCAN.x + BTN_RESCAN.w / 2, BTN_RESCAN.y + BTN_RESCAN.h / 2, 2);

    int pages = (g_netCount + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
    if (pages > 1) {
        tft.fillRoundRect(BTN_MORE.x, BTN_MORE.y, BTN_MORE.w, BTN_MORE.h, 6, panelColor());
        tft.setTextColor(textColor(), panelColor());
        String label = "More " + String(g_page + 1) + "/" + String(pages);
        tft.drawString(label, BTN_MORE.x + BTN_MORE.w / 2, BTN_MORE.y + BTN_MORE.h / 2, 2);
    }

    uint16_t phoneBg = tft.color565(60, 55, 95);
    tft.fillRoundRect(BTN_PHONE.x, BTN_PHONE.y, BTN_PHONE.w, BTN_PHONE.h, 6, phoneBg);
    tft.setTextColor(textColor(), phoneBg);
    tft.drawString("Use phone", BTN_PHONE.x + BTN_PHONE.w / 2, BTN_PHONE.y + BTN_PHONE.h / 2, 2);
}

static void drawNetworkList() {
    tft.fillScreen(bgColor());
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(textColor(), bgColor());
    tft.drawString("Pick a WiFi network", 12, 8, 4);

    // Only useful if we were already online before this screen opened (e.g.
    // an accidental tap on the WiFi button) -- wifi_setup_run() tries a
    // quick reconnect on the way out so backing out doesn't stay offline.
    uint16_t backBg = tft.color565(60, 66, 78);
    tft.fillRoundRect(BTN_BACK.x, BTN_BACK.y, BTN_BACK.w, BTN_BACK.h, 6, backBg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(textColor(), backBg);
    tft.drawString("X", BTN_BACK.x + BTN_BACK.w / 2, BTN_BACK.y + BTN_BACK.h / 2, 2);

    if (g_netCount == 0) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(dimText(), bgColor());
        tft.drawString("No networks found", SCREEN_W / 2, 120, 2);
    } else {
        int start = g_page * ROWS_PER_PAGE;
        int end = min(start + ROWS_PER_PAGE, g_netCount);
        for (int i = start; i < end; i++) {
            Rect r = rowRect(i - start);
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, panelColor());
            tft.setTextDatum(ML_DATUM);
            tft.setTextColor(textColor(), panelColor());
            String label = g_nets[i].ssid;
            if (!g_nets[i].open) label += "  [locked]";
            tft.drawString(label, r.x + 12, r.y + r.h / 2, 2);
            drawSignalBars(r.x + r.w - 32, r.y + r.h / 2, g_nets[i].rssi);
        }
    }

    drawFooterButtons();
}

// ---- connecting ------------------------------------------------------

// Full-screen takeover, standalone (no assumptions about the previous
// screen's layout) -- called both directly from the list (open networks)
// and after the on-screen keyboard (secured ones).
static bool attemptConnect(const String &ssid, const char *password) {
    tft.fillScreen(bgColor());
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(textColor(), bgColor());
    tft.drawString("Connecting to", SCREEN_W / 2, 90, 2);
    tft.drawString(ssid, SCREEN_W / 2, 118, 4);

    Serial.printf("[wifi-setup] connecting to \"%s\" (pw len %d)\n", ssid.c_str(), (int)strlen(password));
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(200);
    }

    bool ok = WiFi.status() == WL_CONNECTED;
    Serial.printf("[wifi-setup] connect result: %s (WiFi.status()=%d), stored SSID now reads \"%s\"\n",
                  ok ? "OK" : "FAILED", (int)WiFi.status(), WiFi.SSID().c_str());
    tft.fillRect(0, 150, SCREEN_W, 40, bgColor());
    tft.setTextColor(ok ? accent() : warnColor(), bgColor());
    tft.drawString(ok ? "Connected!" : "Couldn't connect,", SCREEN_W / 2, 160, 2);
    if (!ok) tft.drawString("check the password", SCREEN_W / 2, 182, 2);
    delay(1400);
    return ok;
}

// ---- on-screen keyboard ------------------------------------------------

enum class KbMode { LOWER, UPPER, SYMBOLS };
static KbMode g_kbMode = KbMode::LOWER;
static char g_pwBuf[64] = "";
static int g_pwLen = 0;

static const char ROW0_LETTERS[10] = {'q','w','e','r','t','y','u','i','o','p'};
static const char ROW0_SYMBOLS[10] = {'1','2','3','4','5','6','7','8','9','0'};
static const char ROW1_LETTERS[9]  = {'a','s','d','f','g','h','j','k','l'};
static const char ROW1_SYMBOLS[9]  = {'-','_','@','#','$','%','&','*','('};
static const char ROW2_LETTERS[7]  = {'z','x','c','v','b','n','m'};
static const char ROW2_SYMBOLS[7]  = {')','+','=','/',':',';','?'};

// Sized to fill the screen's spare vertical room -- the first pass packed
// everything into a small strip up top and left ~80px unused at the bottom,
// which made every key too small to tap reliably.
static const int16_t CELL_W = 31, CELL_H = 42;
static const int16_t GRID_X0 = 5;
static const int16_t ROW0_Y = 42, ROW1_Y = 88, ROW2_Y = 134, ROW3_Y = 180;

static const Rect BTN_123     = {5, ROW3_Y, 60, 46};
static const Rect BTN_SPACE   = {69, ROW3_Y, 118, 46};
static const Rect BTN_CANCEL  = {191, ROW3_Y, 60, 46};
static const Rect BTN_CONNECT = {255, ROW3_Y, 60, 46};

static Rect keyRect(int row, int col) {
    int16_t y = row == 0 ? ROW0_Y : row == 1 ? ROW1_Y : ROW2_Y;
    return { (int16_t)(GRID_X0 + col * CELL_W), y, (int16_t)(CELL_W - 2), CELL_H };
}

// Returns 0 for cells with no plain character (backspace/shift), which the
// caller special-cases before ever getting here.
static char charForCell(int row, int col) {
    bool symbols = (g_kbMode == KbMode::SYMBOLS);
    bool upper = (g_kbMode == KbMode::UPPER);
    char c = 0;
    if (row == 0 && col >= 0 && col < 10) {
        c = symbols ? ROW0_SYMBOLS[col] : ROW0_LETTERS[col];
    } else if (row == 1 && col >= 0 && col < 9) {
        c = symbols ? ROW1_SYMBOLS[col] : ROW1_LETTERS[col];
    } else if (row == 2 && col >= 1 && col < 8) {
        c = symbols ? ROW2_SYMBOLS[col - 1] : ROW2_LETTERS[col - 1];
    } else if (row == 2 && col == 8) {
        c = symbols ? '!' : ',';
    }
    if (!symbols && upper && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    return c;
}

static void drawPasswordField() {
    tft.fillRect(0, 20, SCREEN_W, 20, bgColor());
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(accent(), bgColor());
    String masked;
    int shown = min(g_pwLen, 28);
    for (int i = 0; i < shown; i++) masked += '*';
    masked += " (" + String(g_pwLen) + ")";
    tft.drawString(masked, 12, 22, 2);
}

static void drawKeyCell(int row, int col) {
    Rect r = keyRect(row, col);
    uint16_t bg = panelColor();
    String label;

    if (row == 1 && col == 9) {
        label = "<-";
        bg = tft.color565(75, 48, 48);
    } else if (row == 2 && col == 0) {
        label = (g_kbMode == KbMode::UPPER) ? "^^" : "^";
        if (g_kbMode == KbMode::UPPER) bg = accent();
    } else {
        char c = charForCell(row, col);
        if (c) label = String(c);
    }

    tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor((row == 2 && col == 0 && g_kbMode == KbMode::UPPER) ? TFT_BLACK : textColor(), bg);
    tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 2);
}

static void drawKeyboardScreen(const String &ssid) {
    tft.fillScreen(bgColor());
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(dimText(), bgColor());
    tft.drawString(ssid, 12, 4, 2);

    drawPasswordField();

    for (int col = 0; col < 10; col++) drawKeyCell(0, col);
    for (int col = 0; col < 10; col++) drawKeyCell(1, col);
    for (int col = 0; col < 9; col++) drawKeyCell(2, col);

    uint16_t cancelBg = tft.color565(95, 55, 55);
    uint16_t connectBg = tft.color565(55, 95, 65);
    tft.fillRoundRect(BTN_123.x, BTN_123.y, BTN_123.w, BTN_123.h, 6, panelColor());
    tft.fillRoundRect(BTN_SPACE.x, BTN_SPACE.y, BTN_SPACE.w, BTN_SPACE.h, 6, panelColor());
    tft.fillRoundRect(BTN_CANCEL.x, BTN_CANCEL.y, BTN_CANCEL.w, BTN_CANCEL.h, 6, cancelBg);
    tft.fillRoundRect(BTN_CONNECT.x, BTN_CONNECT.y, BTN_CONNECT.w, BTN_CONNECT.h, 6, connectBg);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(textColor(), panelColor());
    tft.drawString(g_kbMode == KbMode::SYMBOLS ? "ABC" : "123", BTN_123.x + BTN_123.w / 2, BTN_123.y + BTN_123.h / 2, 2);
    tft.drawString("space", BTN_SPACE.x + BTN_SPACE.w / 2, BTN_SPACE.y + BTN_SPACE.h / 2, 2);
    tft.setTextColor(textColor(), cancelBg);
    tft.drawString("Cancel", BTN_CANCEL.x + BTN_CANCEL.w / 2, BTN_CANCEL.y + BTN_CANCEL.h / 2, 2);
    tft.setTextColor(textColor(), connectBg);
    tft.drawString("Connect", BTN_CONNECT.x + BTN_CONNECT.w / 2, BTN_CONNECT.y + BTN_CONNECT.h / 2, 2);
}

static bool runKeyboardScreen(const String &ssid) {
    g_pwLen = 0;
    g_pwBuf[0] = '\0';
    g_kbMode = KbMode::LOWER;
    drawKeyboardScreen(ssid);

    for (;;) {
        touch_update();
        int16_t x, y;
        if (touch_get_tap(x, y)) {
            Serial.printf("[wifi-setup] tap at (%d,%d)\n", x, y);
            if (BTN_CANCEL.hit(x, y)) return false;

            if (BTN_CONNECT.hit(x, y)) {
                return attemptConnect(ssid, g_pwBuf);
            }
            if (BTN_123.hit(x, y)) {
                g_kbMode = (g_kbMode == KbMode::SYMBOLS) ? KbMode::LOWER : KbMode::SYMBOLS;
                drawKeyboardScreen(ssid);
                continue;
            }
            if (BTN_SPACE.hit(x, y)) {
                if (g_pwLen < (int)sizeof(g_pwBuf) - 1) { g_pwBuf[g_pwLen++] = ' '; g_pwBuf[g_pwLen] = '\0'; }
                drawPasswordField();
                continue;
            }
            if (keyRect(1, 9).hit(x, y)) {
                if (g_pwLen > 0) g_pwBuf[--g_pwLen] = '\0';
                drawPasswordField();
                continue;
            }
            if (keyRect(2, 0).hit(x, y)) {
                g_kbMode = (g_kbMode == KbMode::UPPER) ? KbMode::LOWER
                         : (g_kbMode == KbMode::LOWER ? KbMode::UPPER : g_kbMode);
                drawKeyboardScreen(ssid);
                continue;
            }

            bool handled = false;
            for (int row = 0; row < 3 && !handled; row++) {
                int startCol = (row == 2) ? 1 : 0;
                int endCol = (row == 0) ? 10 : 9;
                for (int col = startCol; col < endCol; col++) {
                    if (keyRect(row, col).hit(x, y)) {
                        char c = charForCell(row, col);
                        if (c && g_pwLen < (int)sizeof(g_pwBuf) - 1) {
                            g_pwBuf[g_pwLen++] = c;
                            g_pwBuf[g_pwLen] = '\0';
                            drawPasswordField();
                        }
                        handled = true;
                        break;
                    }
                }
            }
        }
        delay(10);
    }
}

// ---- phone / captive-portal fallback -----------------------------------

static bool runPhonePortal() {
    tft.fillScreen(bgColor());
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(textColor(), bgColor());
    tft.drawString("Join WiFi \"Dino Setup\"...", SCREEN_W / 2, 100, 2);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setAPCallback([](WiFiManager *) {
        tft.fillRect(0, 118, SCREEN_W, 30, bgColor());
        tft.setTextColor(accent(), bgColor());
        tft.drawString("Join \"Dino Setup\" -> 192.168.4.1", SCREEN_W / 2, 133, 2);
    });

    bool ok = wm.startConfigPortal(WIFI_SETUP_AP_NAME);

    tft.fillRect(0, 118, SCREEN_W, 30, bgColor());
    tft.setTextColor(ok ? accent() : warnColor(), bgColor());
    tft.drawString(ok ? "Connected!" : "Setup timed out", SCREEN_W / 2, 133, 2);
    delay(1200);
    return ok;
}

// ---- public API ---------------------------------------------------------

bool wifi_setup_try_stored(uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);

    // WiFi.SSID() only reports the network of an ACTIVE connection (it reads
    // esp_wifi_sta_get_ap_info(), which fails whenever STA isn't currently
    // connected) -- right after boot, before anything has connected yet,
    // that's always empty even when a network IS stored in flash. That made
    // this check treat every boot as "nothing stored" and fall straight
    // through to the on-screen picker, so the board never actually reused a
    // saved network. Read the persisted STA config directly instead, which
    // reflects what's in flash regardless of current connection state.
    wifi_config_t storedConf;
    if (esp_wifi_get_config(WIFI_IF_STA, &storedConf) != ESP_OK || storedConf.sta.ssid[0] == 0) {
        Serial.println("[wifi-setup] no stored network on this board yet");
        return false;
    }

    WiFi.begin();
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

// The main app runs portrait; this whole flow was written for the original
// landscape screen and is left exactly as-is (per user's call -- not worth
// redesigning for portrait) -- wifi_setup_run() below just borrows landscape
// rotation for the duration of this call, then hands the panel back.
static bool wifiSetupRunLandscape() {
    Serial.println("[wifi-setup] opening network picker");
    WiFi.mode(WIFI_STA);
    scanNetworks();

    for (;;) {
        drawNetworkList();

        bool acted = false;
        while (!acted) {
            touch_update();
            int16_t x, y;
            if (touch_get_tap(x, y)) {
                Serial.printf("[wifi-setup] tap at (%d,%d)\n", x, y);
                if (BTN_BACK.hit(x, y)) {
                    wifi_setup_try_stored(6000); // best-effort: don't strand an already-online board
                    return WiFi.status() == WL_CONNECTED;
                }

                if (BTN_RESCAN.hit(x, y)) { scanNetworks(); acted = true; break; }

                int pages = (g_netCount + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
                if (pages > 1 && BTN_MORE.hit(x, y)) {
                    g_page = (g_page + 1) % pages;
                    acted = true;
                    break;
                }

                if (BTN_PHONE.hit(x, y)) {
                    return runPhonePortal();
                }

                int start = g_page * ROWS_PER_PAGE;
                int end = min(start + ROWS_PER_PAGE, g_netCount);
                for (int i = start; i < end; i++) {
                    if (rowRect(i - start).hit(x, y)) {
                        WifiNet net = g_nets[i];
                        if (net.open) {
                            if (attemptConnect(net.ssid, "")) return true;
                        } else {
                            if (runKeyboardScreen(net.ssid)) return true;
                        }
                        acted = true;
                        break;
                    }
                }
            }
            delay(10);
        }
    }
}

bool wifi_setup_run() {
    g_tft = &display_get_tft();
    tft.setRotation(1);
    touch_set_orientation(TouchOrientation::LANDSCAPE);

    bool ok = wifiSetupRunLandscape();

    tft.setRotation(0);
    touch_set_orientation(TouchOrientation::PORTRAIT);
    display_force_redraw();
    return ok;
}
