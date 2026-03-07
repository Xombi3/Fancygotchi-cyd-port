#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

static Preferences _calPrefs;
struct TouchCal { int16_t xMin,xMax,yMin,yMax; bool valid; };
static TouchCal _cal = {300,3800,300,3800,false};

static bool calLoad() {
  _calPrefs.begin("tcal", true);
  bool v = _calPrefs.getBool("v", false);
  if (!v) { _calPrefs.end(); return false; }
  _cal.xMin = _calPrefs.getShort("x0", 300);
  _cal.xMax = _calPrefs.getShort("x1", 3800);
  _cal.yMin = _calPrefs.getShort("y0", 300);
  _cal.yMax = _calPrefs.getShort("y1", 3800);
  _calPrefs.end();
  if (_cal.xMax-_cal.xMin < 500 || _cal.yMax-_cal.yMin < 500) return false;
  _cal.valid = true;
  Serial.printf("[Cal] x=%d..%d y=%d..%d\n",_cal.xMin,_cal.xMax,_cal.yMin,_cal.yMax);
  return true;
}

static void calSave() {
  _calPrefs.begin("tcal", false);
  _calPrefs.putBool("v",   true);
  _calPrefs.putShort("x0", _cal.xMin);
  _calPrefs.putShort("x1", _cal.xMax);
  _calPrefs.putShort("y0", _cal.yMin);
  _calPrefs.putShort("y1", _cal.yMax);
  _calPrefs.end();
}

static void _drawTarget(TFT_eSPI& tft, int16_t x, int16_t y, uint16_t col) {
  tft.drawCircle(x,y,14,col);
  tft.drawCircle(x,y,4,col);
  tft.drawFastHLine(x-22,y,8,col); tft.drawFastHLine(x+14,y,8,col);
  tft.drawFastVLine(x,y-22,8,col); tft.drawFastVLine(x,y+14,8,col);
}

static TS_Point _waitTap(XPT2046_Touchscreen& ts) {
  // wait for lift
  uint32_t t = millis()+1500;
  while (millis()<t && ts.tirqTouched() && ts.touched()) delay(20);
  delay(80);
  // wait for tap — 25s timeout
  t = millis()+25000;
  while (millis()<t) {
    if (ts.tirqTouched() && ts.touched()) {
      int32_t ax=0,ay=0; uint8_t n=0;
      for (uint8_t i=0;i<10;i++) {
        if (ts.touched()) {
          TS_Point p=ts.getPoint();
          if (p.z>100&&p.x>50&&p.x<4050) { ax+=p.x; ay+=p.y; n++; }
        }
        delay(8);
      }
      if (n>=5) {
        TS_Point r; r.x=ax/n; r.y=ay/n; r.z=500;
        Serial.printf("[Cal] tap x=%d y=%d n=%d\n",r.x,r.y,n);
        while (ts.tirqTouched()&&ts.touched()) delay(20);
        delay(200);
        return r;
      }
    }
    delay(20);
  }
  TS_Point fb; fb.x=2048; fb.y=2048; fb.z=0;
  return fb;
}

static void runCalibration(TFT_eSPI& tft, XPT2046_Touchscreen& ts,
                            int16_t W, int16_t H) {
  Serial.println("[Cal] Running calibration");
  const uint8_t M = 22;
  struct { int16_t x,y; } pts[3] = {{M,M},{W-M,H-M},{W-M,M}};
  int16_t rx[3], ry[3];

  for (uint8_t i=0;i<3;i++) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM); tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE,TFT_BLACK);
    char buf[24]; snprintf(buf,24,"Tap point %d / 3",i+1);
    tft.drawString(buf, W/2, H/2);
    tft.setTextFont(1);
    tft.drawString("Tap the circle firmly", W/2, H/2+18);
    _drawTarget(tft, pts[i].x, pts[i].y, TFT_RED);
    delay(300);

    TS_Point p = _waitTap(ts);
    rx[i]=p.x; ry[i]=p.y;
    _drawTarget(tft, pts[i].x, pts[i].y, TFT_GREEN);
    delay(400);
  }

  _cal.xMin = min({rx[0],rx[1],rx[2]}) - 60;
  _cal.xMax = max({rx[0],rx[1],rx[2]}) + 60;
  _cal.yMin = min({ry[0],ry[1],ry[2]}) - 60;
  _cal.yMax = max({ry[0],ry[1],ry[2]}) + 60;
  _cal.valid = true;
  calSave();

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM); tft.setTextFont(2);
  tft.setTextColor(TFT_GREEN,TFT_BLACK);
  tft.drawString("Calibration saved!", W/2, H/2-10);
  tft.setTextFont(1); tft.setTextColor(TFT_WHITE,TFT_BLACK);
  char buf[40];
  snprintf(buf,40,"x:%d-%d  y:%d-%d",_cal.xMin,_cal.xMax,_cal.yMin,_cal.yMax);
  tft.drawString(buf, W/2, H/2+10);
  delay(1800);
}

// Call once in setup — runs calibration if no saved data
bool touchCalInit(TFT_eSPI& tft, XPT2046_Touchscreen& ts, int16_t W, int16_t H) {
  if (calLoad()) return true;
  runCalibration(tft, ts, W, H);
  return false;
}

void touchCalForce(TFT_eSPI& tft, XPT2046_Touchscreen& ts, int16_t W, int16_t H) {
  _calPrefs.begin("tcal",false); _calPrefs.putBool("v",false); _calPrefs.end();
  runCalibration(tft, ts, W, H);
}

void touchMap(int16_t rx, int16_t ry, int16_t& sx, int16_t& sy, int16_t W, int16_t H) {
  sx = (int16_t)constrain(map(rx,_cal.xMin,_cal.xMax,0,W),0,W);
  sy = (int16_t)constrain(map(ry,_cal.yMin,_cal.yMax,0,H),0,H);
}
