#pragma once
// Cardputer Radio — WiFi setup: scan, input, NVS credential storage
// Adapted from cyberwisk/M5Card_Wifi_KeyBoard_Setup (MIT-style)
// Ported to PlatformIO with no Arduino String — all fixed char[] buffers.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <vector>
#include "config.h"

// ── NVS keys ─────────────────────────────────────────────────
#define NVS_NS  "cardradio"
#define NVS_SSID "ssid"
#define NVS_PASS "pass"

// ── Externs from main.cpp ─────────────────────────────────────
extern bool sdAvailable;
extern bool wifiConnected;

// ── WiFi network entry ───────────────────────────────────────
struct WiFiNet {
    char ssid[33];
    int  rssi;
    bool encrypted;
};

// ── Generic text input (fixed buffer, no String) ─────────────
// Returns number of chars entered (excluding null). Buffer must be pre-allocated.
// Pass isPassword=true to mask with '*'.
static int inputText(const char* prompt, char* buf, size_t bufSize,
                     int x, int y, bool isPassword = false) {
    auto& d = M5Cardputer.Display;
    auto& kbd = M5Cardputer.Keyboard;
    int len = strlen(buf);
    auto lastKs = kbd.keysState();
    std::vector<char> prevWord;

    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.drawString(prompt, x, y);
    y += 16;

    while (true) {
        M5Cardputer.update();
        kbd.updateKeysState();
        auto& ks = kbd.keysState();
        bool enterNow = ks.enter && !lastKs.enter;
        bool delNow   = ks.del && !lastKs.del;
        bool spaceNow = ks.space && !lastKs.space;
        bool newChar  = (ks.word.size() > prevWord.size()) && !ks.fn;

        if (enterNow) {
            d.fillScreen(TFT_BLACK);
            return len;
        }

        if (delNow && len > 0) {
            buf[--len] = '\0';
            prevWord.clear();
        }

        if (spaceNow && len < (int)bufSize - 1) {
            buf[len++] = ' ';
            buf[len] = '\0';
        }

        if (newChar && ks.word.size() > 0) {
            char c = ks.word.back();
            if (len < (int)bufSize - 1) {
                buf[len++] = c;
                buf[len] = '\0';
            }
        }

        // Redraw input field
        d.fillRect(x, y, d.width() - x, 16, TFT_BLACK);
        d.setTextColor(TFT_WHITE);
        if (isPassword) {
            for (int i = 0; i < len && i * 6 < d.width() - x - 10; i++) {
                d.drawChar('*', x + i * 6, y);
            }
        } else {
            d.drawString(buf, x, y);
        }
        // Cursor blink
        if ((millis() / 500) % 2 == 0) {
            int cx = x + len * 6;
            if (cx < d.width() - 4) d.drawFastVLine(cx, y, 10, TFT_WHITE);
        }

        lastKs = ks;
        prevWord = ks.word;
        delay(10);
    }
}

// ── WiFi scanner with selection UI ────────────────────────────
// Returns true if user picked a network (SSID stored in out_ssid).
static bool scanAndPickNetwork(char* out_ssid, size_t ssidSize) {
    auto& d = M5Cardputer.Display;
    auto& kbd = M5Cardputer.Keyboard;

    // Start async scan
    WiFi.scanDelete();
    WiFi.scanNetworks(true);

    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.drawString("Scanning WiFi...", 4, 4);

    int16_t scanResult;
    do {
        scanResult = WiFi.scanComplete();
        delay(50);
    } while (scanResult == WIFI_SCAN_RUNNING);

    if (scanResult <= 0) {
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_RED);
        d.drawString("No networks found", 4, 20);
        delay(1500);
        return false;
    }

    // Collect & sort by signal strength
    std::vector<WiFiNet> nets;
    for (int i = 0; i < scanResult && i < 20; i++) {
        if (WiFi.RSSI(i) >= -85) {
            WiFiNet net;
            strncpy(net.ssid, WiFi.SSID(i).c_str(), sizeof(net.ssid) - 1);
            net.ssid[sizeof(net.ssid) - 1] = '\0';
            net.rssi = WiFi.RSSI(i);
            net.encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            nets.push_back(net);
        }
    }
    std::sort(nets.begin(), nets.end(),
              [](const WiFiNet& a, const WiFiNet& b) { return a.rssi > b.rssi; });

    if (nets.empty()) {
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_RED);
        d.drawString("No strong networks", 4, 20);
        delay(1500);
        return false;
    }

    // Selection UI
    int sel = 0;
    auto lastKs = kbd.keysState();
    size_t lastHid = 0;

    while (true) {
        d.fillScreen(TFT_BLACK);
        d.setTextSize(1);
        d.setTextColor(TFT_CYAN);
        d.drawString("Pick WiFi network:", 4, 2);

        int itemsPerPage = 6;
        int start = (sel / itemsPerPage) * itemsPerPage;
        for (int i = 0; i < itemsPerPage && (start + i) < (int)nets.size(); i++) {
            int idx = start + i;
            int y = 16 + i * 18;
            bool isSel = (idx == sel);

            d.fillRect(2, y, d.width() - 4, 16, TFT_BLACK);
            if (isSel) d.fillRoundRect(2, y, d.width() - 4, 16, 4, TFT_BLUE);

            d.setTextColor(isSel ? TFT_WHITE : TFT_GREEN);
            // Truncate long SSIDs
            char ssidDisplay[22];
            strncpy(ssidDisplay, nets[idx].ssid, 20);
            ssidDisplay[20] = '\0';
            if (strlen(nets[idx].ssid) > 20) { ssidDisplay[18]='.'; ssidDisplay[19]='.'; ssidDisplay[20]='\0'; }
            d.drawString(ssidDisplay, 6, y + 1);

            char info[16];
            snprintf(info, sizeof(info), "%ddBm%s", nets[idx].rssi,
                     nets[idx].encrypted ? " *" : "");
            d.setTextColor(0x8410);
            d.drawString(info, d.width() - 72, y + 1);
        }

        d.setTextColor(0x8410);
        d.drawString("Fn+^v:nav  Enter:pick", 4, d.height() - 10);

        M5Cardputer.update();
        kbd.updateKeysState();
        auto& ks = kbd.keysState();
        bool enterNow = ks.enter && !lastKs.enter;

        if (enterNow) {
            strncpy(out_ssid, nets[sel].ssid, ssidSize - 1);
            out_ssid[ssidSize - 1] = '\0';
            return true;
        }

        if (ks.fn && ks.hid_keys.size() > lastHid) {
            for (size_t i = lastHid; i < ks.hid_keys.size(); i++) {
                uint8_t hk = ks.hid_keys[i];
                if (hk == 0x33 && sel > 0) sel--;           // Fn+;
                else if (hk == 0x37 && sel < (int)nets.size() - 1) sel++;  // Fn+.
            }
            lastHid = ks.hid_keys.size();
        }

        lastKs = ks;
        delay(10);
    }
}

// ── Save credentials to NVS ───────────────────────────────────
static void nvsSave(const char* ssid, const char* pass) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putString(NVS_SSID, ssid);
    prefs.putString(NVS_PASS, pass);
    prefs.end();
    Serial.printf("[NVS] Saved: %s\n", ssid);
}

// ── Load credentials from NVS ─────────────────────────────────
// Returns true if valid creds were found.
static bool nvsLoad(WifiConfig& cfg) {
    Preferences prefs;
    prefs.begin(NVS_NS, true);
    String ssid = prefs.getString(NVS_SSID, "");
    String pass = prefs.getString(NVS_PASS, "");
    prefs.end();

    if (ssid.length() == 0) return false;

    strncpy(cfg.ssid, ssid.c_str(), sizeof(cfg.ssid) - 1);
    strncpy(cfg.pass, pass.c_str(), sizeof(cfg.pass) - 1);
    Serial.printf("[NVS] Loaded: %s\n", cfg.ssid);
    return true;
}

// ── Full WiFi connection flow ─────────────────────────────────
// 1. Try NVS stored creds
// 2. Fall back to SD /wifi.cfg
// 3. If both fail, run scanner + input
// Sets wifiConnected on success.
static void connectWiFiEx(WifiConfig& cfg) {
    auto& d = M5Cardputer.Display;

    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);     // Disable sleep entirely for streaming

    // Try NVS first
    bool hasCreds = nvsLoad(cfg);
    // Fall back to SD config if NVS empty
    if (!hasCreds && strlen(cfg.ssid) == 0) {
        // (already loaded by caller from SD earlier)
    }

    if (strlen(cfg.ssid) > 0) {
        d.fillScreen(TFT_BLACK);
        d.setTextSize(1);
        d.setTextColor(TFT_WHITE);
        d.drawString("Connecting...", 4, 20);
        d.setTextColor(0x8410);
        d.drawString(cfg.ssid, 4, 34);

        WiFi.begin(cfg.ssid, cfg.pass);
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
            int n = ((millis() - t0) / 500) % 4;
            d.fillRect(4, 48, 40, 10, TFT_BLACK);
            for (int i = 0; i < n; i++) d.drawChar('.', 4 + i * 6, 48, 1);
            d.setTextColor(TFT_CYAN);
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            nvsSave(cfg.ssid, cfg.pass);  // Save successful creds
            d.fillScreen(TFT_BLACK);
            d.setTextColor(TFT_GREEN);
            d.drawString("Connected!", 4, 20);
            d.setTextColor(TFT_WHITE);
            d.setTextColor(0x8410);
            d.drawString(WiFi.localIP().toString().c_str(), 4, 34);
            delay(1000);
            Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
            return;
        }
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_RED);
        d.drawString("Connection failed", 4, 20);
        delay(1000);
    }

    // No valid stored creds — run interactive setup
    char pickedSsid[33] = {0};
    if (!scanAndPickNetwork(pickedSsid, sizeof(pickedSsid))) return;

    char pickedPass[65] = {0};
    inputText("Password:", pickedPass, sizeof(pickedPass), 4, 4, true);

    strncpy(cfg.ssid, pickedSsid, sizeof(cfg.ssid) - 1);
    strncpy(cfg.pass, pickedPass, sizeof(cfg.pass) - 1);

    // Save to both NVS and SD
    nvsSave(cfg.ssid, cfg.pass);
    if (sdAvailable) saveWifiConfig(cfg);

    // Connect
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_WHITE);
    d.drawString("Connecting...", 4, 20);
    WiFi.begin(cfg.ssid, cfg.pass);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_GREEN);
        d.drawString("Connected!", 4, 20);
        d.setTextColor(TFT_WHITE);
        d.drawString(WiFi.localIP().toString().c_str(), 4, 34);
        delay(1000);
        Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
    } else {
        d.fillScreen(TFT_BLACK);
        d.setTextColor(TFT_RED);
        d.drawString("Failed to connect", 4, 20);
        delay(1500);
    }
}
