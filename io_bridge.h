#pragma once
/*
 * پل I/O: وب‌اپ با درخواست‌های کوتاه GET به /io وضعیت پین‌ها را می‌خواند و می‌نویسد.
 *
 *   GET /io?i=34,32u,33d&o=2:1,4:0
 *       i = پین‌های ورودی (پسوند u = pull-up ، d = pull-down)
 *       o = پین‌های خروجی به‌شکل پین:سطح
 *     پاسخ: {"in":{"34":1,"32":0,"33":1},"t":12345}
 *     پین‌هایی که در درخواست نیستند آزاد می‌شوند (INPUT شناور).
 *
 *   GET /io/caps  ->  {"in":[...],"out":[...]}
 *
 * فقط پین‌های لیست‌شده در config.h قابل استفاده‌اند.
 */
#include <Arduino.h>
#include <WebServer.h>
#include "config.h"

namespace iob {

static const uint8_t PINS_IN[]  = { IO_PINS_IN };
static const uint8_t PINS_OUT[] = { IO_PINS_OUT };

enum : uint8_t { FREE = 0, IN_FLOAT, IN_PU, IN_PD, OUTPUT_PIN };
static uint8_t  cfg[40];
static uint32_t lastSync = 0;
static bool     anyActive = false;
static WebServer* srv = nullptr;

static bool has(const uint8_t* a, size_t n, int p) {
  for (size_t i = 0; i < n; i++) if (a[i] == p) return true;
  return false;
}
static bool okIn(int p)  { return p >= 0 && p < 40 && has(PINS_IN,  sizeof(PINS_IN),  p); }
static bool okOut(int p) { return p >= 0 && p < 40 && has(PINS_OUT, sizeof(PINS_OUT), p); }

static void release(int p) {
  if (cfg[p] == OUTPUT_PIN) digitalWrite(p, LOW);
  pinMode(p, INPUT);
  cfg[p] = FREE;
}

static void releaseAll() {
  for (int p = 0; p < 40; p++) if (cfg[p] != FREE) release(p);
  anyActive = false;
}

static void sendCors() {
#if IO_ALLOW_CORS
  srv->sendHeader("Access-Control-Allow-Origin", "*");
#endif
}

static void handleSync() {
  uint8_t want[40] = {0};
  uint8_t lvl[40]  = {0};

  // ---- ورودی‌ها: i=34,32u,33d
  String s = srv->arg("i");
  int start = 0;
  while (start < (int)s.length()) {
    int e = s.indexOf(',', start);
    if (e < 0) e = s.length();
    String t = s.substring(start, e);
    start = e + 1;
    if (t.length() == 0) continue;
    int p = t.toInt();
    char suf = t[t.length() - 1];
    if (!okIn(p)) continue;
    uint8_t m = IN_FLOAT;
    if (p < 34) m = (suf == 'u') ? IN_PU : (suf == 'd') ? IN_PD : IN_FLOAT;  // 34-39: بدون pull داخلی
    want[p] = m;
  }

  // ---- خروجی‌ها: o=2:1,4:0   (روی ورودی هم‌پین اولویت دارد)
  s = srv->arg("o");
  start = 0;
  while (start < (int)s.length()) {
    int e = s.indexOf(',', start);
    if (e < 0) e = s.length();
    String t = s.substring(start, e);
    start = e + 1;
    int c = t.indexOf(':');
    if (c < 1) continue;
    int p = t.substring(0, c).toInt();
    if (!okOut(p)) continue;
    want[p] = OUTPUT_PIN;
    lvl[p]  = t.substring(c + 1).toInt() ? 1 : 0;
  }

  // ---- اعمال تنظیمات
  anyActive = false;
  for (int p = 0; p < 40; p++) {
    if (want[p] != cfg[p]) {
      if (want[p] == FREE) { release(p); continue; }
      switch (want[p]) {
        case IN_FLOAT:   pinMode(p, INPUT);          break;
        case IN_PU:      pinMode(p, INPUT_PULLUP);   break;
        case IN_PD:      pinMode(p, INPUT_PULLDOWN); break;
        case OUTPUT_PIN: pinMode(p, OUTPUT);         break;
      }
      cfg[p] = want[p];
    }
    if (want[p] == OUTPUT_PIN) digitalWrite(p, lvl[p]);
    if (want[p] != FREE) anyActive = true;
  }
  lastSync = millis();

  // ---- پاسخ
  String j;
  j.reserve(256);
  j += F("{\"in\":{");
  bool first = true;
  for (int p = 0; p < 40; p++) {
    if (cfg[p] >= IN_FLOAT && cfg[p] <= IN_PD) {
      if (!first) j += ',';
      first = false;
      j += '"'; j += p; j += F("\":"); j += (digitalRead(p) ? 1 : 0);
    }
  }
  j += F("},\"t\":"); j += (unsigned long)millis(); j += '}';
  srv->sendHeader("Cache-Control", "no-store");
  sendCors();
  srv->send(200, "application/json", j);
}

static void handleCaps() {
  String j = F("{\"in\":[");
  for (size_t i = 0; i < sizeof(PINS_IN); i++)  { if (i) j += ','; j += PINS_IN[i]; }
  j += F("],\"out\":[");
  for (size_t i = 0; i < sizeof(PINS_OUT); i++) { if (i) j += ','; j += PINS_OUT[i]; }
  j += F("]}");
  srv->sendHeader("Cache-Control", "no-store");
  sendCors();
  srv->send(200, "application/json", j);
}

}  // namespace iob

inline void ioBridgeBegin(WebServer& server) {
  iob::srv = &server;
  server.on("/io", HTTP_GET, iob::handleSync);
  server.on("/io/caps", HTTP_GET, iob::handleCaps);
}

// در loop() صدا بزنید: اگر مرورگر قطع شد، خروجی‌ها را امن می‌کند.
inline void ioBridgeLoop() {
#if IO_WATCHDOG_MS > 0
  if (iob::anyActive && millis() - iob::lastSync > IO_WATCHDOG_MS) iob::releaseAll();
#endif
}
