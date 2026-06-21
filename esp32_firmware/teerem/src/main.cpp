/*
 * ============================================================
 *  ESP32-S3-Zero Тээрэм + Цахилгаан тоолуурууд → Firebase RTDB
 * ============================================================
 *
 *  Нэг RS485 шугам дээр Modbus RTU @ 19200bps :
 *    Slave 1 : Тээрмийн тэжээлийн жин (flow t/h + cumulative t)
 *    Slave 2 : SPM33 — Боловсруулах үйлдвэр ХС     (P, E)
 *    Slave 3 : SPM33 — Нунтаглах хэсэг ХС          (P, E)
 *    Slave 4 : SPM33 — Бөмбөлөгт тээрэм 1          (P, E, Ia/Ib/Ic)
 *    Slave 5 : SPM33 — Бөмбөлөгт тээрэм 2          (P, E, Ia/Ib/Ic)
 *    Slave 6 : Тээрмийн тэжээлийн ус (flowmeter)   (flow rate + totalizer)
 *
 *  Firebase RTDB:
 *    /teerem/weight_rate, /teerem/cumulative_kg, /teerem/last_updated
 *    /teerem/feed_water/{flow_rate,totalizer}
 *    /energy_meters/em01/{power_kw,total_energy_kwh}
 *    /energy_meters/em02/{power_kw,total_energy_kwh}
 *    /energy_meters/em04/{power_kw,total_energy_kwh,current_a,current_b,current_c}
 *    /energy_meters/em05/{power_kw,total_energy_kwh,current_a,current_b,current_c}
 *
 *  Pinout (S3-Zero):
 *    GPIO 4 — MAX485 RO  (RX)
 *    GPIO 5 — MAX485 DI  (TX)
 *    GPIO 6 — MAX485 DE+RE
 */

#include "secrets.h"
#include "ota.h"
#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <WiFi.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>
#include <esp_task_wdt.h>

// ── Modbus тохиргоо ────────────────────────────────────────────────────
namespace cfg {
constexpr uint8_t RX_PIN = 4;
constexpr uint8_t TX_PIN = 5;
constexpr uint8_t DE_RE = 6;
constexpr uint32_t BAUD = 9600;

// Slave IDs
constexpr uint8_t SCALE_SLAVE = 1; // Тээрмийн жин
constexpr uint8_t EM01_SLAVE = 2;  // Боловсруулах үйлдвэр ХС
constexpr uint8_t EM02_SLAVE = 3;  // Нунтаглах хэсэг ХС
constexpr uint8_t EM04_SLAVE = 4;  // Бөмбөлөгт тээрэм 1
constexpr uint8_t EM05_SLAVE = 5;  // Бөмбөлөгт тээрэм 2
constexpr uint8_t FLOW_SLAVE = 6;  // Тээрмийн тэжээлийн ус (flowmeter)
constexpr uint8_t ULS_SLAVE = 7;   // Supmea ultrasonic — Эргэлтийн усан сан

// Тэжээлийн жин (одоо байгаа сенсор)
constexpr uint16_t REG_FLOW = 0;     // 40001: Flow Rate (Float, t/h)
constexpr uint16_t REG_WEIGHT_T = 6; // 40007: Cumulative weight (Double, t)

// Тээрмийн тэжээлийн ус (flowmeter project-той ижил registers)
constexpr uint16_t FLOW_REG_RATE = 0x0000;  // [0..1] Flow rate float BE
constexpr uint16_t FLOW_REG_TOTAL = 0x0003; // [3..6] uint32 int + float frac

// Ultrasonic level transmitter — Level instantaneous (uint16, raw/1000 = m)
constexpr uint16_t REG_ULS_LEVEL = 0x2002;
constexpr uint16_t REG_ULS_MOUNT = 0x2009; // Mount Height — суурилуулалтын нийт өндөр (raw/1000 = m, W/R)

// SPM33 register addresses (4xxxx - 40001 = Modbus address)
constexpr uint16_t SPM33_REG_IA = 6;     // 40007: Phase A current (UINT16, ×0.001 A)
constexpr uint16_t SPM33_REG_POWER = 10; // 40011: Total active power LINT32, ×0.1 W
constexpr uint16_t SPM33_REG_ENERGY = 25; // 40026: Total active energy LUINT32, ×0.1 kWh
constexpr uint16_t SPM33_REG_CT_PRI = 201; // 40202: CT primary side value (1..50000)
constexpr uint8_t SPM33_CT_SEC = 5;        // SPM33 CT secondary side (5A typical)

constexpr uint32_t POLL_MS = 3000;                // Flow cycle interval (tick rate)
constexpr uint32_t TOTALIZER_INTERVAL_MS = 60000; // Totalizer cycle — 1 минут тутамд
constexpr uint32_t RX_TMO = 200;
constexpr uint32_t FRAME_GAP_MS = 10;

constexpr uint32_t WDT_TIMEOUT_S = 30;
constexpr uint32_t WIFI_RETRY_MS = 10000;
constexpr uint8_t MODBUS_RETRY = 2;
constexpr uint8_t MAX_CONSECUTIVE_FAILS = 10;
constexpr uint8_t MAX_RECOVERY_ATTEMPTS = 20;

// Long-running stability — flowmeter firmware-тэй ижил pattern
constexpr uint32_t MIN_FREE_HEAP_BYTES = 20480;                  // 20KB threshold
constexpr uint32_t MAX_UPTIME_MS = 24UL * 60UL * 60UL * 1000UL;  // 24 цаг
constexpr uint32_t HEAP_LOG_INTERVAL_MS = 5UL * 60UL * 1000UL;   // 5 минут тутам log
} // namespace cfg

// ── RGB STATUS LED (WS2812 GPIO 21 — Waveshare ESP32-S3-Zero onboard) ──
//   SLOW_RED — WiFi / Modbus / Firebase аль нэг нь алдсан үед удаан анивчина
//              (500ms on / 500ms off → 1 Hz).
//   pulse()  — Firebase-д амжилттай upload болгонд нэг богино ногоон импульс
//              (~120ms). Modbus poll-ийн 1 Hz ритмтэй синхрон.
namespace led {
constexpr uint8_t PIN = 21;
constexpr uint8_t BRIGHTNESS = 60;
constexpr uint16_t SLOW_ON_MS = 500;
constexpr uint16_t SLOW_OFF_MS = 500;
constexpr uint16_t PULSE_MS = 120;

inline void writeRGB(uint8_t r, uint8_t g, uint8_t b) {
  // Энэ board-ийн WS2812 чип нь R/G сувгуудыг солиод эмиттэр-лж байна
  // (rgbLedWrite-ийн GRB conversion нь зарим S3-Zero batch-д таарахгүй).
  // Тиймээс r ба g-г солиод дамжуулна — дуудлагын талд "red"/"green" хэвээр.
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
void pulse() {
  pulseOffAt = millis() + PULSE_MS;
  writeRGB(0, scale(255), 0);
}

void update() {
  unsigned long now = millis();

  if (pulseOffAt) {
    if (now < pulseOffAt)
      return;
    pulseOffAt = 0;
    writeRGB(0, 0, 0);
    on = false;
    nextToggle = now;
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

void ledTask(void *) {
  for (;;) {
    led::update();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ── Modbus ─────────────────────────────────────────────────────────────
class Modbus {
public:
  void begin() {
    pinMode(cfg::DE_RE, OUTPUT);
    digitalWrite(cfg::DE_RE, LOW);
    Serial1.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);
  }

  void recover() {
    // UART-ыг бүрэн салгаад MAX485-г RX горимд хүчээр оруулна.
    // "Stuck" transceiver-ийг сэргээх өргөтгөсөн хувилбар.
    Serial1.end();

    pinMode(cfg::DE_RE, OUTPUT);
    digitalWrite(cfg::DE_RE, LOW);
    delay(100);

    pinMode(cfg::RX_PIN, INPUT_PULLUP);
    delay(50);

    Serial1.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);
    delay(20);

    while (Serial1.available())
      Serial1.read();
  }

  // Big-Endian 32-bit IEEE 754 float (тэжээлийн жин)
  bool readFloat(uint8_t slave, uint16_t addr, float &out) {
    uint8_t rx[9];
    if (!readRegs(slave, addr, 2, rx))
      return false;
    uint32_t raw = (uint32_t)rx[3] << 24 | (uint32_t)rx[4] << 16 |
                   (uint32_t)rx[5] << 8 | rx[6];
    memcpy(&out, &raw, 4);
    return true;
  }

  // Big-Endian 64-bit IEEE 754 double (тэжээлийн жин cumulative)
  bool readDouble(uint8_t slave, uint16_t addr, double &out) {
    uint8_t rx[13];
    if (!readRegs(slave, addr, 4, rx))
      return false;
    uint64_t raw = ((uint64_t)rx[3] << 56) | ((uint64_t)rx[4] << 48) |
                   ((uint64_t)rx[5] << 40) | ((uint64_t)rx[6] << 32) |
                   ((uint64_t)rx[7] << 24) | ((uint64_t)rx[8] << 16) |
                   ((uint64_t)rx[9] << 8) | (uint64_t)rx[10];
    memcpy(&out, &raw, 8);
    return true;
  }

  // SPM33 — нэг UINT16 регистр
  bool readU16(uint8_t slave, uint16_t addr, uint16_t &out) {
    uint8_t rx[7];
    if (!readRegs(slave, addr, 1, rx))
      return false;
    out = ((uint16_t)rx[3] << 8) | rx[4];
    return true;
  }

  // SPM33 — гурван дараалсан UINT16 (Ia, Ib, Ic)
  bool readU16x3(uint8_t slave, uint16_t addr, uint16_t out[3]) {
    uint8_t rx[11];
    if (!readRegs(slave, addr, 3, rx))
      return false;
    for (int i = 0; i < 3; i++)
      out[i] = ((uint16_t)rx[3 + i * 2] << 8) | rx[4 + i * 2];
    return true;
  }

  // SPM33 LINT32 / LUINT32 — Low word first, byte order big-endian within each word
  bool readU32LowFirst(uint8_t slave, uint16_t addr, uint32_t &out) {
    uint8_t rx[9];
    if (!readRegs(slave, addr, 2, rx))
      return false;
    out = ((uint32_t)rx[5] << 24) | ((uint32_t)rx[6] << 16) |
          ((uint32_t)rx[3] << 8) | (uint32_t)rx[4];
    return true;
  }

  bool readI32LowFirst(uint8_t slave, uint16_t addr, int32_t &out) {
    uint32_t raw;
    if (!readU32LowFirst(slave, addr, raw))
      return false;
    out = (int32_t)raw;
    return true;
  }

  // Урсгал хэмжигч (flowmeter) totalizer — 4 register (8 byte):
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
    // 9600 baud дээр 1 байт ≈ 1.04ms. flush() дууссаны дараа RS485 шугам
    // тогтворжих хүртэл хүлээж DE/RE-г салгана.
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

// ── SPM33 helper-үүд ───────────────────────────────────────────────────
struct Spm33Reading {
  bool powerOk = false;
  bool energyOk = false;
  bool currentsOk = false;
  float powerKW = 0;          // primary side kW
  float energyKWh = 0;        // primary side kWh
  float currentA = 0;         // primary A
  float currentB = 0;
  float currentC = 0;
};

// Modbus retry helper
template <typename F>
static bool withRetry(F &&fn) {
  for (uint8_t i = 0; i < cfg::MODBUS_RETRY; i++) {
    if (fn())
      return true;
    if (i + 1 < cfg::MODBUS_RETRY)
      delay(cfg::FRAME_GAP_MS);
  }
  return false;
}

// CT primary бол метр дээр гараар тохируулдаг тогтмол тоо (рег. 40202).
// Boot үед уншиж кэшилнэ. Бүтэлгүй бол loop() cycle бүрт амжилттай болтол дахин
// оролдоно. Метр дээр CT-г өөрчилсөн бол ESP32-г restart хийхэд шинэ утга авна.
static uint16_t ctPrimary[6] = {1, 1, 1, 1, 1, 1};
static bool ctPrimaryKnown[6] = {false, false, false, false, false, false};

bool Spm33_readCtPrimary(Modbus &mb, uint8_t slave) {
  uint16_t v = 0;
  bool ok = withRetry([&] { return mb.readU16(slave, cfg::SPM33_REG_CT_PRI, v); });
  if (ok && v > 0) {
    ctPrimary[slave] = v;
    ctPrimaryKnown[slave] = true;
    Serial.printf("[SPM33 #%u] CT primary = %u A\n", slave, v);
    return true;
  }
  return false;
}

// Flow cycle (2с тутам) — зөвхөн power + сонгож гүйдэл уншина. Energy уншихгүй.
Spm33Reading Spm33_readPower(Modbus &mb, uint8_t slave, bool withCurrents) {
  Spm33Reading r;

  int32_t powerRaw = 0;
  r.powerOk = withRetry(
      [&] { return mb.readI32LowFirst(slave, cfg::SPM33_REG_POWER, powerRaw); });
  if (r.powerOk) {
    // Secondary side W (×0.1). To primary side: × (CT_pri / CT_sec).
    float secW = powerRaw * 0.1f;
    float priW = secW * (float)ctPrimary[slave] / (float)cfg::SPM33_CT_SEC;
    r.powerKW = priW / 1000.0f;
  }

  if (withCurrents) {
    delay(cfg::FRAME_GAP_MS);
    uint16_t ix[3] = {0, 0, 0};
    r.currentsOk = withRetry([&] {
      return mb.readU16x3(slave, cfg::SPM33_REG_IA, ix);
    });
    if (r.currentsOk) {
      const float k = 0.001f * (float)ctPrimary[slave] / (float)cfg::SPM33_CT_SEC;
      r.currentA = ix[0] * k;
      r.currentB = ix[1] * k;
      r.currentC = ix[2] * k;
    }
  }

  return r;
}

// Totalizer cycle (60с тутам) — зөвхөн нийт идэвхтэй энерги уншина.
Spm33Reading Spm33_readEnergy(Modbus &mb, uint8_t slave) {
  Spm33Reading r;

  uint32_t energyRaw = 0;
  r.energyOk = withRetry(
      [&] { return mb.readU32LowFirst(slave, cfg::SPM33_REG_ENERGY, energyRaw); });
  if (r.energyOk) {
    // 40026 нь primary side kWh-ийг ×0.1-ээр өгдөг (5.1 хүснэгт)
    r.energyKWh = energyRaw * 0.1f;
  }

  return r;
}

// ── Глобал ─────────────────────────────────────────────────────────────
Modbus modbus;
FirebaseData fbData;
FirebaseAuth fbAuth;
FirebaseConfig fbConfig;

bool firebaseReady = false;
unsigned long lastWifiCheck = 0;
unsigned long lastTotalizerReadTime = 0; // Сүүлд totalizer уншсан агшин (ms)
unsigned long lastHeapLogTime = 0;       // Сүүлд heap-н хэмжээг log хийсэн агшин

unsigned int consecutiveReadFails = 0;
unsigned int totalRecoveryAttempts = 0;

unsigned long fbNextAllowedAt = 0;
unsigned int fbFailStreak = 0;

// Totalizer утга 1 минутад нэг л шинэчлэгдэх тул хооронд нь cache-д хадгална.
// Анхны boot-д valid biш — эхний амжилттай уншилт хүртэл Firebase-руу бичигдэхгүй.
double cachedWeightT = 0.0;        bool cachedWeightTValid = false;
float  cachedEm01EnergyKWh = 0.0f; bool cachedEm01EnergyValid = false;
float  cachedEm02EnergyKWh = 0.0f; bool cachedEm02EnergyValid = false;
float  cachedEm04EnergyKWh = 0.0f; bool cachedEm04EnergyValid = false;
float  cachedEm05EnergyKWh = 0.0f; bool cachedEm05EnergyValid = false;
float  cachedFeedTotal = 0.0f;     bool cachedFeedTotalValid = false;

// Ultrasonic mount height (рег. 0x2009) — boot дээр нэг удаа уншиж Firebase-д
// нэг удаа нийтэлнэ. Дашбоард савны дээд хязгаарт (data-max) ашиглана.
float ulsMountM = 0.0f; bool ulsMountKnown = false; bool ulsMountSent = false;

void fbOnFailure() {
  fbFailStreak++;
  unsigned long backoff =
      2000UL * (1UL << min((unsigned int)8, fbFailStreak));
  if (backoff > 300000UL)
    backoff = 300000UL;
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

void recoverModbusBus() {
  totalRecoveryAttempts++;
  Serial.printf("[Recovery] Attempt #%u after %u failed cycles\n",
                totalRecoveryAttempts, consecutiveReadFails);
  if (totalRecoveryAttempts >= cfg::MAX_RECOVERY_ATTEMPTS) {
    Serial.println("[Recovery] Max attempts — forcing reboot via WDT");
    delay(100);
    while (true) {
    }
  }
  modbus.recover();
  consecutiveReadFails = 0;
  Serial.println("[Recovery] RS485 bus re-initialized");
}

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

void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED)
    return;

  Serial.printf("[WiFi] Connecting to network: %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected — IP: %s\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Connection failed — will retry later");
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
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

// ========================== DATA-LED LIVENESS FLIP =========================
// Сул зогссон сенсорын утга өөрчлөгдөхгүй бол Firebase-ийн .on("value")
// listener асдаггүй тул dashboard-ийн ногоон data-LED анивчдаггүй. Үүнийг
// засахын тулд утгыг 2 орноор бөөрөнхийлж, гацсан тохиолдолд 3 дахь орныг
// 0↔1 сэлгэнэ — dashboard 2 орон харуулдаг тул flag үл харагдана. Уншилт
// амжилттай болоход л дуудагдана тул үхсэн сенсор анивчихгүй, stale болно.
// ЗӨВХӨН жижиг утганд (power/weight/feed/level); totalizer/energy/currents-д ХЭРЭГЛЭХГҮЙ.
struct LiveState { float last2 = NAN; bool flip = false; };
static float liveValue(float v, LiveState &st) {
  float v2 = roundf(v * 100.0f) / 100.0f;
  st.flip = (v2 == st.last2) ? !st.flip : false;
  st.last2 = v2;
  return v2 + (st.flip ? 0.001f : 0.0f);
}
LiveState lvEm01, lvEm02, lvEm04, lvEm05, lvWeight, lvFeed, lvWaterTank;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n========= Teerem IoT (S3-Zero) =========");

  led::begin();
  xTaskCreatePinnedToCore(ledTask, "ledTask", 2048, nullptr, 1, nullptr, 0);

  modbus.begin();

  // SPM33 CT primary утгыг боотлох үед бүх 4 slave-ээс уншиж кэшилнэ — P
  // (40011) болон гүйдлийг secondary→primary хөрвүүлэхэд хэрэглэнэ. Унших нь
  // алдвал loop() cycle бүрт амжилттай болтол дахин оролдоно.
  delay(100);
  const uint8_t spmSlaves[4] = {cfg::EM01_SLAVE, cfg::EM02_SLAVE,
                                cfg::EM04_SLAVE, cfg::EM05_SLAVE};
  for (uint8_t s : spmSlaves) {
    Spm33_readCtPrimary(modbus, s);
    delay(cfg::FRAME_GAP_MS);
  }

  WiFi.onEvent(onWifiEvent);
  wifiConnect();
  firebaseInit();

  esp_task_wdt_config_t wdtConfig = {.timeout_ms = cfg::WDT_TIMEOUT_S * 1000,
                                     .idle_core_mask = 0,
                                     .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdtConfig);
  esp_task_wdt_add(NULL);
  Serial.printf("[WDT] Watchdog started — %lu second timeout\n",
                (unsigned long)cfg::WDT_TIMEOUT_S);
}

void loop() {
  esp_task_wdt_reset();
  unsigned long now = millis();

  // ── Long-running stability watchdog ────────────────────────────────
  // Heap fragmentation (Firebase TLS буфер г.м.) удаан ажиллах тусам
  // хуримтлагдаж тогтворгүй болгож болзошгүй. 5 минут тутамд heap-г log
  // хийнэ. Free heap босгоос (20KB) доош унавал эсвэл uptime 24 цаг хэтэрвэл
  // cleaн restart хийнэ.
  if (now - lastHeapLogTime >= cfg::HEAP_LOG_INTERVAL_MS) {
    lastHeapLogTime = now;
    Serial.printf("[Heap] Free: %u | Min ever: %u | Uptime: %lus\n",
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

  if (WiFi.status() != WL_CONNECTED &&
      now - lastWifiCheck >= cfg::WIFI_RETRY_MS) {
    lastWifiCheck = now;
    Serial.println("[WiFi] retry — async reconnect");
    WiFi.reconnect();
  }

  static unsigned long lastPoll = 0;
  if (now - lastPoll < cfg::POLL_MS)
    return;
  lastPoll = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] not connected");
    led::setMode(led::SLOW_RED);
    return;
  }

  // Boot үед уншиж чадаагүй CT primary-уудыг дахин оролдох — нэг slave/cycle
  // (RS485 bus-ыг сенсорын уншилттай зэрэгцэхгүй).
  {
    static const uint8_t spmSlaves[4] = {cfg::EM01_SLAVE, cfg::EM02_SLAVE,
                                         cfg::EM04_SLAVE, cfg::EM05_SLAVE};
    for (uint8_t s : spmSlaves) {
      if (!ctPrimaryKnown[s]) {
        Spm33_readCtPrimary(modbus, s);
        delay(cfg::FRAME_GAP_MS);
        break; // зөвхөн нэгийг л оролдоно, цаг хэмнэхийн тулд
      }
    }
  }

  // Цикл бүрд flow-cycle эсвэл totalizer-cycle гэсэн 2 төрлийн нэг нь явна.
  // RS485 bus-н ачааллыг бууруулж, "хоёуланг нь зэрэг уншихгүй" гэсэн
  // шаардлагыг хангахын тулд totalizer-ийг тусад нь 1 минутын циклд уншина.
  // Boot-ийн дараа нэг удаа totalizer-ийг шууд уншиж cache-г бөглөнө.
  bool totalizerCycle = (lastTotalizerReadTime == 0) ||
                        (now - lastTotalizerReadTime >= cfg::TOTALIZER_INTERVAL_MS);

  // Энэ цикл-д Firebase upload руу очих утгууд (flow cycle-ийнх)
  float flow = 0;
  bool flowOk = false;
  Spm33Reading em01, em02, em04, em05;
  float feedFlow = 0;
  bool feedFlowOk = false;
  uint16_t ulsRaw = 0;
  bool ulsLevelOk = false;
  float ulsLevel = 0.0f;

  // Totalizer cycle-ийн төлөв (uploaded утгууд cache-ээс)
  double weightT = 0;
  bool weightOk = false;
  float feedTotal = 0;
  bool feedTotalOk = false;

  if (totalizerCycle) {
    // ── Totalizer cycle: жингийн нийт жин + EM01-05 энерги + feed water totalizer
    lastTotalizerReadTime = now;
    Serial.println("[Cycle] Totalizer read (1 min interval)");

    weightOk = withRetry(
        [&] { return modbus.readDouble(cfg::SCALE_SLAVE, cfg::REG_WEIGHT_T, weightT); });
    if (weightOk) { cachedWeightT = weightT; cachedWeightTValid = true; }
    delay(cfg::FRAME_GAP_MS);

    em01 = Spm33_readEnergy(modbus, cfg::EM01_SLAVE);
    if (em01.energyOk) { cachedEm01EnergyKWh = em01.energyKWh; cachedEm01EnergyValid = true; }
    delay(cfg::FRAME_GAP_MS);

    em02 = Spm33_readEnergy(modbus, cfg::EM02_SLAVE);
    if (em02.energyOk) { cachedEm02EnergyKWh = em02.energyKWh; cachedEm02EnergyValid = true; }
    delay(cfg::FRAME_GAP_MS);

    em04 = Spm33_readEnergy(modbus, cfg::EM04_SLAVE);
    if (em04.energyOk) { cachedEm04EnergyKWh = em04.energyKWh; cachedEm04EnergyValid = true; }
    delay(cfg::FRAME_GAP_MS);

    em05 = Spm33_readEnergy(modbus, cfg::EM05_SLAVE);
    if (em05.energyOk) { cachedEm05EnergyKWh = em05.energyKWh; cachedEm05EnergyValid = true; }
    delay(cfg::FRAME_GAP_MS);

    feedTotalOk = withRetry(
        [&] { return modbus.readTotalizer(cfg::FLOW_SLAVE, cfg::FLOW_REG_TOTAL, feedTotal); });
    if (feedTotalOk) { cachedFeedTotal = feedTotal; cachedFeedTotalValid = true; }
    delay(cfg::FRAME_GAP_MS);

    Serial.printf("[Totalizer] Weight:%s EM01:%s EM02:%s EM04:%s EM05:%s Feed:%s\n",
                  weightOk ? String(weightT, 3).c_str() : "#",
                  em01.energyOk ? String(em01.energyKWh, 1).c_str() : "#",
                  em02.energyOk ? String(em02.energyKWh, 1).c_str() : "#",
                  em04.energyOk ? String(em04.energyKWh, 1).c_str() : "#",
                  em05.energyOk ? String(em05.energyKWh, 1).c_str() : "#",
                  feedTotalOk ? String(feedTotal, 2).c_str() : "#");
  } else {
    // ── Flow cycle: жингийн урсгал + EM01-05 power + currents + feed water flow + level
    flowOk = withRetry(
        [&] { return modbus.readFloat(cfg::SCALE_SLAVE, cfg::REG_FLOW, flow); });
    delay(cfg::FRAME_GAP_MS);

    em01 = Spm33_readPower(modbus, cfg::EM01_SLAVE, false);
    delay(cfg::FRAME_GAP_MS);
    em02 = Spm33_readPower(modbus, cfg::EM02_SLAVE, false);
    delay(cfg::FRAME_GAP_MS);
    em04 = Spm33_readPower(modbus, cfg::EM04_SLAVE, true);
    delay(cfg::FRAME_GAP_MS);
    em05 = Spm33_readPower(modbus, cfg::EM05_SLAVE, true);
    delay(cfg::FRAME_GAP_MS);

    feedFlowOk = withRetry(
        [&] { return modbus.readFloat(cfg::FLOW_SLAVE, cfg::FLOW_REG_RATE, feedFlow); });
    delay(cfg::FRAME_GAP_MS);

    ulsLevelOk = withRetry(
        [&] { return modbus.readU16(cfg::ULS_SLAVE, cfg::REG_ULS_LEVEL, ulsRaw); });
    ulsLevel = ulsLevelOk ? (ulsRaw / 1000.0f) : 0.0f;

    // Mount height — савны нийт өндөр, нэг л удаа (амжилттай болтол оролдоно).
    if (!ulsMountKnown) {
      uint16_t mountRaw = 0;
      if (withRetry([&] { return modbus.readU16(cfg::ULS_SLAVE, cfg::REG_ULS_MOUNT, mountRaw); }) && mountRaw > 0) {
        ulsMountM = mountRaw / 1000.0f; ulsMountKnown = true;
        Serial.printf("[ULS] Mount height: %.3f m\n", ulsMountM);
      }
    }

    Serial.printf("[Flow] Scale:%s EM01:%s EM02:%s EM04:%s EM05:%s Feed:%s Tank:%s\n",
                  flowOk ? String(flow, 2).c_str() : "#",
                  em01.powerOk ? String(em01.powerKW, 2).c_str() : "#",
                  em02.powerOk ? String(em02.powerKW, 2).c_str() : "#",
                  em04.powerOk ? String(em04.powerKW, 2).c_str() : "#",
                  em05.powerOk ? String(em05.powerKW, 2).c_str() : "#",
                  feedFlowOk ? String(feedFlow, 2).c_str() : "#",
                  ulsLevelOk ? String(ulsLevel, 3).c_str() : "#");
  }

  // ── Алдаа escalate ──────────────────────────────────────────────
  bool anyOk = flowOk || weightOk || em01.powerOk || em01.energyOk ||
               em02.powerOk || em02.energyOk || em04.powerOk || em04.energyOk ||
               em04.currentsOk || em05.powerOk || em05.energyOk ||
               em05.currentsOk || feedFlowOk || feedTotalOk || ulsLevelOk;
  if (anyOk) {
    consecutiveReadFails = 0;
    totalRecoveryAttempts = 0;
  } else {
    consecutiveReadFails++;
    if (consecutiveReadFails >= cfg::MAX_CONSECUTIVE_FAILS)
      recoverModbusBus();
  }

  // ── 5) Firebase upload — root("/") нэг update нь rules engine-д бүх 13+
  // замыг шалгуулж response timeout үүсгэдэг. Тиймээс /teerem ба
  // /energy_meters subtree-үүдийг тус тусад нь жижиг updateNode-оор бичнэ.
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
    Serial.println("[Firebase] ready");
    ota::begin(&fbData);  // DEVICE_ID register + командын stream нээх
  }

  // ─ Subtree 1: /teerem ──────────────────────────────────────────────
  bool teeremOk = true;
  bool teeremAny = false;
  {
    FirebaseJson j;
    if (totalizerCycle) {
      // Totalizer cycle: жингийн нийт жин + feed water totalizer
      if (weightOk) {
        j.set("cumulative_kg", (int)(cachedWeightT * 1000.0));
        teeremAny = true;
      }
      if (feedTotalOk) {
        j.set("feed_water/totalizer", cachedFeedTotal);
        teeremAny = true;
      }
    } else {
      // Flow cycle: жингийн урсгал + feed water flow + water tank level
      if (flowOk) { j.set("weight_rate", liveValue(flow, lvWeight)); teeremAny = true; }
      if (feedFlowOk) {
        j.set("feed_water/flow_rate", liveValue(feedFlow, lvFeed));
        teeremAny = true;
      }
      if (ulsLevelOk) {
        j.set("water_tank/level", liveValue(ulsLevel, lvWaterTank));
        teeremAny = true;
      }
      // Mount height-г нэг удаа нийтэлнэ (амжилттай upload болтол оролдоно).
      if (ulsMountKnown && !ulsMountSent) {
        j.set("water_tank/mount_height", ulsMountM);
        teeremAny = true;
      }
    }
    if (teeremAny) {
      j.set("last_updated", (int)(millis() / 1000));
      teeremOk = Firebase.RTDB.updateNodeSilent(&fbData, "/teerem", &j);
      if (!teeremOk)
        Serial.printf("[Firebase] /teerem ERROR: %s\n",
                      fbData.errorReason().c_str());
    }
  }

  // ─ Subtree 2: /energy_meters ───────────────────────────────────────
  // Flow cycle: power + currents (instantaneous утгууд)
  // Totalizer cycle: total_energy_kwh (cumulative утгууд)
  bool emOk = true;
  bool emAny = false;
  {
    FirebaseJson j;
    auto addEm = [&](const char *id, const Spm33Reading &r, bool withI, LiveState &lv) {
      String base = String(id) + "/";
      if (totalizerCycle) {
        if (r.energyOk) {
          j.set((base + "total_energy_kwh").c_str(), r.energyKWh);
          emAny = true;
        }
      } else {
        if (r.powerOk) {
          j.set((base + "power_kw").c_str(), liveValue(r.powerKW, lv));
          emAny = true;
        }
        if (withI && r.currentsOk) {
          j.set((base + "current_a").c_str(), r.currentA);
          j.set((base + "current_b").c_str(), r.currentB);
          j.set((base + "current_c").c_str(), r.currentC);
          emAny = true;
        }
      }
    };
    addEm("em01", em01, false, lvEm01);
    addEm("em02", em02, false, lvEm02);
    addEm("em04", em04, true, lvEm04);
    addEm("em05", em05, true, lvEm05);
    if (emAny) {
      emOk = Firebase.RTDB.updateNodeSilent(&fbData, "/energy_meters", &j);
      if (!emOk)
        Serial.printf("[Firebase] /energy_meters ERROR: %s\n",
                      fbData.errorReason().c_str());
    }
  }

  if (!teeremAny && !emAny) {
    led::setMode(led::SLOW_RED);
    return;
  }

  if (teeremOk && emOk) {
    Serial.println("[Firebase] Updated");
    fbOnSuccess();
    if (ulsMountKnown) ulsMountSent = true; // mount height нийтлэгдсэн
    led::setMode(led::OFF);
    led::pulse();
  } else {
    fbOnFailure();
    led::setMode(led::SLOW_RED);
  }

  // OTA heartbeat + self-test gate (амжилттай upload бүрд +1)
  ota::loop(&fbData, teeremOk && emOk);
}
