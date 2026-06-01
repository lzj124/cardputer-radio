#pragma once
// Cardputer Radio - Settings page (WiFi + Volume config)

#include <M5Cardputer.h>
#include <SD.h>
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
    Serial.printf("[CFG] Loaded: SSID=\"%s\" vol=%d\n", cfg.ssid, cfg.volume);
    return true;
}

static bool saveWifiConfig(const WifiConfig& cfg) {
    SD.remove(WIFI_CFG_FILE);
    File f = SD.open(WIFI_CFG_FILE, FILE_WRITE);
    if (!f) return false;
    f.printf("%s\n%s\n%d\n", cfg.ssid, cfg.pass, cfg.volume);
    f.close();
    Serial.printf("[CFG] Saved: SSID=\"%s\" vol=%d\n", cfg.ssid, cfg.volume);
    return true;
}

enum class SetupField { SSID, PASSWORD, VOLUME, SAVE, CANCEL };
static constexpr int FIELD_SSID     = 0;
static constexpr int FIELD_PASSWORD = 1;
static constexpr int FIELD_VOLUME   = 2;
static constexpr int FIELD_SAVE     = 3;
static constexpr int FIELD_CANCEL   = 4;
static constexpr int FIELD_COUNT    = 5;

static void drawSettingsPage(const WifiConfig& cfg, SetupField field, unsigned long tickMs) {
    auto& d = M5Cardputer.Display;
    int w = d.width();
    int h = d.height();
    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.drawString("Settings", (w - 8 * 6) / 2, 2);
    int y = 16;
    int fidx = (int)field;

    // SSID
    {
        int x = 4;
        d.setTextColor(fidx == FIELD_SSID ? TFT_YELLOW : TFT_CYAN);
        d.drawString("WiFi:", x, y);
        x += 5 * 6 + 4;
        if (fidx == FIELD_SSID) d.fillRect(x - 2, y - 1, w - x + 2, 10, TFT_NAVY);
        d.setTextColor(TFT_WHITE);
        char display[65];
        strncpy(display, cfg.ssid, sizeof(display) - 1);
        int maxChars = (w - x) / 6;
        int dlen = strlen(display);
        if (dlen > maxChars) { display[maxChars-2]='.'; display[maxChars-1]='.'; display[maxChars]='\0'; }
        d.drawString(display, x, y);
        if (fidx == FIELD_SSID && (tickMs / 500) % 2 == 0) {
            d.drawFastVLine(x + strlen(display) * 6, y, 8, TFT_WHITE);
        }
    }
    y += 11;

    // Password
    {
        int x = 4;
        d.setTextColor(fidx == FIELD_PASSWORD ? TFT_YELLOW : TFT_CYAN);
        d.drawString("Pass:", x, y);
        x += 5 * 6 + 4;
        if (fidx == FIELD_PASSWORD) d.fillRect(x - 2, y - 1, w - x + 2, 10, TFT_NAVY);
        d.setTextColor(TFT_WHITE);
        char display[65];
        int plen = strlen(cfg.pass);
        for (int i = 0; i < plen && i < 64; i++) display[i] = '*';
        display[plen > 64 ? 64 : plen] = '\0';
        d.drawString(display, x, y);
        if (fidx == FIELD_PASSWORD && (tickMs / 500) % 2 == 0) {
            d.drawFastVLine(x + plen * 6, y, 8, TFT_WHITE);
        }
    }
    y += 11;

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
    y += 12;

    // Save
    {
        int x = 4;
        if (fidx == FIELD_SAVE) {
            d.fillRoundRect(x, y - 1, w - 8, 12, 3, TFT_BLUE);
            d.setTextColor(TFT_WHITE);
        } else {
            d.setTextColor(TFT_GREEN);
        }
        d.drawString("[ Save & Exit ]", x + 4, y);
    }
    y += 13;

    // Cancel
    {
        int x = 4;
        if (fidx == FIELD_CANCEL) {
            d.fillRoundRect(x, y - 1, w - 8, 12, 3, TFT_BLUE);
            d.setTextColor(TFT_WHITE);
        } else {
            d.setTextColor(TFT_RED);
        }
        d.drawString("[ Cancel ]", x + 4, y);
    }

    d.setTextColor(0x8410);
    d.drawString("Fn+^<>v:nav  Tab:next", 4, h - 12);
    d.drawString("Enter:sel  Del:dec", 4, h - 4);
}

static bool runSettingsPage(WifiConfig& cfg) {
    auto& d = M5Cardputer.Display;
    auto& kbd = M5Cardputer.Keyboard;
    SetupField field = SetupField::SSID;
    unsigned long startMs = millis();
    bool redraw = true;
    bool confChanged = false;
    auto lastKs = kbd.keysState();
    std::vector<char> prevWord;
    size_t prevHidSize = 0;

    while (true) {
        M5Cardputer.update();
        kbd.updateKeysState();
        auto& ks = kbd.keysState();
        bool enterNow = ks.enter && !lastKs.enter;
        bool delNow   = ks.del && !lastKs.del;
        bool tabNow   = ks.tab && !lastKs.tab;
        bool spaceNow = ks.space && !lastKs.space;
        bool newChar  = (ks.word.size() > prevWord.size()) && !ks.fn;

        if (tabNow) {
            int next = ((int)field + 1) % FIELD_COUNT;
            field = (SetupField)next;
            prevWord.clear();
            redraw = true;
        }

        if (enterNow) {
            if (field == SetupField::SAVE) {
                saveWifiConfig(cfg);
                confChanged = true;
                break;
            } else if (field == SetupField::CANCEL) {
                break;
            } else {
                int next = ((int)field + 1) % FIELD_COUNT;
                field = (SetupField)next;
            }
            prevWord.clear();
            redraw = true;
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
            prevWord.clear();
        }

        if (delNow) {
            if (field == SetupField::SSID) {
                int len = strlen(cfg.ssid);
                if (len > 0) cfg.ssid[len - 1] = '\0';
            } else if (field == SetupField::PASSWORD) {
                int len = strlen(cfg.pass);
                if (len > 0) cfg.pass[len - 1] = '\0';
            }
            prevWord.clear();
            redraw = true;
        }

        if (newChar && ks.word.size() > 0) {
            char c = ks.word.back();
            char* target = (field == SetupField::SSID) ? cfg.ssid : cfg.pass;
            int len = strlen(target);
            if (len < 63) {
                target[len] = c;
                target[len + 1] = '\0';
            }
            redraw = true;
        }

        if (spaceNow && field == SetupField::SSID) {
            int len = strlen(cfg.ssid);
            if (len < 63) { cfg.ssid[len] = ' '; cfg.ssid[len + 1] = '\0'; }
        }

        lastKs = ks;
        prevWord = ks.word;
        prevHidSize = ks.hid_keys.size();

        if (redraw) {
            drawSettingsPage(cfg, field, millis() - startMs);
            redraw = false;
        }

        unsigned long elapsed = millis() - startMs;
        if (elapsed > 30 && (elapsed % 250 < 5)) {
            drawSettingsPage(cfg, field, elapsed);
        }
        delay(20);
    }

    d.fillScreen(TFT_BLACK);
    delay(50);
    return confChanged;
}
