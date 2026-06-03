/**
 * Cardputer Radio Plus
 * Based on M5Cardputer_WebRadio by Aurelio Avanzi (cyberwisk)
 * Extended with: Radio Browser API search + favorites
 *
 * Original: https://github.com/cyberwisk/M5Cardputer_WebRadio
 * Dependencies: M5Cardputer, ESP32-audioI2S, ArduinoJson
 */

#include "M5Cardputer.h"
#include <Audio.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <vector>

// ── Display ─────────────────────────────────────────────────
#define FFT_SIZE 256
#define WAVE_SIZE 320
static uint16_t prev_y[(FFT_SIZE / 2) + 1];
static uint16_t peak_y[(FFT_SIZE / 2) + 1];
static int header_height = 51;
static bool fft_enabled = false;
static bool fftSimON = true;

// ── I2S Pins ────────────────────────────────────────────────
#define I2S_BCK  41
#define I2S_WS   43
#define I2S_DOUT 42

// ── Audio ───────────────────────────────────────────────────
Audio audio;

// ── Station limits ──────────────────────────────────────────
#define MAX_STATIONS   50
#define MAX_NAME_LEN   64
#define MAX_URL_LEN    256
#define MAX_TAGS_LEN   64

// ── WiFi ────────────────────────────────────────────────────
#define NVS_NAMESPACE  "M5_settings"
#define NVS_SSID_KEY   "wifi_ssid"
#define NVS_PASS_KEY   "wifi_pass"
#define WIFI_TIMEOUT   20000
#define MIN_WIFI_RSSI  -80
#define MAX_NETWORKS   10
String CFG_WIFI_SSID;
String CFG_WIFI_PASS;
Preferences preferences;

// ── Radio Browser API ───────────────────────────────────────
#define RB_HOST        "de1.api.radio-browser.info"
#define RB_PORT        443
#define SEARCH_LIMIT   20

// ── State Machine ────────────────────────────────────────────
enum State { PLAYING, MENU, SEARCH_INPUT, SEARCH_RESULTS, SCANNING, FAVORITES };
State state = PLAYING;
State prevState = PLAYING;

// ── Station structures ──────────────────────────────────────
struct Station {
  char name[MAX_NAME_LEN];
  char url[MAX_URL_LEN];
  char tags[MAX_TAGS_LEN];
  int  bitrate = 0;
};

Station stations[MAX_STATIONS];
size_t numStations = 0;
size_t curStation = 0;
uint16_t curVolume = 115;
bool isMuted = false;
uint16_t prevVolume = 0;

// Search results
Station searchResults[MAX_STATIONS];
int searchCount = 0;
int searchSelected = 0;
int searchScroll = 0;

// Favorites
Station favorites[MAX_STATIONS];
int favCount = 0;
int favSelected = 0;
int favScroll = 0;

// Text input
String searchQuery = "";
int menuSel = 0;
bool stateChanged = true;
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 200;

// ── Forward declarations ────────────────────────────────────
void enterState(State s);
void showStation();
void showVolume();
void Playfile();
void loadFavorites();
void saveFavorites();
bool isFavorited(const char* url);
void drawPlaying();
void drawMenu();
void drawSearchInput();
void drawSearchResults();
void drawFavorites();

// ============================================================
// FFT CLASS (original WebRadio)
// ============================================================
class fft_t {
public:
  fft_t() { for (int i = 0; i < FFT_SIZE; i++) _data[i] = 0; }
  void exec(const int16_t* in) {
    if (fftSimON) for (int i = 0; i < FFT_SIZE; i++) _data[i] = abs(in[i]);
  }
  uint32_t get(size_t index) {
    return (index < FFT_SIZE) ? _data[index] : 0;
  }
private:
  uint32_t _data[FFT_SIZE];
};
static fft_t fft;
static int16_t raw_data[WAVE_SIZE * 2];

static uint32_t bgcolor(int y) {
  auto h = M5Cardputer.Display.height();
  auto dh = h - header_height;
  int v = ((h - y) << 5) / dh;
  if (dh > header_height) {
    int v2 = ((h - y - 1) << 5) / dh;
    if ((v >> 2) != (v2 >> 2)) return 0x666666u;
  }
  return M5Cardputer.Display.color888(v + 2, v, v + 6);
}

// ============================================================
// FFT FUNCTIONS
// ============================================================
void setupFFT() {
  if (!fft_enabled) return;
  for (int x = 0; x < (FFT_SIZE / 2) + 1; ++x) {
    prev_y[x] = INT16_MAX;
    peak_y[x] = INT16_MAX;
  }
  int dh = M5Cardputer.Display.height();
  for (int y = header_height; y < dh; ++y)
    M5Cardputer.Display.drawFastHLine(0, y, M5Cardputer.Display.width(), bgcolor(y));
}

void updateFFT() {
  if (!fft_enabled || state != PLAYING) return;
  static unsigned long lastFFTUpdate = 0;
  if (millis() - lastFFTUpdate < 50) return;
  lastFFTUpdate = millis();
  for (int i = 0; i < WAVE_SIZE * 2; i++) raw_data[i] = random(-32000, 32000);
  fft.exec(raw_data);
  size_t bw = M5Cardputer.Display.width() / 30;
  if (bw < 3) bw = 3;
  int32_t dsp_h = M5Cardputer.Display.height();
  int32_t ffth = dsp_h - header_height - 1;
  size_t xe = M5Cardputer.Display.width() / bw;
  if (xe > (FFT_SIZE / 2)) xe = (FFT_SIZE / 2);
  uint32_t bar_color[2] = {0x000033u, 0x99AAFFu};
  M5Cardputer.Display.startWrite();
  for (size_t bx = 0; bx <= xe; ++bx) {
    size_t x = bx * bw;
    int32_t f = fft.get(bx) * 3;
    int32_t y = (f * ffth) >> 17;
    if (y > ffth) y = ffth;
    y = dsp_h - y;
    int32_t py = prev_y[bx];
    if (y != py) {
      M5Cardputer.Display.fillRect(x, y, bw - 1, py - y, bar_color[(y < py)]);
      prev_y[bx] = y;
    }
    py = peak_y[bx] + ((peak_y[bx] - y) > 5 ? 2 : 1);
    if (py < y) M5Cardputer.Display.writeFastHLine(x, py - 1, bw - 1, bgcolor(py - 1));
    else py = y - 1;
    if (peak_y[bx] != py) {
      peak_y[bx] = py;
      M5Cardputer.Display.writeFastHLine(x, py, bw - 1, TFT_WHITE);
    }
  }
  M5Cardputer.Display.endWrite();
}

void toggleFFT() {
  fft_enabled = !fft_enabled;
  M5Cardputer.Display.fillRect(0, 51, 240, 89, TFT_BLACK);
  if (fft_enabled) setupFFT();
}

// ============================================================
// BATTERY DISPLAY
// ============================================================
void updateBatteryDisplay(unsigned long interval) {
  static unsigned long lastUpd = 0;
  if (millis() - lastUpd < interval) return;
  lastUpd = millis();
  int level = M5.Power.getBatteryLevel();
  uint16_t color = level < 30 ? TFT_RED : TFT_GREEN;
  M5Cardputer.Display.fillRect(215, 5, 40, 12, TFT_BLACK);
  M5Cardputer.Display.fillRect(215, 5, 20, 10, TFT_DARKGREY);
  M5Cardputer.Display.fillRect(235, 7, 3, 6, TFT_DARKGREY);
  M5Cardputer.Display.fillRect(217, 7, (level * 16) / 100, 6, color);
}

// ============================================================
// VOLUME CONTROL
// ============================================================
void volumeUp() {
  if (curVolume < 255) {
    curVolume = min((uint16_t)(curVolume + 10), (uint16_t)255);
    audio.setVolume(map(curVolume, 0, 255, 0, 21));
    showVolume();
  }
}
void volumeDown() {
  if (curVolume > 0) {
    curVolume = max((uint16_t)(curVolume - 10), (uint16_t)0);
    audio.setVolume(map(curVolume, 0, 255, 0, 21));
    showVolume();
  }
}
void volumeMute() {
  if (!isMuted) { prevVolume = curVolume; curVolume = 0; isMuted = true; }
  else { curVolume = prevVolume; isMuted = false; }
  audio.setVolume(map(curVolume, 0, 255, 0, 21));
  showVolume();
}

void showVolume() {
  static uint8_t lastVol = 255;
  if (curVolume == lastVol) return;
  lastVol = curVolume;
  M5Cardputer.Display.fillRect(0, 6, 200, 6, TFT_BLACK);
  int w = map(curVolume, 0, 200, 0, M5Cardputer.Display.width());
  if (w < 200) M5Cardputer.Display.fillRect(0, 6, w, 4, 0xAAFFAA);
}

// ============================================================
// STATION MANAGEMENT
// ============================================================
void stationUp() {
  if (numStations == 0) return;
  curStation = (curStation + 1) % numStations;
  Playfile();
}
void stationDown() {
  if (numStations == 0) return;
  curStation = (curStation - 1 + numStations) % numStations;
  Playfile();
}

void Playfile() {
  if (WiFi.status() != WL_CONNECTED) {
    M5Cardputer.Display.fillRect(0, 15, 240, 35, TFT_BLACK);
    M5Cardputer.Display.drawString("WiFi not connected!", 0, 15);
    return;
  }
  audio.stopSong();
  String url = String(stations[curStation].url);
  if (url.indexOf("http") != -1) audio.connecttohost(stations[curStation].url);
  else if (url.indexOf("/mp3") != -1) audio.connecttoFS(SD, stations[curStation].url);
  else audio.connecttospeech("Station not available.", "en");
  showStation();
}

void showStation() {
  fftSimON = false;
  M5Cardputer.Display.fillRect(0, 15, 240, 35, TFT_BLACK);
  M5Cardputer.Display.drawString(stations[curStation].name, 0, 15);
  showVolume();
}

// ============================================================
// STATION LOADING FROM SD
// ============================================================
void loadDefaultStations() {
  Station def[4];
  strncpy(def[0].name, "SomaFM u80s", MAX_NAME_LEN-1); strncpy(def[0].url, "https://ice6.somafm.com/u80s-128-mp3", MAX_URL_LEN-1); strncpy(def[0].tags, "80s,pop", MAX_TAGS_LEN-1); def[0].bitrate = 128;
  strncpy(def[1].name, "SomaFM Metal", MAX_NAME_LEN-1); strncpy(def[1].url, "https://ice4.somafm.com/metal-128-mp3", MAX_URL_LEN-1); strncpy(def[1].tags, "metal,rock", MAX_TAGS_LEN-1); def[1].bitrate = 128;
  strncpy(def[2].name, "181.fm Beatles", MAX_NAME_LEN-1); strncpy(def[2].url, "https://listen.181fm.com/181-beatles_128k.mp3", MAX_URL_LEN-1); strncpy(def[2].tags, "beatles,classic", MAX_TAGS_LEN-1); def[2].bitrate = 128;
  strncpy(def[3].name, "GTA Classics", MAX_NAME_LEN-1); strncpy(def[3].url, "https://gta-classics.stream.laut.fm/gta-classics", MAX_URL_LEN-1); strncpy(def[3].tags, "game,rock", MAX_TAGS_LEN-1); def[3].bitrate = 128;
  numStations = min((size_t)4, (size_t)MAX_STATIONS);
  memcpy(stations, def, sizeof(Station) * numStations);
}

void loadStationsFromSD() {
  if (!SD.begin()) { loadDefaultStations(); return; }
  File f = SD.open("/station_list.txt");
  if (!f) { loadDefaultStations(); return; }
  numStations = 0;
  while (f.available() && numStations < MAX_STATIONS) {
    String line = f.readStringUntil('\n');
    int comma = line.indexOf(',');
    if (comma > 0) {
      String name = line.substring(0, comma); name.trim();
      String url  = line.substring(comma + 1); url.trim();
      if (name.length() > 0 && url.length() > 0) {
        strncpy(stations[numStations].name, name.c_str(), MAX_NAME_LEN - 1);
        strncpy(stations[numStations].url,  url.c_str(),  MAX_URL_LEN - 1);
        stations[numStations].tags[0] = '\0';
        stations[numStations].bitrate = 0;
        numStations++;
      }
    }
  }
  f.close();
  if (numStations == 0) loadDefaultStations();
}

// ============================================================
// FAVORITES (SD JSON)
// ============================================================
void loadFavorites() {
  favCount = 0;
  if (!SD.exists("/fav.json")) return;
  File f = SD.open("/fav.json");
  if (!f) return;
  size_t sz = f.size();
  if (sz > 8192) { f.close(); return; }
  char* buf = (char*)malloc(sz + 1);
  if (!buf) { f.close(); return; }
  f.readBytes(buf, sz);
  buf[sz] = '\0';
  f.close();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, buf, sz);
  free(buf);
  if (err) return;
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject item : arr) {
    if (favCount >= MAX_STATIONS) break;
    const char* n = item["name"]; const char* u = item["url"];
    if (!n || !u) continue;
    strncpy(favorites[favCount].name, n, MAX_NAME_LEN - 1);
    strncpy(favorites[favCount].url,  u, MAX_URL_LEN - 1);
    const char* t = item["tags"]; if (t) strncpy(favorites[favCount].tags, t, MAX_TAGS_LEN - 1);
    else favorites[favCount].tags[0] = '\0';
    favorites[favCount].bitrate = item["bitrate"] | 0;
    favCount++;
  }
}

void saveFavorites() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < favCount; i++) {
    JsonObject item = arr.add<JsonObject>();
    item["name"] = favorites[i].name;
    item["url"]  = favorites[i].url;
    if (favorites[i].tags[0]) item["tags"] = favorites[i].tags;
    item["bitrate"] = favorites[i].bitrate;
  }
  SD.remove("/fav.json");
  File f = SD.open("/fav.json", FILE_WRITE);
  if (!f) return;
  size_t len = measureJson(doc);
  char* buf = (char*)malloc(len + 1);
  if (buf) { serializeJson(doc, buf, len + 1); f.write((uint8_t*)buf, len); free(buf); }
  f.close();
}

bool isFavorited(const char* url) {
  for (int i = 0; i < favCount; i++)
    if (strcmp(favorites[i].url, url) == 0) return true;
  return false;
}

void addFavorite(const Station& s) {
  if (favCount >= MAX_STATIONS || isFavorited(s.url)) return;
  favorites[favCount] = s;
  favCount++;
  saveFavorites();
}

void removeFavorite(int idx) {
  if (idx < 0 || idx >= favCount) return;
  for (int i = idx; i < favCount - 1; i++) favorites[i] = favorites[i + 1];
  favCount--;
  saveFavorites();
}

// ============================================================
// RADIO BROWSER API SEARCH
// ============================================================
void searchRadioBrowser(const String& query) {
  searchCount = 0;
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.drawString("Searching...", 4, 20);
  M5Cardputer.Display.drawString(query, 4, 36);

  // URL-encode query
  String encoded = "";
  for (int i = 0; i < query.length(); i++) {
    char c = query[i];
    if (c == ' ') encoded += "%20";
    else if (isalnum(c) || c == '-' || c == '_' || c == '.') encoded += c;
    else { char h[4]; snprintf(h, 4, "%%%02X", c); encoded += h; }
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10);
  if (!client.connect(RB_HOST, RB_PORT)) {
    M5Cardputer.Display.drawString("Connect failed", 4, 52);
    delay(1500);
    return;
  }

  String path = "/json/stations/search?name=" + encoded + "&limit=" + String(SEARCH_LIMIT) + "&hidebroken=true";
  client.print("GET " + path + " HTTP/1.1\r\nHost: " RB_HOST "\r\nConnection: close\r\n\r\n");

  String body = "";
  bool headDone = false;
  unsigned long t0 = millis();
  while (millis() - t0 < 15000) {
    if (!client.connected() && !client.available()) break;
    while (client.available()) {
      String line = client.readStringUntil('\n');
      if (!headDone) {
        if (line == "\r" || line.length() <= 1) headDone = true;
      } else body += line;
    }
  }
  client.stop();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.drawString("Parse error", 4, 20);
    delay(1500);
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject item : arr) {
    if (searchCount >= MAX_STATIONS) break;
    const char* name = item["name"];
    const char* url  = item["url"];
    if (!name || !url) continue;
    Station& s = searchResults[searchCount];
    strncpy(s.name, name, MAX_NAME_LEN - 1); s.name[MAX_NAME_LEN - 1] = '\0';
    strncpy(s.url,  url,  MAX_URL_LEN - 1);  s.url[MAX_URL_LEN - 1]   = '\0';
    const char* tags = item["tags"];
    if (tags) { strncpy(s.tags, tags, MAX_TAGS_LEN - 1); s.tags[MAX_TAGS_LEN - 1] = '\0'; }
    else s.tags[0] = '\0';
    s.bitrate = item["bitrate"] | 0;
    searchCount++;
  }

  searchSelected = 0;
  searchScroll = 0;
  enterState(SEARCH_RESULTS);
}

// ============================================================
// WIFI SETUP (from CardWifiSetup.h)
// ============================================================
struct WiFiNet { String ssid; int32_t rssi; wifi_auth_mode_t enc; };
std::vector<WiFiNet> networks;
uint32_t calcHash(const String& s) {
  uint32_t h = 5381; for (char c : s) h = ((h << 5) + h) + c; return h;
}

String inputText(const String& prompt, int x, int y, bool isPwd = false) {
  String data = "> ", displayData = "> ";
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextScroll(true);
  M5Cardputer.Display.drawString(prompt, x, y);
  while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      auto status = M5Cardputer.Keyboard.keysState();
      for (auto i : status.word) { data += i; displayData += i; }
      if (status.del && data.length() > 2) { data.remove(data.length() - 1); displayData.remove(displayData.length() - 1); }
      if (status.enter) { data.remove(0, 2); return data; }
      M5Cardputer.Display.fillRect(0, y - 4, M5Cardputer.Display.width(), 25, BLACK);
      M5Cardputer.Display.drawString(displayData, 4, y);
    }
    delay(10);
  }
}

String scanAndSelectNetwork() {
  WiFi.scanDelete(); WiFi.scanNetworks(true);
  M5Cardputer.Display.clear(); M5Cardputer.Display.drawString("Scanning...", 1, 1);
  int16_t sr;
  do { sr = WiFi.scanComplete(); delay(100); } while (sr == WIFI_SCAN_RUNNING);
  if (sr <= 0) { M5Cardputer.Display.drawString("No networks", 1, 15); delay(2000); return ""; }
  networks.clear();
  for (int i = 0; i < sr && i < MAX_NETWORKS; i++)
    if (WiFi.RSSI(i) >= MIN_WIFI_RSSI)
      networks.push_back({WiFi.SSID(i), WiFi.RSSI(i), WiFi.encryptionType(i)});
  std::sort(networks.begin(), networks.end(), [](const WiFiNet& a, const WiFiNet& b) { return a.rssi > b.rssi; });
  M5Cardputer.Display.clear(); M5Cardputer.Display.drawString("Networks:", 1, 1);
  int sel = 0;
  while (true) {
    for (size_t i = 0; i < networks.size(); i++) {
      String prefix = (i == sel) ? "-> " : "   ";
      String sec = (networks[i].enc != WIFI_AUTH_OPEN) ? " *" : "";
      M5Cardputer.Display.fillRect(1, 18 + i * 18, 240, 18, BLACK);
      M5Cardputer.Display.drawString(prefix + networks[i].ssid + sec, 1, 18 + i * 18);
    }
    M5Cardputer.Display.drawString("Enter:select", 1, 108);
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      if (M5Cardputer.Keyboard.isKeyPressed(';') && sel > 0) sel--;
      if (M5Cardputer.Keyboard.isKeyPressed('.') && sel < (int)networks.size() - 1) sel++;
      if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) return networks[sel].ssid;
    }
    delay(10);
  }
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA); esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
  preferences.begin(NVS_NAMESPACE, true);
  CFG_WIFI_SSID = preferences.getString(NVS_SSID_KEY, "");
  CFG_WIFI_PASS = preferences.getString(NVS_PASS_KEY, "");
  uint32_t sh = preferences.getUInt("ssid_hash", 0), ph = preferences.getUInt("pass_hash", 0);
  preferences.end();
  bool valid = !CFG_WIFI_SSID.isEmpty() && calcHash(CFG_WIFI_SSID) == sh && calcHash(CFG_WIFI_PASS) == ph;
  if (valid) {
    WiFi.begin(CFG_WIFI_SSID.c_str(), CFG_WIFI_PASS.c_str());
    M5Cardputer.Display.print("Connecting");
    M5Cardputer.Display.drawString("BtnA:forget", 2, 100);
    unsigned long t0 = millis();
    while (millis() - t0 < WIFI_TIMEOUT) {
      M5Cardputer.update();
      if (M5Cardputer.BtnA.isPressed()) { preferences.begin(NVS_NAMESPACE, false); preferences.clear(); preferences.end(); M5Cardputer.Display.clear(); M5Cardputer.Display.drawString("Forgotten.", 1, 60); delay(1000); ESP.restart(); }
      if (WiFi.status() == WL_CONNECTED) { M5Cardputer.Display.fillRect(0, 20, 240, 135, BLACK); M5Cardputer.Display.drawString("WiFi OK: " + WiFi.localIP().toString(), 4, 4); delay(1500); return; }
      M5Cardputer.Display.print("."); delay(50);
    }
  }
  M5Cardputer.Display.clear(); M5Cardputer.Display.drawString("WiFi Setup", 1, 1);
  CFG_WIFI_SSID = scanAndSelectNetwork();
  if (CFG_WIFI_SSID.isEmpty()) return;
  M5Cardputer.Display.clear(); M5Cardputer.Display.drawString("SSID: " + CFG_WIFI_SSID, 1, 20);
  M5Cardputer.Display.drawString("Password:", 1, 38);
  CFG_WIFI_PASS = inputText("> ", 4, M5Cardputer.Display.height() - 24, true);
  preferences.begin(NVS_NAMESPACE, false);
  preferences.putString(NVS_SSID_KEY, CFG_WIFI_SSID); preferences.putString(NVS_PASS_KEY, CFG_WIFI_PASS);
  preferences.putUInt("ssid_hash", calcHash(CFG_WIFI_SSID)); preferences.putUInt("pass_hash", calcHash(CFG_WIFI_PASS));
  preferences.end();
  WiFi.begin(CFG_WIFI_SSID.c_str(), CFG_WIFI_PASS.c_str());
  delay(500 + 300);
  M5Cardputer.Display.drawString("WiFi OK: " + WiFi.localIP().toString(), 1, 50);
  delay(1500);
}

// ============================================================
// STATE MACHINE
// ============================================================
void enterState(State s) {
  prevState = state; state = s; stateChanged = true;
}

// ============================================================
// DISPLAY DRAWING
// ============================================================
void drawPlaying() {
  // Original WebRadio playing screen with station info + FFT area
  showStation();
  if (fft_enabled && !stateChanged) return;
}

void drawMenu() {
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.drawString("Menu", 100, 2);
  const char* items[] = {"Search Radio Browser", "Favorites", "Load SD stations", "Back to Radio"};
  for (int i = 0; i < 4; i++) {
    int y = 20 + i * 22;
    if (i == menuSel) {
      M5Cardputer.Display.fillRoundRect(4, y - 2, 232, 18, 4, TFT_BLUE);
      M5Cardputer.Display.setTextColor(TFT_WHITE);
    } else {
      M5Cardputer.Display.setTextColor(TFT_CYAN);
    }
    M5Cardputer.Display.drawString(items[i], 8, y);
  }
  M5Cardputer.Display.setTextColor(0x8410);
  M5Cardputer.Display.drawString("Fn+^v:nav  Enter:sel  Tab:back", 4, 120);
  stateChanged = false;
}

void drawSearchInput() {
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.drawString("Search:", 4, 10);
  M5Cardputer.Display.fillRect(2, 24, 236, 16, TFT_NAVY);
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.drawString(searchQuery, 6, 26);
  if ((millis() / 500) % 2 == 0) {
    int cx = 6 + searchQuery.length() * 6;
    M5Cardputer.Display.drawFastVLine(cx, 26, 10, TFT_WHITE);
  }
  M5Cardputer.Display.setTextColor(0x8410);
  M5Cardputer.Display.drawString("Enter:search  Tab:cancel", 4, 50);
  M5Cardputer.Display.drawString("Del:delete", 4, 62);
  stateChanged = false;
}

void drawSearchResults() {
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.drawString("Results (" + String(searchCount) + ")", 4, 2);
  int perPage = 6;
  for (int i = 0; i < perPage; i++) {
    int idx = searchScroll + i;
    if (idx >= searchCount) break;
    int y = 14 + i * 18;
    bool sel = (idx == searchSelected);
    if (sel) M5Cardputer.Display.fillRoundRect(2, y - 1, 236, 16, 3, TFT_BLUE);
    M5Cardputer.Display.setTextColor(sel ? TFT_WHITE : TFT_GREEN);
    String name = String(searchResults[idx].name).substring(0, 26);
    if (isFavorited(searchResults[idx].url)) name = "*" + name;
    M5Cardputer.Display.drawString(name, 4, y);
    if (searchResults[idx].bitrate > 0) {
      M5Cardputer.Display.setTextColor(0x8410);
      M5Cardputer.Display.drawString(String(searchResults[idx].bitrate) + "k", 190, y);
    }
  }
  M5Cardputer.Display.setTextColor(0x8410);
  M5Cardputer.Display.drawString("Fn+^v:nav  Enter:play  Tab:back", 4, 122);
  stateChanged = false;
}

void drawFavorites() {
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.drawString("Favorites (" + String(favCount) + ")", 4, 2);
  if (favCount == 0) {
    M5Cardputer.Display.drawString("No favorites yet", 4, 40);
    M5Cardputer.Display.drawString("Tab:back", 4, 120);
    return;
  }
  int perPage = 6;
  for (int i = 0; i < perPage; i++) {
    int idx = favScroll + i;
    if (idx >= favCount) break;
    int y = 14 + i * 18;
    bool sel = (idx == favSelected);
    if (sel) M5Cardputer.Display.fillRoundRect(2, y - 1, 236, 16, 3, TFT_BLUE);
    M5Cardputer.Display.setTextColor(sel ? TFT_WHITE : TFT_GREEN);
    M5Cardputer.Display.drawString(String(favorites[idx].name).substring(0, 28), 4, y);
  }
  M5Cardputer.Display.setTextColor(0x8410);
  M5Cardputer.Display.drawString("Fn+^v:nav  Enter:play  Del:remove", 4, 114);
  M5Cardputer.Display.drawString("Tab:back", 4, 126);
  stateChanged = false;
}

// ============================================================
// AUDIO CALLBACKS
// ============================================================
void audio_info(const char* info) { /* silent */ }
void audio_id3data(const char* info) {
  M5Cardputer.Display.fillRect(0, 33, 240, 12, TFT_BLACK);
  M5Cardputer.Display.drawString(info, 0, 33);
}
void audio_showstation(const char* info) {
  if (info && *info) {
    M5Cardputer.Display.fillRect(0, 15, 240, 15, TFT_BLACK);
    char buf[25]; strncpy(buf, info, 24); buf[24] = '\0';
    M5Cardputer.Display.drawString(buf, 0, 15);
    fftSimON = true;
  }
}
void audio_showstreamtitle(const char* info) {
  if (info && *info) {
    M5Cardputer.Display.fillRect(0, 33, 240, 12, TFT_BLACK);
    char buf[25]; strncpy(buf, info, 24); buf[24] = '\0';
    M5Cardputer.Display.drawString(buf, 0, 33);
    fftSimON = true;
  }
}
void audio_eof_mp3(const char* info) { /* stream ended */ }

// ============================================================
// SETUP
// ============================================================
void setup() {
  auto cfg = M5.config();
  auto spk_cfg = M5Cardputer.Speaker.config();
  spk_cfg.sample_rate = 128000;
  spk_cfg.task_pinned_core = APP_CPU_NUM;
  M5Cardputer.Speaker.config(spk_cfg);
  M5Cardputer.begin(cfg, true);

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setFont(&fonts::FreeMonoOblique9pt7b);

  connectToWiFi();

  audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT);
  audio.setVolume(map(curVolume, 0, 255, 0, 21));
  audio.setBalance(0);

  M5Cardputer.Display.fillScreen(BLACK);
  loadStationsFromSD();
  loadFavorites();

  curStation = 0;
  if (numStations > 0) Playfile();
  toggleFFT();
  enterState(PLAYING);
}

// ============================================================
// STATE HANDLERS
// ============================================================
void handlePlaying() {
  if (stateChanged) { drawPlaying(); stateChanged = false; }
  updateBatteryDisplay(5000);
  updateFFT();
}

void handleMenu() {
  if (stateChanged) { menuSel = 0; drawMenu(); }
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && (millis() - lastButtonPress > DEBOUNCE_DELAY)) {
    if (M5Cardputer.Keyboard.isKeyPressed(';') && menuSel > 0) { menuSel--; drawMenu(); }
    if (M5Cardputer.Keyboard.isKeyPressed('.') && menuSel < 3) { menuSel++; drawMenu(); }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
      switch (menuSel) {
        case 0: searchQuery = ""; enterState(SEARCH_INPUT); break;
        case 1: favSelected = 0; favScroll = 0; enterState(FAVORITES); break;
        case 2: loadStationsFromSD(); curStation = 0; if (numStations > 0) Playfile(); enterState(PLAYING); break;
        case 3: enterState(PLAYING); break;
      }
    }
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) enterState(PLAYING);
    lastButtonPress = millis();
  }
}

void handleSearchInput() {
  if (stateChanged) { drawSearchInput(); }
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && (millis() - lastButtonPress > DEBOUNCE_DELAY)) {
    auto& kbd = M5Cardputer.Keyboard;
    if (kbd.isKeyPressed(KEY_TAB)) enterState(MENU);
    else if (kbd.isKeyPressed(KEY_ENTER) && searchQuery.length() > 0) searchRadioBrowser(searchQuery);
    else if (kbd.isKeyPressed(KEY_BACKSPACE) && searchQuery.length() > 0) { searchQuery.remove(searchQuery.length() - 1); drawSearchInput(); }
    else {
      auto status = kbd.keysState();
      for (auto i : status.word) { if (searchQuery.length() < 30) searchQuery += i; }
      if (status.word.size() > 0) drawSearchInput();
    }
    lastButtonPress = millis();
  }
  if ((millis() / 500) % 2 == 0 && !stateChanged) drawSearchInput();
}

void handleSearchResults() {
  if (stateChanged) { drawSearchResults(); }
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && (millis() - lastButtonPress > DEBOUNCE_DELAY)) {
    auto& kbd = M5Cardputer.Keyboard;
    if (kbd.isKeyPressed(KEY_TAB)) enterState(MENU);
    if (kbd.isKeyPressed(';') && searchSelected > 0) { searchSelected--; if (searchSelected < searchScroll) searchScroll = searchSelected; drawSearchResults(); }
    if (kbd.isKeyPressed('.') && searchSelected < searchCount - 1) { searchSelected++; if (searchSelected >= searchScroll + 6) searchScroll = searchSelected - 5; drawSearchResults(); }
    if (kbd.isKeyPressed(KEY_ENTER) && searchCount > 0) {
      // Copy to stations list and play
      if (numStations < MAX_STATIONS) {
        stations[numStations] = searchResults[searchSelected];
        curStation = numStations;
        numStations++;
      } else {
        stations[0] = searchResults[searchSelected];
        curStation = 0;
      }
      Playfile();
      enterState(PLAYING);
    }
    lastButtonPress = millis();
  }
}

void handleFavorites() {
  if (stateChanged) { drawFavorites(); }
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && (millis() - lastButtonPress > DEBOUNCE_DELAY)) {
    auto& kbd = M5Cardputer.Keyboard;
    if (kbd.isKeyPressed(KEY_TAB)) enterState(MENU);
    if (kbd.isKeyPressed(';') && favSelected > 0) { favSelected--; if (favSelected < favScroll) favScroll = favSelected; drawFavorites(); }
    if (kbd.isKeyPressed('.') && favSelected < favCount - 1) { favSelected++; if (favSelected >= favScroll + 6) favScroll = favSelected - 5; drawFavorites(); }
    if (kbd.isKeyPressed(KEY_BACKSPACE) && favCount > 0) {
      removeFavorite(favSelected);
      if (favSelected >= favCount && favCount > 0) favSelected = favCount - 1;
      drawFavorites();
    }
    if (kbd.isKeyPressed(KEY_ENTER) && favCount > 0) {
      if (numStations < MAX_STATIONS) {
        stations[numStations] = favorites[favSelected];
        curStation = numStations;
        numStations++;
      } else {
        stations[0] = favorites[favSelected];
        curStation = 0;
      }
      Playfile();
      enterState(PLAYING);
    }
    lastButtonPress = millis();
  }
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  audio.loop();
  M5Cardputer.update();

  // Global Tab key: open menu from PLAYING
  if (state == PLAYING) {
    if (M5Cardputer.Keyboard.isChange() && (millis() - lastButtonPress > DEBOUNCE_DELAY)) {
      if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) { enterState(MENU); }
      else if (M5Cardputer.Keyboard.isKeyPressed(';')) volumeUp();
      else if (M5Cardputer.Keyboard.isKeyPressed('.')) volumeDown();
      else if (M5Cardputer.Keyboard.isKeyPressed('m')) volumeMute();
      else if (M5Cardputer.Keyboard.isKeyPressed('/')) stationUp();
      else if (M5Cardputer.Keyboard.isKeyPressed(',')) stationDown();
      else if (M5Cardputer.Keyboard.isKeyPressed('r')) { audio.stopSong(); audio.connecttohost(stations[curStation].url); }
      else if (M5Cardputer.Keyboard.isKeyPressed('f')) toggleFFT();
      else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) { addFavorite(stations[curStation]); }
      lastButtonPress = millis();
    }
    handlePlaying();
  }
  else if (state == MENU) handleMenu();
  else if (state == SEARCH_INPUT) handleSearchInput();
  else if (state == SEARCH_RESULTS) handleSearchResults();
  else if (state == FAVORITES) handleFavorites();

  delay(1);
}
