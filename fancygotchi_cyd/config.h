#pragma once
// ================================================================
//  FancyGotchi CYD v2 — Standalone WiFi Handshake Capture
//  Board: ESP32-2432S028R  |  ST7789  |  320x240 landscape
//
//  No pwnagotchi needed. This IS the pwnagotchi.
//  Passively captures WPA2 EAPOL handshakes and PMKID packets
//  in promiscuous mode and saves them as .pcap to SD card.
// ================================================================

// ── Channel hopping ──────────────────────────────────────────────
#define CHANNEL_HOP_INTERVAL_MS  200   // ms per channel
#define CHANNEL_MIN              1
#define CHANNEL_MAX              13

// ── Capture ──────────────────────────────────────────────────────
#define MAX_APS           64    // max APs to track
#define MAX_STATIONS      64    // max stations to track
#define BEACON_TIMEOUT_S  30    // seconds before AP considered gone

// ── SD card ──────────────────────────────────────────────────────
// CS pin is auto-detected at boot
#define SD_CLK   18
#define SD_MISO  19
#define SD_MOSI  23

// ── Display ──────────────────────────────────────────────────────
#define DISPLAY_ROTATION  1
#define PIN_BL            21
#define BL_FREQ           5000
#define BL_BITS           8
#define BL_BRIGHTNESS     220

// ── Touch ────────────────────────────────────────────────────────
#define TOUCH_CLK   25
#define TOUCH_MISO  39
#define TOUCH_MOSI  32
#define TOUCH_CS    33
#define TOUCH_IRQ   36
#define TOUCH_DEBOUNCE_MS  500

// ── RGB LED (active LOW) ─────────────────────────────────────────
#define PIN_LED_R  4
#define PIN_LED_G  16
#define PIN_LED_B  17

// ── Timing ───────────────────────────────────────────────────────
#define DRAW_INTERVAL_MS   150
#define ANIM_FRAME_MS      120
