/*
 * ============================================================
 *  ESP32-S3-Zero Тээрэм → Firebase Realtime Database
 * ============================================================
 *
 *  Modbus RTU (RS485 via MAX485) → flow rate (t/h) + cumulative weight (t)
 *  → Firebase RTDB: /teerem/weight_rate (float t/h),
 *                   /teerem/cumulative_kg (int kg, эх double-аар уншсаныг
 * ×1000)
 *
 *  Pinout (S3-Zero):
 *    GPIO 4 — MAX485 RO  (RX)
 *    GPIO 5 — MAX485 DI  (TX)
 *    GPIO 6 — MAX485 DE+RE
 */

#include "secrets.h"
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
constexpr uint8_t SLAVE = 1;
constexpr uint32_t BAUD = 9600;
constexpr uint16_t REG_FLOW = 0;     // 40001: Flow Rate (Float, t/h)
constexpr uint16_t REG_WEIGHT_T = 6; // 40007: Cumulative weight (Double, t)
constexpr uint32_t POLL_MS = 1000;   // Modbus poll interval
constexpr uint32_t RX_TMO = 200;
constexpr uint32_t FRAME_GAP_MS = 10; // Modbus RTU inter-frame silence

// Resilience tuning
constexpr uint32_t WDT_TIMEOUT_S = 30;        // Hard reset if loop stalls
constexpr uint32_t WIFI_RETRY_MS = 10000;     // WiFi reconnect probe interval
constexpr uint8_t MODBUS_RETRY = 3;           // Per-read attempts
constexpr uint8_t MAX_CONSECUTIVE_FAILS = 10; // Escalate after this many
constexpr uint8_t MAX_RECOVERY_ATTEMPTS = 20; // Force reboot after this many
} // namespace cfg

// ── Firebase paths ─────────────────────────────────────────────────────
#define FB_PATH_FLOW "/teerem/weight_rate"
#define FB_PATH_KG "/teerem/cumulative_kg"
#define FB_PATH_UPDATE "/teerem/last_updated"

// ── RGB LED (WS2812 GPIO 21 — Waveshare ESP32-S3-Zero onboard) ─────────
// Uses the Arduino-ESP32 built-in driver (rgbLedWrite in core 3.x,
// neopixelWrite in 2.x). The driver handles WS2812 timing and GRB byte
// order internally — call writeRGB(R, G, B) and you get exactly those colors.
namespace led {
constexpr uint8_t PIN = 21;
constexpr uint8_t BRIGHTNESS = 40; // 0-255 — same as user's working test sketch
constexpr uint16_t ON_MS = 250;    // visible-on portion of each blink
constexpr uint16_t OFF_MS = 250;   // off portion of each blink (1 Hz total)
constexpr uint16_t FLASH_ON_MS = 250; // one-shot green flash window

inline void writeRGB(uint8_t r, uint8_t g, uint8_t b) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  rgbLedWrite(PIN, r, g, b);
#else
  neopixelWrite(PIN, r, g, b);
#endif
}

inline uint8_t scale(uint8_t v) { return (uint16_t(v) * BRIGHTNESS) / 255; }

enum Mode { OFF, BLINK_RED, BLINK_PURPLE };
Mode mode = OFF;
unsigned long nextToggle = 0;
unsigned long flashOffAt = 0;
bool on = false;

void begin() { writeRGB(0, 0, 0); }

// Set the background blink mode. OFF stops blinking entirely.
void setMode(Mode m) {
  if (m == mode)
    return;
  mode = m;
  on = false;
  nextToggle = millis();
  if (m == OFF)
    writeRGB(0, 0, 0);
}

// One-shot green flash on every successful Firebase upload.
void flashGreen() {
  flashOffAt = millis() + FLASH_ON_MS;
  writeRGB(0, scale(255), 0);
}

// Non-blocking driver — call once per loop iteration.
void update() {
  unsigned long now = millis();

  if (flashOffAt) {
    if (now < flashOffAt)
      return; // green flash still on
    flashOffAt = 0;
    writeRGB(0, 0, 0);
    on = false;
    nextToggle = now; // restart blink cycle cleanly
  }

  if (mode == OFF)
    return;

  if (now < nextToggle)
    return;
  if (on) {
    writeRGB(0, 0, 0);
    on = false;
    nextToggle = now + OFF_MS;
  } else {
    if (mode == BLINK_RED)
      writeRGB(scale(255), 0, 0);
    else // BLINK_PURPLE — red + blue mixed
      writeRGB(scale(255), 0, scale(255));
    on = true;
    nextToggle = now + ON_MS;
  }
}
} // namespace led

// Dedicated LED task — pinned to core 0 so Modbus / Firebase blocking on
// core 1 (where Arduino loop runs) can't stall the blink cadence.
void ledTask(void *) {
  for (;;) {
    led::update();
    vTaskDelay(pdMS_TO_TICKS(20)); // 50 Hz refresh
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

  // Drain UART, restart Serial1, cycle MAX485 DE/RE — clears wedged bus state.
  void recover() {
    while (Serial1.available())
      Serial1.read();
    Serial1.end();
    delay(50);
    Serial1.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);
    digitalWrite(cfg::DE_RE, HIGH);
    delay(5);
    digitalWrite(cfg::DE_RE, LOW);
  }

  bool readFloat(uint16_t addr, float &out) {
    uint8_t rx[9];
    if (!readRegs(addr, 2, rx))
      return false;
    uint32_t raw = (uint32_t)rx[3] << 24 | (uint32_t)rx[4] << 16 |
                   (uint32_t)rx[5] << 8 | rx[6];
    memcpy(&out, &raw, 4);
    return true;
  }

  // Big-Endian 64-bit IEEE 754 double, 4 регистр (8 байт), word-swap байхгүй
  bool readDouble(uint16_t addr, double &out) {
    uint8_t rx[13]; // 5 framing + 4*2 data
    if (!readRegs(addr, 4, rx))
      return false;
    uint64_t raw = ((uint64_t)rx[3] << 56) | ((uint64_t)rx[4] << 48) |
                   ((uint64_t)rx[5] << 40) | ((uint64_t)rx[6] << 32) |
                   ((uint64_t)rx[7] << 24) | ((uint64_t)rx[8] << 16) |
                   ((uint64_t)rx[9] << 8) | (uint64_t)rx[10];
    memcpy(&out, &raw, 8);
    return true;
  }

private:
  bool readRegs(uint16_t addr, uint8_t regCnt, uint8_t *rx) {
    while (Serial1.available())
      Serial1.read(); // өмнөх амжилтгүй уншилтын хоцрогдсон байтыг цэвэрлэх

    uint8_t req[8] = {
        cfg::SLAVE, 0x03, uint8_t(addr >> 8), uint8_t(addr), 0, regCnt, 0, 0};
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF;
    req[7] = c >> 8;

    send(req, sizeof(req));

    const uint8_t respLen = 5 + regCnt * 2;
    if (!receive(rx, respLen))
      return false;
    if (rx[0] != cfg::SLAVE || rx[1] != 0x03 || rx[2] != regCnt * 2)
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
    delayMicroseconds(100);
    Serial1.write(data, len);
    Serial1.flush();
    delayMicroseconds(100);
    digitalWrite(cfg::DE_RE, LOW);
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

// ── Глобал ─────────────────────────────────────────────────────────────
Modbus modbus;
FirebaseData fbData;
FirebaseAuth fbAuth;
FirebaseConfig fbConfig;

bool firebaseReady = false;
unsigned long lastWifiCheck = 0;

// Modbus recovery — escalate after repeated read failures, reboot if hopeless.
unsigned int consecutiveReadFails = 0;
unsigned int totalRecoveryAttempts = 0;

// Firebase exponential backoff — keeps a flaky network from generating retry
// storms during auth failures or sustained upload errors.
unsigned long fbNextAllowedAt = 0;
unsigned int fbFailStreak = 0;

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

// Escalating Modbus recovery: re-init UART + cycle DE/RE; reboot if exhausted.
void recoverModbusBus() {
  totalRecoveryAttempts++;
  Serial.printf("[Recovery] Attempt #%u after %u failed cycles\n",
                totalRecoveryAttempts, consecutiveReadFails);
  if (totalRecoveryAttempts >= cfg::MAX_RECOVERY_ATTEMPTS) {
    Serial.println("[Recovery] Max attempts — forcing reboot via WDT");
    delay(100);
    while (true) {
    } // Let watchdog reset the chip cleanly
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

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n========= Teerem IoT (S3-Zero) =========");

  led::begin();
  // LED runs on its own core 0 task so Modbus / Firebase blocking calls
  // on core 1 (Arduino loop) never freeze the blink animation.
  xTaskCreatePinnedToCore(ledTask, "ledTask", 2048, nullptr, 1, nullptr, 0);

  modbus.begin();
  WiFi.onEvent(onWifiEvent);
  wifiConnect();
  firebaseInit();

  // Hardware watchdog — reboots the chip if loop() goes silent for too long
  // (e.g. Firebase SSL handshake wedged on a flaky network).
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
  // led::update() runs on the dedicated ledTask (core 0) — no need to call
  // it here. Driving from loop() was making blocks in Modbus / Firebase
  // freeze the LED state visually.
  unsigned long now = millis();

  // Manual WiFi retry — fallback for when onWifiEvent stops firing. MUST be
  // non-blocking: wifiConnect() has a 15s wait loop that would freeze the LED
  // state machine and make the blink look like a solid colour. WiFi.reconnect()
  // just kicks the async reconnect attempt and returns immediately.
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

  // 1) WiFi шалгах — холбогдоогүй бол улаан LED тогтмол анивчина
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] not connected");
    led::setMode(led::BLINK_RED);
    return;
  }

  // 2) Modbus унших — retry-тэй (transient bus noise-ыг тойрох)
  float flow = 0;
  bool flowOk = false;
  for (uint8_t i = 0; i < cfg::MODBUS_RETRY && !flowOk; i++) {
    flowOk = modbus.readFloat(cfg::REG_FLOW, flow);
    if (!flowOk && i + 1 < cfg::MODBUS_RETRY)
      delay(cfg::FRAME_GAP_MS);
  }

  delay(cfg::FRAME_GAP_MS);

  double weightT = 0;
  bool weightOk = false;
  for (uint8_t i = 0; i < cfg::MODBUS_RETRY && !weightOk; i++) {
    weightOk = modbus.readDouble(cfg::REG_WEIGHT_T, weightT);
    if (!weightOk && i + 1 < cfg::MODBUS_RETRY)
      delay(cfg::FRAME_GAP_MS);
  }

  if (flowOk)
    Serial.printf("Flow   = %.2f t/h\n", flow);
  else
    Serial.println("Flow   = #");

  if (weightOk)
    Serial.printf("Weight = %.3f t\n", weightT);
  else
    Serial.println("Weight = #");

  // Track read failures; escalate to bus recovery if they pile up.
  if (flowOk || weightOk) {
    consecutiveReadFails = 0;
    totalRecoveryAttempts = 0;
  } else {
    consecutiveReadFails++;
    if (consecutiveReadFails >= cfg::MAX_CONSECUTIVE_FAILS)
      recoverModbusBus();
  }

  // Modbus уншилтын аль нэг нь алдсан бол ягаан анивчина
  if (!flowOk || !weightOk) {
    led::setMode(led::BLINK_PURPLE);
    return;
  }

  // 3) Firebase backoff/ready шалгах
  if (!fbCanUpload()) {
    led::setMode(led::BLINK_PURPLE);
    return;
  }
  if (!Firebase.ready()) {
    led::setMode(led::BLINK_PURPLE);
    return;
  }
  if (!firebaseReady) {
    firebaseReady = true;
    Serial.println("[Firebase] ready");
  }

  bool uploadOk = false;
  bool anyFail = false;

  if (flowOk) {
    if (Firebase.RTDB.setFloat(&fbData, FB_PATH_FLOW, flow))
      uploadOk = true;
    else {
      anyFail = true;
      Serial.printf("[Firebase] flow err: %s\n", fbData.errorReason().c_str());
    }
  }

  if (weightOk) {
    int32_t weightKg = (int32_t)(weightT * 1000.0);
    if (Firebase.RTDB.setInt(&fbData, FB_PATH_KG, weightKg))
      uploadOk = true;
    else {
      anyFail = true;
      Serial.printf("[Firebase] kg err: %s\n", fbData.errorReason().c_str());
    }
  }

  if (uploadOk) {
    Firebase.RTDB.setInt(&fbData, FB_PATH_UPDATE, (int)(millis() / 1000));
    led::setMode(led::OFF);
    led::flashGreen(); // GREEN — амжилттай upload бүрт нэг анивчина
  } else {
    led::setMode(led::BLINK_PURPLE);
  }

  if (anyFail)
    fbOnFailure();
  else if (uploadOk)
    fbOnSuccess();
}
