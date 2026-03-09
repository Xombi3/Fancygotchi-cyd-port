/*
 * FancyGotchi CYD v2.0 — Standalone WiFi Handshake Capture
 * ESP32-2432S028R  |  ST7789 320x240  |  XPT2046 touch  |  microSD
 *
 * This device IS the pwnagotchi. No Raspberry Pi needed.
 *
 * What it does:
 *   - Puts ESP32 WiFi into promiscuous (monitor) mode
 *   - Hops all 13 channels automatically
 *   - Passively captures WPA2 EAPOL handshake packets
 *   - Captures PMKID packets (clientless, no deauth needed)
 *   - Saves captures as .pcap to SD card (Wireshark / hashcat compatible)
 *   - Shows pwnagotchi-style face, mood and live stats on screen
 *
 * Touch zones:
 *   Top-LEFT    = cycle theme
 *   Top-RIGHT   = cycle face pack
 *   Bottom-LEFT = toggle web UI (http://192.168.4.1)
 *   Bottom-RIGHT= toggle deauth on/off
 *
 * After capture, crack offline on PC:
 *   hcxpcapngtool -o hash.hc22000 /handshakes/*.pcap
 *   hashcat -m 22000 hash.hc22000 wordlist.txt
 *
 * Credits:
 *   Pwnagotchi concept  — @evilsocket  https://github.com/evilsocket/pwnagotchi
 *   Fancygotchi UI      — @V0r-T3x    https://github.com/V0r-T3x/Fancygotchi
 *   Hash Monster method — @G4lile0    https://github.com/G4lile0/ESP32-WiFi-Hash-Monster
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <SPIFFS.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

#include "config.h"
#include "theme.h"
#include "faces.h"
#include "font.h"
#include "sd_pcap.h"
#include "wifi_capture.h"
#include "touch_calibrate.h"
#include "web_ui.h"

// ── Screen ────────────────────────────────────────────────────────
#define SCR_W  320
#define SCR_H  240

// ── Layout ────────────────────────────────────────────────────────
#define TOPBAR_H   22
#define BOTBAR_Y  202
#define DIVIDER_X 198

// Sprite regions
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

// ── Globals ───────────────────────────────────────────────────────
TFT_eSPI    tft;
TFT_eSprite faceSpr(&tft);   // face only (small — fits in heap)
bool        fontLoaded = false;

SPIClass    touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

unsigned long lastDraw=0, lastAnim=0;
bool redrawAll = true;

// ── Face / mood system ────────────────────────────────────────────
// Mood is derived from capture activity, just like real pwnagotchi
enum Mood { MOOD_IDLE, MOOD_SCANNING, MOOD_EXCITED, MOOD_HAPPY,
            MOOD_INTENSE, MOOD_BORED, MOOD_SLEEP };

// Face arrays live in faces.h — edit or add packs there.
// Switch packs at runtime: tap TOP-RIGHT on screen.

static const char* MOODS_STR[] = {
  "idle", "scanning", "excited!", "happy", "intense", "bored", "sleepy"
};

static const char* STATUS_MSGS[][3] = {
  { "waiting...",        "nothing yet",      "all quiet"          },  // idle
  { "scanning...",       "sniffing air",     "watching channels"  },  // scan
  { "got one!!!",        "handshake!!!",     "EAPOL captured!"    },  // excited
  { "pwned it :)",       "nice catch",       "saved to SD"        },  // happy
  { "many APs around",   "busy out here",    "lots of traffic"    },  // intense
  { "slow day...",       "nothing happening","where's the WiFi?"  },  // bored
  { "zzz...",            "napping",          "wake me up"         },  // sleep
};

char     displayFace[48]  = "(^‿‿^)";
char     displayStatus[48]= "booting...";
char     displayMood[16]  = "idle";
uint8_t  moodFrameIdx     = 0;
Mood     currentMood      = MOOD_IDLE;
uint32_t lastMoodChange   = 0;
uint32_t lastCapture      = 0;
uint32_t bootTime         = 0;

// Value caches — only redraw when value changes
char     dFace[48]   = "";
char     dStatus[48] = "";
char     dMood[16]   = "";
uint32_t dAPs=0xFFFFFFFF, dEapol=0xFFFFFFFF, dPmkid=0xFFFFFFFF;
uint32_t dPwned=0xFFFFFFFF, dPkts=0xFFFFFFFF, dCh=0xFFFFFFFF;
bool     dSdReady    = false;
char     dSdStatus[32] = "";
uint8_t  dTheme=0xFF;
uint8_t  dFaceSet=0xFF;

// Touch debounce
uint32_t lastTouchMs = 0;

// Deauth toggle — on by default
bool deauthEnabled = true;

// ── LED ───────────────────────────────────────────────────────────
void ledRGB(bool r,bool g,bool b) {
  digitalWrite(PIN_LED_R,!r);
  digitalWrite(PIN_LED_G,!g);
  digitalWrite(PIN_LED_B,!b);
}

void ledFlash(bool r,bool g,bool b,uint16_t ms=80) {
  ledRGB(r,g,b); delay(ms); ledRGB(false,false,false);
}

// ── Mood engine ───────────────────────────────────────────────────
void updateMood() {
  uint32_t now     = millis();
  uint32_t uptime  = (now - bootTime) / 1000;
  uint32_t eapol   = captureEapol();
  uint32_t pkts    = capturePackets();
  uint8_t  aps     = captureApCount();

  Mood newMood;
  if (uptime < 5) {
    newMood = MOOD_IDLE;
  } else if (now - lastCapture < 3000 && eapol > 0) {
    newMood = MOOD_EXCITED;  // just captured something
  } else if (now - lastCapture < 15000 && eapol > 0) {
    newMood = MOOD_HAPPY;    // captured recently
  } else if (pkts > 500 && aps > 8) {
    newMood = MOOD_INTENSE;  // lots happening
  } else if (aps > 0 && pkts > 50) {
    newMood = MOOD_SCANNING; // actively seeing traffic
  } else if (uptime > 120 && aps == 0) {
    newMood = MOOD_SLEEP;    // been running a while, nothing seen
  } else if (uptime > 60 && eapol == 0) {
    newMood = MOOD_BORED;    // nothing captured
  } else {
    newMood = MOOD_SCANNING;
  }

  if (newMood != currentMood || now - lastMoodChange > 8000) {
    currentMood = newMood;
    lastMoodChange = now;
    moodFrameIdx = (moodFrameIdx + 1) % 3;

    const char** faces;
    switch (currentMood) {
      case MOOD_IDLE:     faces = (const char**)activeFaceSet()->idle;    break;
      case MOOD_SCANNING: faces = (const char**)activeFaceSet()->scan;    break;
      case MOOD_EXCITED:  faces = (const char**)activeFaceSet()->excited; break;
      case MOOD_HAPPY:    faces = (const char**)activeFaceSet()->happy;   break;
      case MOOD_INTENSE:  faces = (const char**)activeFaceSet()->intense; break;
      case MOOD_BORED:    faces = (const char**)activeFaceSet()->bored;   break;
      case MOOD_SLEEP:    faces = (const char**)activeFaceSet()->sleep;   break;
      default:            faces = (const char**)activeFaceSet()->scan;    break;
    }
    strlcpy(displayFace,  faces[moodFrameIdx],               sizeof(displayFace));
    strlcpy(displayStatus,STATUS_MSGS[currentMood][moodFrameIdx], sizeof(displayStatus));
    strlcpy(displayMood,  MOODS_STR[currentMood],            sizeof(displayMood));
  }
}

// Mood bar values (0.0–1.0) derived from capture activity
float moodExcited() {
  uint32_t age = (millis() - lastCapture) / 1000;
  return constrain(1.0f - age/30.0f, 0.0f, 1.0f);
}
float moodBored() {
  uint32_t age = (millis() - lastCapture) / 1000;
  return constrain(age/120.0f, 0.0f, 1.0f);
}
float moodTired() {
  float u = (millis()-bootTime) / 3600000.0f;  // hours
  return constrain(u, 0.0f, 1.0f);
}
float moodHopeful() {
  return constrain((float)captureApCount() / 20.0f, 0.0f, 1.0f);
}

// ── Helpers ───────────────────────────────────────────────────────
// Draw progress bar into a sprite
void hbarSpr(TFT_eSprite& spr, int16_t x,int16_t y,int16_t w,int16_t h,
             float v,uint16_t fc,uint16_t bc) {
  v=constrain(v,0.0f,1.0f);
  int16_t f=(int16_t)(v*w);
  if(f>0) spr.fillRect(x,  y,f,  h,fc);
  if(f<w) spr.fillRect(x+f,y,w-f,h,bc);
}

// ── Chrome (full redraw) ──────────────────────────────────────────
void drawChrome() {
  const ThemeColors& c = activeTheme()->c;
  tft.fillScreen(c.bg);

  // Top bar
  tft.fillRect(0,0,SCR_W,TOPBAR_H,c.panel);
  tft.drawFastHLine(0,TOPBAR_H,SCR_W,c.accent);
  tft.setTextFont(FONT_TOPBAR); tft.setTextSize(FONT_TOPBAR_SIZE); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(c.accent,c.panel);
  tft.drawString("FancyGotchi",4,4);
  tft.setTextColor(c.ok,c.panel);
  tft.drawString("[CYD]",96,4);

  // Divider + right panel
  tft.drawFastVLine(DIVIDER_X,TOPBAR_H+1,BOTBAR_Y-TOPBAR_H-1,c.accent);
  tft.fillRect(DIVIDER_X+1,TOPBAR_H+1,SCR_W-DIVIDER_X-1,BOTBAR_Y-TOPBAR_H-1,c.panel);

  // Widget labels
  tft.setTextFont(FONT_STATS); tft.setTextSize(FONT_STATS_SIZE); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(c.textDim,c.panel);
  tft.drawString("APs",   WID_X, WID_Y);
  tft.drawString("EAPOL", WID_X, WID_Y+WID_SP);
  tft.drawString("PMKID", WID_X, WID_Y+WID_SP*2);
  tft.drawString("Pwned", WID_X, WID_Y+WID_SP*3);
  int16_t dY = WID_Y+WID_SP*4+2;
  tft.drawFastHLine(WID_X,dY,SCR_W-WID_X-2,c.accent);
  tft.setTextFont(FONT_LABEL); tft.setTextDatum(TC_DATUM);
  tft.setTextColor(c.accent,c.panel);
  char tb[22]; snprintf(tb,22,"<%s>",activeThemeName());
  tft.drawString(tb,(DIVIDER_X+SCR_W)/2,dY+4);
  char fb[22]; snprintf(fb,22,"{%s}",activeFaceSetName());
  tft.drawString(fb,(DIVIDER_X+SCR_W)/2,dY+13);
  tft.setTextColor(c.textDim,c.panel);
  tft.drawString("^L:theme ^R:face",  (DIVIDER_X+SCR_W)/2, dY+22);
  tft.drawString("vL:web   vR:deauth",(DIVIDER_X+SCR_W)/2, dY+31);

  // Mood bar labels
  tft.setTextFont(FONT_LABEL); tft.setTextSize(FONT_LABEL_SIZE); tft.setTextDatum(TR_DATUM);
  tft.setTextColor(c.textDim,c.bg);
  tft.drawString("exc",BAR_X-2,BARS_Y);
  tft.drawString("brd",BAR_X-2,BARS_Y+BAR_SP);
  tft.drawString("trd",BAR_X-2,BARS_Y+BAR_SP*2);
  tft.drawString("hop",BAR_X-2,BARS_Y+BAR_SP*3);

  // Bottom bar
  tft.fillRect(0,BOTBAR_Y,SCR_W,SCR_H-BOTBAR_Y,c.panel);
  tft.drawFastHLine(0,BOTBAR_Y,SCR_W,c.accent);
  tft.setTextFont(FONT_BOTBAR); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(c.textDim,c.panel);
  tft.drawString("pkts:",4,BOTBAR_Y+4);
  tft.setTextDatum(TR_DATUM); tft.setTextColor(c.textDim,c.panel);
  tft.drawString("v2.0",SCR_W-3,BOTBAR_Y+4);

  // Corners
  if (activeTheme()->corners) {
    uint16_t a=c.accent; uint8_t s=8;
    tft.drawFastHLine(0,      0,      s,a); tft.drawFastVLine(0,      0,      s,a);
    tft.drawFastHLine(SCR_W-s,0,      s,a); tft.drawFastVLine(SCR_W-1,0,      s,a);
    tft.drawFastHLine(0,      SCR_H-1,s,a); tft.drawFastVLine(0,      SCR_H-s,s,a);
    tft.drawFastHLine(SCR_W-s,SCR_H-1,s,a); tft.drawFastVLine(SCR_W-1,SCR_H-s,s,a);
  }

  // Scanlines over face area only
  if (activeTheme()->scanlines)
    for (int16_t y=TOPBAR_H+1;y<BOTBAR_Y;y+=2)
      tft.drawFastHLine(0,y,DIVIDER_X,tft.alphaBlend(30,TFT_BLACK,c.bg));

  // Face sprite — small, always fits (180x30 = 10.8KB)
  faceSpr.deleteSprite();
  faceSpr.createSprite(FACE_SPR_W, FACE_SPR_H);
  faceSpr.setColorDepth(16);

  // Reset caches so everything redraws on first pass
  dFace[0]=dStatus[0]=dMood[0]=dSdStatus[0]='\0';
  dAPs=dEapol=dPmkid=dPwned=dPkts=dCh=0xFFFFFFFF;
}

// ── Dynamic update — cached direct draw, no sprites needed ──────────
// Each field only redraws when its value changes — no full-panel clear,
// no flicker. fillRect erases old value, drawString writes new one atomically.

#define FIELD(x,y,w,h,bg) tft.fillRect(x,y,w,h,bg)

void updateDynamic() {
  const ThemeColors& c = activeTheme()->c;

  // ── Face (small sprite — 180x30, only 10.5KB) ────────────────
  if (strcmp(displayFace, dFace) != 0) {
    strlcpy(dFace, displayFace, sizeof(dFace));
    faceSpr.fillSprite(c.bg);
    faceSpr.setTextDatum(MC_DATUM);
    faceSpr.setTextColor(c.face, c.bg);
    faceSpr.setTextSize(FONT_FACE_SIZE); faceSpr.setTextFont(FONT_FACE);
    faceSpr.drawString(displayFace, FACE_SPR_W/2, FACE_SPR_H/2);
    faceSpr.pushSprite(FACE_SPR_X, FACE_SPR_Y);
  }

  // ── Status text ───────────────────────────────────────────────
  if (strcmp(displayStatus, dStatus) != 0) {
    strlcpy(dStatus, displayStatus, sizeof(dStatus));
    FIELD(0, STATUS_Y, DIVIDER_X, 18, c.bg);
    tft.setTextFont(FONT_STATUS); tft.setTextSize(FONT_STATUS_SIZE);
    tft.setTextDatum(TC_DATUM); tft.setTextColor(c.text, c.bg);
    tft.drawString(displayStatus, FACE_CX, STATUS_Y);
  }

  // ── Mood ──────────────────────────────────────────────────────
  if (strcmp(displayMood, dMood) != 0) {
    strlcpy(dMood, displayMood, sizeof(dMood));
    FIELD(0, MOOD_Y, DIVIDER_X, 10, c.bg);
    tft.setTextFont(FONT_MOOD); tft.setTextDatum(TC_DATUM);
    tft.setTextColor(c.textDim, c.bg);
    char buf[28]; snprintf(buf, 28, "mood: %s", displayMood);
    tft.drawString(buf, FACE_CX, MOOD_Y);
  }

  // ── Mood bars (redraw every frame — fast fillRect, no flicker) 
  {
    auto bar = [&](int16_t y, float v, uint16_t fc, const char* lbl) {
      v = constrain(v, 0.0f, 1.0f);
      int16_t f = (int16_t)(v * BAR_W);
      if (f > 0)   tft.fillRect(BAR_X,     y, f,       BAR_H, fc);
      if (f < BAR_W) tft.fillRect(BAR_X+f, y, BAR_W-f, BAR_H, c.barBg);
    };
    bar(BARS_Y,           moodExcited(), c.ok,      "exc");
    bar(BARS_Y+BAR_SP,    moodBored(),   c.textDim, "brd");
    bar(BARS_Y+BAR_SP*2,  moodTired(),   c.err,     "trd");
    bar(BARS_Y+BAR_SP*3,  moodHopeful(), c.barFill, "hop");
  }

  // ── Right panel numbers ───────────────────────────────────────
  // Option A: clear dynamically after the label width so we never erase the label (e.g. "Pwned")
  // Clear dynamically after the label width so we never erase the label (e.g. "Pwned")
  tft.setTextFont(FONT_STATS);
  tft.setTextSize(FONT_STATS_SIZE);

  auto wnum = [&](const char* label, uint32_t v, uint32_t& cache, int16_t y, uint16_t col) {
    if (v == cache) return;
    cache = v;

    // Compute where the label ends (in the current font/size) and clear after it.
    int16_t labelW = tft.textWidth(label);
    int16_t x0 = WID_X + labelW + 6;     // padding after label
    int16_t xMin = WID_X + 32;           // keep previous minimum gap
    if (x0 < xMin) x0 = xMin;

    int16_t w = SCR_W - x0 - 1;          // 1px safety at right edge
    if (w < 1) w = 1;

    FIELD(x0, y, w, 15, c.panel);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(col, c.panel);
    char buf[12]; snprintf(buf, 12, "%lu", v);
    tft.drawString(buf, SCR_W-4, y);
  };

  wnum("APs",   captureApCount(), dAPs,   WID_Y,          c.text);
  wnum("EAPOL", captureEapol(),   dEapol, WID_Y+WID_SP,   captureEapol()>0  ? c.ok     : c.text);
  wnum("PMKID", capturePmkid(),   dPmkid, WID_Y+WID_SP*2, capturePmkid()>0  ? c.ok     : c.text);
  wnum("Pwned", capturePwned(),   dPwned, WID_Y+WID_SP*3, capturePwned()>0  ? c.accent : c.text);

  // ── Top bar: channel + deauth ─────────────────────────────────
  uint32_t ch = captureChannel();
  if (ch != dCh) {
    dCh = ch;
    FIELD(152, 4, 100, 14, c.panel);
    tft.setTextFont(FONT_STATS); tft.setTextSize(FONT_STATS_SIZE);
    tft.setTextDatum(TL_DATUM); tft.setTextColor(c.textDim, c.panel);
    char buf[20]; snprintf(buf, 20, "CH:%02d D:%lu", ch, captureDeauth()%1000);
    tft.drawString(buf, 154, 4);
  }

  // ── Bottom bar: packet count + SD status ─────────────────────
  uint32_t pkts = capturePackets();
  if (pkts != dPkts) {
    dPkts = pkts;
    FIELD(30, BB_Y+3, 110, 11, c.panel);
    tft.setTextFont(FONT_BOTBAR); tft.setTextDatum(TL_DATUM);
    tft.setTextColor(c.textDim, c.panel);
    char buf[16]; snprintf(buf, 16, "%lu", pkts);
    tft.drawString(buf, 32, BB_Y+5);
  }

  const char* ss = sdGetStatus();
  if (strcmp(ss, dSdStatus) != 0) {
    strlcpy(dSdStatus, ss, sizeof(dSdStatus));
    FIELD(138, BB_Y+2, SCR_W-142, 13, c.panel);
    tft.setTextFont(FONT_LABEL); tft.setTextDatum(TC_DATUM);
    tft.setTextColor(sdIsReady() ? c.ok : c.err, c.panel);
    tft.drawString(ss, (138+SCR_W)/2, BB_Y+5);
  }

  // ── LED ───────────────────────────────────────────────────────
  if (currentMood == MOOD_EXCITED)   ledRGB(false, true,  false);
  else if (captureApCount() > 0)     ledRGB(false, false, true);
  else                               ledRGB(false, false, false);
}

// ── Touch ─────────────────────────────────────────────────────────
void handleTouch() {
  // GPIO36 (TOUCH_IRQ) is input-only on ESP32 — interrupts don't work on it.
  // tirqTouched() relies on an interrupt flag that never gets set on GPIO36.
  // Fix: poll ts.touched() on a timer instead (SPI read every 20ms).
  static bool     wasDown  = false;
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (now - lastPoll < 20) return;
  lastPoll = now;

  if (!ts.touched()) { wasDown = false; return; }
  TS_Point p = ts.getPoint();
  if (p.z < 50 || p.x < 100 || p.x > 3900 || p.y < 100 || p.y > 3900) {
    wasDown = false; return;
  }
  if (wasDown) return;
  if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
  wasDown = true; lastTouchMs = now;
  int16_t sx,sy;
  touchMap(p.x,p.y,sx,sy,SCR_W,SCR_H);
  Serial.printf("[Touch] raw(%d,%d) z=%d -> screen(%d,%d)\n",p.x,p.y,p.z,sx,sy);
  if (sy < SCR_H/2) {
    if (sx < SCR_W/2) {
      themeCycleNext(); redrawAll=true;
      Serial.printf("[Touch] Theme: %s\n",activeThemeName());
    } else {
      faceSetCycleNext(); redrawAll=true;
      dFace[0]='\0';
      Serial.printf("[Touch] Face pack: %s\n",activeFaceSetName());
    }
  } else if (sx < SCR_W/2) {
    // Bottom-LEFT: toggle web UI
    if (webUiIsActive()) webUiStop();
    else                 webUiStart();
    redrawAll = true;
    Serial.printf("[Touch] Web UI: %s\n", webUiIsActive()?"ON":"OFF");
  } else {
    // Bottom-RIGHT: toggle deauth
    deauthEnabled = !deauthEnabled;
    Serial.printf("[Touch] Deauth: %s\n", deauthEnabled?"ON":"OFF");
    redrawAll = true;
  }
}

// ── setup() ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[FancyGotchi CYD v2.0]");

  pinMode(PIN_LED_R,OUTPUT);
  pinMode(PIN_LED_G,OUTPUT);
  pinMode(PIN_LED_B,OUTPUT);
  ledRGB(false,false,true);

  // Display
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.invertDisplay(false);
  tft.fillScreen(TFT_BLACK);
  ledcAttach(PIN_BL,BL_FREQ,BL_BITS);
  ledcWrite(PIN_BL,BL_BRIGHTNESS);
  Serial.println("[boot] TFT ok");

  // Splash
  tft.setTextFont(FONT_SPLASH_TITLE); tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN,TFT_BLACK);
  tft.drawString("FancyGotchi",SCR_W/2,80);
  tft.setTextFont(FONT_SPLASH_SUB); tft.setTextColor(TFT_DARKGREY,TFT_BLACK);
  tft.drawString("Standalone Capture Device",SCR_W/2,110);
  tft.setTextFont(FONT_SPLASH_HINT); tft.setTextColor(TFT_YELLOW,TFT_BLACK);
  tft.drawString("Hold screen to calibrate touch",SCR_W/2,134);
  tft.setTextColor(0x4208,TFT_BLACK);
  tft.drawString("Release to skip  (3 seconds)",SCR_W/2,146);

  // Touch init
  touchSPI.begin(TOUCH_CLK,TOUCH_MISO,TOUCH_MOSI,TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);
  Serial.println("[boot] Touch ok");

  // SPIFFS
  if (SPIFFS.begin(false)) {
    fontLoaded = SPIFFS.exists("/pwna_face.vlw");
  } else {
    SPIFFS.begin(true);
  }
  Serial.printf("[boot] SPIFFS ok font=%d\n",fontLoaded);

  // Hold-to-calibrate window
  bool forceRecal = false;
  uint32_t calEnd = millis()+3000;
  while (millis()<calEnd) {
    if (ts.tirqTouched() && ts.touched()) {
      TS_Point p=ts.getPoint();
      if (p.z>100 && p.x<4000 && p.y<4000) {
        forceRecal=true;
        tft.fillRect(0,128,SCR_W,22,TFT_BLACK);
        tft.setTextFont(1); tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_GREEN,TFT_BLACK);
        tft.drawString("Calibration detected! Release...",SCR_W/2,138);
        while(ts.tirqTouched()&&ts.touched()) delay(20);
        delay(300); break;
      }
    }
    delay(50);
  }
  if (forceRecal) touchCalForce(tft,ts,SCR_W,SCR_H);
  else            touchCalInit(tft,ts,SCR_W,SCR_H);
  Serial.println("[boot] Cal ok");

  // SD card
  sdInit();
  Serial.println("[boot] SD ok");

  // WiFi capture engine
  captureInit();
  Serial.println("[boot] Capture started");

  // Theme
  themeInit();

  bootTime=millis();
  redrawAll=true; dTheme=0xFF; dFaceSet=0xFF;
  lastDraw=0; lastAnim=0;

  ledRGB(false,false,false);
  Serial.printf("[boot] DONE  heap=%lu\n",ESP.getFreeHeap());
}

// ── loop() ────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // ── Touch FIRST — never blocked by WiFi ops ───────────────────
  handleTouch();

  // ── Web UI tick ───────────────────────────────────────────────
  webUiTick();

  // ── Channel hop ───────────────────────────────────────────────
  captureChannelTick();

  // ── Deauth — rate-limited, non-blocking ───────────────────────
  if (deauthEnabled) deauthTick();

  // ── Track captures for mood ───────────────────────────────────
  static uint32_t prevEapol = 0;
  uint32_t e = captureEapol();
  if (e != prevEapol) {
    prevEapol = e;
    lastCapture = now;
    ledFlash(false,true,false,60);
  }

  // Debug print every 5s
  static uint32_t lastDbg = 0;
  if (now - lastDbg >= 5000) {
    lastDbg = now;
    Serial.printf("[UI] aps=%d eapol=%lu pmkid=%lu pkts=%lu ch=%d mood=%d\n",
      captureApCount(), captureEapol(), capturePmkid(),
      capturePackets(), captureChannel(), (int)currentMood);
  }

  // Theme change
  uint8_t ti=0;
  for(uint8_t i=0;i<THEME_COUNT;i++)
    if(THEMES[i].name==activeThemeName()){ti=i;break;}
  if(ti!=dTheme){dTheme=ti;redrawAll=true;}
  if(activeFaceSetIdx()!=dFaceSet){dFaceSet=activeFaceSetIdx();redrawAll=true;}

  // Mood + animation
  if (now-lastAnim>=ANIM_FRAME_MS) {
    lastAnim=now;
    updateMood();
  }

  // Draw
  if (redrawAll || now-lastDraw>=DRAW_INTERVAL_MS) {
    lastDraw=now;
    if(redrawAll){drawChrome();redrawAll=false;}
    updateDynamic();
  }
}
