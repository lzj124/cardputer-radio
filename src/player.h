#pragma once
// Cardputer Radio — Audio player wrapper around ESP32-audioI2S
// Handles MP3/AAC stream playback with volume control.
//
// Mirrors cyberwisk/M5Cardputer_WebRadio's proven approach:
// - Configure M5 speaker BEFORE begin() with sample_rate=128000
// - Use same I2S pin mapping (BCLK=41, WS=43, DOUT=42)
// - Let M5 speaker and audio library coexist (no end/begin)

#include <Arduino.h>
#include <Audio.h>
#include <M5Cardputer.h>
#include "config.h"

class Audio;

class RadioPlayer {
public:
    Audio* audio = nullptr;
    char stationName[64] = {0};
    char streamTitle[128] = {0};
    char streamUrl[256] = {0};
    int  volume = 12;            // 0-21
    bool isPlaying = false;

    void begin() {
        if (audio) return;
        audio = new Audio();
        audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
        audio->setVolume(volume);
        audio->setBalance(0);
        // Big buffers for smooth high-bitrate streaming
        // Input: 48KB (~3s @ 128kbps), Output: 256KB PSRAM
        audio->setBufsize(49152, 262144);
        Serial.println("[Player] Initialized (buf=304KB)");
    }

    void play(const char* url, const char* name) {
        if (!audio) begin();
        audio->stopSong();
        strncpy(streamUrl, url, sizeof(streamUrl) - 1);
        strncpy(stationName, name, sizeof(stationName) - 1);
        streamTitle[0] = '\0';
        audio->connecttohost(url);
        isPlaying = true;
        Serial.printf("[Player] Playing: %s\n", name);
    }

    void stop() {
        if (!audio || !isPlaying) return;
        audio->stopSong();
        isPlaying = false;
        streamUrl[0] = '\0';
        stationName[0] = '\0';
        streamTitle[0] = '\0';
        Serial.println("[Player] Stopped");
    }

    void setVol(int v) {
        volume = constrain(v, VOLUME_MIN, VOLUME_MAX);
        if (audio) audio->setVolume(volume);
        // M5WebRadio does NOT call Speaker.setVolume — audio handles it
    }

    void volUp()   { setVol(volume + VOLUME_STEP); }
    void volDown() { setVol(volume - VOLUME_STEP); }

    void loop() {
        if (audio && isPlaying) {
            audio->loop();
        }
    }
};

// ── Audio callbacks (called from ESP32-audioI2S internal task) ──

extern RadioPlayer player;

void audio_info(const char* info) {
    Serial.printf("[Audio] %s\n", info);
}

void audio_id3data(const char* data) {
    Serial.printf("[ID3] %s\n", data);
}

void audio_showstation(const char* name) {
    Serial.printf("[Station] %s\n", name);
    if (name && strlen(name) > 0) {
        strncpy(player.stationName, name, sizeof(player.stationName) - 1);
    }
}

void audio_showstreamtitle(const char* title) {
    Serial.printf("[Title] %s\n", title);
    if (title && strlen(title) > 0) {
        strncpy(player.streamTitle, title, sizeof(player.streamTitle) - 1);
    }
}

void audio_eof_mp3(const char* info) {
    Serial.printf("[EOF] %s\n", info);
}
