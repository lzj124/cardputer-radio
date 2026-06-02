#pragma once
// Cardputer Radio - Settings page (WiFi scan + Volume config)

#include <M5Cardputer.h>
#include <SD.h>
#include <WiFi.h>
#include <vector>
#include "config.h"

struct WifiConfig {
    char ssid[65] = {0};
    char pass[65] = {0};
    int  volume   = 12;
};

static bool loadWifiConfig(WifiConfig& cfg) {
    strncpy(cfg.ssid, WIFI_SSID, sizeof(cfg.ssid) - 1);
    strncpy(cfg.pass, WIFI_PASS, sizeof(cfg.pass) - 1);
    if (!SD.exists(WIFI_CFG_FILE)) return false;
    File f = SD.open(WIFI_CFG_FILE, FILE_READ);
    if (!f) return false;
    int idx = 0;
    while (f.available() && idx < (int)sizeof(cfg.ssid) - 1) {
        char c = f.read();
        if (c == '\n' || c == '\r') {
            if (c == '\r' && f.available() && f.peek() == '\n') f.read();
            break;
        }
        cfg.ssid[idx++] = c;
    }
    cfg.ssid[idx] = '\0';
    idx = 0;
    while (f.available() && idx < (int)sizeof(cfg.pass) - 1) {
        char c = f.read();
        if (c == '\n' || c == '\r') break;
        cfg.pass[idx++] = c;
    }
    cfg.pass[idx] = '\0';
    char volBuf[8] = {0};
    idx = 0;
    while (f.available() && idx < 7) {
        char c = f.read();
        if (c == '\n' || c == '\r') break;
        volBuf[idx++] = c;
    }
    volBuf[idx] = '\0';
    if (idx > 0) {
        int v = atoi(volBuf);
        if (v >= VOLUME_MIN && v <= VOLUME_MAX) cfg.volume = v;
    }
    f.close();
    return true;
}

static bool saveWifiConfig(const WifiConfig& cfg) {
    SD.remove(WIFI_CFG_FILE);
    File f = SD.open(WIFI_CFG_FILE, FILE_WRITE);
    if (!f) return false;
    f.printf("%s\n%s\n%d\n", cfg.ssid, cfg.pass, cfg.volume);
    f.close();
    return true;
}

enum class SetupField { WIFI, VOLUME, SAVE, CANCEL };
static constexpr int FIELD_WIFI    = 0;
static constexpr int FIELD_VOLUME  = 1;
static constexpr int FIELD_SAVE    = 2;
static constexpr int FIELD_CANCEL  = 3;
static constexpr int FIELD_COUNT   = 4;

static constexpr int MAX_SCAN_RESULTS = 30;

struct WifiNetwork {
    char ssid[33];
    int  rssi;
    bool encrypted;
};

static WifiNetwork scanResults[MAX_SCAN_RESULTS];
static int scanCount = 0;

// ── Draw settings page ──────────────────────────────────────
static void drawSettingsPage(const WifiConfig& cfg, SetupField field,
                             unsigned long tickMs, bool wifiOk, int wifiRssi) {
    auto& d = M5Cardputer.Display;
    int w = d.width();
    int h = d.height();
    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.drawString("Settings", (w - 8 * 6) / 2, 2);
    int y = 20;
    int fidx = (int)field;

    // WiFi — shows connected status or "Select WiFi"
    {
        int x = 4;
        d.setTextColor(fidx == FIELD_WIFI ? TFT_YELLOW : TFT_CYAN);
        d.drawString("WiFi:", x, y);
        x += 5 * 6 + 4;

        if (fidx == FIELD_WIFI) {
            d.fillRoundRect(x - 2, y - 1, w - x + 2, 14, 3, TFT_NAVY);
        }

        if (wifiOk) {
            // Show connected SSID + signal bars
            d.setTextColor(TFT_GREEN);
            char display[22];
            strncpy(display, cfg.ssid, 20);
            display[20] = '\0';
            int maxC = (w - x - 54) / 6;
            int dlen = strlen(display);
            if (dlen > maxC) { display[maxC-1]='.'; display[maxC]='\0'; }
            d.drawString(display, x, y + 2);

            // Signal bars
            int bars = 0;
            if (wifiRssi > -50) bars = 4;
            else if (wifiRssi > -60) bars = 3;
            else if (wifiRssi > -70) bars = 2;
            else if (wifiRssi > -80) bars = 1;
            int bx = w - 44;
            for (int b = 0; b < 4; b++) {
                d.setTextColor(b < bars ? TFT_CYAN : 0x4208);
                d.drawString("|", bx + b * 5, y + 2);
            }
        } else {
            d.setTextColor(0x8410);
            d.drawString("[Select WiFi]", x, y + 2);
        }
    }
    y += 18;

    // Volume
    {
        int x = 4;
        d.setTextColor(fidx == FIELD_VOLUME ? TFT_YELLOW : TFT_CYAN);
        d.drawString("Vol:", x, y);
        x += 4 * 6 + 4;
        if (fidx == FIELD_VOLUME) d.fillRect(x - 2, y - 1, w - x + 2, 10, TFT_NAVY);
        d.setTextColor(TFT_WHITE);
        char buf[8]; snprintf(buf, sizeof(buf), "%d", cfg.volume);
        d.drawString(buf, x, y);
        int barW = (cfg.volume * (w - x - 30)) / VOLUME_MAX;
        int barX = x + 24;
        d.drawRect(barX, y, w - barX - 4, 8, 0x8410);
        if (barW > 0) {
            uint16_t color = cfg.volume > 16 ? TFT_RED : (cfg.volume > 8 ? TFT_YELLOW : TFT_GREEN);
            d.fillRect(barX + 1, y + 1, barW, 6, color);
        }
        if (fidx == FIELD_VOLUME && (tickMs / 500) % 2 == 0) {
            d.drawFastVLine(x + strlen(buf) * 6, y, 8, TFT_WHITE);
        }
    }
    y += 16;

    // Save
    {
        int x = 4;
        if (fidx == FIELD_SAVE) {
            d.fillRoundRect(x, y - 1, w - 8, 14, 3, TFT_BLUE);
            d.setTextColor(TFT_WHITE);
        } else {
            d.setTextColor(TFT_GREEN);
        }
        d.drawString("[ Save & Exit ]", x + 4, y + 1);
    }
    y += 17;

    // Cancel
    {
        int x = 4;
        if (fidx == FIELD_CANCEL) {
            d.fillRoundRect(x, y - 1, w - 8, 14, 3, TFT_BLUE);
            d.setTextColor(TFT_WHITE);
        } else {
            d.setTextColor(TFT_RED);
        }
        d.drawString("[ Cancel ]", x + 4, y + 1);
    }

    d.setTextColor(0x8410);
    d.drawString("Fn+^v:nav  Tab:next  Enter:sel", 4, h - 8);
}

// ── Draw WiFi scan results ──────────────────────────────────
static int drawWifiScanList(int selected, int scroll) {
    auto& d = M5Cardputer.Display;
    int w = d.width();
    int h = d.height();
    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.drawString("WiFi Networks", (w - 12 * 6) / 2, 2);

    if (scanCount == 0) {
        d.setTextColor(0x8410);
        d.drawString("No networks found", 4, 30);
        d.setTextColor(TFT_WHITE);
        d.drawString("Tab: back", 4, h - 8);
        return 0;
    }

    int itemsPerPage = 7;
    for (int i = 0; i < itemsPerPage; i++) {
        int idx = scroll + i;
        if (idx >= scanCount) break;
        int y = 14 + i * 15;

        bool sel = (idx == selected);
        if (sel) d.fillRoundRect(2, y - 1, w - 4, 13, 3, TFT_BLUE);

        const WifiNetwork& net = scanResults[idx];

        // Lock icon
        d.setTextColor(net.encrypted ? TFT_YELLOW : TFT_GREEN);
        d.drawString(net.encrypted ? "L" : "O", 4, y);

        // Signal bars
        int bars = 0;
        if (net.rssi > -50) bars = 4;
        else if (net.rssi > -60) bars = 3;
        else if (net.rssi > -70) bars = 2;
        else if (net.rssi > -80) bars = 1;
        for (int b = 0; b < 4; b++) {
            d.setTextColor(b < bars ? TFT_CYAN : 0x4208);
            d.drawString("|", 16 + b * 5, y);
        }

        // SSID
        d.setTextColor(sel ? TFT_WHITE : TFT_GREEN);
        int x = 40;
        char name[22];
        strncpy(name, net.ssid, 21);
        name[21] = '\0';
        int maxC = (w - x - 4) / 6;
        if ((int)strlen(name) > maxC) { name[maxC-1]='.'; name[maxC]='\0'; }
        d.drawString(name, x, y);
    }

    if (scanCount > itemsPerPage) {
        int bh = (itemsPerPage * h) / scanCount;
        if (bh < 4) bh = 4;
        int by = 14 + (scroll * (15 * itemsPerPage - bh)) / (scanCount - itemsPerPage);
        d.fillRect(w - 3, 14, 2, 15 * itemsPerPage, 0x4208);
        d.fillRect(w - 3, by, 2, bh, 0x8410);
    }

    d.setTextColor(0x8410);
    d.drawString("Fn+^v:nav  Enter:pick  Tab:back", 4, h - 8);
    return itemsPerPage;
}

// ── Password input dialog ───────────────────────────────────
static bool passwordInputDialog(const char* ssid, char* password, int maxLen) {
    auto& d = M5Cardputer.Display;
    auto& kbd = M5Cardputer.Keyboard;
    int w = d.width();
    int h = d.height();

    char pwd[65] = {0};
    auto lastKs = kbd.keysState();
    std::vector<char> prevWord;
    bool redraw = true;
    unsigned long startMs = millis();

    while (true) {
        M5Cardputer.update();
        kbd.updateKeysState();
        auto& ks = kbd.keysState();
        bool enterNow = ks.enter && !lastKs.enter;
        bool delNow   = ks.del && !lastKs.del;
        bool tabNow   = ks.tab && !lastKs.tab;
        bool newChar  = (ks.word.size() > prevWord.size()) && !ks.fn;

        if (tabNow) return false;

        if (enterNow && strlen(pwd) > 0) {
            strncpy(password, pwd, maxLen - 1);
            password[maxLen - 1] = '\0';
            return true;
        }

        if (delNow) {
            int len = strlen(pwd);
            if (len > 0) pwd[len - 1] = '\0';
            prevWord.clear();
            redraw = true;
        }

        if (newChar && ks.word.size() > 0) {
            char c = ks.word.back();
            int len = strlen(pwd);
            if (len < 63) { pwd[len] = c; pwd[len + 1] = '\0'; }
            redraw = true;
        }

        if (redraw) {
            d.fillScreen(TFT_BLACK);
            d.setTextSize(1);
            d.setTextColor(TFT_YELLOW);
            d.drawString("Password", (w - 8 * 6) / 2, 10);
            d.setTextColor(0x8410);
            char ssidLine[28];
            snprintf(ssidLine, sizeof(ssidLine), "for: %s", ssid);
            d.drawString(ssidLine, 4, 26);

            int fieldY = 45;
            d.fillRect(2, fieldY - 1, w - 8, 18, TFT_NAVY);
            d.setTextColor(TFT_WHITE);
            char display[33];
            int plen = strlen(pwd);
            int maxDots = (w - 12) / 6;
            int showLen = plen < maxDots ? plen : maxDots;
            for (int i = 0; i < showLen; i++) display[i] = '*';
            display[showLen] = '\0';
            d.drawString(display, 6, fieldY + 2);

            unsigned long elapsed = millis() - startMs;
            if ((elapsed / 500) % 2 == 0) {
                d.drawFastVLine(6 + showLen * 6, fieldY + 2, 12, TFT_WHITE);
            }
            d.setTextColor(0x8410);
            d.drawString("Enter:confirm  Tab:cancel", 4, h - 8);
            redraw = false;
        }

        unsigned long elapsed = millis() - startMs;
        if (elapsed > 30 && (elapsed % 250 < 5)) redraw = true;

        lastKs = ks;
        prevWord = ks.word;
        delay(20);
    }
}

// ── Try connecting and show progress ────────────────────────
static bool tryConnect(const char* ssid, const char* pass) {
    auto& d = M5Cardputer.Display;
    int w = d.width();
    int h = d.height();

    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.drawString("Connecting...", 4, 30);

    d.setTextColor(0x8410);
    char line[28];
    snprintf(line, sizeof(line), "SSID: %s", ssid);
    d.drawString(line, 4, 46);

    WiFi.disconnect();
    delay(200);
    WiFi.begin(ssid, pass);

    int attempt = 0;
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(300);
        attempt++;
        char dots[4];
        int n = (attempt % 4);
        for (int i = 0; i < n; i++) dots[i] = '.';
        dots[n] = '\0';
        d.fillRect(4, 62, w, 10, TFT_BLACK);
        d.setTextColor(TFT_CYAN);
        d.drawString(dots, 4, 62);
    }

    if (WiFi.status() == WL_CONNECTED) {
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_GREEN);
        d.drawString("Connected!", 4, 40);
        d.setTextColor(TFT_WHITE);
        d.drawString(WiFi.localIP().toString().c_str(), 4, 56);
        delay(1000);
        return true;
    } else {
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_RED);
        d.drawString("Connect failed!", 4, 40);
        delay(1500);
        return false;
    }
}

// ── Run WiFi scan and select (connects immediately) ─────────
// Returns true if connected successfully, sets cfg.ssid/pass
static bool runWifiScan(WifiConfig& cfg) {
    auto& d = M5Cardputer.Display;
    auto& kbd = M5Cardputer.Keyboard;
    int w = d.width();

    // Scan
    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.drawString("Scanning...", 4, 40);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(200);

    int n = WiFi.scanNetworks();
    scanCount = 0;

    for (int i = 0; i < n && scanCount < MAX_SCAN_RESULTS; i++) {
        const char* ssid = WiFi.SSID(i).c_str();
        if (strlen(ssid) == 0) continue;
        bool dup = false;
        for (int j = 0; j < scanCount; j++) {
            if (strcmp(scanResults[j].ssid, ssid) == 0) { dup = true; break; }
        }
        if (dup) continue;
        strncpy(scanResults[scanCount].ssid, ssid, 32);
        scanResults[scanCount].ssid[32] = '\0';
        scanResults[scanCount].rssi = WiFi.RSSI(i);
        scanResults[scanCount].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        scanCount++;
    }
    WiFi.scanDelete();

    if (scanCount == 0) {
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_RED);
        d.drawString("No networks found!", 4, 40);
        delay(1500);
        return false;
    }

    // Sort by RSSI (strongest first)
    for (int i = 0; i < scanCount - 1; i++)
        for (int j = i + 1; j < scanCount; j++)
            if (scanResults[j].rssi > scanResults[i].rssi) {
                WifiNetwork tmp = scanResults[i];
                scanResults[i] = scanResults[j];
                scanResults[j] = tmp;
            }

    // Browse and select
    int selected = 0, scroll = 0;
    auto lastKs = kbd.keysState();
    size_t lastHid = 0;
    int itemsPerPage = drawWifiScanList(0, 0);

    while (true) {
        M5Cardputer.update();
        kbd.updateKeysState();
        auto& ks = kbd.keysState();
        bool enterNow = ks.enter && !lastKs.enter;
        bool tabNow   = ks.tab && !lastKs.tab;

        if (tabNow) return false;

        bool moved = false;
        if (ks.fn) {
            for (size_t i = lastHid; i < ks.hid_keys.size(); i++) {
                uint8_t hk = ks.hid_keys[i];
                if (hk == 0x33 && selected > 0) {
                    selected--;
                    if (selected < scroll) scroll = selected;
                    moved = true;
                } else if (hk == 0x37 && selected < scanCount - 1) {
                    selected++;
                    if (selected >= scroll + itemsPerPage) scroll = selected - itemsPerPage + 1;
                    moved = true;
                }
            }
            lastHid = ks.hid_keys.size();
        }

        if (moved) itemsPerPage = drawWifiScanList(selected, scroll);

        if (enterNow && scanCount > 0) {
            const WifiNetwork& net = scanResults[selected];
            strncpy(cfg.ssid, net.ssid, sizeof(cfg.ssid) - 1);

            char newPass[65] = {0};
            bool gotPassword = true;

            if (net.encrypted) {
                gotPassword = passwordInputDialog(net.ssid, newPass, sizeof(newPass));
                if (!gotPassword) {
                    itemsPerPage = drawWifiScanList(selected, scroll);
                    continue;  // User cancelled, stay in scan list
                }
            }

            // Connect immediately
            if (tryConnect(net.ssid, gotPassword ? newPass : "")) {
                strncpy(cfg.pass, newPass, sizeof(cfg.pass) - 1);
                return true;
            } else {
                // Failed — brief error, back to scan list
                itemsPerPage = drawWifiScanList(selected, scroll);
            }
        }

        lastKs = ks;
        delay(20);
    }
}

// ── Run settings page (blocking) ────────────────────────────
static bool runSettingsPage(WifiConfig& cfg) {
    auto& d = M5Cardputer.Display;
    auto& kbd = M5Cardputer.Keyboard;
    SetupField field = SetupField::WIFI;
    unsigned long startMs = millis();
    bool redraw = true;
    bool wifiChanged = false;
    auto lastKs = kbd.keysState();
    size_t prevHidSize = 0;

    while (true) {
        M5Cardputer.update();
        kbd.updateKeysState();
        auto& ks = kbd.keysState();

        bool enterNow = ks.enter && !lastKs.enter;
        bool tabNow   = ks.tab && !lastKs.tab;

        if (tabNow) {
            int next = ((int)field + 1) % FIELD_COUNT;
            field = (SetupField)next;
            redraw = true;
        }

        if (enterNow) {
            if (field == SetupField::WIFI) {
                if (runWifiScan(cfg)) {
                    wifiChanged = true;
                }
                redraw = true;
            } else if (field == SetupField::SAVE) {
                saveWifiConfig(cfg);
                break;
            } else if (field == SetupField::CANCEL) {
                break;
            }
        }

        if (ks.fn && ks.hid_keys.size() > prevHidSize) {
            for (size_t i = prevHidSize; i < ks.hid_keys.size(); i++) {
                uint8_t hk = ks.hid_keys[i];
                if (hk == 0x33) {
                    int prev = (int)field - 1;
                    if (prev < 0) prev = FIELD_COUNT - 1;
                    field = (SetupField)prev;
                    redraw = true;
                } else if (hk == 0x37) {
                    int next = ((int)field + 1) % FIELD_COUNT;
                    field = (SetupField)next;
                    redraw = true;
                } else if (hk == 0x36) {
                    if (field == SetupField::VOLUME) {
                        int step = cfg.volume > VOLUME_STEP ? VOLUME_STEP : cfg.volume;
                        cfg.volume = (cfg.volume > step) ? (cfg.volume - step) : VOLUME_MIN;
                        M5Cardputer.Speaker.tone(440, 40);
                        redraw = true;
                    }
                } else if (hk == 0x38) {
                    if (field == SetupField::VOLUME) {
                        int step = cfg.volume < VOLUME_MAX - VOLUME_STEP ? VOLUME_STEP : (VOLUME_MAX - cfg.volume);
                        cfg.volume = cfg.volume + step;
                        M5Cardputer.Speaker.tone(880, 40);
                        redraw = true;
                    }
                }
            }
        }

        lastKs = ks;
        prevHidSize = ks.hid_keys.size();

        if (redraw) {
            bool wifiOk = (WiFi.status() == WL_CONNECTED);
            drawSettingsPage(cfg, field, millis() - startMs, wifiOk, WiFi.RSSI());
            redraw = false;
        }

        unsigned long elapsed = millis() - startMs;
        if (elapsed > 30 && (elapsed % 250 < 5)) {
            bool wifiOk = (WiFi.status() == WL_CONNECTED);
            drawSettingsPage(cfg, field, elapsed, wifiOk, WiFi.RSSI());
        }
        delay(20);
    }

    d.fillScreen(TFT_BLACK);
    delay(50);
    return wifiChanged;
}
