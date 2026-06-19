/*
 * ============================================================
 *  ESP32-S3-Zero Flowmeter (×3) → Firebase Realtime Database
 * ============================================================
 *
 * Hardware setup:
 *   - Waveshare ESP32-S3-Zero (4MB flash, USB-CDC serial)
 *   - MAX485 RS485 transceiver module
 *   - Flowmeter 1 (Modbus RTU, Slave ID 2, 9600 baud, 8N1)
 *   - Flowmeter 2 (Modbus RTU, Slave ID 3, 9600 baud, 8N1)
 *   - Flowmeter 3 (Modbus RTU, Slave ID 4, 9600 baud, 8N1)
 *
 *  Pinout (S3-Zero, mirrors Teerem):
 *    GPIO 4 — MAX485 RO  (RX)
 *    GPIO 5 — MAX485 DI  (TX)
 *    GPIO 6 — MAX485 DE+RE
 *    GPIO 21 — WS2812 onboard RGB LED (status indicator)
 *
 * Required libraries (configured in platformio.ini):
 *   - Firebase Arduino Client Library by Mobizt
 *
 * Data flow:
 *   ESP32-S3 → RS485 bus → Flowmeters (Modbus RTU) → Modbus class → Firebase RTDB
 *
 * Remote OTA:
 *   • ota::begin() устгана /devices/flowmeter_<mac> heartbeat + /commands stream
 *   • Дашбоардаас Ping/Reboot/Update команд хүлээж авна
 *   • Шинэ firmware-г GitHub releases-ээс татаж 2-slot OTA-р суулгана
 */

#include "secrets.h"

// Нөөц WiFi (Photon). Тодорхойлоогүй хуучин secrets.h-тэй ажиллах — хоосон = нөөцгүй.
#ifndef WIFI_SSID2
#define WIFI_SSID2 ""
#define WIFI_PASSWORD2 ""
#endif // WiFi and Firebase credentials (not tracked in git)
#include "ota.h"     // Remote OTA + heartbeat (DEVICE_ID = flowmeter_<mac>)
#include <Arduino.h> // Arduino core library for ESP32
#include <Firebase_ESP_Client.h> // Firebase client library for ESP32
#include <WiFi.h>                // WiFi connectivity library
#include <addons/RTDBHelper.h>   // Firebase Realtime Database helper functions
#include <addons/TokenHelper.h>  // Firebase authentication token helper
#include <esp_task_wdt.h>        // ESP32 hardware watchdog timer library

// ========================== CONFIGURATION ==========================

namespace cfg {
// MAX485 RS485 transceiver pins
constexpr uint8_t DE_RE = 6;  // MAX485 RE+DE (HIGH = transmit, LOW = receive)
constexpr uint8_t RX_PIN = 4; // MAX485 RO  (Serial1 RX)
constexpr uint8_t TX_PIN = 5; // MAX485 DI  (Serial1 TX)

// Modbus slave addresses (3 flowmeters + 1 ultrasonic level transmitter)
constexpr uint8_t FM1_SLAVE = 2;  // Суларсан уусмал
constexpr uint8_t FM2_SLAVE = 3;  // Баян уусмал
constexpr uint8_t FM3_SLAVE = 4;  // Суларсан уусмал 2
constexpr uint8_t ULS_SLAVE = 1;  // Supmea ultrasonic level transmitter (Суларсан уусмал савны түвшин)
constexpr uint32_t BAUD = 9600;

// Modbus holding register addresses
constexpr uint16_t REG_FLOW_RATE = 0x0000; // Reg[00-01]: Flow rate (float BE)
constexpr uint16_t REG_TOTALIZER = 0x0003; // Reg[03-06]: int32 + float fraction
constexpr uint16_t REG_ULS_LEVEL = 0x2002; // Ultrasonic Level instantaneous (raw/1000 = m)
constexpr uint16_t REG_ULS_MOUNT = 0x2009; // Ultrasonic Mount Height — суурилуулалтын нийт өндөр (raw/1000 = m, W/R)

// Timing
constexpr uint32_t READ_INTERVAL_MS = 3000;        // Flow rate read interval (also tick rate)
constexpr uint32_t TOTALIZER_INTERVAL_MS = 60000;  // Totalizer уншилт — 1 минут тутамд
constexpr uint32_t WIFI_RETRY_MS = 10000;
constexpr uint8_t  FB_FAILOVER_STREAK = 5;               // Mandal дээр Firebase энэ удаа дараалан алдвал нөөц рүү
constexpr uint32_t BACKUP_HOLD_MS = 2UL * 60UL * 1000UL; // Photon дээр энэ хугацаанд барина; дараа нь Mandal-ыг дахин үзнэ          // WiFi reconnect probe
constexpr uint32_t WDT_TIMEOUT_S = 30;             // Watchdog timeout
constexpr uint32_t RX_TMO = 100;                   // Modbus receive timeout (ms)
constexpr uint32_t MODBUS_RETRY_DELAY_MS = 50;     // Уншилт амжилтгүй болсон үед хүлээх хугацаа

// Auto-recovery thresholds
constexpr uint8_t MAX_CONSECUTIVE_READ_FAILS = 10;
constexpr uint8_t MAX_TOTAL_RECOVERY_FAILS = 20;

// Long-running stability:
// - Heap free 20KB-аас доош унавал тогтворгүй болохын өмнөхөн соft restart хийнэ
// - 24 цаг ажилласны дараа сэргэлтийн restart (heap fragmentation, TLS leak г.м.)
constexpr uint32_t MIN_FREE_HEAP_BYTES = 20480;     // 20KB threshold
constexpr uint32_t MAX_UPTIME_MS = 24UL * 60UL * 60UL * 1000UL;  // 24 цаг
constexpr uint32_t HEAP_LOG_INTERVAL_MS = 5UL * 60UL * 1000UL;   // 5 минут тутамд heap log
} // namespace cfg

// Firebase Realtime Database paths (string literals — keep as macros)
#define FB_PATH_FM1_FLOW "/flow_system/flowmeter1/flow_rate"
#define FB_PATH_FM1_TOTAL "/flow_system/flowmeter1/totalizer"
#define FB_PATH_FM2_FLOW "/flow_system/flowmeter2/flow_rate"
#define FB_PATH_FM2_TOTAL "/flow_system/flowmeter2/totalizer"
#define FB_PATH_FM3_FLOW "/flow_system/flowmeter3/flow_rate"
#define FB_PATH_FM3_TOTAL "/flow_system/flowmeter3/totalizer"
#define FB_PATH_LAST_UPDATED "/flow_system/last_updated"

// ========================== RGB STATUS LED ==========================
// WS2812 on GPIO 21 (Waveshare ESP32-S3-Zero onboard).
//   SLOW_RED — WiFi / Modbus / Firebase аль нэг нь алдсан үед удаан анивчина
//              (500ms on / 500ms off → 1 Hz). Энэ логик хэвээр үлдсэн.
//   pulse()  — Firebase-д амжилттай upload болгонд нэг богино ногоон импульс
//              (~120ms). Modbus poll-ийн 1 Hz ритмтэй синхрон —
//              terminal-ийн "[Firebase] Updated" мөртэй яг адил хэмнэлтэй.
namespace led {
constexpr uint8_t PIN = 21;
constexpr uint8_t BRIGHTNESS = 60;
constexpr uint16_t SLOW_ON_MS = 500;  // улаан blink ON
constexpr uint16_t SLOW_OFF_MS = 500; // улаан blink OFF
constexpr uint16_t PULSE_MS = 120;    // нэг ногоон импульсийн үргэлжлэх хугацаа

inline void writeRGB(uint8_t r, uint8_t g, uint8_t b) {
  // Энэ board-ийн WS2812 чип нь R/G сувгуудыг солиод эмиттэр-лж байна
  // (rgbLedWrite-ийн GRB conversion нь зарим S3-Zero batch-д таарахгүй).
  // Тиймээс r ба g-г солиод дамжуулна — дуудлагын талд "red"/"green"
  // гэсэн утга хэвээр.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  rgbLedWrite(PIN, g, r, b);
#else
  neopixelWrite(PIN, g, r, b);
#endif
}

inline uint8_t scale(uint8_t v) { return (uint16_t(v) * BRIGHTNESS) / 255; }

enum Mode { OFF, SLOW_RED };
// Boot үед өгөгдөл хараахан явуулаагүй учир улаан анивчиж эхэлнэ.
// Эхний амжилттай upload-аас хойш OFF болж ногоон pulse-ээр л асна.
Mode mode = SLOW_RED;
unsigned long nextToggle = 0;
unsigned long pulseOffAt = 0;
bool on = false;

void begin() { writeRGB(0, 0, 0); }

// Background blink mode. OFF = no blink (зөвхөн pulse-аар асаана).
void setMode(Mode m) {
  if (m == mode)
    return;
  mode = m;
  on = false;
  nextToggle = millis();
  if (m == OFF)
    writeRGB(0, 0, 0);
}

// One-shot green pulse — амжилттай upload болгонд дуудагдана.
// Аливаа background blink-ийг түр давж ногоон гэрэлтүүлнэ.
void pulse() {
  pulseOffAt = millis() + PULSE_MS;
  writeRGB(0, scale(255), 0);
}

// Non-blocking driver — ledTask-аас ~50 Hz-ийн давтамжтай дуудагдана.
void update() {
  unsigned long now = millis();

  // Ногоон импульс идэвхтэй бол blink-ийг түр зогсооно.
  if (pulseOffAt) {
    if (now < pulseOffAt)
      return;
    pulseOffAt = 0;
    writeRGB(0, 0, 0);
    on = false;
    nextToggle = now; // blink фазыг цэвэрхэн restart
  }

  if (mode == OFF)
    return;

  if (now < nextToggle)
    return;
  if (on) {
    writeRGB(0, 0, 0);
    on = false;
    nextToggle = now + SLOW_OFF_MS;
  } else {
    writeRGB(scale(255), 0, 0); // SLOW_RED
    on = true;
    nextToggle = now + SLOW_ON_MS;
  }
}
} // namespace led

// LED task — pinned to core 0 so Modbus / Firebase blocking on core 1 (where
// the Arduino loop runs) can't stall the blink cadence.
void ledTask(void *) {
  for (;;) {
    led::update();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ========================== MODBUS CLASS =========================
// Teerem firmware-тэй ижил Modbus pattern. readFloat / readTotalizer нь
// flowmeter-ийн ашигладаг хоёр гол функц. recover() нь UART buffer-ийг
// цэвэрлэж Serial1-г restart хийдэг.
class Modbus {
public:
  void begin() {
    pinMode(cfg::DE_RE, OUTPUT);
    digitalWrite(cfg::DE_RE, LOW);
    Serial1.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);
  }

  void recover() {
    // 1. UART-ыг бүрэн салгах
    Serial1.end();

    // 2. MAX485-ийг хүчээр RX горимд оруулна — TX drive-аа тавих
    pinMode(cfg::DE_RE, OUTPUT);
    digitalWrite(cfg::DE_RE, LOW);
    delay(100);

    // 3. RX шугамыг түр INPUT_PULLUP болгож idle (logical high) болгоно.
    //    Энэ нь зарим тохиолдолд "stuck" болсон transceiver-ийг сэргээдэг.
    pinMode(cfg::RX_PIN, INPUT_PULLUP);
    delay(50);

    // 4. UART-г дахин эхлүүлэх
    Serial1.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);
    delay(20);

    // 5. RX буфер цэвэрлэх
    while (Serial1.available())
      Serial1.read();
  }

  // Big-Endian 16-bit unsigned register (ultrasonic level register 0x2002)
  bool readShort(uint8_t slave, uint16_t addr, uint16_t &out) {
    uint8_t rx[7];
    if (!readRegs(slave, addr, 1, rx))
      return false;
    out = ((uint16_t)rx[3] << 8) | rx[4];
    return true;
  }

  // Big-Endian 32-bit IEEE 754 float (flow rate)
  bool readFloat(uint8_t slave, uint16_t addr, float &out) {
    uint8_t rx[9];
    if (!readRegs(slave, addr, 2, rx))
      return false;
    uint32_t raw = (uint32_t)rx[3] << 24 | (uint32_t)rx[4] << 16 |
                   (uint32_t)rx[5] << 8 | rx[6];
    memcpy(&out, &raw, 4);
    return true;
  }

  // Урсгал хэмжигч totalizer — 4 register (8 byte):
  // [0..3]  UINT32 BE = бүхэл хэсэг
  // [4..7]  Float BE  = бутархай хэсэг
  // Нийт нь: float (бүхэл + бутархай) cubic meter.
  bool readTotalizer(uint8_t slave, uint16_t addr, float &out) {
    uint8_t rx[13];
    if (!readRegs(slave, addr, 4, rx))
      return false;
    uint32_t intPart = ((uint32_t)rx[3] << 24) | ((uint32_t)rx[4] << 16) |
                       ((uint32_t)rx[5] << 8)  |  (uint32_t)rx[6];
    uint32_t fracRaw = ((uint32_t)rx[7] << 24) | ((uint32_t)rx[8] << 16) |
                       ((uint32_t)rx[9] << 8)  |  (uint32_t)rx[10];
    float fracPart;
    memcpy(&fracPart, &fracRaw, 4);
    out = (float)((double)intPart + (double)fracPart);
    return true;
  }

private:
  bool readRegs(uint8_t slave, uint16_t addr, uint8_t regCnt, uint8_t *rx) {
    while (Serial1.available())
      Serial1.read();

    uint8_t req[8] = {slave,        0x03,    uint8_t(addr >> 8),
                      uint8_t(addr), 0,       regCnt,
                      0,             0};
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF;
    req[7] = c >> 8;

    send(req, sizeof(req));

    const uint8_t respLen = 5 + regCnt * 2;
    if (!receive(rx, respLen))
      return false;
    if (rx[0] != slave || rx[1] != 0x03 || rx[2] != regCnt * 2)
      return false;
    if (crc16(rx, respLen - 2) !=
        uint16_t(rx[respLen - 2] | (rx[respLen - 1] << 8)))
      return false;
    return true;
  }

  static uint16_t crc16(const uint8_t *buf, size_t len) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
      c ^= buf[i];
      for (int b = 0; b < 8; b++)
        c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
    }
    return c;
  }

  void send(const uint8_t *data, size_t len) {
    digitalWrite(cfg::DE_RE, HIGH);
    delayMicroseconds(50);          // DE өндөр болсны дараа драйвер идэвхжих хугацаа
    Serial1.write(data, len);
    Serial1.flush();                // TX register-г бүрэн хоослох
    // 9600 baud дээр нэг байт ≈ 1.04ms. flush() дууссаны дараа MAX485 шугам
    // тогтворжих хүртэл хүлээж DE/RE-г салгана — өмнө нь 100µs нь хэт богино
    // байсан тул сүүлийн bit таслагдаж slave хариу өгөхгүй болж байсан.
    delayMicroseconds(1200);
    digitalWrite(cfg::DE_RE, LOW);
    delayMicroseconds(200);         // RX горим идэвхжих, bus settle
  }

  bool receive(uint8_t *buf, size_t want) {
    size_t got = 0;
    unsigned long start = millis();
    while (millis() - start < cfg::RX_TMO && got < want) {
      if (Serial1.available())
        buf[got++] = Serial1.read();
    }
    return got == want;
  }
};

Modbus modbus;

// ========================== GLOBAL OBJECTS ==========================

FirebaseData fbData; // Firebase data object for storing request/response data
FirebaseAuth fbAuth; // Firebase authentication credentials holder
FirebaseConfig fbConfig; // Firebase configuration settings holder

unsigned long lastReadTime = 0;      // Timestamp of last flow rate reading (ms)
unsigned long lastTotalizerReadTime = 0; // Сүүлд totalizer уншсан агшин (ms)
unsigned long lastWifiCheck = 0;     // Timestamp of last WiFi check (ms)
unsigned long lastHeapLogTime = 0;   // Сүүлд heap-н хэмжээг log хийсэн агшин
bool firebaseReady = false;          // Firebase auth complete flag

// Totalizer утга 1 минутад нэг л шинэчлэгдэх тул хооронд нь cache-д хадгална.
// Анхны утга 0.0 нь Firebase upload-д орохгүй — `cachedTotal*Valid` flag
// эхний амжилттай уншилтын дараа л true болж, шинэчлэлт эхэлнэ.
float cachedTotal1 = 0.0f, cachedTotal2 = 0.0f, cachedTotal3 = 0.0f;
bool  cachedTotal1Valid = false, cachedTotal2Valid = false, cachedTotal3Valid = false;

// Ultrasonic mount height (суурилуулалтын нийт өндөр, рег. 0x2009) — boot дээр
// нэг л удаа уншиж, Firebase-д нэг удаа нийтэлнэ. Дашбоард савны дээд хязгаарт
// (data-max) үүнийг ашиглана — HTML дээр гараар тохируулах шаардлагагүй.
float ulsMountM = 0.0f;
bool  ulsMountKnown = false;
bool  ulsMountSent = false;

// Firebase upload backoff — prevents auth rate-limit storms on failure
unsigned long fbNextAllowedAt = 0; // Next allowed upload timestamp
unsigned int fbFailStreak = 0;
bool wifiOnBackup = false;            // одоо нөөц сүлжээ (Photon) дээр байна уу
unsigned long primaryRetryAt = 0;     // нөөц дээр байх үед энэ хугацаанд Mandal-ыг дахин шалгана     // Consecutive upload failure count

// Modbus read failure recovery state
unsigned int consecutiveReadFails = 0; // Straight failed read cycles
unsigned int totalRecoveryAttempts = 0; // Recovery escalations since last success

// ========================== WI-FI CONNECTION =========================

// WiFi event handler — auto-triggers reconnection on any disconnect event.
void onWifiEvent(WiFiEvent_t event) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    Serial.println("[WiFi] Disconnected — attempting auto-reconnect");
    WiFi.reconnect();
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    Serial.printf("[WiFi] Reconnected — IP: %s\n",
                  WiFi.localIP().toString().c_str());
    break;
  default:
    break;
  }
}

/*
 * Connect to WiFi network using credentials from secrets.h.
 * Skips if already connected. Waits up to 15 seconds for connection.
 * setAutoReconnect + WiFi event handler keep the link up automatically after
 * this initial connect, so loop-level polling is just a fallback.
 */
// Нөөц сүлжээ (Photon) тохируулсан эсэх.
static bool wifiHasBackup() { return WIFI_SSID2[0] != '\0'; }

// Нэг сүлжээнд блоклон холбогдохыг оролдоно. Амжилттай бол true.
static bool wifiTry(const char *ssid, const char *pass, uint32_t timeoutMs) {
  Serial.printf("[WiFi] Connecting to network: %s", ssid);
  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected to %s — IP: %s\n", ssid,
                  WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.printf("\n[WiFi] %s failed\n", ssid);
  return false;
}

// Strict-priority холболт: ҮРГЭЛЖ эхэлж үндсэн (Mandal) сүлжээнд оролдоно, зөвхөн
// бүтэхгүй үед нөөц (Photon) рүү шилжинэ. Дохионы хүчээр (RSSI) сонгохгүй.
void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED)
    return;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  if (wifiTry(WIFI_SSID, WIFI_PASSWORD, 10000)) {
    wifiOnBackup = false;
    return;
  }
  if (wifiHasBackup() && wifiTry(WIFI_SSID2, WIFI_PASSWORD2, 10000)) {
    wifiOnBackup = true;
    primaryRetryAt = millis() + cfg::BACKUP_HOLD_MS;
    return;
  }
  Serial.println("[WiFi] All networks failed — will retry later");
}

// Холбоотой үед ажиллах failover шийдвэр:
//  • Mandal дээр байгаа ч Firebase рүү өгөгдөл явахгүй (fail streak) бол → Photon руу.
//  • Photon дээр байгаа бол BACKUP_HOLD_MS тутамд Mandal сэргэсэн эсэхийг шалгана.
void wifiFailover(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED || !wifiHasBackup())
    return;

  if (!wifiOnBackup) {
    // Үндсэн (Mandal) дээр. Firebase тогтмол алдаж, throttle гарсан бол нөөц рүү.
    if (fbFailStreak >= cfg::FB_FAILOVER_STREAK && now >= primaryRetryAt) {
      Serial.printf("[WiFi] Primary up but Firebase failing (streak %u) — trying backup\n",
                    fbFailStreak);
      primaryRetryAt = now + cfg::BACKUP_HOLD_MS;
      if (wifiTry(WIFI_SSID2, WIFI_PASSWORD2, 10000)) {
        wifiOnBackup = true;
      } else {
        Serial.println("[WiFi] Backup unavailable — staying on primary");
        wifiTry(WIFI_SSID, WIFI_PASSWORD, 10000);
      }
      fbFailStreak = 0;
      fbNextAllowedAt = 0;
    }
    return;
  }

  // Нөөц (Photon) дээр. Хааяа Mandal сэргэсэн эсэхийг шалгаж, сэргсэн бол буцна.
  if (now >= primaryRetryAt) {
    primaryRetryAt = now + cfg::BACKUP_HOLD_MS;
    Serial.println("[WiFi] Re-checking primary (Mandal)...");
    if (wifiTry(WIFI_SSID, WIFI_PASSWORD, 8000)) {
      wifiOnBackup = false;
      Serial.println("[WiFi] Back on primary (Mandal)");
    } else {
      wifiTry(WIFI_SSID2, WIFI_PASSWORD2, 10000);
    }
    fbFailStreak = 0;
    fbNextAllowedAt = 0;
  }
}

// ========================== FIREBASE INITIALIZATION
// ===============================

/*
 * Initialize Firebase connection with API key, database URL, and user
 * credentials. Uses email/password authentication method. Called once during
 * setup().
 */
void firebaseInit() {
  fbConfig.api_key = FIREBASE_API_KEY;     // Set Firebase project API key
  fbConfig.database_url = FIREBASE_DB_URL; // Set Firebase Realtime Database URL

  fbAuth.user.email = FIREBASE_USER_EMAIL;   // Set Firebase auth email
  fbAuth.user.password = FIREBASE_USER_PASS; // Set Firebase auth password

  fbConfig.token_status_callback =
      tokenStatusCallback; // Set callback for auth token status updates
  fbData.setBSSLBufferSize(
      2048, 2048); // Set SSL buffer size for secure communication

  Firebase.begin(&fbConfig,
                 &fbAuth);         // Initialize Firebase with config and auth
  Firebase.reconnectNetwork(true); // Enable automatic network reconnection

  Serial.println("[Firebase] Initialized — authenticating...");
}

// Exponential backoff: 2s → 4s → 8s → 16s → ... → 5min cap.
// Called after any Firebase.RTDB.set*() returns false.
void fbOnFailure() {
  fbFailStreak++;
  unsigned long backoff =
      2000UL * (1UL << min((unsigned int)8, fbFailStreak)); // 2s..~8min raw
  if (backoff > 300000UL)
    backoff = 300000UL; // cap at 5 min
  fbNextAllowedAt = millis() + backoff;
  Serial.printf("[Firebase] Backoff %lus (fail streak %u)\n", backoff / 1000,
                fbFailStreak);
}

void fbOnSuccess() {
  if (fbFailStreak > 0)
    Serial.println("[Firebase] Recovered");
  fbFailStreak = 0;
  fbNextAllowedAt = 0;
}

bool fbCanUpload() { return (long)(millis() - fbNextAllowedAt) >= 0; }

// ========================== MODBUS READ WITH RETRY =========================
// Slave түр хариу өгөхгүй байгаа тохиолдлыг (intermittent failure)
// нэг удаагийн дахин оролдлогоор нөхнө. 9600 baud дээр нэг уншилт ~30ms тул
// дахин оролдлого + 50ms delay = ~80ms нэмж зарцуулна. Bus contention багатай
// үед энэ нь гол шалтгаан болж байгаа учир үр дүн өндөр.
bool readFloatRetry(uint8_t slave, uint16_t reg, float &out) {
  if (modbus.readFloat(slave, reg, out))
    return true;
  delay(cfg::MODBUS_RETRY_DELAY_MS);
  return modbus.readFloat(slave, reg, out);
}

bool readTotalizerRetry(uint8_t slave, uint16_t reg, float &out) {
  if (modbus.readTotalizer(slave, reg, out))
    return true;
  delay(cfg::MODBUS_RETRY_DELAY_MS);
  return modbus.readTotalizer(slave, reg, out);
}

bool readShortRetry(uint8_t slave, uint16_t reg, uint16_t &out) {
  if (modbus.readShort(slave, reg, out))
    return true;
  delay(cfg::MODBUS_RETRY_DELAY_MS);
  return modbus.readShort(slave, reg, out);
}

// ========================== MODBUS RECOVERY =========================

/*
 * Escalating recovery when Modbus reads fail repeatedly:
 *   Level 1: modbus.recover() (UART restart + DE/RE toggle) — most common fix
 *   Level 2: hardware reboot via watchdog (last resort)
 */
void recoverModbusBus() {
  totalRecoveryAttempts++;
  Serial.printf("[Recovery] Attempt #%u after %u failed cycles\n",
                totalRecoveryAttempts, consecutiveReadFails);

  if (totalRecoveryAttempts >= cfg::MAX_TOTAL_RECOVERY_FAILS) {
    Serial.println("[Recovery] Max attempts reached — forcing reboot via WDT");
    delay(100);
    while (true) {
    } // Let watchdog reset the chip cleanly
  }

  modbus.recover();
  consecutiveReadFails = 0; // Give the bus a fresh chance
  Serial.println("[Recovery] RS485 bus re-initialized");
}

// ========================== SETUP (RUNS ONCE AT BOOT)
// ==================================

/*
 * Arduino setup function — runs once when ESP32 powers on or resets.
 * Initializes serial ports, RS485 transceiver, WiFi, Firebase, and watchdog
 * timer.
 */
void setup() {
  Serial.begin(115200); // Initialize debug serial port at 115200 baud
  delay(300);           // Brief delay for USB-CDC serial to stabilize
  Serial.println("\n========= Flowmeter IoT (S3-Zero x3) =========");

  led::begin();
  // LED runs on its own core 0 task so Modbus / Firebase blocking calls
  // on core 1 (Arduino loop) never freeze the blink animation.
  xTaskCreatePinnedToCore(ledTask, "ledTask", 2048, nullptr, 1, nullptr, 0);

  // Modbus class дотор pinMode + digitalWrite + Serial1.begin бүгдийг хийнэ
  modbus.begin();
  Serial.printf(
      "[Modbus] Initialized — Flowmeter 1: Slave %d, Flowmeter 2: Slave %d, "
      "Flowmeter 3: Slave %d\n",
      cfg::FM1_SLAVE, cfg::FM2_SLAVE, cfg::FM3_SLAVE);

  WiFi.onEvent(onWifiEvent); // Register auto-reconnect event handler BEFORE
                             // first connect
  wifiConnect();             // Connect to WiFi network
  firebaseInit();            // Initialize Firebase connection

  // Configure ESP32 hardware watchdog timer to reset the chip if code hangs
  esp_task_wdt_config_t wdtConfig = {
      .timeout_ms = cfg::WDT_TIMEOUT_S * 1000, // Timeout in milliseconds
      .idle_core_mask = 0,                // Don't watch idle tasks
      .trigger_panic = true};             // Reset ESP32 on timeout
  esp_task_wdt_reconfigure(&wdtConfig);   // Apply watchdog configuration
  esp_task_wdt_add(NULL); // Add current task to watchdog monitoring
  Serial.printf("[WDT] Watchdog started — %d second timeout\n",
                cfg::WDT_TIMEOUT_S);
}

// ========================== MAIN LOOP (RUNS REPEATEDLY) =================

/*
 * Arduino loop function — runs continuously after setup().
 * Reads both flowmeters via Modbus RTU and uploads values to Firebase.
 * Includes WiFi reconnection logic and watchdog timer reset.
 */
void loop() {
  esp_task_wdt_reset();
  unsigned long now = millis();

  // ---- Long-running stability watchdog ----
  // Heap fragmentation (Firebase TLS буфер, FirebaseJson allocation г.м.) удаан
  // ажиллах тусам хуримтлагдаж тогтворгүй болгож болзошгүй. 2 хамгаалалт:
  //   1) 5 минут тутам free heap-г log хийнэ — graphana/serial monitor дээр
  //      санах ой буурч буй эсэхийг харж болно.
  //   2) Free heap нь босгоос (20KB) доош унавал, эсвэл uptime 24 цаг хэтэрвэл
  //      cleaн restart хийж бүх ресурсыг чөлөөлнө. ESP.restart() нь
  //      крашгүй perezagruz хийдэг — Firebase, ota::loop хувилбаруудаас
  //      найдвартай. WiFi/Firebase setup() дээр дахин эхэлнэ.
  if (now - lastHeapLogTime >= cfg::HEAP_LOG_INTERVAL_MS) {
    lastHeapLogTime = now;
    Serial.printf("[Heap] Free: %u bytes | Min ever: %u | Uptime: %lus\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), now / 1000);
  }
  if (ESP.getFreeHeap() < cfg::MIN_FREE_HEAP_BYTES) {
    Serial.printf("[Stability] Free heap %u < %u — restarting\n",
                  ESP.getFreeHeap(), cfg::MIN_FREE_HEAP_BYTES);
    delay(200);
    ESP.restart();
  }
  if (now >= cfg::MAX_UPTIME_MS) {
    Serial.println("[Stability] Reached 24h uptime — scheduled restart");
    delay(200);
    ESP.restart();
  }

  // Fallback WiFi polling — onWifiEvent/setAutoReconnect handle most cases,
  // but we retry manually if the connection has been down for cfg::WIFI_RETRY_MS.
  if (WiFi.status() != WL_CONNECTED &&
      now - lastWifiCheck >= cfg::WIFI_RETRY_MS) {
    lastWifiCheck = now;
    wifiConnect();
  }
  wifiFailover(now);

  // Rate limit: 2 секунд тутамд нэг cycle (READ_INTERVAL_MS)
  if (now - lastReadTime < cfg::READ_INTERVAL_MS)
    return;
  lastReadTime = now;

  // Цикл бүрд flow-cycle эсвэл totalizer-cycle гэсэн 2 төрлийн нэг нь явна.
  // Modbus bus-н ачааллыг бууруулж, "хоёуланг нь зэрэг уншихгүй" гэсэн
  // шаардлагыг хангахын тулд totalizer-ийг тусад нь minute-тэй уншина.
  // Boot-ийн дараа нэг удаа totalizer-ийг шууд уншиж cache-г бөглөнө.
  bool totalizerCycle = (lastTotalizerReadTime == 0) ||
                        (now - lastTotalizerReadTime >= cfg::TOTALIZER_INTERVAL_MS);

  // Энэ цикл-д Firebase upload руу очих утгууд
  float flow1 = 0.0f, flow2 = 0.0f, flow3 = 0.0f;
  float total1 = 0.0f, total2 = 0.0f, total3 = 0.0f;
  bool okFlow1 = false, okFlow2 = false, okFlow3 = false;
  bool okTotal1 = false, okTotal2 = false, okTotal3 = false;

  if (totalizerCycle) {
    // ---- Totalizer cycle: 3 flowmeter-ийн totalizer-ийг л унших ----
    lastTotalizerReadTime = now;
    Serial.println("[Cycle] Totalizer read (1 min interval)");

    okTotal1 = readTotalizerRetry(cfg::FM1_SLAVE, cfg::REG_TOTALIZER, total1);
    if (okTotal1) { cachedTotal1 = total1; cachedTotal1Valid = true; }
    delay(50);

    okTotal2 = readTotalizerRetry(cfg::FM2_SLAVE, cfg::REG_TOTALIZER, total2);
    if (okTotal2) { cachedTotal2 = total2; cachedTotal2Valid = true; }
    delay(50);

    okTotal3 = readTotalizerRetry(cfg::FM3_SLAVE, cfg::REG_TOTALIZER, total3);
    if (okTotal3) { cachedTotal3 = total3; cachedTotal3Valid = true; }
    delay(50);

    Serial.printf("[Totalizer] FM1:%.3f(%d) FM2:%.3f(%d) FM3:%.3f(%d) m3\n",
                  total1, okTotal1, total2, okTotal2, total3, okTotal3);
  } else {
    // ---- Flow cycle: 3 flowmeter-ийн flow rate-ийг л унших ----
    okFlow1 = readFloatRetry(cfg::FM1_SLAVE, cfg::REG_FLOW_RATE, flow1);
    delay(50);
    okFlow2 = readFloatRetry(cfg::FM2_SLAVE, cfg::REG_FLOW_RATE, flow2);
    delay(50);
    okFlow3 = readFloatRetry(cfg::FM3_SLAVE, cfg::REG_FLOW_RATE, flow3);
    delay(50);

    Serial.printf("[Flow] FM1:%.3f(%d) FM2:%.3f(%d) FM3:%.3f(%d) m3/h\n",
                  flow1, okFlow1, flow2, okFlow2, flow3, okFlow3);
  }

  // ---- Read Ultrasonic Level Transmitter (Slave ID 1) ----
  // Register 0x2002 = Level instantaneous (uint16, decimal=3 → /1000 = m).
  // Level нь нэг л register учир хоёр төрлийн цикл-д унших — bus-д бараг
  // ачаалал өгөхгүй.
  uint16_t levelRaw = 0;
  bool okLevel = readShortRetry(cfg::ULS_SLAVE, cfg::REG_ULS_LEVEL, levelRaw);
  float ulsLevel = okLevel ? (levelRaw / 1000.0f) : 0.0f;

  if (okLevel)
    Serial.printf("[ULS] Level: %.3f m\n", ulsLevel);
  else
    Serial.println("[ULS] Read failed");

  // Mount height-г нэг л удаа (амжилттай болтол cycle бүрт оролдоно) уншина.
  if (!ulsMountKnown) {
    uint16_t mountRaw = 0;
    if (readShortRetry(cfg::ULS_SLAVE, cfg::REG_ULS_MOUNT, mountRaw) && mountRaw > 0) {
      ulsMountM = mountRaw / 1000.0f;
      ulsMountKnown = true;
      Serial.printf("[ULS] Mount height: %.3f m\n", ulsMountM);
    }
  }

  // ---- Track read failures and escalate recovery if needed ----
  bool anyReadOk = okFlow1 || okFlow2 || okFlow3 ||
                   okTotal1 || okTotal2 || okTotal3 || okLevel;
  if (anyReadOk) {
    consecutiveReadFails = 0;
    totalRecoveryAttempts = 0;
  } else {
    consecutiveReadFails++;
    if (consecutiveReadFails >= cfg::MAX_CONSECUTIVE_READ_FAILS) {
      recoverModbusBus();
    }
  }

  // WiFi сүлжээ байхгүй бол улаан анивчина — Firebase ямартай ч ажиллахгүй.
  if (WiFi.status() != WL_CONNECTED) {
    led::setMode(led::SLOW_RED);
    return;
  }

  // ---- Upload to Firebase (single multi-path PATCH → 1 HTTP request) ----
  // 6-7 setFloat дуудлагыг нэг updateNode болгож хувиргав. RS485 + 3 sensor
  // дуусаад нэг л HTTPS round-trip — loop iteration ~1.5s → ~0.7s болсон.
  if (!fbCanUpload()) {
    led::setMode(led::SLOW_RED);
    return;
  }
  if (!Firebase.ready()) {
    led::setMode(led::SLOW_RED);
    return;
  }
  if (!firebaseReady) {
    firebaseReady = true;
    Serial.println("[Firebase] Ready");
    ota::begin(&fbData); // DEVICE_ID register + командын stream нээх
  }

  FirebaseJson json;
  bool anyWrite = false;

  if (totalizerCycle) {
    // Зөвхөн totalizer-уудыг шинэчилнэ. Амжилтгүй уншилт бол cache-ийг
    // өмнөх утга дээр үлдээгээд бичихгүй (хуучин зөв утгыг 0-р дарж болохгүй).
    if (okTotal1) { json.set("flowmeter1/totalizer", cachedTotal1); anyWrite = true; }
    if (okTotal2) { json.set("flowmeter2/totalizer", cachedTotal2); anyWrite = true; }
    if (okTotal3) { json.set("flowmeter3/totalizer", cachedTotal3); anyWrite = true; }
  } else {
    if (okFlow1) { json.set("flowmeter1/flow_rate", flow1); anyWrite = true; }
    if (okFlow2) { json.set("flowmeter2/flow_rate", flow2); anyWrite = true; }
    if (okFlow3) { json.set("flowmeter3/flow_rate", flow3); anyWrite = true; }
  }
  if (okLevel)  { json.set("level_sensor/level", ulsLevel); anyWrite = true; }
  // Mount height-г нэг удаа нийтэлнэ (амжилттай upload болтол дахин оролдоно).
  if (ulsMountKnown && !ulsMountSent) { json.set("level_sensor/mount_height", ulsMountM); anyWrite = true; }

  bool uploadOk = false;
  if (!anyWrite) {
    // Modbus бүх 3 sensor энэ cycle-д хариу өгсөнгүй → улаан
    led::setMode(led::SLOW_RED);
  } else {
    json.set("last_updated", (int)(millis() / 1000));

    if (Firebase.RTDB.updateNode(&fbData, "/flow_system", &json)) {
      Serial.println("[Firebase] Updated");
      fbOnSuccess();
      led::setMode(led::OFF);
      led::pulse(); // upload бүрд нэг ногоон импульс — terminal-той synchron
      uploadOk = true;
      if (ulsMountKnown) ulsMountSent = true; // mount height нийтлэгдсэн

    } else {
      Serial.printf("[Firebase] update ERROR: %s\n", fbData.errorReason().c_str());
      fbOnFailure();
      led::setMode(led::SLOW_RED);
    }
  }

  // OTA heartbeat + progress publish + command stream tick.
  // Firebase ready болсон бөгөөд upload амжилттай эсэхийг дамжуулна —
  // self-test шалгуурт ашиглана.
  ota::loop(&fbData, uploadOk);
}
