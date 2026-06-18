/*
 * wifitools.cpp — WiFi оношлогооны үйлдлүүд.
 *
 *  Гаралт нь Serial → tee → OLED terminal дээр гарна. (display.h-ийн gLog.)
 *  WiFi нь зөвхөн оношлогоонд — Firebase/OTA байхгүй.
 */
#include "wifitools.h"
#include "display.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

// Энэ файлын Serial.print* мөн OLED дээр гарна (main.cpp-тэй ижил арга).
#define Serial gLog

namespace wifitools {
namespace {

struct WNet { const char *ssid; const char *pass; };
const WNet KNOWN[] = {
    {"Mandal Hothon", "Mandal0202"},
    {"Mandal Resource Guest", ""},
};
constexpr int KNOWN_N = sizeof(KNOWN) / sizeof(KNOWN[0]);

constexpr char TEST_URL[]  = "http://connectivitycheck.gstatic.com/generate_204";
constexpr char SPEED_URL[] = "http://speedtest.tele2.net/10MB.zip";

// RSSI-г сигналын баганаар (0..4 од) дүрсэлнэ.
const char *bars(int rssi) {
  if (rssi >= -55) return "****";
  if (rssi >= -65) return "*** ";
  if (rssi >= -75) return "**  ";
  if (rssi >= -85) return "*   ";
  return "    ";
}

// Мэдэгдэж буй сүлжээнүүдээс хамгийн хүчтэйд нь холбогдоно. Амжилттай бол индекс,
// эс бөгөөс -1. Явцаа дэлгэцэнд харуулна.
int connectBest(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  Serial.printf("scan...\n");
  disp::render();
  int n = WiFi.scanNetworks();

  // Олдсон мэдэгдэх сүлжээнүүдийг RSSI-аар эрэмбэлэхийн тулд хамгийн сайныг сонгоно.
  int bestKnown = -1, bestRssi = -999;
  for (int k = 0; k < KNOWN_N; k++) {
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == KNOWN[k].ssid && WiFi.RSSI(i) > bestRssi) {
        bestRssi = WiFi.RSSI(i);
        bestKnown = k;
      }
    }
  }
  WiFi.scanDelete();
  if (bestKnown < 0) {
    Serial.printf("no known net\n");
    return -1;
  }

  Serial.printf("conn %s\n", KNOWN[bestKnown].ssid);
  WiFi.begin(KNOWN[bestKnown].ssid, KNOWN[bestKnown].pass);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
    disp::render();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("\nfail\n");
    return -1;
  }
  Serial.printf("\nip %s\n", WiFi.localIP().toString().c_str());
  return bestKnown;
}

} // namespace

// ── 1) Сигналын хүч (RSSI) — холбогдохгүйгээр зүгээр скан ──────────────────
void signalCheck() {
  Serial.printf("== Signal ==\n");
  WiFi.mode(WIFI_STA);
  disp::render();
  int n = WiFi.scanNetworks();
  for (int k = 0; k < KNOWN_N; k++) {
    int rssi = -999;
    for (int i = 0; i < n; i++)
      if (WiFi.SSID(i) == KNOWN[k].ssid && WiFi.RSSI(i) > rssi) rssi = WiFi.RSSI(i);
    if (rssi > -999)
      Serial.printf("%s %ddBm\n", bars(rssi), rssi);
    else
      Serial.printf("---- not found\n");
    // Сүлжээний нэрийг дараагийн мөрд (богино COLS тул тусад нь).
    Serial.printf("  %.28s\n", KNOWN[k].ssid);
  }
  WiFi.scanDelete();
  Serial.printf("done\n");
}

// ── 2) Холбогдох тест — 2 сүлжээ тус бүрд оролдоно ────────────────────────
void connectTest() {
  Serial.printf("== Connect ==\n");
  for (int k = 0; k < KNOWN_N; k++) {
    WiFi.mode(WIFI_STA);
    Serial.printf("%.20s\n", KNOWN[k].ssid);
    WiFi.begin(KNOWN[k].ssid, KNOWN[k].pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
      delay(250);
      Serial.print(".");
      disp::render();
    }
    if (WiFi.status() == WL_CONNECTED)
      Serial.printf("\n OK %s\n", WiFi.localIP().toString().c_str());
    else
      Serial.printf("\n FAIL\n");
    WiFi.disconnect(true);
    delay(300);
  }
  Serial.printf("done\n");
}

// ── 3) Өгөгдөл илгээж/хүлээн авах тест — HTTP GET round-trip ───────────────
void dataTest() {
  Serial.printf("== Data ==\n");
  if (connectBest(10000) < 0) { Serial.printf("no link\n"); return; }

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  Serial.printf("GET 204...\n");
  disp::render();
  if (!http.begin(TEST_URL)) { Serial.printf("begin fail\n"); return; }
  uint32_t t0 = millis();
  int code = http.GET();
  uint32_t rtt = millis() - t0;
  int len = http.getSize();
  http.end();

  if (code > 0) {
    Serial.printf("HTTP %d\n", code);
    Serial.printf("rtt %ums\n", rtt);
    Serial.printf("recv %dB\n", len < 0 ? 0 : len);
    Serial.printf(code == 204 ? "send/recv OK\n" : "reachable\n");
  } else {
    Serial.printf("err %s\n", http.errorToString(code).c_str());
  }
}

// ── 4) Хурдны тест — файл татаж throughput хэмжинэ ─────────────────────────
void speedTest() {
  Serial.printf("== Speed ==\n");
  if (connectBest(10000) < 0) { Serial.printf("no link\n"); return; }

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(8000);
  Serial.printf("downloading...\n");
  disp::render();
  if (!http.begin(SPEED_URL)) { Serial.printf("begin fail\n"); return; }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP %d\n", code);
    http.end();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[1024];
  uint32_t total = 0;
  uint32_t start = millis();
  const uint32_t DURATION = 6000;          // 6 секунд хэмжинэ
  while (http.connected() && millis() - start < DURATION) {
    size_t avail = stream->available();
    if (avail) {
      int got = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      total += got;
      // Явцыг ~0.5с тутамд харуулна.
      static uint32_t lastShow = 0;
      if (millis() - lastShow > 500) {
        lastShow = millis();
        uint32_t el = millis() - start;
        float kbps = el ? (total * 8.0f / el) : 0;   // bytes*8/ms = kbps
        Serial.printf("%uKB %.0fkbps\n", total / 1024, kbps);
        disp::render();
      }
    } else {
      delay(1);
    }
  }
  http.end();

  uint32_t el = millis() - start;
  float kbps = el ? (total * 8.0f / el) : 0;
  Serial.printf("== %u KB ==\n", total / 1024);
  if (kbps >= 1000) Serial.printf("%.2f Mbps\n", kbps / 1000.0f);
  else              Serial.printf("%.0f kbps\n", kbps);
}

} // namespace wifitools
