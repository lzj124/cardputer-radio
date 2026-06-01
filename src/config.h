#pragma once
// Cardputer Radio — Configuration & constants

// ── WiFi defaults (override via SD /wifi.cfg or build flags) ──
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// ── State Machine ───────────────────────────────────────────
enum class State {
    MENU,            // Main menu: Search | Favorites | Settings
    SEARCH_INPUT,    // Typing search keywords
    SEARCHING,       // Querying Radio Browser API
    SEARCH_RESULTS,  // Browsing search results
    FAVORITES,       // Browsing saved stations
    PLAYING,         // Streaming radio
    SETTINGS,        // WiFi + volume config
};

// ── I2S Pins (StampS3 Cardputer — MAX98357 DAC) ────────────
static constexpr int I2S_BCLK = 41;
static constexpr int I2S_LRC  = 42;
static constexpr int I2S_DOUT = 43;

// ── SD Card ─────────────────────────────────────────────────
static constexpr int SD_SPI_SCK  = 40;
static constexpr int SD_SPI_MISO = 39;
static constexpr int SD_SPI_MOSI = 14;
static constexpr int SD_SPI_CS   = 4;   // Cardputer SD CS
static constexpr const char* FAVORITES_FILE = "/fav.json";
static constexpr const char* WIFI_CFG_FILE   = "/wifi.cfg";

// ── Radio Browser API ──────────────────────────────────────
static constexpr const char* RB_HOST = "de1.api.radio-browser.info";
static constexpr int   RB_HTTPS_PORT = 443;
static constexpr int   RB_SEARCH_LIMIT = 20;
static constexpr int   MAX_RESULTS    = 20;
static constexpr int   MAX_FAVORITES  = 50;

// ── Display ─────────────────────────────────────────────────
static constexpr int MENU_ITEMS     = 3;
static constexpr int RESULTS_PER_PAGE = 6;
static constexpr int TEXT_MAX_CHARS = 32;

// ── Timing (ms) ─────────────────────────────────────────────
static constexpr unsigned long HTTP_TIMEOUT     = 15000;  // 15s for Radio Browser search
static constexpr unsigned long SEARCH_COOLDOWN   = 2000;   // min interval between searches
static constexpr unsigned long BLINK_INTERVAL    = 500;    // cursor blink
static constexpr unsigned long SCREEN_SAVER_MS   = 60000;  // dim after 60s idle

// ── Volume ─────────────────────────────────────────────────
static constexpr int VOLUME_MAX  = 21;   // ESP32-audioI2S volume range
static constexpr int VOLUME_MIN  = 0;
static constexpr int VOLUME_STEP = 2;
