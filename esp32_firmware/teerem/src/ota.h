/*
 * ota.h — Remote firmware management for teerem ESP32.
 *
 * Responsibilities:
 *   1) Derive a stable DEVICE_ID from the WiFi MAC.
 *   2) Mirror device state to RTDB /devices/{id}.
 *   3) Listen to RTDB /commands/{id}/pending for developer-issued commands.
 *   4) Execute OTA from GitHub releases with sha256 verification + rollback.
 *   5) Self-validate after boot, or fall back to the previous app via bootloader.
 *
 * Public surface kept minimal: begin() once after Firebase auth, loop() per cycle.
 */
#pragma once
#include <Firebase_ESP_Client.h>
#include <Arduino.h>

namespace ota {

extern String deviceId;         // "teerem_aabbccddeeff" (set in begin())
extern const char* firmwareVersion;  // FIRMWARE_VERSION macro from build flags

/** Initialise: derive DEVICE_ID, publish boot state, open command stream.
 *  MUST be called after Firebase.ready() returns true. */
void begin(FirebaseData* fbData);

/** Per-loop tick: heartbeat publish + self-test gate. */
void loop(FirebaseData* fbData, bool firebaseUploadOk);

/** Called after every successful Firebase publish so self-test can count. */
void noteSuccessfulPublish();

} // namespace ota
