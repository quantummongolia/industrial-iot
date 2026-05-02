/*
 * ============================================================
 *  ESP32-S3-Zero Бутлуур → Firebase Realtime Database
 * ============================================================
 *
 *  Modbus RTU (RS485 via MAX485) → flow rate (t/h) + cumulative weight (kg)
 *  → Firebase RTDB path: /butluur/flow_rate, /butluur/cumulative_kg
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

// ── Modbus тохиргоо ────────────────────────────────────────────────────
namespace cfg {
constexpr uint8_t  RX_PIN   = 4;
constexpr uint8_t  TX_PIN   = 5;
constexpr uint8_t  DE_RE    = 6;
constexpr uint8_t  SLAVE    = 1;
constexpr uint32_t BAUD     = 9600;
constexpr uint16_t REG_FLOW = 0;     // 40001: Flow Rate (Float, t/h)
constexpr uint16_t REG_KG   = 10;    // 40011: Cumulative weight (Int32, kg)
constexpr uint32_t POLL_MS  = 1000;
constexpr uint32_t RX_TMO   = 200;
} // namespace cfg

// ── Firebase paths ─────────────────────────────────────────────────────
#define FB_PATH_FLOW   "/butluur/weight_rate"
#define FB_PATH_UPDATE "/butluur/last_updated"

// ── Modbus ─────────────────────────────────────────────────────────────
class Modbus {
public:
  void begin() {
    pinMode(cfg::DE_RE, OUTPUT);
    digitalWrite(cfg::DE_RE, LOW);
    Serial1.begin(cfg::BAUD, SERIAL_8N1, cfg::RX_PIN, cfg::TX_PIN);
  }

  bool readFloat(uint16_t addr, float &out) {
    uint8_t rx[9];
    if (!readRegs(addr, 2, rx)) return false;
    uint32_t raw = (uint32_t)rx[3] << 24 | (uint32_t)rx[4] << 16 |
                   (uint32_t)rx[5] << 8  | rx[6];
    memcpy(&out, &raw, 4);
    return true;
  }

  bool readInt32(uint16_t addr, int32_t &out) {
    uint8_t rx[9];
    if (!readRegs(addr, 2, rx)) return false;
    out = (int32_t)((uint32_t)rx[3] << 24 | (uint32_t)rx[4] << 16 |
                    (uint32_t)rx[5] << 8  | rx[6]);
    return true;
  }

private:
  bool readRegs(uint16_t addr, uint8_t regCnt, uint8_t *rx) {
    uint8_t req[8] = {
        cfg::SLAVE, 0x03, uint8_t(addr >> 8), uint8_t(addr),
        0,          regCnt, 0, 0};
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF;
    req[7] = c >> 8;

    send(req, sizeof(req));

    const uint8_t respLen = 5 + regCnt * 2;
    if (!receive(rx, respLen)) return false;
    if (rx[0] != cfg::SLAVE || rx[1] != 0x03 || rx[2] != regCnt * 2) return false;
    if (crc16(rx, respLen - 2) !=
        uint16_t(rx[respLen - 2] | (rx[respLen - 1] << 8))) return false;
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
      if (Serial1.available()) buf[got++] = Serial1.read();
    }
    return got == want;
  }
};

// ── Глобал ─────────────────────────────────────────────────────────────
Modbus modbus;
FirebaseData   fbData;
FirebaseAuth   fbAuth;
FirebaseConfig fbConfig;

bool firebaseReady = false;

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
  if (WiFi.status() == WL_CONNECTED) return;

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
  fbConfig.api_key      = FIREBASE_API_KEY;
  fbConfig.database_url = FIREBASE_DB_URL;
  fbAuth.user.email     = FIREBASE_USER_EMAIL;
  fbAuth.user.password  = FIREBASE_USER_PASS;
  fbConfig.token_status_callback = tokenStatusCallback;
  fbData.setBSSLBufferSize(2048, 2048);
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectNetwork(true);
  Serial.println("[Firebase] init — authenticating");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n========= Butluur IoT (S3-Zero) =========");

  modbus.begin();
  WiFi.onEvent(onWifiEvent);
  wifiConnect();
  firebaseInit();
}

void loop() {
  static unsigned long lastPoll = 0;
  if (millis() - lastPoll < cfg::POLL_MS) return;
  lastPoll = millis();

  float flow;
  bool flowOk = modbus.readFloat(cfg::REG_FLOW, flow);

  if (flowOk) Serial.printf("Flow = %.2f t/h\n", flow);
  else        Serial.println("Flow = #");

  if (!Firebase.ready()) return;
  if (!firebaseReady) { firebaseReady = true; Serial.println("[Firebase] ready"); }

  if (flowOk) {
    if (!Firebase.RTDB.setFloat(&fbData, FB_PATH_FLOW, flow))
      Serial.printf("[Firebase] flow err: %s\n", fbData.errorReason().c_str());
    else
      Firebase.RTDB.setInt(&fbData, FB_PATH_UPDATE, (int)(millis() / 1000));
  }
}
