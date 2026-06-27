/*
 * ============================================================
 *  ESP32-WROOM Gen → Firebase Realtime Database
 * ============================================================
 *
 *  Нэг RS485 шугам дээр Modbus RTU @ 9600bps :
 *    Slave 1 : SPM33 — Уурын зуух ХС        (P, E)  → em13
 *    Slave 2 : SPM33 — Шатахуун түгээх ХС   (P, E)  → em12
 *
 *  Firebase RTDB:
 *    /energy_meters/em13/{power_kw,total_energy_kwh}
 *    /energy_meters/em12/{power_kw,total_energy_kwh}
 *
 *  MAX485 модуль ↔ ESP32-WROOM холболт (1 transceiver, UART2):
 *    MAX485 RO  (Receiver Out) → ESP GPIO 16  (UART2 RX)
 *    MAX485 DI  (Driver In)    → ESP GPIO 17  (UART2 TX)
 *    MAX485 DE + RE (хооронд нь холбоно) → ESP GPIO 5  (чиглэл удирдлага)
 *    MAX485 A / B              → RS485 шугам (Slave 1 ба Slave 2 SPM33)
 *    MAX485 VCC / GND          → ESP 3V3 / GND
 *    GPIO 21 — WS2812 статус LED (заавал биш; WROOM-д onboard байхгүй)
 */

#include "secrets.h"
#include "ota.h"
#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <WiFi.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>
#include <esp_task_wdt.h>
#include <esp_system.h>          // esp_restart() — WDT-ISR доторх цэвэр reset

// ── Modbus тохиргоо ────────────────────────────────────────────────────
namespace cfg {
constexpr uint8_t RX_PIN = 16;
constexpr uint8_t TX_PIN = 17;
constexpr uint8_t DE_RE = 5;
constexpr uint32_t BAUD = 9600;

// Slave IDs
constexpr uint8_t EM13_SLAVE = 1;  // SPM33 — Уурын зуух ХС
constexpr uint8_t EM12_SLAVE = 2;  // SPM33 — Шатахуун түгээх ХС

// SPM33 register addresses (4xxxx - 40001 = Modbus address)
constexpr uint16_t SPM33_REG_POWER = 10;   // 40011: Total active power LINT32, ×0.1 W
constexpr uint16_t SPM33_REG_ENERGY = 25;  // 40026: Total active energy LUINT32, ×0.1 kWh
constexpr uint16_t SPM33_REG_CT_PRI = 201; // 40202: CT primary side value (1..50000)
constexpr uint8_t SPM33_CT_SEC = 5;        // SPM33 CT secondary side (5A typical)

constexpr uint32_t POLL_MS = 3000;                // Flow cycle interval (tick rate)
constexpr uint32_t TOTALIZER_INTERVAL_MS = 60000; // Totalizer cycle — 1 минут тутамд
constexpr uint32_t RX_TMO = 200;
constexpr uint32_t FRAME_GAP_MS = 10;

constexpr uint32_t WDT_TIMEOUT_S = 60;             // Watchdog timeout — supervisor-аас урт байх ёстой
constexpr uint32_t SUPERVISOR_TIMEOUT_MS = 45000;  // loop ийм удаан зогсвол → цэвэр ESP.restart()
constexpr uint32_t WIFI_RETRY_MS = 10000;
constexpr uint32_t WIFI_DISCONNECT_RESTART_MS = 5UL * 60UL * 1000UL; // 5 минут тасрахад reset
constexpr uint8_t MODBUS_RETRY = 2;
constexpr uint8_t MAX_CONSECUTIVE_FAILS = 10;
constexpr uint8_t MAX_RECOVERY_ATTEMPTS = 20;
// Connectivity watchdog: WiFi/Firebase энэ хугацаанаас удаан унавал болзолгүй
// ESP.restart() хийж сэргэнэ — while(true)+WDT-д найдахгүй сэргэлтийн гол баталгаа.
constexpr uint32_t MAX_OFFLINE_MS = 3UL * 60UL * 1000UL;  // 3 минут

// Long-running stability — flowmeter firmware-тэй ижил pattern
constexpr uint32_t MIN_FREE_HEAP_BYTES = 20480;                  // 20KB threshold
constexpr uint32_t MAX_UPTIME_MS = 24UL * 60UL * 60UL * 1000UL;  // 24 цаг
constexpr uint32_t HEAP_LOG_INTERVAL_MS = 5UL * 60UL * 1000UL;   // 5 минут тутам log
} // namespace cfg

// ── RGB STATUS LED (WS2812 GPIO 21) ───────────────────────────────────
//   ESP32-WROOM-д onboard RGB байхгүй ч rgbLedWrite(GPIO21) аюулгүй ажиллана
//   (гадны WS2812 залгавал статус харагдана; үгүй бол нөлөөгүй).
//   SLOW_RED — WiFi / Modbus / Firebase аль нэг нь алдсан үед удаан анивчина.
//   pulse()  — Firebase-д амжилттай upload болгонд нэг богино ногоон импульс.
namespace led {
constexpr uint8_t PIN = 21;
constexpr uint8_t BRIGHTNESS = 60;
constexpr uint16_t SLOW_ON_MS = 500;
constexpr uint16_t SLOW_OFF_MS = 500;
constexpr uint16_t PULSE_MS = 120;

inline void writeRGB(uint8_t r, uint8_t g, uint8_t b) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  rgbLedWrite(PIN, r, g, b);
#else
  neopixelWrite(PIN, r, g, b);
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

// Supervisor watchdog — loop ахиц гаргаж буйг хянана. loop нь
// cfg::SUPERVISOR_TIMEOUT_MS-ээс удаан зогсвол (Firebase/WiFi/Modbus блоклосон г.м.)
// ЦЭВЭР ESP.restart() хийнэ — Task WDT-ийн panic зам (S3+USB-CDC дээр backtrace
// хэвлэхдээ гацаж болзошгүй)-аас тойрно. Core 0 дээр тусдаа ажилладаг тул core 1-ийн
// loop бүрэн блоклосон ч энэ task ажиллана.
volatile uint32_t g_loopBeat = 0;
void watchdogTask(void *) {
  uint32_t lastBeat = 0;
  unsigned long lastChangeMs = millis();
  for (;;) {
    if (g_loopBeat != lastBeat) {
      lastBeat = g_loopBeat;
      lastChangeMs = millis();
    } else if (millis() - lastChangeMs >= cfg::SUPERVISOR_TIMEOUT_MS) {
      Serial.println("[Supervisor] loop stalled — clean ESP.restart()");
      Serial.flush();
      ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void ledTask(void *) {
  for (;;) {
    led::update();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ── Modbus (ESP32-WROOM — UART2 / Serial2) ─────────────────────────────
class Modbus {
public:
  void begin() {
    pinMode(cfg::DE_RE, OUTPUT);
    digitalWrite(cfg::DE_RE, LOW);
    Serial2.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);
  }

  void recover() {
    // UART-ыг бүрэн салгаад MAX485-г RX горимд хүчээр оруулна.
    // "Stuck" transceiver-ийг сэргээх өргөтгөсөн хувилбар.
    Serial2.end();

    pinMode(cfg::DE_RE, OUTPUT);
    digitalWrite(cfg::DE_RE, LOW);
    delay(100);

    pinMode(cfg::RX_PIN, INPUT_PULLUP);
    delay(50);

    Serial2.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);
    delay(20);

    while (Serial2.available())
      Serial2.read();
  }

  // SPM33 — нэг UINT16 регистр
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
    while (Serial2.available())
      Serial2.read();

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
    Serial2.write(data, len);
    // flush() нь UART wedge болоход (MAX485/RS485 гэмтэл) ХЯЗГААРГҮЙ блоклож болзошгүй тул
    // ашиглахгүй. Оронд нь бүх байт физикээр гарах хугацааг тооцоолон хүлээнэ (bounded).
    uint32_t txUs = (uint32_t)len * 10UL * 1000000UL / cfg::BAUD;
    delayMicroseconds(txUs + 1200);
    digitalWrite(cfg::DE_RE, LOW);
    delayMicroseconds(200);         // RX горим идэвхжих, bus settle
  }

  bool receive(uint8_t *buf, size_t want) {
    size_t got = 0;
    unsigned long start = millis();
    while (millis() - start < cfg::RX_TMO && got < want) {
      if (Serial2.available())
        buf[got++] = Serial2.read();
    }
    return got == want;
  }
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
// Boot үед уншиж кэшилнэ. Бүтэлгүй бол loop() cycle бүрт амжилттай болтол дахин
// оролдоно. Метр дээр CT-г өөрчилсөн бол ESP32-г restart хийхэд шинэ утга авна.
// Индекс нь slave ID (хамгийн их slave = 2 тул [3] хангалттай).
static uint16_t ctPrimary[3] = {1, 1, 1};
static bool ctPrimaryKnown[3] = {false, false, false};

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

// Flow cycle (2с тутам) — зөвхөн power уншина. Energy уншихгүй.
Spm33Reading Spm33_readPower(Modbus &mb, uint8_t slave) {
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
unsigned long wifiDisconnectedAt = 0; // 0 = холбоотой; !=0 = тасрахад эхэлсэн агшин
unsigned long lastTotalizerReadTime = 0; // Сүүлд totalizer уншсан агшин (ms)
unsigned long lastHeapLogTime = 0;       // Сүүлд heap-н хэмжээг log хийсэн агшин

unsigned int consecutiveReadFails = 0;
unsigned int totalRecoveryAttempts = 0;

// Connectivity watchdog state — салгагдсан агшнаас хойш хэдий хугацаа өнгөрснийг
// хэмжинэ (0 = одоо холбоотой, асуудалгүй).
unsigned long wifiDownSince = 0;
unsigned long fbNotReadySince = 0;

unsigned long fbNextAllowedAt = 0;
unsigned int fbFailStreak = 0;

// Energy (totalizer) утга 1 минутад нэг л шинэчлэгдэх тул хооронд нь cache-д хадгална.
float cachedEm13EnergyKWh = 0.0f; bool cachedEm13EnergyValid = false;
float cachedEm12EnergyKWh = 0.0f; bool cachedEm12EnergyValid = false;

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
    // Цэвэр reboot — while(true)+WDT panic нь S3/USB-CDC дээр backtrace-аа гацсан
    // USB порт руу хэвлэхийг оролдоод reboot хүртэл хүрдэггүй. esp_restart() нь
    // panic printer-ээр орохгүй тул заавал найдвартай сэргэнэ.
    Serial.println("[Recovery] Max attempts — clean ESP.restart()");
    Serial.flush();
    delay(100);
    ESP.restart();
  }
  modbus.recover();
  consecutiveReadFails = 0;
  Serial.println("[Recovery] RS485 bus re-initialized");
}

void onWifiEvent(WiFiEvent_t event) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    Serial.println("[WiFi] Disconnected — attempting auto-reconnect");
    if (wifiDisconnectedAt == 0)
      wifiDisconnectedAt = millis(); // тасралтгүй тасарсан эхний агшныг тэмдэглэнэ
    WiFi.reconnect();
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    Serial.printf("[WiFi] Reconnected — IP: %s\n",
                  WiFi.localIP().toString().c_str());
    wifiDisconnectedAt = 0;
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
// ЗӨВХӨН жижиг утганд (power); totalizer/energy/currents-д ХЭРЭГЛЭХГҮЙ.
struct LiveState { float last2 = NAN; bool flip = false; };
static float liveValue(float v, LiveState &st) {
  float v2 = roundf(v * 100.0f) / 100.0f;
  st.flip = (v2 == st.last2) ? !st.flip : false;
  st.last2 = v2;
  return v2 + (st.flip ? 0.001f : 0.0f);
}
LiveState lvEm13, lvEm12;

// ── Task WDT timeout handler (cfg::WDT_TIMEOUT_S) ─────────────────────────────
// trigger_panic=false тул loop() нь хугацаандаа esp_task_wdt_reset() хийгээгүй үед
// TWDT нь panic үүсгэхгүйгээр энэ weak handler-ийг timer-ISR контекстэд дууддаг.
// Энд ШУУД esp_restart() хийнэ — panic/coredump/serial хэвлэлийн зам бүрэн алгасагдаж,
// "амьд атлаа хөшсөн" (Firebase/Modbus блоклосон, эсвэл panic handler гацсан) төлвөөс
// найдвартай гарна. Энэ нь гадны hardware watchdog-гүйгээр сэргэлтийн гол баталгаа.
extern "C" void esp_task_wdt_isr_user_handler(void) {
  esp_restart();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n========= Gen IoT (ESP32-WROOM) =========");

  led::begin();
  xTaskCreatePinnedToCore(ledTask, "ledTask", 2048, nullptr, 1, nullptr, 0);

  modbus.begin();

  // SPM33 CT primary утгыг боотлох үед 2 slave-ээс уншиж кэшилнэ — P (40011)-г
  // secondary→primary хөрвүүлэхэд хэрэглэнэ. Унших нь алдвал loop() cycle бүрт
  // амжилттай болтол дахин оролдоно.
  delay(100);
  const uint8_t spmSlaves[2] = {cfg::EM13_SLAVE, cfg::EM12_SLAVE};
  for (uint8_t s : spmSlaves) {
    Spm33_readCtPrimary(modbus, s);
    delay(cfg::FRAME_GAP_MS);
  }

  WiFi.onEvent(onWifiEvent);
  wifiConnect();
  firebaseInit();

  esp_task_wdt_config_t wdtConfig = {.timeout_ms = cfg::WDT_TIMEOUT_S * 1000,
                                     .idle_core_mask = 0,
                                     // panic БИШ: timeout болоход esp_task_wdt_isr_user_handler()-ээс ШУУД esp_restart().
      .trigger_panic = false};
  esp_task_wdt_reconfigure(&wdtConfig);
  esp_task_wdt_add(NULL);

  // Supervisor watchdog task — setup дууссаны дараа эхлүүлнэ (setup-ийн блоклох
  // wifiConnect зэргийг false-restart болгохгүйн тулд). Core 0.
  xTaskCreatePinnedToCore(watchdogTask, "wdog", 2048, nullptr, 1, nullptr, 0);
  Serial.printf("[WDT] Watchdog started — %lu second timeout\n",
                (unsigned long)cfg::WDT_TIMEOUT_S);
}

void loop() {
  g_loopBeat++;           // supervisor task-д "loop ахиж байна" дохио
  esp_task_wdt_reset();
  unsigned long now = millis();

  // ── Long-running stability watchdog ────────────────────────────────
  // Heap fragmentation (Firebase TLS буфер г.м.) удаан ажиллах тусам
  // хуримтлагдаж тогтворгүй болгож болзошгүй. 5 минут тутамд heap-г log
  // хийнэ. Free heap босгоос (20KB) доош унавал эсвэл uptime 24 цаг хэтэрвэл
  // clean restart хийнэ.
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

  // ---- Connectivity watchdog ----
  // WiFi эсвэл Firebase нь cfg::MAX_OFFLINE_MS-ээс удаан салгагдвал цэвэр reboot.
  // WiFi буцаж ирэхэд / сенсор сэргэхэд төхөөрөмж 'бүр мөсөн алга' болохгүй,
  // заавал өөрөө сэргэх баталгаа. Firebase.ready()-г цикл бүрт дуудах нь
  // token-refresh-ийг тасралтгүй ажиллуулж бас тустай.
  if (WiFi.status() == WL_CONNECTED) {
    wifiDownSince = 0;
  } else if (wifiDownSince == 0) {
    wifiDownSince = now;
  } else if (now - wifiDownSince >= cfg::MAX_OFFLINE_MS) {
    Serial.println("[Watchdog] WiFi offline too long — clean restart");
    Serial.flush();
    delay(200);
    ESP.restart();
  }

  if (WiFi.status() == WL_CONNECTED && !Firebase.ready()) {
    if (fbNotReadySince == 0) {
      fbNotReadySince = now;
    } else if (now - fbNotReadySince >= cfg::MAX_OFFLINE_MS) {
      Serial.println("[Watchdog] Firebase not ready too long — clean restart");
      Serial.flush();
      delay(200);
      ESP.restart();
    }
  } else {
    fbNotReadySince = 0;
  }

  // ---- Firebase delivery watchdog ----
  // WiFi асан, Firebase.ready()==true мөртөө бичилт сервер хүрэхгүй (token хүчинтэй ч
  // доорх TCP/TLS session үхсэн "дүлий" төлөв) бол дээрх !Firebase.ready() болзол
  // ХЭЗЭЭ Ч барихгүй. Иймд сүүлийн амжилттай Firebase контактаас MAX_OFFLINE_MS
  // хэтэрвэл цэвэр reset хийж шинээр холбогдоно.
  if (firebaseReady && WiFi.status() == WL_CONNECTED &&
      ota::lastContactMs() != 0 &&
      now - ota::lastContactMs() >= cfg::MAX_OFFLINE_MS) {
    Serial.println("[Watchdog] Firebase delivery stalled — clean restart");
    Serial.flush();
    delay(200);
    ESP.restart();
  }

  // WiFi.reconnect() / auto-reconnect зарим тохиолдолд бодит сүлжээ сэргэсэн ч
  // chip-ийн WiFi stack хатуу "stuck" болсон үед амжилтгүй хэвээр үлддэг —
  // үүнийг зөвхөн бүрэн restart л засдаг тул threshold давсан бол reboot хийнэ.
  if (wifiDisconnectedAt != 0 &&
      now - wifiDisconnectedAt >= cfg::WIFI_DISCONNECT_RESTART_MS) {
    Serial.println("[Stability] WiFi 5+ минут тасархад — restarting");
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
  {
    static const uint8_t spmSlaves[2] = {cfg::EM13_SLAVE, cfg::EM12_SLAVE};
    for (uint8_t s : spmSlaves) {
      if (!ctPrimaryKnown[s]) {
        Spm33_readCtPrimary(modbus, s);
        delay(cfg::FRAME_GAP_MS);
        break; // зөвхөн нэгийг л оролдоно
      }
    }
  }

  // Цикл бүрд flow-cycle (power) эсвэл totalizer-cycle (energy) гэсэн 2 төрлийн
  // нэг нь явна. RS485 bus-н ачааллыг бууруулж, energy-г тусад нь 1 минутын циклд
  // уншина. Boot-ийн дараа нэг удаа energy-г шууд уншиж cache-г бөглөнө.
  bool totalizerCycle = (lastTotalizerReadTime == 0) ||
                        (now - lastTotalizerReadTime >= cfg::TOTALIZER_INTERVAL_MS);

  Spm33Reading em13, em12;

  if (totalizerCycle) {
    // ── Totalizer cycle: EM13 + EM12 энерги
    lastTotalizerReadTime = now;
    Serial.println("[Cycle] Totalizer read (1 min interval)");

    em13 = Spm33_readEnergy(modbus, cfg::EM13_SLAVE);
    if (em13.energyOk) { cachedEm13EnergyKWh = em13.energyKWh; cachedEm13EnergyValid = true; }
    delay(cfg::FRAME_GAP_MS);

    em12 = Spm33_readEnergy(modbus, cfg::EM12_SLAVE);
    if (em12.energyOk) { cachedEm12EnergyKWh = em12.energyKWh; cachedEm12EnergyValid = true; }
    delay(cfg::FRAME_GAP_MS);

    Serial.printf("[Totalizer] EM13:%s EM12:%s\n",
                  em13.energyOk ? String(em13.energyKWh, 1).c_str() : "#",
                  em12.energyOk ? String(em12.energyKWh, 1).c_str() : "#");
  } else {
    // ── Flow cycle: EM13 + EM12 power
    em13 = Spm33_readPower(modbus, cfg::EM13_SLAVE);
    delay(cfg::FRAME_GAP_MS);
    em12 = Spm33_readPower(modbus, cfg::EM12_SLAVE);
    delay(cfg::FRAME_GAP_MS);

    Serial.printf("[Flow] EM13:%s EM12:%s\n",
                  em13.powerOk ? String(em13.powerKW, 2).c_str() : "#",
                  em12.powerOk ? String(em12.powerKW, 2).c_str() : "#");
  }

  // ── Алдаа escalate ──────────────────────────────────────────────
  bool anyOk = em13.powerOk || em13.energyOk || em12.powerOk || em12.energyOk;
  if (anyOk) {
    consecutiveReadFails = 0;
    totalRecoveryAttempts = 0;
  } else {
    consecutiveReadFails++;
    if (consecutiveReadFails >= cfg::MAX_CONSECUTIVE_FAILS)
      recoverModbusBus();
  }

  // ── Firebase upload — /energy_meters subtree-г жижиг updateNode-оор бичнэ.
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

  // ─ /energy_meters/{em13,em12} ──────────────────────────────────────
  // Flow cycle: power_kw (instantaneous)
  // Totalizer cycle: total_energy_kwh (cumulative)
  bool emOk = true;
  bool emAny = false;
  {
    FirebaseJson j;
    auto addEm = [&](const char *id, const Spm33Reading &r, LiveState &lv) {
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
      }
    };
    addEm("em13", em13, lvEm13);
    addEm("em12", em12, lvEm12);
    if (emAny) {
      emOk = Firebase.RTDB.updateNodeSilent(&fbData, "/energy_meters", &j);
      if (!emOk)
        Serial.printf("[Firebase] /energy_meters ERROR: %s\n",
                      fbData.errorReason().c_str());
    }
  }

  if (!emAny) {
    led::setMode(led::SLOW_RED);
    return;
  }

  if (emOk) {
    Serial.println("[Firebase] Updated");
    fbOnSuccess();
    ota::noteFbSuccess(); // delivery watchdog-ийн \"амьд\" тэмдэг
    led::setMode(led::OFF);
    led::pulse();
  } else {
    fbOnFailure();
    led::setMode(led::SLOW_RED);
  }

  // OTA heartbeat + self-test gate (амжилттай upload бүрд +1)
  ota::loop(&fbData, emOk);
}
