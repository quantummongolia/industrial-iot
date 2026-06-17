/*
 * ============================================================
 *  ESP32-S3 N8R2 industrialMachine — USB Host Serial Monitor
 * ============================================================
 *
 *  Энэ төхөөрөмж нь USB HOST болж бусад ESP-г USB-C-ээр шууд залгахад тэдгээрийн
 *  serial гаралтыг уншиж 0.92" I2C OLED (SSD1306, 0x3C) дээр realtime хэвлэнэ.
 *  Дэмжих чип: CDC-ACM (S3 native USB), CP210x ба CH34x (WROOM devkit), FTDI —
 *  өөрөөр хэлбэл S3-Zero / WROOM / S3 N8R8 / N8R2 бүгдийг уншина.
 *
 *  • USB төхөөрөмж залгагдсан үед → түүний serial OLED дээр гүйнэ.
 *  • Залгаагүй үед → OLED дээр "NOT CONNECTED" + хувилбар/IP.
 *  • WiFi: нээлттэй (нууц үггүй), интернэттэй сүлжээ рүү автоматаар холбогдоно.
 *  • OTA: дашбоардаас Firebase-ээр шинэчлэлт хүлээж авна.
 *
 *  Холболт:
 *    USB-C (native, GPIO19/20) → монитор хийх ESP-ийн USB-C порт
 *    OLED SCL→GPIO10, SDA→GPIO11, VCC/GND→3V3/GND
 *    UART0 (GPIO43 TX / 44 RX) → өөрийн debug лог (заавал биш)
 *
 *  Анхаар: `ARDUINO_USB_CDC_ON_BOOT=0` тул native USB нь HOST, өөрийн `Serial`
 *  нь UART0 руу гарна. Дахин flash хийхэд BOOT горимд гараар оруулах эсвэл OTA.
 */

#include "secrets.h"
#include "ota.h"
#include "display.h"
#include <Arduino.h>
#include <USBHostSerial.h>
#include <Firebase_ESP_Client.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>
#include <esp_task_wdt.h>

namespace cfg {
constexpr uint32_t MONITOR_BAUD = 115200;     // монитор хийх ESP-ийн baud (репо даяар 115200)
constexpr uint32_t WDT_TIMEOUT_S = 30;
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t WIFI_TRY_MS = 12000;       // нэг сүлжээнд холбогдох хугацаа
constexpr uint32_t OTA_TICK_MS = 2000;
constexpr uint32_t RENDER_MS = 80;            // OLED refresh
constexpr uint32_t STATUS_MS = 1000;          // NOT CONNECTED статусын мөр шинэчлэх
} // namespace cfg

// ── USB Host serial — CDC-ACM/CP210x/CH34x/FTDI автомат таних ──
USBHostSerial hostSerial;

// ── Firebase ──
FirebaseData   fbData;
FirebaseAuth   fbAuth;
FirebaseConfig fbConfig;

bool          otaStarted = false;
unsigned long lastWifiTry = 0;
unsigned long lastOtaTick = 0;
unsigned long lastRender = 0;
unsigned long lastStatus = 0;
bool          lastUsbState = false;

// USBHostSerial доторх лог → UART0 руу
void usbHostLogger(const char* msg) {
  Serial.print("[USBHostSerial] ");
  Serial.println(msg);
}

// ── WiFi ────────────────────────────────────────────────────────────────
// Интернэт жинхэнэ ажиллаж байгаа эсэхийг шалгана (captive portal-ыг шүүнэ).
bool internetOk() {
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  if (!http.begin("http://connectivitycheck.gstatic.com/generate_204")) return false;
  int code = http.GET();
  http.end();
  return code == 204;
}

bool wifiTry(const char* ssid, const char* pass) {
  Serial.printf("[WiFi] Trying '%s' ...", ssid);
  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < cfg::WIFI_TRY_MS) {
    delay(300);
    esp_task_wdt_reset();
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(" fail");
    return false;
  }
  Serial.printf(" got IP %s — checking internet...\n", WiFi.localIP().toString().c_str());
  if (!internetOk()) {
    Serial.println("[WiFi] No internet on this network — skipping");
    WiFi.disconnect(true);
    return false;
  }
  Serial.println("[WiFi] Connected with internet");
  return true;
}

// 1) Тохируулсан сүлжээ (secrets.h, заавал биш) → 2) нээлттэй сүлжээнүүдийг
// RSSI-аар эрэмбэлж дараалан оролдоно. Интернэттэй эхнийхийг сонгоно.
void wifiAutoConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  if (strlen(WIFI_SSID) > 0) {
    if (wifiTry(WIFI_SSID, WIFI_PASSWORD)) return;
  }

  Serial.println("[WiFi] Scanning for open networks...");
  esp_task_wdt_reset();
  int n = WiFi.scanNetworks();
  esp_task_wdt_reset();
  if (n <= 0) {
    Serial.println("[WiFi] No networks found");
    return;
  }

  // Нээлттэй (encryption == OPEN) сүлжээнүүдийг RSSI буурахаар эрэмбэлнэ.
  int order[40];
  int cnt = 0;
  for (int i = 0; i < n && cnt < 40; i++) {
    if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) order[cnt++] = i;
  }
  for (int a = 0; a < cnt - 1; a++)
    for (int b = a + 1; b < cnt; b++)
      if (WiFi.RSSI(order[b]) > WiFi.RSSI(order[a])) {
        int t = order[a]; order[a] = order[b]; order[b] = t;
      }

  Serial.printf("[WiFi] %d open network(s) found\n", cnt);
  for (int i = 0; i < cnt; i++) {
    String ssid = WiFi.SSID(order[i]);
    if (ssid.length() == 0) continue;
    if (wifiTry(ssid.c_str(), "")) {
      WiFi.scanDelete();
      return;
    }
  }
  WiFi.scanDelete();
  Serial.println("[WiFi] No open network with internet");
}

void firebaseInit() {
  fbConfig.api_key = FIREBASE_API_KEY;
  fbConfig.database_url = FIREBASE_DB_URL;
  fbAuth.user.email = FIREBASE_USER_EMAIL;
  fbAuth.user.password = FIREBASE_USER_PASS;
  fbConfig.token_status_callback = tokenStatusCallback;
  fbData.setBSSLBufferSize(2048, 2048);
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectNetwork(true);
  Serial.println("[Firebase] init — authenticating");
}

// NOT CONNECTED дэлгэцийн доод мөр — хувилбар + IP/WiFi төлөв.
void updateStatusLine() {
  char buf[26];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof(buf), "v%s %s", ota::firmwareVersion,
             WiFi.localIP().toString().c_str());
  } else {
    snprintf(buf, sizeof(buf), "v%s no-wifi", ota::firmwareVersion);
  }
  disp::setStatus(buf);
}

void setup() {
  // UART0 — өөрийн debug лог (native USB нь host тул USB-CDC console байхгүй).
  Serial.begin(115200);
  delay(300);

  disp::begin();  // OLED — олдохгүй бол non-fatal

  Serial.println("\n===== industrialMachine (USB Host Serial Monitor) =====");

  // USB host serial — CDC-ACM/CP210x/CH34x/FTDI автомат
  hostSerial.setLogger(usbHostLogger);
  hostSerial.begin(cfg::MONITOR_BAUD, 0 /*1 stopbit*/, 0 /*no parity*/, 8 /*databits*/);

  // WDT-г WiFi скан/холболтоос ӨМНӨ асаана — scan blocking тул reset хийнэ.
  esp_task_wdt_config_t wdtConfig = {.timeout_ms = cfg::WDT_TIMEOUT_S * 1000,
                                     .idle_core_mask = 0,
                                     .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdtConfig);
  esp_task_wdt_add(NULL);

  wifiAutoConnect();
  firebaseInit();

  updateStatusLine();
  disp::setConnected((bool)hostSerial);
  disp::render();
}

void loop() {
  esp_task_wdt_reset();
  unsigned long now = millis();

  // ── WiFi maintenance ──
  if (WiFi.status() != WL_CONNECTED && now - lastWifiTry >= cfg::WIFI_RETRY_MS) {
    lastWifiTry = now;
    Serial.println("[WiFi] Disconnected — re-running auto-connect");
    wifiAutoConnect();
  }

  // ── Firebase / OTA ──
  if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
    if (!otaStarted) {
      otaStarted = true;
      Serial.println("[Firebase] ready");
      ota::begin(&fbData);
    }
    if (now - lastOtaTick >= cfg::OTA_TICK_MS) {
      lastOtaTick = now;
      ota::loop(&fbData, true);  // heartbeat + self-test (төхөөрөмж эрүүл online)
    }
  }

  // ── USB host serial → OLED + UART0 echo ──
  bool usb = (bool)hostSerial;
  if (usb != lastUsbState) {
    lastUsbState = usb;
    disp::setConnected(usb);
    Serial.printf("[USB] device %s\n", usb ? "connected" : "disconnected");
  }
  if (usb) {
    uint8_t buf[128];
    size_t got;
    while ((got = hostSerial.read(buf, sizeof(buf))) > 0) {
      disp::feed(buf, got);
      Serial.write(buf, got);     // UART0-руу давхар (debug)
      if (got < sizeof(buf)) break;
    }
  }

  // ── OLED refresh + статус ──
  if (now - lastStatus >= cfg::STATUS_MS) {
    lastStatus = now;
    updateStatusLine();
  }
  if (now - lastRender >= cfg::RENDER_MS) {
    lastRender = now;
    disp::render();
  }
}
