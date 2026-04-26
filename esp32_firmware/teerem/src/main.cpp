/*
 * ============================================================
 *  ESP32 Тээрэм → Firebase Realtime Database
 * ============================================================
 *
 *  TODO: Sensor холболт болон Modbus/GPIO логикийг нэмэх.
 *  Одоогоор зөвхөн skeleton — WiFi + Firebase init.
 */

#include "secrets.h"
#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <WiFi.h>
#include <addons/RTDBHelper.h>
#include <addons/TokenHelper.h>

#define FB_PATH_STATUS "/teerem/status"

FirebaseData fbData;
FirebaseAuth fbAuth;
FirebaseConfig fbConfig;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n========= Teerem IoT System =========");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Failed");

  fbConfig.api_key = FIREBASE_API_KEY;
  fbConfig.database_url = FIREBASE_DB_URL;
  fbAuth.user.email = FIREBASE_USER_EMAIL;
  fbAuth.user.password = FIREBASE_USER_PASS;
  fbConfig.token_status_callback = tokenStatusCallback;
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectNetwork(true);
}

void loop() {
  // TODO: Тээрэмийн sensor унших, Firebase руу илгээх
  delay(1000);
}
