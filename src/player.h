#pragma once
// Cardputer Radio — Audio player wrapper around ESP32-audioI2S
// Handles MP3/AAC stream playback with volume control

#include <Arduino.h>
#include <Audio.h>
#include "config.h"

// Forward declare Audio class from ESP32-audioI2S
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
        Serial.println("[Player] Initialized");
    }

    void play(const char* url, const char* name) {
        if (!audio) begin();
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
    }

    void volUp() {
        setVol(volume + VOLUME_STEP);
    }

    void volDown() {
        setVol(volume - VOLUME_STEP);
    }

    void loop() {
        if (audio && isPlaying) {
            audio->loop();
        }
    }
};

// ── Audio callbacks (defined once, shared) ──────────────────
// These are called by ESP32-audioI2S from its internal task

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
    // Stream ended — reconnect or stop
}
