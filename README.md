# Cardputer Radio

Internet radio streamer for M5Stack Cardputer (ESP32-S3).

Search and play 50,000+ radio stations via [Radio Browser API](https://www.radio-browser.info/).

## Features

- **Search** — Keyword search across global radio stations
- **Favorites** — Save stations to SD card, persistent across reboots
- **Streaming** — MP3/AAC playback via I2S speaker
- **Settings** — WiFi config + volume control, saved to SD card
- **Volume** — Adjustable during playback (Fn + left/right)

## Hardware

- M5Stack Cardputer (ESP32-S3, StampS3)
- SD card (optional, for favorites + settings persistence)

## Build & Flash

```bash
# Clone
git clone <this-repo>
cd cardputer-radio

# Set WiFi defaults (or configure via device settings page)
export WIFI_SSID="your-ssid"
export WIFI_PASS="your-password"

# Build & flash
~/Library/Python/3.9/bin/pio run -t upload
```

If using a proxy (required in China):

```bash
export HTTP_PROXY="http://127.0.0.1:7897"
export HTTPS_PROXY="http://127.0.0.1:7897"
pio run -t upload
```

## Controls

| Key | Action |
|-----|--------|
| Fn + ↑↓ | Navigate menus/lists |
| Fn + ←→ | Volume down/up (during playback) |
| Enter | Select / Confirm |
| Tab | Back / Switch mode |
| Del / Backspace | Delete character (in text fields) |
| Direct keyboard | Type search query / WiFi credentials |

## Architecture

```
src/
├── main.cpp         # Entry point + state machine
├── config.h         # Pins, constants, state enum
├── player.h         # ESP32-audioI2S wrapper
├── radio_browser.h  # Radio Browser API client
├── favorites.h      # Favorites JSON CRUD (SD card)
├── settings.h       # Settings page (WiFi + volume)
└── display.h        # UI drawing functions
```

## Dependencies

- [M5Cardputer](https://github.com/m5stack/M5Cardputer) — Display, keyboard, speaker
- [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) — MP3/AAC streaming decoder
- [ArduinoJson](https://arduinojson.org/) — JSON parsing

## License

MIT
