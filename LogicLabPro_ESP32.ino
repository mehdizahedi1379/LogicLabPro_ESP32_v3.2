/*
 * LogicLab Pro - Digital Logic Circuit Simulator
 * ESP32 web server sketch
 *
 * وب‌اپ (HTML) به‌صورت gzip داخل حافظه‌ی فلش ESP32 قرار گرفته (index_html.h)
 * و از طریق وب‌سرور داخلی ESP32 سرو می‌شود. کتابخانه‌ی اضافه لازم نیست
 * (فقط ESP32 Arduino core).
 *
 * باز کردن:  http://<IP نمایش‌داده‌شده در Serial>   یا   http://logiclab.local
 */

#include <WiFi.h>
#include "config.h"          // قبل از WebServer.h: زمان‌های انتظار HTTP از اینجا می‌آیند
#include <WebServer.h>
#include <ESPmDNS.h>
#include "index_html.h"
#if IO_BRIDGE_ENABLED
#include "io_bridge.h"
#endif

WebServer server(HTTP_PORT);
bool mdnsStarted = false;
volatile bool wifiWasDown = false;   // با رویداد قطع Wi-Fi true می‌شود تا mDNS دوباره راه بیفتد

// ------------------------------------------------------------------
// Handlers
// ------------------------------------------------------------------
void handleRoot() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8",
                (PGM_P)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}

void handleHealth() {
  String json = "{\"status\":\"ok\",\"mode\":\"";
  json += "sta";
  json += "\",\"ip\":\"";
  json += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
  json += "\",\"rssi\":";
  json += String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  json += ",\"uptime_s\":";
  json += String(millis() / 1000);
  json += ",\"free_heap\":";
  json += String(ESP.getFreeHeap());
  json += ",\"io\":";
  json += IO_BRIDGE_ENABLED ? "true" : "false";
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  // هر مسیر ناشناخته را به صفحه‌ی اصلی برگردان
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ------------------------------------------------------------------
// Wi-Fi
// ------------------------------------------------------------------
bool connectStation() {
  if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0) {
    Serial.println("[WiFi] SSID not configured in config.h");
    WiFi.mode(WIFI_STA);
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setSleep(false);          // بدون modem-sleep: تأخیرهای چندصد میلی‌ثانیه‌ای و قطع‌های کوتاه حذف می‌شود
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("[WiFi] Connecting to \"%s\" ", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void startMdns() {
  if (mdnsStarted) return;
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    mdnsStarted = true;
    Serial.printf("[mDNS] http://%s.local\n", HOSTNAME);
  }
}

// ------------------------------------------------------------------
// Arduino
// ------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== LogicLab Pro on ESP32 ===");

  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) wifiWasDown = true;
  });

  if (connectStation()) {
    Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    startMdns();
  } else {
    Serial.println("[WiFi] Not connected. Check WIFI_SSID / WIFI_PASS in config.h; retrying in the background.");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/health", HTTP_GET, handleHealth);
#if IO_BRIDGE_ENABLED
  ioBridgeBegin(server);
#endif
  server.onNotFound(handleNotFound);
  server.begin();

  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("[HTTP] Open: http://%s\n", WiFi.localIP().toString().c_str());
}

// نگهبان Wi-Fi: قطع واقعی را بدون دست‌زدن به اتصالِ در حال برقراری مدیریت می‌کند.
void wifiWatch() {
  static uint32_t downSince = 0, lastTry = 0;
  if (WiFi.status() == WL_CONNECTED) {
    downSince = 0;
    if (wifiWasDown) {                      // بعد از هر قطع، mDNS را از نو بساز
      wifiWasDown = false;
      if (mdnsStarted) { MDNS.end(); mdnsStarted = false; }
      WiFi.setSleep(false);
    }
    startMdns();
    return;
  }
  if (strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0) return;
  uint32_t now = millis();
  if (downSince == 0) { downSince = now; lastTry = now; }
  if (now - lastTry >= 8000) {              // هر ۸ ثانیه یک تلاش
    lastTry = now;
    Serial.println("[WiFi] Reconnecting...");
    if (now - downSince > 60000) {          // بعد از ۱ دقیقه: شروع کامل از نو
      WiFi.disconnect(false);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      downSince = now;
    } else {
      WiFi.reconnect();                     // اتصال را قطع نمی‌کند؛ فقط دوباره تلاش می‌کند
    }
  }
}

void loop() {
  server.handleClient();
  wifiWatch();
#if IO_BRIDGE_ENABLED
  ioBridgeLoop();
#endif
  delay(1);
}
