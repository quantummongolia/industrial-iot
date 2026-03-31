/*
 * ============================================================
 *  ESP32 Түвшин хэмжигч → Firebase Realtime Database
 * ============================================================
 *
 * Тоног төхөөрөмж:
 *   - ESP32 DevKit
 *   - MAX485 RS485 хөрвүүлэгч (RE/DE → GPIO 4, RX2 → GPIO 16, TX2 → GPIO 17)
 *   - Ultrasonic түвшин хэмжигч (Modbus RTU, Slave ID 1, 9600 8N1)
 *
 * Шаардлагатай сангууд (platformio.ini-д тохируулсан):
 *   - Firebase Arduino Client Library (Mobizt)
 *
 * Регистрийн зураглал:
 *   0x2001 (1 регистр) → Түвшний утга (uint16)
 */

#include "secrets.h"
#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <WiFi.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>
#include <esp_task_wdt.h>

// ========================== ТОХИРГОО ==========================

// MAX485 удирдлагын пин (HIGH = дамжуулах, LOW = хүлээн авах)
#define MAX485_DE_RE 4
#define RS485_RX_PIN 16
#define RS485_TX_PIN 17

// Modbus slave тохиргоо
#define MODBUS_SLAVE_ID 2
#define MODBUS_BAUD 9600

// Регистрийн хаяг
#define REG_CURRENT_FLOW 0x0000

// Хугацааны тохиргоо
#define READ_INTERVAL_MS 800
#define WIFI_RETRY_MS 10000
#define WDT_TIMEOUT_S 30

// Firebase RTDB зам
#define FB_PATH_CURRENT "/flow_system/current_flow"

// ========================== ГЛОБАЛ ОБЪЕКТУУД ==========================

FirebaseData fbData;
FirebaseAuth fbAuth;
FirebaseConfig fbConfig;

unsigned long lastReadTime = 0;
unsigned long lastWifiCheck = 0;
bool firebaseReady = false;

// ========================== CRC16 ================================

uint16_t crc16(uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// ========================== RAW MODBUS ============================

static uint8_t rxbuf[64];

int sendAndRead(uint16_t reg, uint16_t count) {
  uint8_t req[8];
  req[0] = MODBUS_SLAVE_ID;
  req[1] = 0x03; // Read Holding Register
  req[2] = (reg >> 8) & 0xFF;
  req[3] = reg & 0xFF;
  req[4] = (count >> 8) & 0xFF;
  req[5] = count & 0xFF;
  uint16_t c = crc16(req, 6);
  req[6] = c & 0xFF;
  req[7] = (c >> 8) & 0xFF;

  while (Serial2.available())
    Serial2.read();

  digitalWrite(MAX485_DE_RE, HIGH);
  delayMicroseconds(500);
  Serial2.write(req, 8);
  Serial2.flush();
  delayMicroseconds(500);
  digitalWrite(MAX485_DE_RE, LOW);

  Serial.print("[TX] ");
  for (int i = 0; i < 8; i++)
    Serial.printf("%02X ", req[i]);
  Serial.println();

  // Timeout-тэй хүлээлт — байт ирэхийг 100ms хүртэл хүлээнэ
  int len = 0;
  unsigned long t = millis();
  while (millis() - t < 100) {
    while (Serial2.available() && len < 64) {
      rxbuf[len++] = Serial2.read();
      t = millis(); // шинэ байт ирэх бүрт timeout сэргээнэ
    }
    delay(1);
  }

  Serial.printf("[RX] (%d bytes): ", len);
  for (int i = 0; i < len; i++)
    Serial.printf("%02X ", rxbuf[i]);
  Serial.println();

  return len;
}

/*
 * Big-Endian float parse (4 байт → float)
 */
float parseFloatBE(uint8_t *data) {
  union {
    uint32_t u;
    float f;
  } conv;
  conv.u = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
  return conv.f;
}

/*
 * Өгөгдсөн хаягаас 2 holding регистр уншиж float утга буцаана.
 * Noise-с хамгаалж slave ID + FC pattern хайж, CRC шалгана.
 */
bool readFlowFloat(uint16_t reg, float &outValue) {
  int len = sendAndRead(reg, 2);
  if (len < 9)
    return false;

  for (int i = 0; i <= len - 9; i++) {
    if (rxbuf[i] == MODBUS_SLAVE_ID && rxbuf[i + 1] == 0x03) {
      uint8_t byteCount = rxbuf[i + 2];
      if (byteCount != 4)
        continue;
      int frameLen = 3 + byteCount + 2;
      if (i + frameLen <= len) {
        uint16_t recvCrc =
            rxbuf[i + frameLen - 2] | (rxbuf[i + frameLen - 1] << 8);
        uint16_t calcCrc = crc16(&rxbuf[i], frameLen - 2);
        if (recvCrc == calcCrc) {
          outValue = parseFloatBE(&rxbuf[i + 3]);
          return true;
        }
      }
    }
  }
  return false;
}

// ========================== WI-FI ХОЛБОЛТ =========================

void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED)
    return;

  Serial.printf("[WiFi] %s сүлжээнд холбогдож байна", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Холбогдлоо — IP: %s\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Холболт амжилтгүй — дараа дахин оролдоно");
  }
}

// ========================== FIREBASE ===============================

void firebaseInit() {
  fbConfig.api_key = FIREBASE_API_KEY;
  fbConfig.database_url = FIREBASE_DB_URL;

  fbAuth.user.email = FIREBASE_USER_EMAIL;
  fbAuth.user.password = FIREBASE_USER_PASS;

  fbConfig.token_status_callback = tokenStatusCallback;
  fbData.setBSSLBufferSize(2048, 2048);

  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectNetwork(true);

  Serial.println("[Firebase] Эхлүүлсэн — нэвтэрч байна...");
}

bool ensureFirebase() {
  if (!Firebase.ready())
    return false;
  if (!firebaseReady) {
    firebaseReady = true;
    Serial.println("[Firebase] Бэлэн");
  }
  return true;
}

// ========================== SETUP ==================================

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n========= Түвшин Хэмжигч IoT Систем =========");

  pinMode(MAX485_DE_RE, OUTPUT);
  digitalWrite(MAX485_DE_RE, LOW);
  pinMode(RS485_RX_PIN, INPUT_PULLUP);

  Serial2.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  Serial.println("[Modbus] Эхлүүлсэн — Slave ID: " + String(MODBUS_SLAVE_ID));

  wifiConnect();
  firebaseInit();

  // Watchdog Timer эхлүүлэх (30 секунд)
  esp_task_wdt_config_t wdtConfig = {.timeout_ms = WDT_TIMEOUT_S * 1000,
                                     .idle_core_mask = 0,
                                     .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdtConfig);
  esp_task_wdt_add(NULL);
  Serial.printf("[WDT] Watchdog эхлүүлсэн — %d секунд\n", WDT_TIMEOUT_S);
}

// ========================== LOOP (ҮНДСЭН ДАВТАЛТ) =================

void loop() {
  esp_task_wdt_reset();
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastWifiCheck >= WIFI_RETRY_MS) {
    lastWifiCheck = now;
    wifiConnect();
  }

  if (now - lastReadTime < READ_INTERVAL_MS)
    return;
  lastReadTime = now;

  float flowValue = 0.0;
  bool ok = false;
  for (int attempt = 0; attempt < 3 && !ok; attempt++) {
    ok = readFlowFloat(REG_CURRENT_FLOW, flowValue);
    if (!ok && attempt < 2)
      delay(50);
  }

  if (!ok) {
    Serial.println("[Modbus] Уншилт амжилтгүй (3 оролдлого)");
    return;
  }

  Serial.printf("[Өгөгдөл] Урсгал: %.4f\n", flowValue);

  if (!ensureFirebase()) {
    Serial.println("[Систем] Firebase бэлэн биш");
    return;
  }

  if (Firebase.RTDB.setFloat(&fbData, FB_PATH_CURRENT, flowValue)) {
    Serial.println("[Firebase] current_flow шинэчлэгдлээ");
  } else {
    Serial.printf("[Firebase] АЛДАА: %s\n", fbData.errorReason().c_str());
  }

  // Timestamp тусдаа бичнэ — ижил утга дахин илгээхэд listener ажиллахын тулд
  Firebase.RTDB.setInt(&fbData, "/flow_system/last_updated",
                       (int)(millis() / 1000));
}
