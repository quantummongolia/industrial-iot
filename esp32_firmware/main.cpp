/*
 * ============================================================
 *  ESP32 Урсгал хэмжигч → Firebase Realtime Database
 * ============================================================
 *
 * Тоног төхөөрөмж:
 *   - ESP32 DevKit
 *   - MAX485 RS485 хөрвүүлэгч (RE/DE → GPIO 4, RX2 → GPIO 16, TX2 → GPIO 17)
 *   - Үйлдвэрлэлийн урсгал хэмжигч (Modbus RTU, Slave ID 1, 9600 8N1)
 *
 * Шаардлагатай сангууд (Arduino Library Manager-ээс суулгана):
 *   - ModbusMaster (Doc Walker)
 *   - Firebase ESP Client (Mobizt) — Firebase_ESP_Client
 *
 * Регистрийн зураглал:
 *   0x0000 (2 регистр) → Одоогийн урсгал (IEEE 754 Float32)
 *   0x0008 (2 регистр) → Нийт урсгал     (IEEE 754 Float32)
 */

#include <WiFi.h>
#include <ModbusMaster.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>   // Firebase токен үүсгэгч
#include <addons/RTDBHelper.h>    // Firebase RTDB туслах функцууд

// ========================== ТОХИРГОО ==========================

// Wi-Fi нэвтрэх мэдээлэл
#define WIFI_SSID     "YOUR_WIFI_SSID"       // Wi-Fi нэрээ бичнэ
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"    // Wi-Fi нууц үгээ бичнэ

// Firebase төслийн мэдээлэл
#define FIREBASE_API_KEY    "YOUR_FIREBASE_API_KEY"                  // Firebase API түлхүүр
#define FIREBASE_DB_URL     "https://YOUR_PROJECT_ID.firebaseio.com" // Firebase DB холбоос

// MAX485 удирдлагын пин (HIGH = дамжуулах, LOW = хүлээн авах)
#define MAX485_DE_RE 4

// Modbus slave тохиргоо
#define MODBUS_SLAVE_ID   1
#define MODBUS_BAUD       9600
#define MODBUS_SERIAL     Serial2   // GPIO 16 (RX2), GPIO 17 (TX2)

// Регистрийн хаягууд
#define REG_CURRENT_FLOW  0x0000    // Одоогийн урсгал
#define REG_TOTAL_FLOW    0x0008    // Нийт урсгал
#define REG_COUNT         2         // 2 x 16-бит регистр = 32-бит float

// Хугацааны тохиргоо
#define READ_INTERVAL_MS  1000      // 1 секунд тутамд уншина
#define WIFI_RETRY_MS     10000     // 10 секунд тутамд Wi-Fi дахин холбоно

// Firebase RTDB зам
#define FB_PATH_CURRENT   "/flow_system/current_flow"
#define FB_PATH_TOTAL     "/flow_system/total_flow"

// ========================== ГЛОБАЛ ОБЪЕКТУУД ==========================

ModbusMaster modbus;
FirebaseData fbData;
FirebaseAuth fbAuth;
FirebaseConfig fbConfig;

unsigned long lastReadTime  = 0;    // Сүүлийн уншсан хугацаа
unsigned long lastWifiCheck = 0;    // Сүүлийн Wi-Fi шалгасан хугацаа
bool firebaseReady = false;         // Firebase бэлэн эсэх

// ========================== RS485 ЧИГЛЭЛ УДИРДЛАГА ================

// Modbus дамжуулахын ӨМНӨ дуудагдана — MAX485-г дамжуулах горимд шилжүүлнэ
void preTransmission() {
  digitalWrite(MAX485_DE_RE, HIGH);
}

// Modbus дамжуулсны ДАРАА дуудагдана — MAX485-г хүлээн авах горимд шилжүүлнэ
void postTransmission() {
  digitalWrite(MAX485_DE_RE, LOW);
}

// ========================== IEEE 754 ХӨРВҮҮЛЭЛТ ====================

/*
 * 2 ширхэг 16-бит Modbus регистрийг нэгтгэж 32-бит IEEE 754 float болгоно.
 * Регистрийн дараалал: Big-Endian (ахлах үг эхэнд).
 * Хэрэв таны төхөөрөмж Low-word-first ашигладаг бол highWord, lowWord-г солино.
 */
float registersToFloat(uint16_t highWord, uint16_t lowWord) {
  uint32_t combined = ((uint32_t)highWord << 16) | (uint32_t)lowWord;
  float result;
  memcpy(&result, &combined, sizeof(result));
  return result;
}

// ========================== WI-FI ХОЛБОЛТ =========================

void wifiConnect() {
  // Аль хэдийн холбогдсон бол алгасна
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[WiFi] %s сүлжээнд холбогдож байна", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // 15 секунд хүлээнэ
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Холбогдлоо — IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Холболт амжилтгүй — дараа дахин оролдоно");
  }
}

// ========================== FIREBASE ===============================

// Токены төлөвийг Serial-д хэвлэх callback функц
void tokenStatusCallback(token_info_t info) {
  Serial.printf("[Firebase] Токен төлөв: %s\n", getTokenStatusString(info));
}

// Firebase эхлүүлэх (Anonymous Authentication — имэйл/нууц үг шаардахгүй)
void firebaseInit() {
  fbConfig.api_key = FIREBASE_API_KEY;
  fbConfig.database_url = FIREBASE_DB_URL;

  fbConfig.token_status_callback = tokenStatusCallback;

  // ESP32 дээрх SSL санах ойн хэрэглээг хязгаарлана
  fbData.setBSSLBufferSize(1024, 1024);

  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectNetwork(true);  // Сүлжээ тасрахад автомат дахин холбоно

  // Anonymous нэвтрэлт — имэйл/нууц үг шаардахгүй
  Firebase.signUp(&fbConfig, &fbAuth, "", "");

  Serial.println("[Firebase] Эхлүүлсэн (Anonymous) — токен хүлээж байна...");
}

// Firebase бэлэн эсэхийг шалгана
bool ensureFirebase() {
  if (!Firebase.ready()) {
    return false;
  }
  if (!firebaseReady) {
    firebaseReady = true;
    Serial.println("[Firebase] Бэлэн");
  }
  return true;
}

// ========================== MODBUS =================================

// Modbus холболт эхлүүлэх
void modbusInit() {
  MODBUS_SERIAL.begin(MODBUS_BAUD, SERIAL_8N1, 16, 17);
  modbus.begin(MODBUS_SLAVE_ID, MODBUS_SERIAL);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);
  Serial.println("[Modbus] Эхлүүлсэн — Slave ID: " + String(MODBUS_SLAVE_ID));
}

/*
 * Өгөгдсөн хаягаас 2 holding регистр уншиж float руу хөрвүүлнэ.
 * Амжилттай бол true буцааж, утгыг outValue-д бичнэ.
 */
bool readFloatRegister(uint16_t startAddr, float &outValue) {
  uint8_t result = modbus.readHoldingRegisters(startAddr, REG_COUNT);

  if (result != modbus.ku8MBSuccess) {
    Serial.printf("[Modbus] 0x%04X уншихад алдаа — код: 0x%02X\n", startAddr, result);
    return false;
  }

  uint16_t highWord = modbus.getResponseBuffer(0);
  uint16_t lowWord  = modbus.getResponseBuffer(1);
  outValue = registersToFloat(highWord, lowWord);
  return true;
}

// ========================== SETUP ==================================

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n========= Урсгал Хэмжигч IoT Систем =========");

  // MAX485 чиглэлийн пин тохируулах
  pinMode(MAX485_DE_RE, OUTPUT);
  digitalWrite(MAX485_DE_RE, LOW);  // Анхдагч: хүлээн авах горим

  wifiConnect();      // Wi-Fi холбох
  firebaseInit();     // Firebase эхлүүлэх
  modbusInit();       // Modbus эхлүүлэх
}

// ========================== LOOP (ҮНДСЭН ДАВТАЛТ) =================

void loop() {
  unsigned long now = millis();

  // Wi-Fi тасарсан бол 10 секунд тутамд дахин холбоно
  if (WiFi.status() != WL_CONNECTED && now - lastWifiCheck >= WIFI_RETRY_MS) {
    lastWifiCheck = now;
    wifiConnect();
  }

  // 1 секунд тутамд уншина
  if (now - lastReadTime < READ_INTERVAL_MS) return;
  lastReadTime = now;

  // Modbus-с урсгалын утгуудыг уншина
  float currentFlow = 0.0f;
  float totalFlow   = 0.0f;

  bool okCurrent = readFloatRegister(REG_CURRENT_FLOW, currentFlow);
  bool okTotal   = readFloatRegister(REG_TOTAL_FLOW,   totalFlow);

  // Хоёулаа амжилтгүй бол алгасна
  if (!okCurrent && !okTotal) {
    Serial.println("[Систем] Modbus уншилт бүгд амжилтгүй — алгаслаа");
    return;
  }

  // Serial монитор дээр хэвлэнэ
  if (okCurrent) Serial.printf("[Өгөгдөл] Одоогийн урсгал: %.4f\n", currentFlow);
  if (okTotal)   Serial.printf("[Өгөгдөл] Нийт урсгал:     %.4f\n", totalFlow);

  // Firebase бэлэн эсэхийг шалгана
  if (!ensureFirebase()) {
    Serial.println("[Систем] Firebase бэлэн биш — зөвхөн Serial-д хэвлэлээ");
    return;
  }

  // Одоогийн урсгалыг Firebase руу илгээнэ
  if (okCurrent) {
    if (Firebase.RTDB.setFloat(&fbData, FB_PATH_CURRENT, currentFlow)) {
      Serial.println("[Firebase] current_flow шинэчлэгдлээ");
    } else {
      Serial.printf("[Firebase] current_flow АЛДАА: %s\n", fbData.errorReason().c_str());
    }
  }

  // Нийт урсгалыг Firebase руу илгээнэ
  if (okTotal) {
    if (Firebase.RTDB.setFloat(&fbData, FB_PATH_TOTAL, totalFlow)) {
      Serial.println("[Firebase] total_flow шинэчлэгдлээ");
    } else {
      Serial.printf("[Firebase] total_flow АЛДАА: %s\n", fbData.errorReason().c_str());
    }
  }
}
