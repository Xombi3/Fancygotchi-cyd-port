#pragma once
#include <TFT_eSPI.h>

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

// ── Colour helpers ────────────────────────────────────────────────
#define RGB(r,g,b) ((uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)))

static const Theme THEMES[] = {
  { "default", { RGB(18,18,24),  RGB(28,28,36),  RGB(0,255,180),
                 RGB(220,220,220), RGB(100,100,120), RGB(0,255,180),
                 RGB(0,220,80),   RGB(255,60,60),
                 RGB(0,200,160),  RGB(40,40,55) },  true,  false },
  { "cyber",   { RGB(0,0,20),    RGB(0,10,35),   RGB(0,200,255),
                 RGB(180,240,255), RGB(60,100,130), RGB(0,200,255),
                 RGB(0,255,120),  RGB(255,50,50),
                 RGB(0,180,255),  RGB(0,20,40)  },  true,  true  },
  { "retro",   { RGB(20,10,0),   RGB(32,16,0),   RGB(255,140,0),
                 RGB(255,180,80), RGB(140,80,20),  RGB(255,160,0),
                 RGB(180,220,0),  RGB(255,60,0),
                 RGB(200,120,0),  RGB(40,20,0)  },  false, true  },
  { "matrix",  { RGB(0,8,0),     RGB(0,14,0),    RGB(0,255,0),
                 RGB(0,220,0),   RGB(0,100,0),    RGB(0,255,0),
                 RGB(0,200,0),   RGB(255,50,50),
                 RGB(0,180,0),   RGB(0,25,0)   },  false, true  },
};
#define THEME_COUNT 4

static uint8_t _themeIdx = 0;
inline const Theme*  activeTheme()     { return &THEMES[_themeIdx]; }
inline const char*   activeThemeName() { return THEMES[_themeIdx].name; }
inline void          themeInit()       { _themeIdx = 0; }
inline void          themeCycleNext()  { _themeIdx = (_themeIdx + 1) % THEME_COUNT; }
