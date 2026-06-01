#pragma once
// Cardputer Radio — Favorites management (SD card JSON)
// Stores favorite stations as a JSON array on SD card

#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "config.h"
#include "radio_browser.h"

class FavoritesManager {
public:
    Station favorites[MAX_FAVORITES];
    int     count = 0;

    // ── Load favorites from SD ──────────────────────────────
    bool load() {
        count = 0;
        if (!SD.exists(FAVORITES_FILE)) {
            Serial.println("[Fav] No favorites file, starting fresh");
            return true;  // Empty is OK
        }

        File f = SD.open(FAVORITES_FILE, FILE_READ);
        if (!f) {
            Serial.println("[Fav] Can't open favorites file");
            return false;
        }

        size_t size = f.size();
        if (size > 8192) {
            Serial.println("[Fav] Favorites file too large");
            f.close();
            return false;
        }

        char buf[8192];
        size_t read = f.readBytes(buf, size);
        f.close();
        buf[read] = '\0';

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, buf, read);
        if (err) {
            Serial.printf("[Fav] JSON parse error: %s\n", err.c_str());
            return false;
        }

        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject item : arr) {
            if (count >= MAX_FAVORITES) break;

            const char* name = item["name"];
            const char* url  = item["url"];
            if (!name || !url) continue;

            Station& s = favorites[count];
            strncpy(s.name, name, sizeof(s.name) - 1);
            s.name[sizeof(s.name) - 1] = '\0';
            strncpy(s.url, url, sizeof(s.url) - 1);
            s.url[sizeof(s.url) - 1] = '\0';

            const char* tags = item["tags"];
            if (tags) {
                strncpy(s.tags, tags, sizeof(s.tags) - 1);
                s.tags[sizeof(s.tags) - 1] = '\0';
            } else {
                s.tags[0] = '\0';
            }

            const char* codec = item["codec"];
            if (codec) {
                strncpy(s.codec, codec, sizeof(s.codec) - 1);
                s.codec[sizeof(s.codec) - 1] = '\0';
            } else {
                s.codec[0] = '\0';
            }

            s.bitrate = item["bitrate"] | 0;
            s.valid = true;
            count++;
        }

        Serial.printf("[Fav] Loaded %d favorites\n", count);
        return true;
    }

    // ── Save favorites to SD ────────────────────────────────
    bool save() {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < count; i++) {
            JsonObject item = arr.add<JsonObject>();
            item["name"]    = favorites[i].name;
            item["url"]     = favorites[i].url;
            if (favorites[i].tags[0])  item["tags"]  = favorites[i].tags;
            if (favorites[i].codec[0]) item["codec"] = favorites[i].codec;
            item["bitrate"] = favorites[i].bitrate;
        }

        SD.remove(FAVORITES_FILE);
        File f = SD.open(FAVORITES_FILE, FILE_WRITE);
        if (!f) {
            Serial.println("[Fav] Can't write favorites file");
            return false;
        }

        size_t len = measureJson(doc);
        char*  buf = (char*)malloc(len + 1);
        if (!buf) {
            f.close();
            return false;
        }
        serializeJson(doc, buf, len + 1);
        f.write((uint8_t*)buf, len);
        free(buf);
        f.close();

        Serial.printf("[Fav] Saved %d favorites\n", count);
        return true;
    }

    // ── Add a station to favorites ──────────────────────────
    bool add(const Station& s) {
        if (count >= MAX_FAVORITES) return false;
        // Check duplicate
        for (int i = 0; i < count; i++) {
            if (strcmp(favorites[i].url, s.url) == 0) return false;
        }
        favorites[count] = s;
        count++;
        return save();
    }

    // ── Remove a station by index ──────────────────────────
    bool remove(int idx) {
        if (idx < 0 || idx >= count) return false;
        for (int i = idx; i < count - 1; i++) {
            favorites[i] = favorites[i + 1];
        }
        count--;
        return save();
    }

    // ── Check if a station URL is already favorited ─────────
    bool isFavorited(const char* url) const {
        for (int i = 0; i < count; i++) {
            if (strcmp(favorites[i].url, url) == 0) return true;
        }
        return false;
    }
};
