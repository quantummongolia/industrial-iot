/*
 * ota.h — Remote firmware management for lab ESP32.
 *
 * Дизайн:
 *   • DEVICE_ID-г WiFi MAC-аас гаргана (deviceId).
 *   • /devices/{id} зам дээр boot state, heartbeat, OTA progress бичнэ.
 *   • /commands/{id}/pending зам дээр RTDB stream сонсож developer command-ыг авна.
 *   • OTA download нь Core 0 дээрх тусдаа FreeRTOS task-д явагдана —
 *     main loop (Modbus, Firebase) огт блоклогдохгүй.
 *   • Progress мэдээллийг volatile state-д хадгална, main loop унших.
 *   • Boot self-test амжилттай бол esp_ota_mark_app_valid_cancel_rollback() дуудна.
 */
#pragma once
#include <Firebase_ESP_Client.h>
#include <Arduino.h>

namespace ota {

extern String deviceId;
extern const char* firmwareVersion;

/** OTA initialization — Firebase auth-ийн дараа нэг удаа дуудна. */
void begin(FirebaseData* fbData);

/** Per-cycle tick — main loop-аас амжилттай Firebase upload бүрд дуудна. */
void loop(FirebaseData* fbData, bool firebaseUploadOk);

/** OTA download/install идэвхтэй явагдаж байгаа эсэх. main loop үүнийг
 *  шалгаад LED-ээ өөрчилж эсвэл sensor read-ийг хойшлуулах боломжтой. */
bool isUpdating();

/** Сүүлд Firebase-д амжилттай бичсэн агшин (millis). 0 = хараахан холбогдоогүй.
 *  main loop-ийн Firebase delivery watchdog-д ашиглана. */
unsigned long lastContactMs();

/** main loop-оос гол data upload амжилттай болсныг мэдэгдэнэ (contact timestamp-г
 *  шинэчилнэ) — Firebase delivery watchdog-ийн "амьд" тэмдэг. */
void noteFbSuccess();

} // namespace ota
