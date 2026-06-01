#pragma once
// Cardputer Radio — Display drawing functions
// All UI rendering for each state

#include <M5Cardputer.h>
#include "config.h"
#include "radio_browser.h"
#include "player.h"

class Display {
public:
    auto& dsp = M5Cardputer.Display;
    int  w = 0, h = 0;
    unsigned long lastActivity = 0;

    void begin() {
        dsp.setRotation(1);  // landscape 240×135
        w = dsp.width();
        h = dsp.height();
        dsp.fillScreen(TFT_BLACK);
    }

    void activity() { lastActivity = millis(); }

    // ── Top status bar (WiFi indicator) ─────────────────────
    void drawStatusBar() {
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        dsp.setTextSize(1);
        dsp.fillRect(0, 0, w, 10, TFT_BLACK);
        dsp.setTextColor(wifiOk ? TFT_GREEN : 0x4208);
        dsp.drawString("W", 2, 1);

        // IP address if connected
        if (wifiOk) {
            dsp.setTextColor(0x8410);
            dsp.drawString(WiFi.localIP().toString().c_str(), 14, 1);
        }

        // Volume indicator
        char vol[8]; snprintf(vol, sizeof(vol), "V:%d", player.volume);
        dsp.setTextColor(0x8410);
        dsp.drawString(vol, w - 42, 1);
    }

    // ── Bottom hint bar ─────────────────────────────────────
    void drawHints(const char* line1, const char* line2 = nullptr) {
        dsp.setTextSize(1);
        dsp.setTextColor(0x8410);
        dsp.fillRect(0, h - 14, w, 14, TFT_BLACK);
        dsp.drawString(line1, 2, h - 12);
        if (line2) dsp.drawString(line2, 2, h - 4);
    }

    // ── MENU screen ─────────────────────────────────────────
    void drawMenu(int selected) {
        dsp.fillScreen(TFT_BLACK);
        drawStatusBar();

        dsp.setTextSize(1);
        dsp.setTextColor(TFT_WHITE);
        dsp.drawString("Cardputer Radio", (w - 15 * 6) / 2, 20);

        const char* items[] = {"Search Stations", "Favorites", "Settings"};
        int startY = 45;
        for (int i = 0; i < 3; i++) {
            int y = startY + i * 22;
            if (i == selected) {
                dsp.fillRoundRect(4, y - 2, w - 8, 18, 4, TFT_BLUE);
                dsp.setTextColor(TFT_WHITE);
            } else {
                dsp.setTextColor(TFT_CYAN);
            }
            dsp.drawString(items[i], 10, y);
        }

        drawHints("Fn+^v:nav  Enter:select");
    }

    // ── SEARCH INPUT screen ─────────────────────────────────
    void drawSearchInput(const char* query, unsigned long tickMs) {
        dsp.fillScreen(TFT_BLACK);
        drawStatusBar();

        dsp.setTextSize(1);
        dsp.setTextColor(TFT_YELLOW);
        dsp.drawString("Search:", 4, 18);

        // Input field
        int fieldY = 32;
        dsp.fillRect(2, fieldY - 1, w - 4, 14, TFT_NAVY);
        dsp.setTextColor(TFT_WHITE);

        char display[33];
        strncpy(display, query, sizeof(display) - 1);
        display[32] = '\0';
        int dlen = strlen(display);
        int maxChars = (w - 8) / 6;
        if (dlen > maxChars) {
            // Scroll to show end
            int start = dlen - maxChars;
            dsp.drawString(display + start, 6, fieldY);
        } else {
            dsp.drawString(display, 6, fieldY);
        }

        // Blinking cursor
        if ((tickMs / BLINK_INTERVAL) % 2 == 0) {
            int cx = 6 + min(dlen, maxChars) * 6;
            dsp.drawFastVLine(cx, fieldY, 10, TFT_WHITE);
        }

        drawHints("Type keyword, Enter:search  Tab:back", "Del:delete");
    }

    // ── SEARCHING / LOADING screen ──────────────────────────
    void drawSearching(const char* query) {
        dsp.fillScreen(TFT_BLACK);
        drawStatusBar();

        dsp.setTextSize(1);
        dsp.setTextColor(TFT_WHITE);
        dsp.drawString("Searching...", (w - 12 * 6) / 2, 40);

        dsp.setTextColor(0x8410);
        char buf[64]; snprintf(buf, sizeof(buf), "\"%s\"", query);
        int bx = (w - strlen(buf) * 6) / 2;
        dsp.drawString(buf, bx > 0 ? bx : 4, 55);
    }

    // ── RESULTS / FAVORITES list screen ─────────────────────
    void drawStationList(const Station* stations, int total, int selected,
                         int scrollOffset, const char* title, bool showFavStar = false,
                         const FavoritesManager* favMgr = nullptr) {
        dsp.fillScreen(TFT_BLACK);
        drawStatusBar();

        // Title
        dsp.setTextSize(1);
        dsp.setTextColor(TFT_WHITE);
        char titleBuf[48];
        snprintf(titleBuf, sizeof(titleBuf), "%s (%d)", title, total);
        dsp.drawString(titleBuf, 4, 12);

        if (total == 0) {
            dsp.setTextColor(0x8410);
            dsp.drawString("No stations found", 4, 40);
            drawHints("Tab:back");
            return;
        }

        // List items
        int listY = 26;
        for (int i = 0; i < RESULTS_PER_PAGE; i++) {
            int idx = scrollOffset + i;
            if (idx >= total) break;

            const Station& s = stations[idx];
            int y = listY + i * 17;

            // Selection highlight
            bool sel = (idx == selected);
            if (sel) {
                dsp.fillRoundRect(2, y - 1, w - 4, 15, 3, TFT_BLUE);
            }

            // Favorite star
            bool fav = favMgr && favMgr->isFavorited(s.url);
            if (fav) {
                dsp.setTextColor(TFT_YELLOW);
                dsp.drawString("*", 4, y);
            }

            // Station name (truncated)
            dsp.setTextColor(sel ? TFT_WHITE : TFT_GREEN);
            int x = fav ? 14 : 4;
            char nameDisplay[28];
            strncpy(nameDisplay, s.name, sizeof(nameDisplay) - 1);
            nameDisplay[27] = '\0';
            int maxW = w - x - 50;
            int maxC = maxW / 6;
            if ((int)strlen(nameDisplay) > maxC) {
                nameDisplay[maxC - 2] = '.';
                nameDisplay[maxC - 1] = '.';
                nameDisplay[maxC] = '\0';
            }
            dsp.drawString(nameDisplay, x, y);

            // Bitrate + codec
            if (s.bitrate > 0) {
                char info[16];
                snprintf(info, sizeof(info), "%d%s", s.bitrate, s.codec[0] ? "" : "k");
                dsp.setTextColor(0x8410);
                dsp.drawString(info, w - 48, y);
            }
        }

        // Scroll indicator
        if (total > RESULTS_PER_PAGE) {
            int barH = (RESULTS_PER_PAGE * h * 13) / total;
            int barY = 26 + (scrollOffset * (17 * RESULTS_PER_PAGE - barH)) / (total - RESULTS_PER_PAGE);
            dsp.fillRect(w - 3, 26, 2, h - 40, 0x4208);
            dsp.fillRect(w - 3, barY, 2, max(barH, 4), 0x8410);
        }

        drawHints("Fn+^v:nav  Enter:play  Tab:back", showFavStar ? "Del:remove fav" : nullptr);
    }

    // ── PLAYING screen ──────────────────────────────────────
    void drawPlaying() {
        dsp.fillScreen(TFT_BLACK);
        drawStatusBar();

        // Station name
        dsp.setTextSize(1);
        dsp.setTextColor(TFT_GREEN);
        dsp.drawString("Now Playing", 4, 14);

        dsp.setTextColor(TFT_WHITE);
        // Truncate long names
        char nameDisplay[28];
        strncpy(nameDisplay, player.stationName, sizeof(nameDisplay) - 1);
        nameDisplay[27] = '\0';
        if (strlen(player.stationName) > 27) {
            nameDisplay[25] = '.'; nameDisplay[26] = '.'; nameDisplay[27] = '\0';
        }
        dsp.drawString(nameDisplay, 4, 28);

        // Stream title (current song)
        if (player.streamTitle[0]) {
            dsp.setTextColor(TFT_YELLOW);
            char titleDisplay[28];
            strncpy(titleDisplay, player.streamTitle, sizeof(titleDisplay) - 1);
            titleDisplay[27] = '\0';
            if (strlen(player.streamTitle) > 27) {
                titleDisplay[25] = '.'; titleDisplay[26] = '.'; titleDisplay[27] = '\0';
            }
            dsp.drawString(titleDisplay, 4, 42);
        }

        // Volume bar
        int barY = 62;
        dsp.setTextColor(0x8410);
        dsp.drawString("Vol", 4, barY);
        int barStart = 28;
        int barW = w - barStart - 8;
        dsp.drawRect(barStart, barY, barW, 10, 0x8410);
        int fillW = (player.volume * barW) / VOLUME_MAX;
        if (fillW > 0) {
            uint16_t color = player.volume > 16 ? TFT_RED : (player.volume > 8 ? TFT_YELLOW : TFT_GREEN);
            dsp.fillRect(barStart + 1, barY + 1, fillW - 2, 8, color);
        }

        // Audio visualization (simple level bars)
        int vizY = 78;
        dsp.drawRect(4, vizY, w - 8, 30, 0x4208);
        dsp.setTextColor(0x8410);
        dsp.drawString("... streaming ...", (w - 15 * 6) / 2, vizY + 10);

        drawHints("Fn+< >:vol  Tab:stop", "Enter:add to favorites");
    }
};
