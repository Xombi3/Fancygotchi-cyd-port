# FancyGotchi CYD v2 — Update Log (March 2026)

Board: ESP32-2432S028R | Display: ST7789 320×240

---

## New Files

### faces.h
Switchable face pack system. Previously faces were hardcoded arrays in the .ino.

**6 built-in packs:** classic, kawaii, ghost, robot, demon, pixel

Each pack has 7 moods with 3 variants each. Variants auto-cycle every 8 seconds.

To add your own pack — copy any block in faces.h, rename it, fill in your strings (max 47 chars each). `FACE_SET_COUNT` updates automatically, nothing else to change.

### font.h
All font numbers and size multipliers are now #defines in one place instead of hardcoded throughout the .ino. Change a value here and it updates everywhere.

Key defines:
- `FONT_FACE` / `FONT_FACE_SIZE` — the kaomoji (default: Font 2, Size 2 — try Size 3 for bigger)
- `FONT_STATUS` — status line e.g. "got one!!!"
- `FONT_MOOD` — mood label e.g. "mood: intense"
- `FONT_STATS` — APs / EAPOL / PMKID / Pwned numbers
- `FONT_TOPBAR` — title and channel in top bar
- `FONT_BOTBAR` — packet count in bottom bar
- `FONT_SPLASH_TITLE/SUB/HINT` — boot screen text

TFT_eSPI font reference: Font 1 = 7px, Font 2 = 14px, Font 4 = 26px. Fonts 6/7/8 are digits only.

---

## Touch Zones (updated)

```
┌─────────────────────┬─────────────────────┐
│  TOP-LEFT           │  TOP-RIGHT  (new)   │
│  cycle theme        │  cycle face pack    │
├─────────────────────┼─────────────────────┤
│  BOTTOM-LEFT        │  BOTTOM-RIGHT       │
│  toggle web UI      │  toggle deauth      │
│  192.168.4.1        │                     │
└─────────────────────┴─────────────────────┘
```

Previously the entire top half cycled themes. It is now split left/right.
Active theme and face pack are shown in the right panel at all times.

---

## How Face Cycling Works

The mood engine runs every 120ms and picks a mood based on device state. Within each mood, 3 face variants rotate every 8 seconds automatically.

**Mood priority (highest first):**
1. EXCITED — EAPOL just captured, overrides all, lasts 3s
2. HAPPY — capture within last 15s
3. INTENSE — pkts > 500 and aps > 8
4. SCANNING — aps > 0 and pkts > 50
5. SLEEP — uptime > 120s, no APs seen
6. BORED — uptime > 60s, no captures
7. IDLE — default / startup

Tapping top-right switches packs immediately. A capture event forces a jump to EXCITED which also changes the face right away.

---

## Sketch Folder

```
fancygotchi_cyd.ino     updated
faces.h                 new
font.h                  new
theme.h
config.h
wifi_capture.h
sd_pcap.h
web_ui.h
touch_calibrate.h
```

Capture behaviour, SD saving, and web UI are unchanged.
Handshakes still save to `/handshakes/<BSSID>_<SSID>.pcap`.
