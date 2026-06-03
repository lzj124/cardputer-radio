// Minimal test: pure WebRadio with PlatformIO
#include "M5Cardputer.h"
#include <Audio.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <SD.h>
#include <vector>

#define I2S_BCK  41
#define I2S_WS   43
#define I2S_DOUT 42
#define NVS_NS "M5_settings"
#define NVS_SSID "wifi_ssid"
#define NVS_PASS "wifi_pass"
#define MAX_STATIONS 20
#define MAX_NAME 30
#define MAX_URL 100

Audio audio;

struct Station { char name[MAX_NAME]; char url[MAX_URL]; };
Station stations[MAX_STATIONS];
size_t numStations = 0, curStation = 0;
uint16_t curVolume = 115;
const Station defStations[] PROGMEM = {
  {"SomaFM u80s","https://ice6.somafm.com/u80s-128-mp3"},
  {"SomaFM Metal","https://ice4.somafm.com/metal-128-mp3"},
};

void showVolume();
void showStation();
void Playfile();

void showVolume() {
  static uint8_t lv=255;
  if(curVolume==lv)return; lv=curVolume;
  M5Cardputer.Display.fillRect(0,6,200,6,TFT_BLACK);
  int w=map(curVolume,0,200,0,M5Cardputer.Display.width());
  if(w<200)M5Cardputer.Display.fillRect(0,6,w,4,0xAAFFAA);
}
void showStation() {
  M5Cardputer.Display.fillRect(0,15,240,35,TFT_BLACK);
  M5Cardputer.Display.drawString(stations[curStation].name,0,15);
  showVolume();
}
void Playfile() {
  audio.stopSong();
  audio.connecttohost(stations[curStation].url);
  showStation();
}
void audio_showstation(const char* info) {
  if(info&&*info){M5Cardputer.Display.fillRect(0,15,240,15,TFT_BLACK);char b[25];strncpy(b,info,24);b[24]=0;M5Cardputer.Display.drawString(b,0,15);}
}
void audio_showstreamtitle(const char* info) {
  if(info&&*info){M5Cardputer.Display.fillRect(0,33,240,12,TFT_BLACK);char b[25];strncpy(b,info,24);b[24]=0;M5Cardputer.Display.drawString(b,0,33);}
}

void loadSD() {
  if(!SD.begin()){numStations=2;memcpy(stations,defStations,sizeof(Station)*2);return;}
  File f=SD.open("/station_list.txt");
  if(!f){numStations=2;memcpy(stations,defStations,sizeof(Station)*2);return;}
  numStations=0;
  while(f.available()&&numStations<MAX_STATIONS){
    String l=f.readStringUntil('\n');int c=l.indexOf(',');
    if(c>0){String n=l.substring(0,c);n.trim();String u=l.substring(c+1);u.trim();
      if(n.length()&&u.length()){strncpy(stations[numStations].name,n.c_str(),MAX_NAME-1);strncpy(stations[numStations].url,u.c_str(),MAX_URL-1);numStations++;}}
  }
  f.close();
  if(!numStations){numStations=2;memcpy(stations,defStations,sizeof(Station)*2);}
}

// WiFi setup
String CFG_SSID, CFG_PASS;
Preferences pref;
uint32_t hh(const String& s){uint32_t h=5381;for(char c:s)h=((h<<5)+h)+c;return h;}

String inputT(const String& p,int x,int y){
  String d="> ";M5Cardputer.Display.setRotation(1);M5Cardputer.Display.drawString(p,x,y);
  while(1){M5Cardputer.update();if(M5Cardputer.Keyboard.isChange()&&M5Cardputer.Keyboard.isPressed()){
    auto s=M5Cardputer.Keyboard.keysState();for(auto i:s.word)d+=i;
    if(s.del&&d.length()>2)d.remove(d.length()-1);
    if(s.enter){d.remove(0,2);return d;}
    M5Cardputer.Display.fillRect(0,y-4,M5Cardputer.Display.width(),25,BLACK);M5Cardputer.Display.drawString(d,4,y);}
  delay(10);}
}

struct WNet{String ssid;int32_t r;wifi_auth_mode_t e;};
std::vector<WNet> nets;
String scanNet(){
  WiFi.scanDelete();WiFi.scanNetworks(true);M5Cardputer.Display.clear();M5Cardputer.Display.drawString("Scanning...",1,1);
  int16_t sr;do{sr=WiFi.scanComplete();delay(100);}while(sr==WIFI_SCAN_RUNNING);
  if(sr<=0){M5Cardputer.Display.drawString("No networks",1,15);delay(2000);return"";}
  nets.clear();for(int i=0;i<sr&&i<10;i++)if(WiFi.RSSI(i)>=-80)nets.push_back({WiFi.SSID(i),WiFi.RSSI(i),WiFi.encryptionType(i)});
  std::sort(nets.begin(),nets.end(),[](const WNet&a,const WNet&b){return a.r>b.r;});
  M5Cardputer.Display.clear();M5Cardputer.Display.drawString("Networks:",1,1);int sel=0;
  while(1){for(size_t i=0;i<nets.size();i++){String px=(i==sel)?"> ":"  ";M5Cardputer.Display.fillRect(1,18+i*18,240,18,BLACK);M5Cardputer.Display.drawString(px+nets[i].ssid,1,18+i*18);}
    M5Cardputer.Display.drawString("Enter:sel",1,108);M5Cardputer.update();
    if(M5Cardputer.Keyboard.isChange()&&M5Cardputer.Keyboard.isPressed()){if(M5Cardputer.Keyboard.isKeyPressed(';')&&sel>0)sel--;if(M5Cardputer.Keyboard.isKeyPressed('.')&&sel<(int)nets.size()-1)sel++;if(M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER))return nets[sel].ssid;}delay(10);}
}

void connectWiFi(){
  WiFi.mode(WIFI_STA);esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
  pref.begin(NVS_NS,true);CFG_SSID=pref.getString(NVS_SSID,"");CFG_PASS=pref.getString(NVS_PASS,"");
  uint32_t sh=pref.getUInt("sh",0),ph=pref.getUInt("ph",0);pref.end();
  bool ok=!CFG_SSID.isEmpty()&&hh(CFG_SSID)==sh&&hh(CFG_PASS)==ph;
  if(ok){WiFi.begin(CFG_SSID.c_str(),CFG_PASS.c_str());unsigned long t=millis();
    while(millis()-t<15000){if(WiFi.status()==WL_CONNECTED)return;delay(50);}
    M5Cardputer.Display.drawString("WiFi fail, rescan",1,80);delay(1000);}
  M5Cardputer.Display.clear();M5Cardputer.Display.drawString("WiFi Setup",1,1);
  CFG_SSID=scanNet();if(CFG_SSID.isEmpty())return;
  M5Cardputer.Display.clear();M5Cardputer.Display.drawString("SSID:"+CFG_SSID,1,20);M5Cardputer.Display.drawString("Pass:",1,38);
  CFG_PASS=inputT(">",4,M5Cardputer.Display.height()-24);
  pref.begin(NVS_NS,false);pref.putString(NVS_SSID,CFG_SSID);pref.putString(NVS_PASS,CFG_PASS);pref.putUInt("sh",hh(CFG_SSID));pref.putUInt("ph",hh(CFG_PASS));pref.end();
  WiFi.begin(CFG_SSID.c_str(),CFG_PASS.c_str());delay(500);
}

void setup() {
  auto cfg=M5.config();
  auto sc=M5Cardputer.Speaker.config();sc.sample_rate=128000;sc.task_pinned_core=APP_CPU_NUM;M5Cardputer.Speaker.config(sc);
  M5Cardputer.begin(cfg,true);

  M5Cardputer.Display.fillScreen(TFT_BLACK);M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.drawString("STEP1: M5 OK",2,2);delay(500);

  M5Cardputer.Display.drawString("STEP2: WiFi...",2,18);
  connectWiFi();
  M5Cardputer.Display.drawString("STEP2: WiFi DONE",2,18);delay(500);

  audio.setPinout(I2S_BCK,I2S_WS,I2S_DOUT);audio.setVolume(map(curVolume,0,255,0,21));
  M5Cardputer.Display.drawString("STEP3: Load SD...",2,34);delay(300);
  loadSD();
  M5Cardputer.Display.drawString("STEP3: SD DONE",2,34);
  M5Cardputer.Display.fillRect(0,50,240,10,TFT_BLACK);
  M5Cardputer.Display.printf("%d stations",numStations);delay(500);

  M5Cardputer.Display.drawString("STEP4: Play...",2,66);
  Playfile();
  M5Cardputer.Display.drawString("STEP4: Playing!",2,66);
}

void loop() {
  audio.loop();M5Cardputer.update();
  static unsigned long lb=0;
  if(M5Cardputer.Keyboard.isChange()&&(millis()-lb>200)){
    if(M5Cardputer.Keyboard.isKeyPressed(';')){if(curVolume<255){curVolume=min((uint16_t)(curVolume+10),(uint16_t)255);audio.setVolume(map(curVolume,0,255,0,21));showVolume();}}
    else if(M5Cardputer.Keyboard.isKeyPressed('.')){if(curVolume>0){curVolume=max((uint16_t)(curVolume-10),(uint16_t)0);audio.setVolume(map(curVolume,0,255,0,21));showVolume();}}
    else if(M5Cardputer.Keyboard.isKeyPressed('/')){if(numStations>0){curStation=(curStation+1)%numStations;Playfile();}}
    else if(M5Cardputer.Keyboard.isKeyPressed(',')){if(numStations>0){curStation=(curStation-1+numStations)%numStations;Playfile();}}
    lb=millis();
  }
  delay(1);
}
