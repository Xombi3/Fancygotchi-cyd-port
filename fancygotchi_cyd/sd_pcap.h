#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

// ================================================================
//  SD Card PCAP writer
//  Auto-detects CS pin. Saves libpcap-format files to SD.
//  Compatible with Wireshark, hashcat (via hcxtools), aircrack-ng.
// ================================================================

#define SD_CLK   18
#define SD_MISO  19
#define SD_MOSI  23

#define PCAP_MAGIC       0xA1B2C3D4
#define PCAP_VER_MAJ     2
#define PCAP_VER_MIN     4
#define PCAP_SNAPLEN     65535
#define PCAP_DLT_80211   105   // IEEE 802.11 raw

static SPIClass  _sdSPI(HSPI);  // HSPI: pins 14/12/13 but we remap to 18/19/23
static bool      sdReady    = false;
static uint32_t  savedCount = 0;
static char      sdStatus[32] = "SD: init";

static void _writePcapGlobalHeader(File& f) {
  uint32_t magic   = PCAP_MAGIC;
  uint16_t vmaj    = PCAP_VER_MAJ, vmin = PCAP_VER_MIN;
  int32_t  tz      = 0;
  uint32_t sigfigs = 0, snaplen = PCAP_SNAPLEN, dlt = PCAP_DLT_80211;
  f.write((uint8_t*)&magic,   4);
  f.write((uint8_t*)&vmaj,    2);
  f.write((uint8_t*)&vmin,    2);
  f.write((uint8_t*)&tz,      4);
  f.write((uint8_t*)&sigfigs, 4);
  f.write((uint8_t*)&snaplen, 4);
  f.write((uint8_t*)&dlt,     4);
}

static void _writePcapPkt(File& f, const uint8_t* data, uint32_t len) {
  uint32_t ts_sec = millis() / 1000;
  uint32_t ts_us  = (millis() % 1000) * 1000;
  f.write((uint8_t*)&ts_sec, 4);
  f.write((uint8_t*)&ts_us,  4);
  f.write((uint8_t*)&len,    4);
  f.write((uint8_t*)&len,    4);
  f.write(data, len);
}

static void _sanitise(const char* in, char* out, size_t max) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j < max - 1; i++) {
    char c = in[i];
    if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_')
      out[j++] = c;
    else if (c != ':')
      out[j++] = '_';
  }
  out[j] = '\0';
}

bool sdInit() {
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
      // Count existing
      File d = SD.open("/handshakes");
      while (true) {
        File f = d.openNextFile();
        if (!f) break;
        if (!f.isDirectory()) savedCount++;
        f.close();
      }
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

static bool _sdEnabled = true;

bool sdSaveHandshake(const char* bssid, const char* ssid,
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

inline bool        sdIsReady()    { return sdReady; }
inline uint32_t    sdSaved()      { return savedCount; }
inline const char* sdGetStatus()  { return sdStatus; }

inline void sdToggle() {
  _sdEnabled = !_sdEnabled;
  if (_sdEnabled)
    snprintf(sdStatus,sizeof(sdStatus),"SD:on %lu",savedCount);
  else
    strlcpy(sdStatus,"SD:paused",sizeof(sdStatus));
}
// Override save to check enabled flag
