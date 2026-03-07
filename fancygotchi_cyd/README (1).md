# FancyGotchi CYD v2 — Standalone WiFi Capture Device

A standalone pwnagotchi-style WiFi handshake capture device for the
**ESP32-2432S028R** (Cheap Yellow Display). No Raspberry Pi needed.
This device IS the pwnagotchi.

---

## What it does

- Puts the ESP32 WiFi radio into **promiscuous (monitor) mode**
- **Hops all 13 2.4GHz channels** automatically
- **Sends deauth frames** to force nearby clients to reconnect, generating EAPOL traffic
- **Passively captures WPA2 EAPOL handshakes** — saved to microSD whenever a device connects
- **Captures PMKID packets** — clientless attack, no connected client required
- Saves captures as **standard `.pcap` files** to microSD, one file per AP
- Displays a **pwnagotchi-style face and mood** that reacts to activity
- **Live web UI** over WiFi — connect your phone to see a live AP table
- Shows live stats: APs seen, EAPOL count, PMKID count, pwned, channel, packet count

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | ESP32-2432S028R ("Cheap Yellow Display") |
| Display | ST7789 320×240 landscape |
| Touch | XPT2046 resistive — auto-calibrates on first boot |
| SD Card | microSD, FAT32 formatted |
| WiFi | ESP32 built-in 2.4GHz, promiscuous + AP mode simultaneously |
| LED | RGB on GPIO 4/16/17 (active LOW) |

---

## Pin Reference

### Display (HSPI / ST7789)
| Signal | GPIO |
|--------|------|
| CLK    | 14   |
| MOSI   | 13   |
| MISO   | 12   |
| CS     | 15   |
| DC     | 2    |
| BL     | 21   |

### Touch (VSPI / XPT2046)
| Signal | GPIO |
|--------|------|
| CLK    | 25   |
| MOSI   | 32   |
| MISO   | 39   |
| CS     | 33   |
| IRQ    | 36 (not used — GPIO36 is input-only, polled instead) |

### SD Card (HSPI)
| Signal | GPIO |
|--------|------|
| CLK    | 18   |
| MOSI   | 23   |
| MISO   | 19   |
| CS     | 5 (auto-detected at boot) |

> **Important:** Touch uses VSPI, SD uses HSPI. They must use separate SPI
> peripherals. Sharing VSPI between touch and SD causes the touch chip to stop
> responding after SD init (MISO floats high, all reads return 0xFF / 8191).

---

## Installation

### 1. Arduino IDE setup

- Board: `ESP32 Dev Module`
- Partition scheme: **`Huge APP (3MB No OTA)`** — required, sketch won't fit otherwise
- Upload speed: `921600`
- PSRAM: Disabled

### 2. Libraries required

| Library | Author | Install via |
|---------|--------|-------------|
| TFT_eSPI | Bodmer | Library Manager |
| XPT2046_Touchscreen | Paul Stoffregen | Library Manager |

Built-in (no install needed): `SD`, `Preferences`, `WiFi`, `WebServer`

### 3. Copy User_Setup.h

**Required before compiling.** Copy `User_Setup.h` from this folder to:
```
Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
```
Overwrite the existing file and restart Arduino IDE. This configures TFT_eSPI
for the CYD's ST7789 display and correct SPI pins.

### 4. Font upload (optional)

For the pwnagotchi-style bitmap face font:
1. Install the ESP32 Sketch Data Upload plugin
2. Place `pwna_face.vlw` in the `data/` folder next to the `.ino`
3. `Tools > ESP32 Sketch Data Upload`

Without the font, faces display using built-in ASCII characters — fully
functional, just less pretty.

### 5. Flash

Open `fancygotchi_cyd.ino` and upload normally.

---

## First Boot — Touch Calibration

On first boot a **3-point calibration screen** appears automatically.
Tap each circle target firmly and hold until it registers.
Calibration is saved to NVS flash and persists across reboots forever.

**To recalibrate:** hold your finger on the screen during the 3-second
splash window at boot. Release when prompted, then complete the 3 taps.

---

## Touch Zones

```
┌─────────────────────────────────────────────────────────┐
│                                                         │
│              TAP ANYWHERE — TOP HALF                    │
│         → Cycle theme (default/cyber/retro/matrix)      │
│                                                         │
├──────────────────────────┬──────────────────────────────┤
│                          │                              │
│    TAP BOTTOM-LEFT       │    TAP BOTTOM-RIGHT          │
│    → Toggle Web UI       │    → Toggle deauth on/off    │
│      (starts WiFi AP)    │                              │
│                          │                              │
└──────────────────────────┴──────────────────────────────┘
```

---

## Web UI

Tap **bottom-left** to start the web interface:

1. A WiFi AP named **`FancyGotchi-CYD`** appears (password: `pwnagotchi`)
2. Connect your phone or laptop to it
3. Browse to **`http://192.168.4.1`**

The page shows a live table of all detected APs with SSID, BSSID, channel,
RSSI, EAPOL count, and pwned status. Auto-refreshes every 5 seconds.

A JSON API is available at `http://192.168.4.1/json` for scripting.

The web UI uses `WIFI_AP_STA` mode — promiscuous capture continues
uninterrupted while the AP is active. Auto-shuts off after 10 minutes.
Tap bottom-left again to stop it manually.

---

## Deauth

The device sends **802.11 deauthentication frames** to force nearby clients
to reconnect, generating EAPOL handshake traffic.

- Runs on **Core 0** as a FreeRTOS task — never blocks display or touch on Core 1
- Targets one AP every 2 seconds, cycling through the full AP list
- Skips APs already marked as pwned (hasHandshake or hasPmkid)
- Skips 5GHz APs (channel > 13) — ESP32 is 2.4GHz only
- Toggle on/off with the **bottom-right** touch zone

> Only use on networks you own or have explicit permission to test.

---

## SD Card

Insert a **FAT32 formatted** microSD. Captures are saved to:
```
/handshakes/<BSSID>_<SSID>.pcap
```

Each file is a standard **libpcap** file (Wireshark, hcxtools, aircrack-ng
compatible). Multiple packets from the same AP are appended to the same file.

### Cracking captured handshakes

```bash
# Convert to hashcat format
hcxpcapngtool -o hashes.hc22000 /handshakes/*.pcap

# Check how many hashes extracted
wc -l hashes.hc22000

# Crack with hashcat
hashcat -m 22000 hashes.hc22000 wordlist.txt

# With rules for better coverage
hashcat -m 22000 hashes.hc22000 wordlist.txt -r best64.rule

# Or with aircrack-ng
aircrack-ng -w wordlist.txt /handshakes/*.pcap
```

---

## Display Layout

```
┌──────────────────────────────────────────────────────────────┐
│  FancyGotchi  [CYD]         CH:06 D:3              v2.0     │ ← top bar
├──────────────────────────────────┬───────────────────────────┤
│                                  │  APs        64            │
│         (ᵔ◡◡ᵔ)                  │  EAPOL      12            │
│                                  │  PMKID       1            │
│   got one!!!                     │  Pwned       8            │
│   mood: excited!                 │  ──────────────           │
│                                  │  <cyber>                  │
│  exc ████████░░░░░░              │  < theme                  │
│  brd ██░░░░░░░░░░░░              │  > deauth | < web         │
│  trd ████░░░░░░░░░░              │                           │
│  hop ██████░░░░░░░               │                           │
├──────────────────────────────────┴───────────────────────────┤
│  pkts: 48265          SD:8 pwned                    v2.0    │ ← bottom bar
└──────────────────────────────────────────────────────────────┘
```

---

## Mood System

| Mood | Trigger |
|------|---------|
| excited! | Just captured a handshake (within last 10s) |
| happy | Captured something recently (within last 60s) |
| scanning | Active, lots of APs visible |
| intense | High AP density and traffic |
| bored | Running a while with nothing new |
| sleepy | No APs seen for a long time |
| idle | Just booted |

---

## Themes

Tap the top half of the screen to cycle:

| Theme | Palette |
|-------|---------|
| default | Dark grey / teal |
| cyber | Deep blue / cyan |
| retro | Black / amber (CRT terminal look) |
| matrix | Black / green |

---

## LED Status

| Colour | Meaning |
|--------|---------|
| Blue pulse (boot) | Initialising |
| Green flash | Handshake or PMKID captured |
| Blue steady | Scanning, APs visible |
| Off | No APs detected yet |

---

## Troubleshooting

**Touch not working after calibration completes**
Ensure `sd_pcap.h` uses `SPIClass(HSPI)` and `fancygotchi_cyd.ino` uses
`SPIClass(VSPI)` for touch. Sharing VSPI between both causes the XPT2046
to return all-ones (x=8191, z=4095) after SD initialises.

**No EAPOL captures**
Normal if no clients are nearby. With deauth enabled, captures appear within
1–3 minutes in a residential area. Check that `data=` in serial output is
rising — if it stays at 0, no data frames are being received at all.

**EAPOL count stuck, same AP repeatedly**
The `hasHandshake` flag only sets after receiving a frame with the MIC bit
set (EAPOL msg 2/3/4). If only msg 1 is being captured, the AP will keep
being deauthed. This is normal — keep running and a full handshake will
arrive eventually.

**Crashing / rebooting at startup**
Select `Huge APP (3MB No OTA)` partition scheme. The default partition is
too small and causes silent flash corruption or boot loops.

**SD card not detected**
Firmware auto-tries CS pins 5, 22, 4, 2, 15, 13. GPIO5 works on most CYD
variants. Format as FAT32 (not exFAT or NTFS).

**`unsupport frame type: 0c0` spam in serial**
Harmless. The ESP32 WiFi driver logs injected deauth frames as they pass
back through the receive path. Does not affect capture or stability.

**Web UI shows but AP list is empty**
The AP table fills within 5–10 seconds of boot as beacons are received.
Reload the page after a few seconds.

---

## File Reference

| File | Purpose |
|------|---------|
| `fancygotchi_cyd.ino` | Main sketch — setup, loop, UI, touch handler |
| `config.h` | All pin definitions and tunable constants |
| `theme.h` | 4 colour themes (default/cyber/retro/matrix) |
| `wifi_capture.h` | Promiscuous sniffer, EAPOL/PMKID parser, deauth FreeRTOS task |
| `sd_pcap.h` | SD init (HSPI), libpcap writer, file management |
| `web_ui.h` | HTTP server, live HTML table, JSON API endpoint |
| `touch_calibrate.h` | 3-point calibration with NVS persistence |
| `User_Setup.h` | TFT_eSPI driver config — must be copied to library folder |

---

## Credits

| Project | Author | Link |
|---------|--------|------|
| Pwnagotchi | @evilsocket | https://github.com/evilsocket/pwnagotchi |
| Fancygotchi | @V0r-T3x | https://github.com/V0r-T3x/Fancygotchi |
| ESP32-WiFi-Hash-Monster | @G4lile0 | https://github.com/G4lile0/ESP32-WiFi-Hash-Monster |
| PacketMonitor32 | @spacehuhn | https://github.com/spacehuhn/PacketMonitor32 |

---

## License

GPL-3.0 — in keeping with the licenses of the projects this builds on.
