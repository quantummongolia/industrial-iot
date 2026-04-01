/*
 * ============================================================
 *  ESP32 Dual Flowmeter Reader → Firebase Realtime Database
 * ============================================================
 *
 * Hardware setup:
 *   - ESP32 DevKit microcontroller
 *   - MAX485 RS485 transceiver module (RE/DE → GPIO 4, RX2 → GPIO 16, TX2 → GPIO 17)
 *   - Flowmeter 1 (Modbus RTU protocol, Slave ID 2, 9600 baud, 8N1 serial config)
 *   - Flowmeter 2 (Modbus RTU protocol, Slave ID 3, 9600 baud, 8N1 serial config)
 *
 * Required libraries (configured in platformio.ini):
 *   - Firebase Arduino Client Library by Mobizt
 *
 * Data flow:
 *   ESP32 → RS485 bus → Flowmeters (Modbus RTU) → Parse float → Firebase RTDB
 */

#include "secrets.h"          // WiFi and Firebase credentials (not tracked in git)
#include <Arduino.h>          // Arduino core library for ESP32
#include <Firebase_ESP_Client.h> // Firebase client library for ESP32
#include <WiFi.h>             // WiFi connectivity library
#include <addons/RTDBHelper.h>   // Firebase Realtime Database helper functions
#include <addons/TokenHelper.h>  // Firebase authentication token helper
#include <esp_task_wdt.h>     // ESP32 hardware watchdog timer library

// ========================== CONFIGURATION ==========================

// MAX485 RS485 transceiver control pin (HIGH = transmit mode, LOW = receive mode)
#define MAX485_DE_RE 4
// RS485 serial receive pin (connected to MAX485 RO pin)
#define RS485_RX_PIN 16
// RS485 serial transmit pin (connected to MAX485 DI pin)
#define RS485_TX_PIN 17

// Modbus slave device addresses
#define FLOWMETER1_SLAVE_ID 2   // Modbus address of first flowmeter
#define FLOWMETER2_SLAVE_ID 3   // Modbus address of second flowmeter
#define MODBUS_BAUD 9600        // Modbus serial communication baud rate

// Modbus holding register address for current flow reading
#define REG_CURRENT_FLOW 0x0000

// Timing configuration
#define READ_INTERVAL_MS 800    // Interval between flowmeter readings in milliseconds
#define WIFI_RETRY_MS 10000     // WiFi reconnection attempt interval in milliseconds
#define WDT_TIMEOUT_S 30        // Watchdog timer timeout in seconds (resets ESP32 if stuck)

// Firebase Realtime Database paths for storing sensor data
#define FB_PATH_FM1_CURRENT "/flow_system/flowmeter1/current_flow"  // Flowmeter 1 current flow value path
#define FB_PATH_FM2_CURRENT "/flow_system/flowmeter2/current_flow"  // Flowmeter 2 current flow value path
#define FB_PATH_LAST_UPDATED "/flow_system/last_updated"            // Timestamp of last successful update

// ========================== GLOBAL OBJECTS ==========================

FirebaseData fbData;       // Firebase data object for storing request/response data
FirebaseAuth fbAuth;       // Firebase authentication credentials holder
FirebaseConfig fbConfig;   // Firebase configuration settings holder

unsigned long lastReadTime = 0;   // Timestamp of last sensor reading (milliseconds)
unsigned long lastWifiCheck = 0;  // Timestamp of last WiFi connection check (milliseconds)
bool firebaseReady = false;       // Flag indicating Firebase authentication is complete

// ========================== CRC16 CHECKSUM ================================

/*
 * Calculate Modbus CRC16 checksum for data integrity verification.
 * Uses the standard Modbus polynomial 0xA001.
 * @param buf  - pointer to data buffer
 * @param len  - number of bytes to process
 * @return     - 16-bit CRC checksum value
 */
uint16_t crc16(uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF;           // Initialize CRC with all bits set
  for (uint16_t i = 0; i < len; i++) {
    crc ^= buf[i];                  // XOR current byte into CRC
    for (uint8_t j = 0; j < 8; j++) { // Process each bit
      if (crc & 0x0001) {           // If least significant bit is set
        crc >>= 1;                  // Shift right by 1
        crc ^= 0xA001;             // XOR with Modbus polynomial
      } else {
        crc >>= 1;                 // Just shift right by 1
      }
    }
  }
  return crc;                       // Return calculated CRC16 value
}

// ========================== RAW MODBUS COMMUNICATION ============================

static uint8_t rxbuf[64];  // Receive buffer for Modbus response data (max 64 bytes)

/*
 * Send a Modbus RTU request and read the response from a slave device.
 * Constructs a Function Code 03 (Read Holding Registers) request frame.
 * @param slaveId - Modbus slave device address
 * @param reg     - Starting register address to read
 * @param count   - Number of registers to read
 * @return        - Number of bytes received in response
 */
int sendAndRead(uint8_t slaveId, uint16_t reg, uint16_t count) {
  uint8_t req[8];                   // Modbus request frame buffer (8 bytes for FC03)
  req[0] = slaveId;                 // Byte 0: Slave address
  req[1] = 0x03;                    // Byte 1: Function code 03 = Read Holding Registers
  req[2] = (reg >> 8) & 0xFF;      // Byte 2: Register address high byte
  req[3] = reg & 0xFF;             // Byte 3: Register address low byte
  req[4] = (count >> 8) & 0xFF;    // Byte 4: Number of registers high byte
  req[5] = count & 0xFF;           // Byte 5: Number of registers low byte
  uint16_t c = crc16(req, 6);      // Calculate CRC16 over first 6 bytes
  req[6] = c & 0xFF;               // Byte 6: CRC low byte
  req[7] = (c >> 8) & 0xFF;        // Byte 7: CRC high byte

  // Flush any stale data from the serial receive buffer
  while (Serial2.available())
    Serial2.read();

  // Switch MAX485 to transmit mode and send the request
  digitalWrite(MAX485_DE_RE, HIGH);   // Enable RS485 transmitter (DE/RE pin HIGH)
  delayMicroseconds(500);            // Small delay for transceiver to stabilize
  Serial2.write(req, 8);            // Send 8-byte Modbus request frame
  Serial2.flush();                   // Wait for all bytes to be transmitted
  delayMicroseconds(500);           // Small delay before switching back
  digitalWrite(MAX485_DE_RE, LOW);   // Switch RS485 back to receive mode (DE/RE pin LOW)

  // Debug: print transmitted request bytes to serial monitor
  Serial.printf("[TX → Slave %d] ", slaveId);
  for (int i = 0; i < 8; i++)
    Serial.printf("%02X ", req[i]);   // Print each byte in hexadecimal format
  Serial.println();

  // Read response bytes with 100ms timeout
  int len = 0;                       // Number of received bytes
  unsigned long t = millis();        // Record start time for timeout
  while (millis() - t < 100) {      // Keep reading until 100ms timeout
    while (Serial2.available() && len < 64) {  // While data available and buffer not full
      rxbuf[len++] = Serial2.read(); // Store received byte and increment counter
      t = millis();                  // Reset timeout on each byte received
    }
    delay(1);                        // Small delay to prevent tight loop
  }

  // Debug: print received response bytes to serial monitor
  Serial.printf("[RX ← Slave %d] (%d bytes): ", slaveId, len);
  for (int i = 0; i < len; i++)
    Serial.printf("%02X ", rxbuf[i]); // Print each received byte in hexadecimal
  Serial.println();

  return len;                        // Return total number of bytes received
}

/*
 * Parse a 32-bit IEEE 754 float from 4 bytes in Big-Endian byte order.
 * Flowmeter sends data as: [MSB] [byte2] [byte3] [LSB]
 * @param data - pointer to 4-byte array containing the float
 * @return     - parsed float value
 */
float parseFloatBE(uint8_t *data) {
  union {
    uint32_t u;   // Unsigned 32-bit integer representation
    float f;      // IEEE 754 float representation
  } conv;         // Union allows reinterpreting the same memory as int or float
  // Assemble 4 bytes into a 32-bit integer in Big-Endian order
  conv.u = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
  return conv.f;  // Return the float interpretation of the assembled bytes
}

/*
 * Read a float value from a Modbus slave's holding registers.
 * Sends a read request, validates the response frame (CRC, slave ID, function code),
 * and parses the 4 data bytes as a Big-Endian float.
 * Retries are handled by the caller.
 * @param slaveId  - Modbus slave device address
 * @param reg      - Starting register address
 * @param outValue - Reference to store the parsed float result
 * @return         - true if valid response received and parsed, false on error
 */
bool readFlowFloat(uint8_t slaveId, uint16_t reg, float &outValue) {
  int len = sendAndRead(slaveId, reg, 2);  // Read 2 registers (4 bytes = 1 float)
  if (len < 9)                             // Minimum valid response: 1(addr) + 1(fc) + 1(bytecount) + 4(data) + 2(crc) = 9 bytes
    return false;

  // Scan through received data to find a valid Modbus response frame
  for (int i = 0; i <= len - 9; i++) {
    if (rxbuf[i] == slaveId && rxbuf[i + 1] == 0x03) {  // Match slave ID and function code 03
      uint8_t byteCount = rxbuf[i + 2];   // Number of data bytes in response
      if (byteCount != 4)                  // Expect exactly 4 bytes (2 registers = 1 float)
        continue;
      int frameLen = 3 + byteCount + 2;   // Total frame length: header(3) + data(4) + CRC(2)
      if (i + frameLen <= len) {           // Ensure complete frame is within buffer
        // Extract and verify CRC16 checksum
        uint16_t recvCrc =
            rxbuf[i + frameLen - 2] | (rxbuf[i + frameLen - 1] << 8);  // Received CRC (little-endian)
        uint16_t calcCrc = crc16(&rxbuf[i], frameLen - 2);             // Calculated CRC over frame
        if (recvCrc == calcCrc) {          // CRC match confirms data integrity
          outValue = parseFloatBE(&rxbuf[i + 3]);  // Parse float from data bytes (offset 3)
          return true;                     // Successfully read and parsed flow value
        }
      }
    }
  }
  return false;  // No valid response frame found in received data
}

// ========================== WI-FI CONNECTION =========================

/*
 * Connect to WiFi network using credentials from secrets.h.
 * Skips if already connected. Waits up to 15 seconds for connection.
 * Called during setup and periodically in the main loop if disconnected.
 */
void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED)  // Skip if already connected to WiFi
    return;

  Serial.printf("[WiFi] Connecting to network: %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);               // Set WiFi to station mode (client, not access point)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Start WiFi connection with SSID and password

  unsigned long start = millis();     // Record connection attempt start time
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {  // Wait up to 15 seconds
    delay(500);                       // Check every 500ms
    Serial.print(".");                // Print progress dots
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected — IP: %s\n",
                  WiFi.localIP().toString().c_str());  // Print assigned IP address
  } else {
    Serial.println("\n[WiFi] Connection failed — will retry later");
  }
}

// ========================== FIREBASE INITIALIZATION ===============================

/*
 * Initialize Firebase connection with API key, database URL, and user credentials.
 * Uses email/password authentication method.
 * Called once during setup().
 */
void firebaseInit() {
  fbConfig.api_key = FIREBASE_API_KEY;       // Set Firebase project API key
  fbConfig.database_url = FIREBASE_DB_URL;   // Set Firebase Realtime Database URL

  fbAuth.user.email = FIREBASE_USER_EMAIL;   // Set Firebase auth email
  fbAuth.user.password = FIREBASE_USER_PASS; // Set Firebase auth password

  fbConfig.token_status_callback = tokenStatusCallback;  // Set callback for auth token status updates
  fbData.setBSSLBufferSize(2048, 2048);      // Set SSL buffer size for secure communication

  Firebase.begin(&fbConfig, &fbAuth);        // Initialize Firebase with config and auth
  Firebase.reconnectNetwork(true);           // Enable automatic network reconnection

  Serial.println("[Firebase] Initialized — authenticating...");
}

/*
 * Check if Firebase is ready for database operations.
 * Prints a one-time "ready" message when authentication completes.
 * @return - true if Firebase is authenticated and ready, false otherwise
 */
bool ensureFirebase() {
  if (!Firebase.ready())        // Check if Firebase auth token is valid
    return false;
  if (!firebaseReady) {         // First time Firebase becomes ready
    firebaseReady = true;       // Set flag to prevent repeated messages
    Serial.println("[Firebase] Ready");
  }
  return true;                  // Firebase is ready for database operations
}

// ========================== SETUP (RUNS ONCE AT BOOT) ==================================

/*
 * Arduino setup function — runs once when ESP32 powers on or resets.
 * Initializes serial ports, RS485 transceiver, WiFi, Firebase, and watchdog timer.
 */
void setup() {
  Serial.begin(115200);         // Initialize debug serial port at 115200 baud
  delay(100);                   // Brief delay for serial port to stabilize
  Serial.println("\n========= Flowmeter IoT System (x2) =========");

  // Configure MAX485 RS485 transceiver pins
  pinMode(MAX485_DE_RE, OUTPUT);      // Set DE/RE pin as output to control transmit/receive
  digitalWrite(MAX485_DE_RE, LOW);    // Start in receive mode (LOW = listening)
  pinMode(RS485_RX_PIN, INPUT_PULLUP); // Enable internal pull-up on RX pin for noise immunity

  // Initialize RS485 serial port (Serial2) for Modbus communication
  Serial2.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  Serial.printf(
      "[Modbus] Initialized — Flowmeter 1: Slave %d, Flowmeter 2: Slave %d\n",
      FLOWMETER1_SLAVE_ID, FLOWMETER2_SLAVE_ID);

  wifiConnect();    // Connect to WiFi network
  firebaseInit();   // Initialize Firebase connection

  // Configure ESP32 hardware watchdog timer to reset the chip if code hangs
  esp_task_wdt_config_t wdtConfig = {.timeout_ms = WDT_TIMEOUT_S * 1000,  // Timeout in milliseconds
                                     .idle_core_mask = 0,                   // Don't watch idle tasks
                                     .trigger_panic = true};                // Reset ESP32 on timeout
  esp_task_wdt_reconfigure(&wdtConfig);  // Apply watchdog configuration
  esp_task_wdt_add(NULL);                // Add current task to watchdog monitoring
  Serial.printf("[WDT] Watchdog started — %d second timeout\n", WDT_TIMEOUT_S);
}

// ========================== MAIN LOOP (RUNS REPEATEDLY) =================

/*
 * Arduino loop function — runs continuously after setup().
 * Reads both flowmeters via Modbus RTU and uploads values to Firebase.
 * Includes WiFi reconnection logic and watchdog timer reset.
 */
void loop() {
  esp_task_wdt_reset();         // Reset watchdog timer to prevent system reboot
  unsigned long now = millis(); // Get current time in milliseconds since boot

  // Reconnect WiFi if disconnected (checked every WIFI_RETRY_MS milliseconds)
  if (WiFi.status() != WL_CONNECTED && now - lastWifiCheck >= WIFI_RETRY_MS) {
    lastWifiCheck = now;        // Update last WiFi check timestamp
    wifiConnect();              // Attempt WiFi reconnection
  }

  // Rate limiting: skip reading if interval hasn't elapsed
  if (now - lastReadTime < READ_INTERVAL_MS)
    return;
  lastReadTime = now;           // Update last read timestamp

  // ---- Read Flowmeter 1 (Slave ID 2) ----
  float flow1 = 0.0;           // Variable to store flowmeter 1 reading
  bool ok1 = false;            // Flag indicating successful read
  for (int attempt = 0; attempt < 3 && !ok1; attempt++) {  // Retry up to 3 times
    ok1 = readFlowFloat(FLOWMETER1_SLAVE_ID, REG_CURRENT_FLOW, flow1);  // Read flow value
    if (!ok1 && attempt < 2)   // If failed and more retries available
      delay(50);               // Wait 50ms before retrying
  }

  if (ok1) {
    Serial.printf("[FM1] Flow: %.4f\n", flow1);  // Print successful reading
  } else {
    Serial.println("[FM1] Read failed (3 attempts)");  // All retries exhausted
  }

  // Brief delay between reading flowmeter 1 and 2 to prevent RS485 bus collision
  delay(50);

  // ---- Read Flowmeter 2 (Slave ID 3) ----
  float flow2 = 0.0;           // Variable to store flowmeter 2 reading
  bool ok2 = false;            // Flag indicating successful read
  for (int attempt = 0; attempt < 3 && !ok2; attempt++) {  // Retry up to 3 times
    ok2 = readFlowFloat(FLOWMETER2_SLAVE_ID, REG_CURRENT_FLOW, flow2);  // Read flow value
    if (!ok2 && attempt < 2)   // If failed and more retries available
      delay(50);               // Wait 50ms before retrying
  }

  if (ok2) {
    Serial.printf("[FM2] Flow: %.4f\n", flow2);  // Print successful reading
  } else {
    Serial.println("[FM2] Read failed (3 attempts)");  // All retries exhausted
  }

  // ---- Upload readings to Firebase Realtime Database ----
  if (!ensureFirebase()) {      // Check if Firebase is authenticated and ready
    Serial.println("[System] Firebase not ready");
    return;                     // Skip upload if Firebase is not available
  }

  // Upload Flowmeter 1 reading to Firebase if read was successful
  if (ok1) {
    if (Firebase.RTDB.setFloat(&fbData, FB_PATH_FM1_CURRENT, flow1)) {
      Serial.println("[Firebase] FM1 current_flow updated");  // Upload successful
    } else {
      Serial.printf("[Firebase] FM1 ERROR: %s\n", fbData.errorReason().c_str());  // Print error message
    }
  }

  // Upload Flowmeter 2 reading to Firebase if read was successful
  if (ok2) {
    if (Firebase.RTDB.setFloat(&fbData, FB_PATH_FM2_CURRENT, flow2)) {
      Serial.println("[Firebase] FM2 current_flow updated");  // Upload successful
    } else {
      Serial.printf("[Firebase] FM2 ERROR: %s\n", fbData.errorReason().c_str());  // Print error message
    }
  }

  // Update the last_updated timestamp if at least one reading was successful
  if (ok1 || ok2) {
    Firebase.RTDB.setInt(&fbData, FB_PATH_LAST_UPDATED, (int)(millis() / 1000));  // Seconds since boot
  }
}
