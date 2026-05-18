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
 *   ESP32-S3 → RS485 bus → Flowmeters (Modbus RTU) → Parse float → Firebase RTDB
 */

#include "secrets.h" // WiFi and Firebase credentials (not tracked in git)
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

// Modbus slave addresses (3 flowmeters)
constexpr uint8_t FM1_SLAVE = 2;
constexpr uint8_t FM2_SLAVE = 3;
constexpr uint8_t FM3_SLAVE = 4;
constexpr uint32_t BAUD = 9600;

// Modbus holding register addresses
constexpr uint16_t REG_FLOW_RATE = 0x0000; // Reg[00-01]: Flow rate (float BE)
constexpr uint16_t REG_TOTALIZER = 0x0003; // Reg[03-06]: int32 + float fraction

// Timing
constexpr uint32_t READ_INTERVAL_MS = 1000;        // Flow rate read interval
constexpr uint32_t TOTALIZER_INTERVAL_MS = 300000; // 5 минут
constexpr uint32_t WIFI_RETRY_MS = 10000;          // WiFi reconnect probe
constexpr uint32_t WDT_TIMEOUT_S = 30;             // Watchdog timeout

// Auto-recovery thresholds
constexpr uint8_t MAX_CONSECUTIVE_READ_FAILS = 10;
constexpr uint8_t MAX_TOTAL_RECOVERY_FAILS = 20;
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

// ========================== GLOBAL OBJECTS ==========================

FirebaseData fbData; // Firebase data object for storing request/response data
FirebaseAuth fbAuth; // Firebase authentication credentials holder
FirebaseConfig fbConfig; // Firebase configuration settings holder

unsigned long lastReadTime = 0;      // Timestamp of last flow rate reading (ms)
unsigned long lastTotalizerRead = 0; // Timestamp of last totalizer reading (ms)
unsigned long lastWifiCheck = 0;     // Timestamp of last WiFi check (ms)
bool firebaseReady = false;          // Firebase auth complete flag

// Cache last-good totalizer values so we keep uploading them between 5-min
// reads
float lastTotal1 = 0.0, lastTotal2 = 0.0, lastTotal3 = 0.0;
bool hasTotal1 = false, hasTotal2 = false, hasTotal3 = false;

// Firebase upload backoff — prevents auth rate-limit storms on failure
unsigned long fbNextAllowedAt = 0; // Next allowed upload timestamp
unsigned int fbFailStreak = 0;     // Consecutive upload failure count

// Modbus read failure recovery state
unsigned int consecutiveReadFails =
    0; // Straight failed read cycles (both FMs failed)
unsigned int totalRecoveryAttempts =
    0; // Recovery escalations since last success

// ========================== CRC16 CHECKSUM ================================

/*
 * Calculate Modbus CRC16 checksum for data integrity verification.
 * Uses the standard Modbus polynomial 0xA001.
 * @param buf  - pointer to data buffer
 * @param len  - number of bytes to process
 * @return     - 16-bit CRC checksum value
 */
uint16_t crc16(uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF; // Initialize CRC with all bits set
  for (uint16_t i = 0; i < len; i++) {
    crc ^= buf[i];                    // XOR current byte into CRC
    for (uint8_t j = 0; j < 8; j++) { // Process each bit
      if (crc & 0x0001) {             // If least significant bit is set
        crc >>= 1;                    // Shift right by 1
        crc ^= 0xA001;                // XOR with Modbus polynomial
      } else {
        crc >>= 1; // Just shift right by 1
      }
    }
  }
  return crc; // Return calculated CRC16 value
}

// ========================== RAW MODBUS COMMUNICATION
// ============================

static uint8_t
    rxbuf[64]; // Receive buffer for Modbus response data (max 64 bytes)

/*
 * Send a Modbus RTU request and read the response from a slave device.
 * Constructs a Function Code 03 (Read Holding Registers) request frame.
 * @param slaveId - Modbus slave device address
 * @param reg     - Starting register address to read
 * @param count   - Number of registers to read
 * @return        - Number of bytes received in response
 */
int sendAndRead(uint8_t slaveId, uint16_t reg, uint16_t count) {
  uint8_t req[8];   // Modbus request frame buffer (8 bytes for FC03)
  req[0] = slaveId; // Byte 0: Slave address
  req[1] = 0x03;    // Byte 1: Function code 03 = Read Holding Registers
  req[2] = (reg >> 8) & 0xFF;   // Byte 2: Register address high byte
  req[3] = reg & 0xFF;          // Byte 3: Register address low byte
  req[4] = (count >> 8) & 0xFF; // Byte 4: Number of registers high byte
  req[5] = count & 0xFF;        // Byte 5: Number of registers low byte
  uint16_t c = crc16(req, 6);   // Calculate CRC16 over first 6 bytes
  req[6] = c & 0xFF;            // Byte 6: CRC low byte
  req[7] = (c >> 8) & 0xFF;     // Byte 7: CRC high byte

  // Flush any stale data from the serial receive buffer
  while (Serial1.available())
    Serial1.read();

  // Switch MAX485 to transmit mode and send the request
  digitalWrite(cfg::DE_RE, HIGH); // Enable RS485 transmitter (DE/RE pin HIGH)
  delayMicroseconds(500);           // Small delay for transceiver to stabilize
  Serial1.write(req, 8);            // Send 8-byte Modbus request frame
  Serial1.flush();                  // Wait for all bytes to be transmitted
  delayMicroseconds(500);           // Small delay before switching back
  digitalWrite(cfg::DE_RE,
               LOW); // Switch RS485 back to receive mode (DE/RE pin LOW)

  // Debug: print transmitted request bytes to serial monitor
  Serial.printf("[TX → Slave %d] ", slaveId);
  for (int i = 0; i < 8; i++)
    Serial.printf("%02X ", req[i]); // Print each byte in hexadecimal format
  Serial.println();

  // Read response bytes with 100ms timeout
  int len = 0;                 // Number of received bytes
  unsigned long t = millis();  // Record start time for timeout
  while (millis() - t < 100) { // Keep reading until 100ms timeout
    while (Serial1.available() &&
           len < 64) { // While data available and buffer not full
      rxbuf[len++] =
          Serial1.read(); // Store received byte and increment counter
      t = millis();       // Reset timeout on each byte received
    }
    delay(1); // Small delay to prevent tight loop
  }

  // Debug: print received response bytes to serial monitor
  Serial.printf("[RX ← Slave %d] (%d bytes): ", slaveId, len);
  for (int i = 0; i < len; i++)
    Serial.printf("%02X ", rxbuf[i]); // Print each received byte in hexadecimal
  Serial.println();

  return len; // Return total number of bytes received
}

/*
 * Parse a 32-bit IEEE 754 float from 4 bytes in Big-Endian byte order.
 * Flowmeter sends data as: [MSB] [byte2] [byte3] [LSB]
 * @param data - pointer to 4-byte array containing the float
 * @return     - parsed float value
 */
float parseFloatBE(uint8_t *data) {
  union {
    uint32_t u; // Unsigned 32-bit integer representation
    float f;    // IEEE 754 float representation
  } conv;       // Union allows reinterpreting the same memory as int or float
  // Assemble 4 bytes into a 32-bit integer in Big-Endian order
  conv.u = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
  return conv.f; // Return the float interpretation of the assembled bytes
}

// Validate a Modbus response frame: slave ID, FC03, CRC, expected byte count.
// Returns pointer to start of data bytes, or nullptr on failure.
static uint8_t *validateFrame(uint8_t slaveId, int len, uint8_t expectBytes) {
  for (int i = 0; i <= len - (int)(3 + expectBytes + 2); i++) {
    if (rxbuf[i] != slaveId || rxbuf[i + 1] != 0x03)
      continue;
    if (rxbuf[i + 2] != expectBytes)
      continue;
    int frameLen = 3 + expectBytes + 2;
    uint16_t recvCrc = rxbuf[i + frameLen - 2] | (rxbuf[i + frameLen - 1] << 8);
    if (recvCrc == crc16(&rxbuf[i], frameLen - 2))
      return &rxbuf[i + 3]; // pointer to first data byte
  }
  return nullptr;
}

// Read flow rate (Reg[00-01], 4 bytes, Big-Endian float) from a slave.
bool readFlowRate(uint8_t slaveId, float &outFlow) {
  int len = sendAndRead(slaveId, cfg::REG_FLOW_RATE, 2);
  uint8_t *data = validateFrame(slaveId, len, 4);
  if (!data)
    return false;
  outFlow = parseFloatBE(data);
  return true;
}

// Read totalizer (Reg[03-06], 14 bytes) from a slave.
// Layout: Reg[03-04] = int32 integer part, Reg[05-06] = float fractional part.
bool readTotalizer(uint8_t slaveId, float &outTotal) {
  int len = sendAndRead(slaveId, cfg::REG_TOTALIZER, 4); // 4 registers = 8 bytes raw
  uint8_t *data = validateFrame(slaveId, len, 8);
  if (!data)
    return false;
  uint32_t intPart = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                     ((uint32_t)data[2] << 8) | (uint32_t)data[3];
  float fracPart = parseFloatBE(&data[4]);
  outTotal = (float)((double)intPart + (double)fracPart);
  return true;
}

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
void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) // Skip if already connected
    return;

  Serial.printf("[WiFi] Connecting to network: %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);         // Station mode
  WiFi.setAutoReconnect(true); // Auto reconnect on disconnect
  WiFi.persistent(true);       // Persist credentials to NVS
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
    WiFi.disconnect(true); // Clear stale state so next begin() is clean
    delay(100);
    WiFi.begin(WIFI_SSID,
               WIFI_PASSWORD); // Kick off a fresh attempt in the background
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

/*
 * Check if Firebase is ready for database operations.
 * Prints a one-time "ready" message when authentication completes.
 * @return - true if Firebase is authenticated and ready, false otherwise
 */
bool ensureFirebase() {
  if (!Firebase.ready())
    return false;
  if (!firebaseReady) {
    firebaseReady = true;
    Serial.println("[Firebase] Ready");
  }
  return true;
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

// ========================== MODBUS RECOVERY =========================

/*
 * Escalating recovery when Modbus reads fail repeatedly:
 *   Level 1: flush UART + re-initialize Serial1 (most common fix — stuck buffer
 * / noise) Level 2: toggle MAX485 DE/RE pin (reset transceiver state) Level 3:
 * hardware reboot via watchdog (last resort)
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

  // Level 1: drain and restart the UART
  while (Serial1.available())
    Serial1.read();
  Serial1.end();
  delay(50);
  Serial1.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);

  // Level 2: cycle the MAX485 driver enable pin to reset transceiver state
  digitalWrite(cfg::DE_RE, HIGH);
  delay(5);
  digitalWrite(cfg::DE_RE, LOW);
  delay(5);

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

  // Configure MAX485 RS485 transceiver pins
  pinMode(cfg::DE_RE,
          OUTPUT); // Set DE/RE pin as output to control transmit/receive
  digitalWrite(cfg::DE_RE, LOW); // Start in receive mode (LOW = listening)

  // Initialize RS485 serial port (Serial1) for Modbus communication
  Serial1.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);
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

  // Fallback WiFi polling — onWifiEvent/setAutoReconnect handle most cases,
  // but we retry manually if the connection has been down for cfg::WIFI_RETRY_MS.
  if (WiFi.status() != WL_CONNECTED &&
      now - lastWifiCheck >= cfg::WIFI_RETRY_MS) {
    lastWifiCheck = now;
    wifiConnect();
  }

  // Rate limit: flow rate reads every cfg::READ_INTERVAL_MS
  if (now - lastReadTime < cfg::READ_INTERVAL_MS)
    return;
  lastReadTime = now;

  // Decide whether this cycle also includes a totalizer read (every 5 min)
  bool doTotalizer = (lastTotalizerRead == 0) ||
                     (now - lastTotalizerRead >= cfg::TOTALIZER_INTERVAL_MS);
  if (doTotalizer)
    lastTotalizerRead = now;

  // ---- Read Flowmeter 1 (Slave ID 2: Суларсан уусмал) ----
  // Retry хийхгүй — sensor хариу өгөхгүй бол энэ cycle-д орхиод явна.
  // Дараагийн cycle-д дахин оролдоно. Bus noise зөвхөн нэг cycle-д утга
  // тасрахад л хүргэнэ, дашбоард дараагийн уншилтаар сэргэнэ.
  float flow1 = 0.0, total1 = 0.0;
  bool okFlow1 = readFlowRate(cfg::FM1_SLAVE, flow1);
  bool okTotal1 = false;

  if (doTotalizer) {
    delay(50);
    okTotal1 = readTotalizer(cfg::FM1_SLAVE, total1);
    if (okTotal1) {
      lastTotal1 = total1;
      hasTotal1 = true;
    }
  }

  if (okFlow1 && (!doTotalizer || okTotal1))
    Serial.printf("[FM1] Flow: %.3f m3/h%s%.3f m3\n", flow1,
                  doTotalizer ? " | Total: " : " (total cached: ",
                  doTotalizer ? total1 : lastTotal1);
  else
    Serial.printf("[FM1] Read failed — flow:%d total:%d (doTotalizer=%d)\n",
                  okFlow1, okTotal1, doTotalizer);

  delay(50);

  // ---- Read Flowmeter 2 (Slave ID 3: Баян уусмал) ----
  float flow2 = 0.0, total2 = 0.0;
  bool okFlow2 = readFlowRate(cfg::FM2_SLAVE, flow2);
  bool okTotal2 = false;

  if (doTotalizer) {
    delay(50);
    okTotal2 = readTotalizer(cfg::FM2_SLAVE, total2);
    if (okTotal2) {
      lastTotal2 = total2;
      hasTotal2 = true;
    }
  }

  if (okFlow2 && (!doTotalizer || okTotal2))
    Serial.printf("[FM2] Flow: %.3f m3/h%s%.3f m3\n", flow2,
                  doTotalizer ? " | Total: " : " (total cached: ",
                  doTotalizer ? total2 : lastTotal2);
  else
    Serial.printf("[FM2] Read failed — flow:%d total:%d (doTotalizer=%d)\n",
                  okFlow2, okTotal2, doTotalizer);

  delay(50);

  // ---- Read Flowmeter 3 (Slave ID 4: Суларсан уусмал 2) ----
  float flow3 = 0.0, total3 = 0.0;
  bool okFlow3 = readFlowRate(cfg::FM3_SLAVE, flow3);
  bool okTotal3 = false;

  if (doTotalizer) {
    delay(50);
    okTotal3 = readTotalizer(cfg::FM3_SLAVE, total3);
    if (okTotal3) {
      lastTotal3 = total3;
      hasTotal3 = true;
    }
  }

  if (okFlow3 && (!doTotalizer || okTotal3))
    Serial.printf("[FM3] Flow: %.3f m3/h%s%.3f m3\n", flow3,
                  doTotalizer ? " | Total: " : " (total cached: ",
                  doTotalizer ? total3 : lastTotal3);
  else
    Serial.printf("[FM3] Read failed — flow:%d total:%d (doTotalizer=%d)\n",
                  okFlow3, okTotal3, doTotalizer);

  // ---- Track read failures and escalate recovery if needed ----
  bool anyReadOk = okFlow1 || okFlow2 || okFlow3 ||
                   (doTotalizer && (okTotal1 || okTotal2 || okTotal3));
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
  if (!ensureFirebase()) {
    led::setMode(led::SLOW_RED);
    return;
  }

  FirebaseJson json;
  bool anyWrite = false;

  if (okFlow1) { json.set("flowmeter1/flow_rate", flow1); anyWrite = true; }
  if (okFlow2) { json.set("flowmeter2/flow_rate", flow2); anyWrite = true; }
  if (okFlow3) { json.set("flowmeter3/flow_rate", flow3); anyWrite = true; }
  // Totalizer-ыг шинээр уншсан үед л бичинэ — өөрчлөгдөөгүй утгаар RTDB-г бөглөхгүй
  if (doTotalizer && okTotal1) { json.set("flowmeter1/totalizer", total1); anyWrite = true; }
  if (doTotalizer && okTotal2) { json.set("flowmeter2/totalizer", total2); anyWrite = true; }
  if (doTotalizer && okTotal3) { json.set("flowmeter3/totalizer", total3); anyWrite = true; }

  if (!anyWrite) {
    // Modbus бүх 3 sensor энэ cycle-д хариу өгсөнгүй → улаан
    led::setMode(led::SLOW_RED);
    return;
  }

  json.set("last_updated", (int)(millis() / 1000));

  if (Firebase.RTDB.updateNode(&fbData, "/flow_system", &json)) {
    Serial.println("[Firebase] Updated");
    fbOnSuccess();
    led::setMode(led::OFF);
    led::pulse(); // upload бүрд нэг ногоон импульс — terminal-той synchron
  } else {
    Serial.printf("[Firebase] update ERROR: %s\n", fbData.errorReason().c_str());
    fbOnFailure();
    led::setMode(led::SLOW_RED);
  }
}
