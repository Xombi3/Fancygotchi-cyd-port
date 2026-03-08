#pragma once
// ================================================================
//  font.h — Font configuration for FancyGotchi CYD v2
//
//  TFT_eSPI built-in font reference:
//    Font 1  =  7px  tiny     (GLCD — good for small labels)
//    Font 2  = 14px  medium   (good for stats, status text)
//    Font 4  = 26px  large    (good for titles)
//    Font 6  = 48px  huge     (digits only — 0-9 : .)
//    Font 7  = 48px  7-seg    (digits only — looks like LCD display)
//    Font 8  = 75px  huge     (digits only)
//
//  setTextSize multiplies the base font:
//    Font 2 + Size 1 = 14px   (normal stats/status)
//    Font 2 + Size 2 = 28px   (default face size)
//    Font 2 + Size 3 = 42px   (big face — may clip on small screen)
//    Font 1 + Size 2 = 14px   (chunky labels)
//
//  TIPS:
//    - FONT_FACE / FONT_FACE_SIZE controls the kaomoji size.
//      Size 2 is the default. Try Size 3 for massive faces.
//      Larger sprite RAM needed: Size3 needs ~54KB — fine on ESP32.
//    - Fonts 6/7/8 only contain digits so don't use them for faces.
//    - Font 4 is the nicest for large text if you want a bigger title.
// ================================================================

// ── Face — the kaomoji drawn in the centre panel ─────────────────
#define FONT_FACE        2    // base font (2 = 14px medium)
#define FONT_FACE_SIZE   2    // multiplier: 1=small  2=normal  3=large

// ── Status text — e.g. "got one!!!"  "scanning..." ───────────────
#define FONT_STATUS      2
#define FONT_STATUS_SIZE 1

// ── Mood label — e.g. "mood: excited!" ───────────────────────────
#define FONT_MOOD        1
#define FONT_MOOD_SIZE   1

// ── Stats panel numbers — APs / EAPOL / PMKID / Pwned ────────────
#define FONT_STATS       2
#define FONT_STATS_SIZE  1

// ── Top bar — title + channel + deauth count ─────────────────────
#define FONT_TOPBAR      2
#define FONT_TOPBAR_SIZE 1

// ── Bottom bar — packet count + SD status ────────────────────────
#define FONT_BOTBAR      1
#define FONT_BOTBAR_SIZE 1

// ── Small labels — bar labels, touch hints, theme/face pack name ──
#define FONT_LABEL       1
#define FONT_LABEL_SIZE  1

// ── Splash screen ─────────────────────────────────────────────────
#define FONT_SPLASH_TITLE  4
#define FONT_SPLASH_SUB    2
#define FONT_SPLASH_HINT   1
