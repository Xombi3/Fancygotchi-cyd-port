#pragma once
// ── FancyGotchi Web UI ────────────────────────────────────────────
// Serves a live status page over WiFi AP mode.
// Activate by touching bottom-LEFT of screen. Deactivate same zone.
// AP SSID: FancyGotchi-CYD  Password: pwnagotchi
// Connect then browse to http://192.168.4.1
//
// Runs a minimal async HTTP server on Core 1 alongside main loop.
// Uses WiFi AP mode — does NOT interfere with promiscuous capture
// because ESP32 can run AP+STA simultaneously.

#include <WiFi.h>
#include <WebServer.h>
#include "wifi_capture.h"
#include "sd_pcap.h"

#define WEBUI_SSID     "FancyGotchi-CYD"
#define WEBUI_PASS     "pwnagotchi"
#define WEBUI_PORT     80

static WebServer  _webServer(WEBUI_PORT);
static bool       _webUiActive = false;
static uint32_t   _webUiStartMs = 0;
#define WEBUI_TIMEOUT_MS  (10UL * 60UL * 1000UL)  // auto-off after 10 min

// ── HTML page ─────────────────────────────────────────────────────
static void handleRoot() {
  uint32_t upSec  = millis() / 1000;
  uint32_t upH    = upSec / 3600;
  uint32_t upM    = (upSec % 3600) / 60;
  uint32_t upS    = upSec % 60;

  ApRecord* aps   = captureAps();
  uint8_t   apCnt = captureApCount();

  // Build AP rows
  String rows = "";
  for (uint8_t i = 0; i < apCnt; i++) {
    ApRecord* ap = &aps[i];
    char bstr[18]; bssidToStr(ap->bssid, bstr);
    String status;
    String badge;
    if (ap->hasPmkid) {
      status = "pmkid";
      badge  = "<span class='badge pmkid'>PMKID</span>";
    } else if (ap->hasHandshake) {
      status = "pwned";
      badge  = "<span class='badge pwned'>PWNED</span>";
    } else {
      status = "";
      badge  = "<span class='badge scan'>SCAN</span>";
    }
    uint32_t ageSec = (millis() - ap->lastSeen) / 1000;
    rows += "<tr class='" + status + "'>"
            "<td>" + String(ap->ssid[0] ? ap->ssid : "<hidden>") + "</td>"
            "<td class='mono'>" + String(bstr) + "</td>"
            "<td>" + String(ap->channel) + "</td>"
            "<td>" + String(ap->rssi) + " dBm</td>"
            "<td>" + String(ap->eapolCount) + "</td>"
            "<td>" + badge + "</td>"
            "<td>" + String(ageSec) + "s ago</td>"
            "</tr>\n";
  }

  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<meta http-equiv='refresh' content='5'>
<title>FancyGotchi CYD</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#0a0a0f;color:#c8ffc8;font-family:monospace;font-size:13px;padding:12px}
  h1{color:#00ff88;font-size:18px;margin-bottom:4px}
  .sub{color:#556655;font-size:11px;margin-bottom:14px}
  .stats{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:14px}
  .stat{background:#0f1f0f;border:1px solid #1a3a1a;border-radius:6px;
        padding:8px 14px;min-width:80px;text-align:center}
  .stat .val{font-size:22px;color:#00ff88;font-weight:bold}
  .stat .lbl{font-size:10px;color:#446644;margin-top:2px}
  table{width:100%;border-collapse:collapse;font-size:12px}
  th{background:#0f1f0f;color:#446644;padding:6px 8px;text-align:left;
     border-bottom:1px solid #1a3a1a;position:sticky;top:0}
  td{padding:5px 8px;border-bottom:1px solid #0f180f}
  tr:hover td{background:#0f1f0f}
  tr.pwned td{color:#00ff88}
  tr.pmkid td{color:#ffcc00}
  .mono{font-family:monospace;letter-spacing:0.5px}
  .badge{display:inline-block;padding:2px 7px;border-radius:3px;font-size:10px;font-weight:bold}
  .badge.pwned{background:#003320;color:#00ff88;border:1px solid #00ff88}
  .badge.pmkid{background:#332200;color:#ffcc00;border:1px solid #ffcc00}
  .badge.scan{background:#111;color:#334433;border:1px solid #222}
  .uptime{color:#334433;font-size:11px;margin-bottom:14px}
  .footer{margin-top:16px;color:#222;font-size:10px;text-align:center}
  @media(max-width:600px){.stat .val{font-size:18px}.stats{gap:6px}}
</style></head><body>
<h1>&#x1F47E; FancyGotchi CYD</h1>
<div class='sub'>passive wifi handshake capture — auto-refresh 5s</div>
<div class='uptime'>uptime: )";
  html += String(upH) + "h " + String(upM) + "m " + String(upS) + "s</div>";

  html += "<div class='stats'>"
          "<div class='stat'><div class='val'>" + String(apCnt) + "</div><div class='lbl'>APs</div></div>"
          "<div class='stat'><div class='val'>" + String(captureEapol()) + "</div><div class='lbl'>EAPOL</div></div>"
          "<div class='stat'><div class='val'>" + String(capturePmkid()) + "</div><div class='lbl'>PMKID</div></div>"
          "<div class='stat'><div class='val'>" + String(capturePwned()) + "</div><div class='lbl'>PWNED</div></div>"
          "<div class='stat'><div class='val'>" + String(capturePackets()) + "</div><div class='lbl'>PKTS</div></div>"
          "<div class='stat'><div class='val'>CH" + String(captureChannel()) + "</div><div class='lbl'>CHAN</div></div>"
          "</div>";

  html += "<table><thead><tr>"
          "<th>SSID</th><th>BSSID</th><th>CH</th><th>RSSI</th>"
          "<th>EAPOL</th><th>Status</th><th>Last Seen</th>"
          "</tr></thead><tbody>";
  html += rows;
  html += "</tbody></table>";
  html += "<div class='footer'>FancyGotchi CYD v2.0 &mdash; touch bottom-left on device to stop AP</div>";
  html += "</body></html>";

  _webServer.send(200, "text/html", html);
}

static void handleJson() {
  ApRecord* aps   = captureAps();
  uint8_t   apCnt = captureApCount();
  String j = "{\"uptime\":" + String(millis()/1000)
           + ",\"aps\":"    + String(apCnt)
           + ",\"eapol\":"  + String(captureEapol())
           + ",\"pmkid\":"  + String(capturePmkid())
           + ",\"pwned\":"  + String(capturePwned())
           + ",\"pkts\":"   + String(capturePackets())
           + ",\"channel\":" + String(captureChannel())
           + ",\"ap_list\":[";
  for (uint8_t i = 0; i < apCnt; i++) {
    ApRecord* ap = &aps[i];
    char bstr[18]; bssidToStr(ap->bssid, bstr);
    if (i) j += ",";
    j += "{\"ssid\":\"" + String(ap->ssid[0] ? ap->ssid : "") + "\""
       + ",\"bssid\":\"" + String(bstr) + "\""
       + ",\"ch\":"     + String(ap->channel)
       + ",\"rssi\":"   + String(ap->rssi)
       + ",\"eapol\":"  + String(ap->eapolCount)
       + ",\"pwned\":"  + String(ap->hasHandshake || ap->hasPmkid ? "true" : "false")
       + "}";
  }
  j += "]}";
  _webServer.send(200, "application/json", j);
}

// ── Public API ────────────────────────────────────────────────────
inline bool webUiIsActive() { return _webUiActive; }

void webUiStart() {
  if (_webUiActive) return;
  Serial.println("[Web] Starting AP...");
  // AP+STA mode — keeps promiscuous capture running
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WEBUI_SSID, WEBUI_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[Web] AP up: %s  IP: %s\n", WEBUI_SSID, ip.toString().c_str());

  _webServer.on("/",      handleRoot);
  _webServer.on("/json",  handleJson);
  _webServer.begin();
  _webUiActive  = true;
  _webUiStartMs = millis();
}

void webUiStop() {
  if (!_webUiActive) return;
  _webServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);  // back to STA-only for promiscuous
  _webUiActive = false;
  Serial.println("[Web] AP stopped");
}

void webUiTick() {
  if (!_webUiActive) return;
  // Auto-shutoff after timeout
  if (millis() - _webUiStartMs > WEBUI_TIMEOUT_MS) {
    Serial.println("[Web] Timeout — stopping AP");
    webUiStop();
    return;
  }
  _webServer.handleClient();
}
