/*
 * ============================================================
 *  ESP32-S3-Zero Шүүн шахах (pressfilter) → Firebase RTDB
 * ============================================================
 *
 *  3 тусдаа MAX485 transceiver, 3 бие даасан RS485 шугам (Modbus RTU @ 9600bps).
 *  MAX485 модуль бүрийн RO/DI/DE+RE → ESP GPIO холболт (DE+RE хооронд нь холбоно):
 *
 *   Bus A (UART1) — MAX485 #1:
 *     RO → GPIO 1 (RX) · DI → GPIO 2 (TX) · DE+RE → GPIO 3 · A/B → RS485 шугам #1
 *     Slave 1 : SPM33 — Десорбци ХС        (P, E)  → em09
 *     Slave 2 : SPM33 — Шүүн шахах УС       (P, E)  → em08
 *
 *   Bus B (UART2) — MAX485 #2:
 *     RO → GPIO 4 (RX) · DI → GPIO 5 (TX) · DE+RE → GPIO 6 · A/B → RS485 шугам #2
 *     Slave 3 : SPM33 — Нуруулдан уусгалт ХС (P, E) → em10
 *     Slave 4 : SPM33 — Компрессор ХС        (P, E) → em11
 *
 *   Bus C (UART0) — MAX485 #3:
 *     RO → GPIO 7 (RX) · DI → GPIO 8 (TX) · DE+RE → GPIO 9 · A/B → RS485 шугам #3
 *     Slave 5 : Supmea ultrasonic — Баян уусмалын сан (level, m)
 *
 *   MAX485 бүрийн VCC / GND → ESP 3V3 / GND.  GPIO 21 — WS2812 onboard RGB LED.
 *
 *  Firebase RTDB:
 *    /energy_meters/em08..em11/{power_kw,total_energy_kwh}
 *    /pressfilter/bayan_tank/level, /pressfilter/last_updated
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
constexpr uint32_t BAUD = 9600;

// Bus A пинүүд (UART1) — 2× SPM33
constexpr uint8_t A_RX = 1, A_TX = 2, A_DE = 3;
// Bus B пинүүд (UART2) — 2× SPM33
constexpr uint8_t B_RX = 4, B_TX = 5, B_DE = 6;
// Bus C пинүүд (UART0) — 1× ultrasonic level
constexpr uint8_t C_RX = 7, C_TX = 8, C_DE = 9;

// Slave IDs (тус тусын bus дээр)
constexpr uint8_t EM09_SLAVE = 1; // Bus A — Десорбци ХС
constexpr uint8_t EM08_SLAVE = 2; // Bus A — Шүүн шахах УС
constexpr uint8_t EM10_SLAVE = 3; // Bus B — Нуруулдан уусгалт ХС
constexpr uint8_t EM11_SLAVE = 4; // Bus B — Компрессор ХС
constexpr uint8_t ULS_SLAVE = 5;  // Bus C — Баян уусмалын сан (Supmea ultrasonic)

// SPM33 register addresses (4xxxx - 40001 = Modbus address)
constexpr uint16_t SPM33_REG_POWER = 10;   // 40011: Total active power LINT32, ×0.1 W
constexpr uint16_t SPM33_REG_ENERGY = 25;  // 40026: Total active energy LUINT32, ×0.1 kWh
constexpr uint16_t SPM33_REG_CT_PRI = 201; // 40202: CT primary side value (1..50000)
constexpr uint8_t SPM33_CT_SEC = 5;        // SPM33 CT secondary side (5A typical)

// Supmea ultrasonic level — Level instantaneous (uint16, raw/1000 = m)
constexpr uint16_t REG_ULS_LEVEL = 0x2002;

constexpr uint32_t POLL_MS = 2000;                // Flow cycle interval (tick rate)
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
//   SLOW_RED — WiFi / Modbus / Firebase аль нэг нь алдсан үед удаан анивчина.
//   pulse()  — Firebase-д амжилттай upload болгонд нэг богино ногоон импульс.
namespace led {
constexpr uint8_t PIN = 21;
constexpr uint8_t BRIGHTNESS = 60;
constexpr uint16_t SLOW_ON_MS = 500;
constexpr uint16_t SLOW_OFF_MS = 500;
constexpr uint16_t PULSE_MS = 120;

inline void writeRGB(uint8_t r, uint8_t g, uint8_t b) {
  // Энэ board-ийн WS2812 чип нь R/G сувгуудыг солиод эмиттэр-лж байна.
  // Тиймээс r ба g-г солиод дамжуулна — дуудлагын талд "red"/"green" хэвээр.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  rgbLedWrite(PIN, g, r, b);
#else
  neopixelWrite(PIN, g, r, b);
#endif
}

inline uint8_t scale(uint8_t v) { return (uint16_t(v) * BRIGHTNESS) / 255; }

enum Mode { OFF, SLOW_RED };
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
// Тус бүр өөрийн HardwareSerial (UART) болон DE/RE пинтэй — 3 bus-д 3 instance.
class Modbus {
public:
  Modbus(HardwareSerial &serial, uint8_t rx, uint8_t tx, uint8_t de)
      : _serial(serial), _rx(rx), _tx(tx), _de(de) {}

  void begin() {
    pinMode(_de, OUTPUT);
    digitalWrite(_de, LOW);
    _serial.begin(cfg::BAUD, SERIAL_8N1, _rx, _tx);
  }

  void recover() {
    // UART-ыг бүрэн салгаад MAX485-г RX горимд хүчээр оруулна.
    _serial.end();

    pinMode(_de, OUTPUT);
    digitalWrite(_de, LOW);
    delay(100);

    pinMode(_rx, INPUT_PULLUP);
    delay(50);

    _serial.begin(cfg::BAUD, SERIAL_8N1, _rx, _tx);
    delay(20);

    while (_serial.available())
      _serial.read();
  }

  // SPM33 / Supmea — нэг UINT16 регистр
  bool readU16(uint8_t slave, uint16_t addr, uint16_t &out) {
    uint8_t rx[7];
    if (!readRegs(slave, addr, 1, rx))
      return false;
    out = ((uint16_t)rx[3] << 8) | rx[4];
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

private:
  bool readRegs(uint8_t slave, uint16_t addr, uint8_t regCnt, uint8_t *rx) {
    while (_serial.available())
      _serial.read();

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
    digitalWrite(_de, HIGH);
    delayMicroseconds(50);          // DE өндөр болсны дараа драйвер идэвхжих хугацаа
    _serial.write(data, len);
    _serial.flush();                // TX register-г бүрэн хоослох
    delayMicroseconds(1200);        // 9600 baud дээр шугам тогтворжих хүртэл
    digitalWrite(_de, LOW);
    delayMicroseconds(200);         // RX горим идэвхжих, bus settle
  }

  bool receive(uint8_t *buf, size_t want) {
    size_t got = 0;
    unsigned long start = millis();
    while (millis() - start < cfg::RX_TMO && got < want) {
      if (_serial.available())
        buf[got++] = _serial.read();
    }
    return got == want;
  }

  HardwareSerial &_serial;
  uint8_t _rx, _tx, _de;
};

// ── SPM33 helper-үүд ───────────────────────────────────────────────────
struct Spm33Reading {
  bool powerOk = false;
  bool energyOk = false;
  float powerKW = 0;          // primary side kW
  float energyKWh = 0;        // primary side kWh
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
// Boot үед уншиж кэшилнэ. Индекс нь slave ID (SPM33 хамгийн их slave = 4 тул [5]).
static uint16_t ctPrimary[5] = {1, 1, 1, 1, 1};
static bool ctPrimaryKnown[5] = {false, false, false, false, false};

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

// Flow cycle (2с тутам) — зөвхөн power уншина.
Spm33Reading Spm33_readPower(Modbus &mb, uint8_t slave) {
  Spm33Reading r;

  int32_t powerRaw = 0;
  r.powerOk = withRetry(
      [&] { return mb.readI32LowFirst(slave, cfg::SPM33_REG_POWER, powerRaw); });
  if (r.powerOk) {
    float secW = powerRaw * 0.1f;
    float priW = secW * (float)ctPrimary[slave] / (float)cfg::SPM33_CT_SEC;
    r.powerKW = priW / 1000.0f;
  }

  return r;
}

// Totalizer cycle (60с тутам) — зөвхөн нийт идэвхтэй энерги уншина.
Spm33Reading Spm33_readEnergy(Modbus &mb, uint8_t slave) {
  Spm33Reading r;

  uint32_t energyRaw = 0;
  r.energyOk = withRetry(
      [&] { return mb.readU32LowFirst(slave, cfg::SPM33_REG_ENERGY, energyRaw); });
  if (r.energyOk)
    r.energyKWh = energyRaw * 0.1f;

  return r;
}

// ── Глобал ─────────────────────────────────────────────────────────────
// 3 bus = 3 Modbus instance (тус бүр өөрийн UART + DE/RE пин).
// UART0-г RS485-д ашиглана — USB-CDC дебаг нь тусдаа `Serial` (HWCDC) тул
// UART0 сул. `Serial0` global бүх core-д байдаггүй тул өөрсдөө instance үүсгэв.
HardwareSerial busCSerial(0); // UART0
Modbus busA(Serial1, cfg::A_RX, cfg::A_TX, cfg::A_DE);     // UART1 — 2× SPM33
Modbus busB(Serial2, cfg::B_RX, cfg::B_TX, cfg::B_DE);     // UART2 — 2× SPM33
Modbus busC(busCSerial, cfg::C_RX, cfg::C_TX, cfg::C_DE);  // UART0 — ultrasonic

FirebaseData fbData;
FirebaseAuth fbAuth;
FirebaseConfig fbConfig;

bool firebaseReady = false;
unsigned long lastWifiCheck = 0;
unsigned long lastTotalizerReadTime = 0;
unsigned long lastHeapLogTime = 0;

unsigned int consecutiveReadFails = 0;
unsigned int totalRecoveryAttempts = 0;

unsigned long fbNextAllowedAt = 0;
unsigned int fbFailStreak = 0;

// Energy (totalizer) утга 1 минутад нэг л шинэчлэгдэх тул cache-д хадгална.
float cachedEm08EnergyKWh = 0.0f; bool cachedEm08EnergyValid = false;
float cachedEm09EnergyKWh = 0.0f; bool cachedEm09EnergyValid = false;
float cachedEm10EnergyKWh = 0.0f; bool cachedEm10EnergyValid = false;
float cachedEm11EnergyKWh = 0.0f; bool cachedEm11EnergyValid = false;

void fbOnFailure() {
  fbFailStreak++;
  unsigned long backoff = 2000UL * (1UL << min((unsigned int)8, fbFailStreak));
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
  busA.recover();
  busB.recover();
  busC.recover();
  consecutiveReadFails = 0;
  Serial.println("[Recovery] All 3 RS485 buses re-initialized");
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

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n========= pressfilter IoT (S3-Zero, 3-bus) =========");

  led::begin();
  xTaskCreatePinnedToCore(ledTask, "ledTask", 2048, nullptr, 1, nullptr, 0);

  busA.begin();
  busB.begin();
  busC.begin();

  // SPM33 CT primary утгыг боотлох үед тус бусын bus-аас уншиж кэшилнэ.
  delay(100);
  Spm33_readCtPrimary(busA, cfg::EM09_SLAVE);
  delay(cfg::FRAME_GAP_MS);
  Spm33_readCtPrimary(busA, cfg::EM08_SLAVE);
  delay(cfg::FRAME_GAP_MS);
  Spm33_readCtPrimary(busB, cfg::EM10_SLAVE);
  delay(cfg::FRAME_GAP_MS);
  Spm33_readCtPrimary(busB, cfg::EM11_SLAVE);

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

  // Boot үед уншиж чадаагүй CT primary-уудыг дахин оролдох — нэг slave/cycle.
  if (!ctPrimaryKnown[cfg::EM09_SLAVE]) {
    Spm33_readCtPrimary(busA, cfg::EM09_SLAVE);
  } else if (!ctPrimaryKnown[cfg::EM08_SLAVE]) {
    Spm33_readCtPrimary(busA, cfg::EM08_SLAVE);
  } else if (!ctPrimaryKnown[cfg::EM10_SLAVE]) {
    Spm33_readCtPrimary(busB, cfg::EM10_SLAVE);
  } else if (!ctPrimaryKnown[cfg::EM11_SLAVE]) {
    Spm33_readCtPrimary(busB, cfg::EM11_SLAVE);
  }

  bool totalizerCycle = (lastTotalizerReadTime == 0) ||
                        (now - lastTotalizerReadTime >= cfg::TOTALIZER_INTERVAL_MS);

  Spm33Reading em08, em09, em10, em11;
  uint16_t ulsRaw = 0;
  bool ulsLevelOk = false;
  float ulsLevel = 0.0f;

  if (totalizerCycle) {
    // ── Totalizer cycle: 4× SPM33 энерги
    lastTotalizerReadTime = now;
    Serial.println("[Cycle] Totalizer read (1 min interval)");

    em09 = Spm33_readEnergy(busA, cfg::EM09_SLAVE);
    if (em09.energyOk) { cachedEm09EnergyKWh = em09.energyKWh; cachedEm09EnergyValid = true; }
    delay(cfg::FRAME_GAP_MS);
    em08 = Spm33_readEnergy(busA, cfg::EM08_SLAVE);
    if (em08.energyOk) { cachedEm08EnergyKWh = em08.energyKWh; cachedEm08EnergyValid = true; }
    delay(cfg::FRAME_GAP_MS);
    em10 = Spm33_readEnergy(busB, cfg::EM10_SLAVE);
    if (em10.energyOk) { cachedEm10EnergyKWh = em10.energyKWh; cachedEm10EnergyValid = true; }
    delay(cfg::FRAME_GAP_MS);
    em11 = Spm33_readEnergy(busB, cfg::EM11_SLAVE);
    if (em11.energyOk) { cachedEm11EnergyKWh = em11.energyKWh; cachedEm11EnergyValid = true; }

    Serial.printf("[Totalizer] EM08:%s EM09:%s EM10:%s EM11:%s\n",
                  em08.energyOk ? String(em08.energyKWh, 1).c_str() : "#",
                  em09.energyOk ? String(em09.energyKWh, 1).c_str() : "#",
                  em10.energyOk ? String(em10.energyKWh, 1).c_str() : "#",
                  em11.energyOk ? String(em11.energyKWh, 1).c_str() : "#");
  } else {
    // ── Flow cycle: 4× SPM33 power + ultrasonic level
    em09 = Spm33_readPower(busA, cfg::EM09_SLAVE);
    delay(cfg::FRAME_GAP_MS);
    em08 = Spm33_readPower(busA, cfg::EM08_SLAVE);
    delay(cfg::FRAME_GAP_MS);
    em10 = Spm33_readPower(busB, cfg::EM10_SLAVE);
    delay(cfg::FRAME_GAP_MS);
    em11 = Spm33_readPower(busB, cfg::EM11_SLAVE);
    delay(cfg::FRAME_GAP_MS);

    ulsLevelOk = withRetry(
        [&] { return busC.readU16(cfg::ULS_SLAVE, cfg::REG_ULS_LEVEL, ulsRaw); });
    ulsLevel = ulsLevelOk ? (ulsRaw / 1000.0f) : 0.0f;

    Serial.printf("[Flow] EM08:%s EM09:%s EM10:%s EM11:%s Tank:%s\n",
                  em08.powerOk ? String(em08.powerKW, 2).c_str() : "#",
                  em09.powerOk ? String(em09.powerKW, 2).c_str() : "#",
                  em10.powerOk ? String(em10.powerKW, 2).c_str() : "#",
                  em11.powerOk ? String(em11.powerKW, 2).c_str() : "#",
                  ulsLevelOk ? String(ulsLevel, 3).c_str() : "#");
  }

  // ── Алдаа escalate ──────────────────────────────────────────────
  bool anyOk = em08.powerOk || em08.energyOk || em09.powerOk || em09.energyOk ||
               em10.powerOk || em10.energyOk || em11.powerOk || em11.energyOk ||
               ulsLevelOk;
  if (anyOk) {
    consecutiveReadFails = 0;
    totalRecoveryAttempts = 0;
  } else {
    consecutiveReadFails++;
    if (consecutiveReadFails >= cfg::MAX_CONSECUTIVE_FAILS)
      recoverModbusBus();
  }

  // ── Firebase upload ────────────────────────────────────────────────
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
    ota::begin(&fbData);
  }

  // ─ Subtree 1: /energy_meters/{em08..em11} ──────────────────────────
  bool emOk = true;
  bool emAny = false;
  {
    FirebaseJson j;
    auto addEm = [&](const char *id, const Spm33Reading &r) {
      String base = String(id) + "/";
      if (totalizerCycle) {
        if (r.energyOk) {
          j.set((base + "total_energy_kwh").c_str(), r.energyKWh);
          emAny = true;
        }
      } else {
        if (r.powerOk) {
          j.set((base + "power_kw").c_str(), r.powerKW);
          emAny = true;
        }
      }
    };
    addEm("em08", em08);
    addEm("em09", em09);
    addEm("em10", em10);
    addEm("em11", em11);
    if (emAny) {
      emOk = Firebase.RTDB.updateNodeSilent(&fbData, "/energy_meters", &j);
      if (!emOk)
        Serial.printf("[Firebase] /energy_meters ERROR: %s\n",
                      fbData.errorReason().c_str());
    }
  }

  // ─ Subtree 2: /pressfilter (ultrasonic level — flow cycle-д л) ──────
  bool pfOk = true;
  bool pfAny = false;
  {
    FirebaseJson j;
    if (!totalizerCycle && ulsLevelOk) {
      j.set("bayan_tank/level", ulsLevel);
      j.set("last_updated", (int)(millis() / 1000));
      pfAny = true;
    }
    if (pfAny) {
      pfOk = Firebase.RTDB.updateNodeSilent(&fbData, "/pressfilter", &j);
      if (!pfOk)
        Serial.printf("[Firebase] /pressfilter ERROR: %s\n",
                      fbData.errorReason().c_str());
    }
  }

  if (!emAny && !pfAny) {
    led::setMode(led::SLOW_RED);
    return;
  }

  if (emOk && pfOk) {
    Serial.println("[Firebase] Updated");
    fbOnSuccess();
    led::setMode(led::OFF);
    led::pulse();
  } else {
    fbOnFailure();
    led::setMode(led::SLOW_RED);
  }

  ota::loop(&fbData, emOk && pfOk);
}
