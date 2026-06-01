#pragma once
// Cardputer Radio — Radio Browser API client
// Searches https://de1.api.radio-browser.info for internet radio stations

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"

struct Station {
    char name[64];
    char url[256];
    char tags[64];
    char codec[16];
    int  bitrate;
    bool valid;
};

class RadioBrowser {
public:
    Station results[MAX_RESULTS];
    int    resultCount = 0;
    char   lastError[64] = {0};

    // ── Search stations by keyword ──────────────────────────
    // Returns number of results found. Populates results[].
    int search(const char* query) {
        resultCount = 0;
        lastError[0] = '\0';

        if (!query || strlen(query) == 0) {
            strncpy(lastError, "Empty query", sizeof(lastError) - 1);
            return 0;
        }

        // Build URL with URL-encoded query
        char path[256];
        buildSearchPath(query, path, sizeof(path));

        WiFiClientSecure client;
        client.setInsecure();  // Skip cert validation for speed
        client.setTimeout(10); // 10s timeout per operation

        Serial.printf("[RB] GET %s\n", path);

        if (!client.connect(RB_HOST, RB_HTTPS_PORT)) {
            strncpy(lastError, "Connect failed", sizeof(lastError) - 1);
            Serial.println("[RB] Connection failed");
            return 0;
        }

        // Send request
        client.printf("GET %s HTTP/1.1\r\n", path);
        client.printf("Host: %s\r\n", RB_HOST);
        client.printf("User-Agent: CardputerRadio/1.0\r\n");
        client.printf("Connection: close\r\n\r\n");

        // Read response — accumulate into buffer
        char buf[8192];
        int  bufLen = 0;
        unsigned long t0 = millis();

        // Skip headers
        bool headersDone = false;
        int  newlineCount = 0;
        char prev = 0;

        while (millis() - t0 < HTTP_TIMEOUT) {
            if (!client.connected() && !client.available()) break;

            while (client.available()) {
                char c = client.read();
                if (!headersDone) {
                    if (c == '\n') {
                        newlineCount++;
                        if (newlineCount == 2 || (prev == '\r' && c == '\n' && newlineCount >= 2)) {
                            headersDone = true;
                        }
                    } else if (c != '\r') {
                        newlineCount = 0;
                    }
                    prev = c;
                } else {
                    if (bufLen < (int)sizeof(buf) - 1) {
                        buf[bufLen++] = c;
                    }
                }
            }
            if (headersDone && !client.available() && !client.connected()) break;
        }
        client.stop();

        if (bufLen == 0) {
            strncpy(lastError, "Empty response", sizeof(lastError) - 1);
            return 0;
        }
        buf[bufLen] = '\0';

        // Parse JSON array
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, buf, bufLen);
        if (err) {
            snprintf(lastError, sizeof(lastError), "JSON: %s", err.c_str());
            Serial.printf("[RB] JSON error: %s\n", err.c_str());
            return 0;
        }

        JsonArray arr = doc.as<JsonArray>();
        int count = 0;
        for (JsonObject station : arr) {
            if (count >= MAX_RESULTS) break;

            const char* name = station["name"];
            const char* url  = station["url"];
            if (!name || !url) continue;

            Station& s = results[count];
            strncpy(s.name, name, sizeof(s.name) - 1);
            s.name[sizeof(s.name) - 1] = '\0';

            strncpy(s.url, url, sizeof(s.url) - 1);
            s.url[sizeof(s.url) - 1] = '\0';

            const char* tags = station["tags"];
            if (tags) {
                strncpy(s.tags, tags, sizeof(s.tags) - 1);
                s.tags[sizeof(s.tags) - 1] = '\0';
            } else {
                s.tags[0] = '\0';
            }

            const char* codec = station["codec"];
            if (codec) {
                strncpy(s.codec, codec, sizeof(s.codec) - 1);
                s.codec[sizeof(s.codec) - 1] = '\0';
            } else {
                s.codec[0] = '\0';
            }

            s.bitrate = station["bitrate"] | 0;
            s.valid = true;
            count++;
        }

        resultCount = count;
        Serial.printf("[RB] Found %d stations for \"%s\"\n", count, query);
        return count;
    }

private:
    void buildSearchPath(const char* query, char* out, size_t outLen) {
        // Manual URL encoding for space and common chars
        char encoded[128];
        int  j = 0;
        for (int i = 0; query[i] && j < (int)sizeof(encoded) - 1; i++) {
            char c = query[i];
            if (c == ' ') {
                if (j + 3 < (int)sizeof(encoded) - 1) {
                    encoded[j++] = '%';
                    encoded[j++] = '2';
                    encoded[j++] = '0';
                }
            } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded[j++] = c;
            } else {
                // Encode safely
                if (j + 3 < (int)sizeof(encoded) - 1) {
                    encoded[j++] = '%';
                    encoded[j++] = "0123456789ABCDEF"[(c >> 4) & 0xF];
                    encoded[j++] = "0123456789ABCDEF"[c & 0xF];
                }
            }
        }
        encoded[j] = '\0';
        snprintf(out, outLen, "/json/stations/search?name=%s&limit=%d&hidebroken=true", encoded, RB_SEARCH_LIMIT);
    }
};
