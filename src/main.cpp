// Cardputer Radio - Internet Radio Streamer for M5Stack Cardputer
// Entry point + state machine
// Dual-core: audio decoding on Core 1, UI + WiFi on Core 0

#include <Arduino.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <vector>
#include <Preferences.h>

// Project headers
#include "config.h"
#include "player.h"
#include "radio_browser.h"
#include "favorites.h"
#include "display.h"
#include "settings.h"
#include "wifi_setup.h"

// ── Global objects ──────────────────────────────────────────
RadioPlayer       player;
RadioBrowser      browser;
FavoritesManager  favMgr;
Display           disp;
WifiConfig        wifiCfg;
State             state = State::MENU;
State             prevState = State::MENU;

// Per-state data
int   menuSelected     = 0;
char  searchQuery[33]  = {0};
int   resultSelected   = 0;
int   resultScroll     = 0;
int   favSelected      = 0;
int   favScroll        = 0;
bool  stateChanged     = true;
bool  sdAvailable      = false;
bool  wifiConnected    = false;
unsigned long stateEnterMs = 0;

// Mutex for player state (accessed by both cores)
portMUX_TYPE playerMux = portMUX_INITIALIZER_UNLOCKED;

// ── Forward declarations ────────────────────────────────────
void enterState(State s);

// ── Audio task (Core 1) — dedicated CPU for decoding ────────
void audioTask(void* param) {
    while (1) {
        // No mutex needed — loop() only reads state, WiFi needs interrupts alive
        player.loop();
        vTaskDelay(1);
    }
}

// ── WiFi Connection ──────────────────────────────────────────
void connectWiFi() {
    connectWiFiEx(wifiCfg);
}

// ── Setup ───────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n\n=== Cardputer Radio ===");
    Serial.flush();
    delay(100);

    // ── Configure speaker BEFORE begin() (matching M5WebRadio) ──
    auto cfg = M5.config();
    auto spk_cfg = M5Cardputer.Speaker.config();
    spk_cfg.sample_rate = 128000;        // Match WebRadio
    spk_cfg.task_pinned_core = APP_CPU_NUM;
    M5Cardputer.Speaker.config(spk_cfg);
    M5Cardputer.begin(cfg, true);

    disp.begin();
    disp.dsp->setTextSize(1);
    disp.dsp->setTextColor(TFT_WHITE);
    disp.dsp->drawString("Cardputer Radio", 2, 2);
    disp.dsp->setTextColor(0x8410);
    disp.dsp->drawString("Starting...", 2, 16);

    // Init SD
    SPI.begin(SD_SPI_SCK, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_CS);
    if (SD.begin(SD_SPI_CS, SPI, 25000000)) {
        sdAvailable = true;
        Serial.println("[SD] OK");
        disp.dsp->setTextColor(TFT_GREEN);
        disp.dsp->drawString("SD: OK", 2, 30);
    } else {
        Serial.println("[SD] Not found");
        disp.dsp->setTextColor(TFT_RED);
        disp.dsp->drawString("SD: N/A", 2, 30);
    }

    // Load config & favorites
    if (sdAvailable) {
        loadWifiConfig(wifiCfg);
        favMgr.load();
    }

    // Set initial volume (audio only — M5WebRadio doesn't use Speaker.setVolume)
    player.setVol(wifiCfg.volume);

    // Init audio player
    player.begin();

    // Connect WiFi
    connectWiFi();

    // Start audio task on Core 1 (dedicated CPU for decoding)
    xTaskCreatePinnedToCore(
        audioTask, "audio", 16384, NULL, 3, NULL, 1);

    enterState(State::MENU);
}

// ── State transition helper ─────────────────────────────────
void enterState(State s) {
    prevState = state;
    state = s;
    stateChanged = true;
    stateEnterMs = millis();
    disp.activity();
    Serial.printf("[State] %d -> %d\n", (int)prevState, (int)state);
}

// ── Handle MENU state ───────────────────────────────────────
void handleMenu() {
    auto& kbd = M5Cardputer.Keyboard;
    static auto lastKs = kbd.keysState();
    static size_t lastHid = 0;

    M5Cardputer.update();
    kbd.updateKeysState();
    auto& ks = kbd.keysState();

    if (stateChanged) {
        disp.drawMenu(menuSelected);
        stateChanged = false;
        lastHid = ks.hid_keys.size();
        lastKs = ks;
    }

    bool moved = false;
    if (ks.fn) {
        for (size_t i = lastHid; i < ks.hid_keys.size(); i++) {
            uint8_t hk = ks.hid_keys[i];
            if (hk == 0x33) {
                menuSelected = (menuSelected - 1 + MENU_ITEMS) % MENU_ITEMS;
                moved = true;
            } else if (hk == 0x37) {
                menuSelected = (menuSelected + 1) % MENU_ITEMS;
                moved = true;
            }
        }
    }
    lastHid = ks.hid_keys.size();

    if (moved) {
        disp.activity();
        disp.drawMenu(menuSelected);
    }

    if (ks.enter && !lastKs.enter) {
        disp.activity();
        switch (menuSelected) {
            case 0:
                searchQuery[0] = '\0';
                enterState(State::SEARCH_INPUT);
                break;
            case 1:
                favSelected = 0;
                favScroll = 0;
                enterState(State::FAVORITES);
                break;
            case 2:
                enterState(State::SETTINGS);
                break;
        }
    }

    lastKs = ks;
}

// ── Handle SEARCH_INPUT state ───────────────────────────────
void handleSearchInput() {
    auto& kbd = M5Cardputer.Keyboard;
    static auto lastKs = kbd.keysState();
    static std::vector<char> prevWord;
    M5Cardputer.update();
    kbd.updateKeysState();
    auto& ks = kbd.keysState();

    unsigned long now = millis();
    bool enterNow = ks.enter && !lastKs.enter;
    bool delNow   = ks.del && !lastKs.del;
    bool tabNow   = ks.tab && !lastKs.tab;
    bool spaceNow = ks.space && !lastKs.space;
    bool newChar  = (ks.word.size() > prevWord.size()) && !ks.fn;

    if (tabNow) {
        enterState(State::MENU);
        return;
    }

    if (enterNow && strlen(searchQuery) > 0) {
        enterState(State::SEARCHING);
        return;
    }

    if (delNow) {
        int len = strlen(searchQuery);
        if (len > 0) searchQuery[len - 1] = '\0';
        prevWord.clear();
        stateChanged = true;
    }

    if (spaceNow) {
        int len = strlen(searchQuery);
        if (len < 31) { searchQuery[len] = ' '; searchQuery[len + 1] = '\0'; }
        stateChanged = true;
    }

    if (newChar && ks.word.size() > 0) {
        char c = ks.word.back();
        int len = strlen(searchQuery);
        if (len < 31) {
            searchQuery[len] = c;
            searchQuery[len + 1] = '\0';
        }
        stateChanged = true;
    }

    if (stateChanged || (now % BLINK_INTERVAL < 20)) {
        disp.drawSearchInput(searchQuery, now);
        stateChanged = false;
    }

    lastKs = ks;
    prevWord = ks.word;
}

// ── Handle SEARCHING state ──────────────────────────────────
void handleSearching() {
    if (stateChanged) {
        disp.drawSearching(searchQuery);
        stateChanged = false;
        resultSelected = 0;
        resultScroll = 0;
    }

    int count = browser.search(searchQuery);

    if (count > 0) {
        enterState(State::SEARCH_RESULTS);
    } else {
        disp.dsp->fillScreen(TFT_BLACK);
        disp.drawStatusBar();
        disp.dsp->setTextColor(TFT_RED);
        disp.dsp->drawString("No results", 4, 40);
        disp.dsp->setTextColor(0x8410);
        disp.dsp->drawString(browser.lastError, 4, 54);
        disp.drawHints("Tab:back");
        delay(1500);
        enterState(State::MENU);
    }
}

// ── Handle SEARCH_RESULTS state ─────────────────────────────
void handleSearchResults() {
    auto& kbd = M5Cardputer.Keyboard;
    static auto lastKs = kbd.keysState();
    static size_t lastHid = 0;

    M5Cardputer.update();
    kbd.updateKeysState();
    auto& ks = kbd.keysState();

    bool enterNow = ks.enter && !lastKs.enter;
    bool tabNow   = ks.tab && !lastKs.tab;

    if (tabNow) {
        enterState(State::MENU);
        return;
    }

    bool moved = false;
    if (ks.fn) {
        for (size_t i = lastHid; i < ks.hid_keys.size(); i++) {
            uint8_t hk = ks.hid_keys[i];
            if (hk == 0x33) {
                if (resultSelected > 0) {
                    resultSelected--;
                    if (resultSelected < resultScroll) resultScroll = resultSelected;
                    moved = true;
                }
            } else if (hk == 0x37) {
                if (resultSelected < browser.resultCount - 1) {
                    resultSelected++;
                    if (resultSelected >= resultScroll + RESULTS_PER_PAGE) {
                        resultScroll = resultSelected - RESULTS_PER_PAGE + 1;
                    }
                    moved = true;
                }
            }
        }
        lastHid = ks.hid_keys.size();
    }

    if (moved || stateChanged) {
        disp.drawStationList(browser.results, browser.resultCount,
                            resultSelected, resultScroll, "Results");
        stateChanged = false;
        lastHid = ks.hid_keys.size();
        disp.activity();
    }

    if (enterNow && browser.resultCount > 0) {
        portENTER_CRITICAL(&playerMux);
        player.play(browser.results[resultSelected].url,
                    browser.results[resultSelected].name);
        portEXIT_CRITICAL(&playerMux);
        enterState(State::PLAYING);
    }

    lastKs = ks;
}

// ── Handle FAVORITES state ──────────────────────────────────
void handleFavorites() {
    auto& kbd = M5Cardputer.Keyboard;
    static auto lastKs = kbd.keysState();
    static size_t lastHid = 0;

    M5Cardputer.update();
    kbd.updateKeysState();
    auto& ks = kbd.keysState();

    bool enterNow = ks.enter && !lastKs.enter;
    bool tabNow   = ks.tab && !lastKs.tab;
    bool delNow   = ks.del && !lastKs.del;

    if (tabNow) {
        enterState(State::MENU);
        return;
    }

    if (delNow && favMgr.count > 0 && favSelected < favMgr.count) {
        favMgr.remove(favSelected);
        if (favSelected >= favMgr.count && favMgr.count > 0) favSelected = favMgr.count - 1;
        if (favSelected < favScroll) favScroll = (favSelected > 0) ? favSelected : 0;
        stateChanged = true;
    }

    bool moved = false;
    if (ks.fn) {
        for (size_t i = lastHid; i < ks.hid_keys.size(); i++) {
            uint8_t hk = ks.hid_keys[i];
            if (hk == 0x33 && favSelected > 0) {
                favSelected--;
                if (favSelected < favScroll) favScroll = favSelected;
                moved = true;
            } else if (hk == 0x37 && favSelected < favMgr.count - 1) {
                favSelected++;
                if (favSelected >= favScroll + RESULTS_PER_PAGE) favScroll = favSelected - RESULTS_PER_PAGE + 1;
                moved = true;
            }
        }
        lastHid = ks.hid_keys.size();
    }

    if (moved || stateChanged) {
        disp.drawStationList(favMgr.favorites, favMgr.count,
                            favSelected, favScroll, "Favorites", true, &favMgr);
        stateChanged = false;
        lastHid = ks.hid_keys.size();
        disp.activity();
    }

    if (enterNow && favMgr.count > 0) {
        portENTER_CRITICAL(&playerMux);
        player.play(favMgr.favorites[favSelected].url,
                    favMgr.favorites[favSelected].name);
        portEXIT_CRITICAL(&playerMux);
        enterState(State::PLAYING);
    }

    lastKs = ks;
}

// ── Handle PLAYING state ────────────────────────────────────
void handlePlaying() {
    auto& kbd = M5Cardputer.Keyboard;
    static auto lastKs = kbd.keysState();
    static size_t lastHid = 0;

    M5Cardputer.update();
    kbd.updateKeysState();
    auto& ks = kbd.keysState();

    bool tabNow   = ks.tab && !lastKs.tab;
    bool enterNow = ks.enter && !lastKs.enter;

    if (stateChanged) {
        disp.drawPlaying();
        stateChanged = false;
        lastHid = ks.hid_keys.size();
    }

    if (tabNow) {
        portENTER_CRITICAL(&playerMux);
        player.stop();
        portEXIT_CRITICAL(&playerMux);
        enterState(State::MENU);
        return;
    }

    if (enterNow && player.isPlaying) {
        Station s;
        strncpy(s.name, player.stationName, sizeof(s.name) - 1);
        strncpy(s.url, player.streamUrl, sizeof(s.url) - 1);
        s.tags[0] = '\0';
        s.codec[0] = '\0';
        s.bitrate = 0;
        s.valid = true;

        if (favMgr.add(s)) {
            disp.dsp->fillRect(4, 105, 200, 12, TFT_BLACK);
            disp.dsp->setTextColor(TFT_GREEN);
            disp.dsp->drawString("Added to favorites!", 4, 105);
            delay(1000);
            stateChanged = true;
        } else {
            disp.dsp->fillRect(4, 105, 200, 12, TFT_BLACK);
            disp.dsp->setTextColor(TFT_YELLOW);
            disp.dsp->drawString("Already in favorites", 4, 105);
            delay(1000);
            stateChanged = true;
        }
    }

    if (ks.fn) {
        for (size_t i = lastHid; i < ks.hid_keys.size(); i++) {
            uint8_t hk = ks.hid_keys[i];
            if (hk == 0x36) {
                portENTER_CRITICAL(&playerMux);
                player.volDown();
                portEXIT_CRITICAL(&playerMux);
                stateChanged = true;
            } else if (hk == 0x38) {
                portENTER_CRITICAL(&playerMux);
                player.volUp();
                portEXIT_CRITICAL(&playerMux);
                stateChanged = true;
            }
        }
        lastHid = ks.hid_keys.size();
    }

    static unsigned long lastRedraw = 0;
    if (millis() - lastRedraw > 3000) {
        stateChanged = true;
        lastRedraw = millis();
    }

    lastKs = ks;
}

// ── Handle SETTINGS state ───────────────────────────────────
void handleSettings() {
    bool confChanged = runSettingsPage(wifiCfg);
    portENTER_CRITICAL(&playerMux);
    // WiFi already connected by tryConnect() — just persist volume
    player.setVol(wifiCfg.volume);
    M5Cardputer.Speaker.setVolume(map(wifiCfg.volume, VOLUME_MIN, VOLUME_MAX, 0, 255));
    enterState(State::MENU);
}

// ── Main Loop (Core 0: WiFi + UI only) ──────────────────────
void loop() {
    switch (state) {
        case State::MENU:            handleMenu();           break;
        case State::SEARCH_INPUT:    handleSearchInput();    break;
        case State::SEARCHING:       handleSearching();      break;
        case State::SEARCH_RESULTS:  handleSearchResults();  break;
        case State::FAVORITES:       handleFavorites();      break;
        case State::PLAYING:         handlePlaying();        break;
        case State::SETTINGS:        handleSettings();       break;
    }
    delay(5);
}
