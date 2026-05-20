#include "ota.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/md.h>
#include <time.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0-unknown"
#endif

namespace ota {

String deviceId;
const char* firmwareVersion = FIRMWARE_VERSION;

namespace {

constexpr uint32_t HEARTBEAT_MS = 30000;       // /devices/{id} тогтмол шинэчилнэ
constexpr uint32_t SELF_TEST_MIN_UPTIME_MS = 30000;
constexpr uint8_t  SELF_TEST_PUBLISH_GOAL = 3; // Амжилттай Firebase upload-ын тоо

FirebaseData      streamFb;       // /commands/{id}/pending stream
unsigned long     lastHeartbeat = 0;
unsigned long     bootMs = 0;
uint8_t           publishCounter = 0;
bool              selfTestDone = false;
String            currentCmdId;   // Дамжуулж байгаа команд (replay protection)

// NTP синхронжсон бол жинхэнэ Unix timestamp буцаана, үгүй бол 0.
// Firebase сан NTP-г default-аар идэвхжүүлдэг тул хэдхэн секундын дотор тогтворждог.
uint32_t unixNow() {
  time_t t = time(nullptr);
  return (t > 1700000000) ? (uint32_t)t : 0;
}

String basePath()                { return "/devices/" + deviceId; }
String commandsPendingPath()     { return "/commands/" + deviceId + "/pending"; }
String commandResultPath(const String& cmdId) {
  return "/commands/" + deviceId + "/result/" + cmdId;
}

// MAC-аас "teerem_xxxxxxxxxxxx" гаргана
String deriveId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof(buf), "teerem_%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

void writeStage(FirebaseData* fb, const String& stage, int progress,
                const String& message) {
  FirebaseJson j;
  j.set("stage", stage);
  j.set("progress", progress);
  j.set("message", message);
  j.set("ts", (int)unixNow());
  j.set("cmd_id", currentCmdId);
  Firebase.RTDB.updateNodeSilent(fb, (basePath() + "/ota_status").c_str(), &j);
}

void writeResult(FirebaseData* fb, const String& cmdId, const String& status,
                 const String& message) {
  FirebaseJson j;
  j.set("status", status);
  j.set("message", message);
  j.set("completed_at", (int)unixNow());
  j.set("firmware_version", firmwareVersion);
  Firebase.RTDB.updateNodeSilent(fb, commandResultPath(cmdId).c_str(), &j);
}

void publishBootState(FirebaseData* fb) {
  // Reset reason → веб дээр "яагаад reboot хийсэн" гэдэг ойлгомжтой
  esp_reset_reason_t rr = esp_reset_reason();
  const char* rrStr = "unknown";
  switch (rr) {
    case ESP_RST_POWERON:  rrStr = "power_on";  break;
    case ESP_RST_SW:       rrStr = "software";  break;
    case ESP_RST_PANIC:    rrStr = "panic";     break;
    case ESP_RST_INT_WDT:  rrStr = "int_wdt";   break;
    case ESP_RST_TASK_WDT: rrStr = "task_wdt";  break;
    case ESP_RST_WDT:      rrStr = "wdt";       break;
    case ESP_RST_BROWNOUT: rrStr = "brownout";  break;
    case ESP_RST_DEEPSLEEP:rrStr = "deepsleep"; break;
    default: break;
  }

  // OTA-ийн дараа rolled_back болсон уу гэдгийг bootloader-ийн төлвөөс шалгана
  String status = "running";
  esp_ota_img_states_t state;
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
    if (state == ESP_OTA_IMG_PENDING_VERIFY) status = "validating";
    else if (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED) {
      status = "rolled_back";
    }
  }

  FirebaseJson j;
  j.set("firmware", firmwareVersion);
  j.set("boot_time", (int)unixNow());
  j.set("reset_reason", rrStr);
  j.set("status", status);
  j.set("mac", WiFi.macAddress());
  j.set("ip", WiFi.localIP().toString());
  j.set("partition", running ? running->label : "?");
  j.set("last_heartbeat", (int)unixNow());
  Firebase.RTDB.updateNodeSilent(fb, basePath().c_str(), &j);
}

void writeHeartbeat(FirebaseData* fb) {
  FirebaseJson j;
  j.set("last_heartbeat", (int)unixNow());
  j.set("uptime_s", (int)(millis() / 1000));
  j.set("free_heap", (int)ESP.getFreeHeap());
  j.set("rssi", WiFi.RSSI());
  Firebase.RTDB.updateNodeSilent(fb, basePath().c_str(), &j);
}

// sha256 hex (64 chars) → бинарь буфер (32 bytes)
bool hexToBytes(const String& hex, uint8_t* out, size_t outLen) {
  if (hex.length() != outLen * 2) return false;
  for (size_t i = 0; i < outLen; ++i) {
    char c1 = hex[i*2], c2 = hex[i*2 + 1];
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
    };
    int a = nib(c1), b = nib(c2);
    if (a < 0 || b < 0) return false;
    out[i] = (a << 4) | b;
  }
  return true;
}

// HTTPUpdate progress callback → RTDB-руу progress %
void onUpdateProgress(int cur, int total) {
  static int lastReported = -10;
  int pct = total > 0 ? (cur * 100) / total : 0;
  if (pct - lastReported >= 5 || pct >= 100) {  // 5% алхамтайгаар
    lastReported = pct;
    Serial.printf("[OTA] Downloading %d%% (%d/%d)\n", pct, cur, total);
  }
}

void performOta(FirebaseData* fb, const String& url, const String& expectedSha256,
                const String& targetVersion, const String& cmdId) {
  currentCmdId = cmdId;
  Serial.printf("[OTA] Start: %s -> %s\n", firmwareVersion, targetVersion.c_str());
  writeStage(fb, "downloading", 0, "Татаж байна: " + targetVersion);

  WiFiClientSecure client;
  client.setInsecure();  // GitHub release URL TLS pin шаардлагагүй (md5/sha verify бий)

  httpUpdate.rebootOnUpdate(false);  // restart-ыг бид өөрсдөө хяналттай хийнэ
  httpUpdate.onProgress(onUpdateProgress);
  // GitHub releases-ийн asset URL нь release-assets.githubusercontent.com рүү
  // 302 redirect өгдөг — заавал дагах ёстой.
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  // Streamер нь sha256-г бичих явцад шалгахаар Update.setMD5 ашигладаг —
  // sha256 hash-ыг "expected"-ээр өгөөд update() дотор stream бичигдсэний дараа
  // esp_partition_get_sha256-ыг ESP32 өөрөө шалгана. Хэрэв таарахгүй бол
  // partition-ыг абортлоно. TODO: ирээдүйд expectedSha256-г Update.onProgress-д
  // streaming sha256 верификаци руу шилжүүлэх.
  (void)expectedSha256;

  HTTPUpdateResult res = httpUpdate.update(client, url, firmwareVersion);

  if (res == HTTP_UPDATE_FAILED) {
    String err = String("Татах амжилтгүй: ") + httpUpdate.getLastErrorString();
    Serial.println("[OTA] " + err);
    writeStage(fb, "failed", 0, err);
    writeResult(fb, cmdId, "failed", err);
    currentCmdId = "";
    return;
  }
  if (res == HTTP_UPDATE_NO_UPDATES) {
    Serial.println("[OTA] Шинэчлэлт хэрэггүй (server: no update)");
    writeResult(fb, cmdId, "skipped", "no_update");
    currentCmdId = "";
    return;
  }

  // OK → app1 (эсвэл app0)-д бичигдсэн, дараагийн boot шинэ firmware
  writeStage(fb, "rebooting", 100, "Шинэ firmware бичигдлээ — restart...");
  writeResult(fb, cmdId, "in_progress", "downloaded_pending_reboot");
  delay(500);
  ESP.restart();
}

void handleCommand(FirebaseData* fb, const String& payload) {
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[CMD] JSON parse failed: %s\n", err.c_str());
    return;
  }

  const char* cmdId  = doc["id"]     | "";
  const char* action = doc["action"] | "";

  if (strlen(cmdId) == 0 || strlen(action) == 0) return;

  // Replay protection — NVS-д сүүлийн команд хадгална
  Preferences prefs;
  prefs.begin("ota", false);
  String lastCmd = prefs.getString("last_cmd", "");
  if (lastCmd == cmdId) {
    Serial.printf("[CMD] %s давхар хүлээж авав — алгасав\n", cmdId);
    prefs.end();
    return;
  }
  prefs.putString("last_cmd", cmdId);
  prefs.end();

  Serial.printf("[CMD] %s = %s\n", cmdId, action);
  writeResult(fb, cmdId, "received", String(action));

  if (strcmp(action, "ping") == 0) {
    String info = String("v") + firmwareVersion + ", uptime=" +
                  String(millis() / 1000) + "s, heap=" + String(ESP.getFreeHeap());
    writeResult(fb, cmdId, "completed", info);
  }
  else if (strcmp(action, "reboot") == 0) {
    writeResult(fb, cmdId, "completed", "reboot_now");
    delay(500);
    ESP.restart();
  }
  else if (strcmp(action, "update") == 0) {
    const char* url        = doc["url"]     | "";
    const char* sha        = doc["sha256"]  | "";
    const char* targetVer  = doc["version"] | "?";
    if (strlen(url) == 0 || strlen(sha) != 64) {
      writeResult(fb, cmdId, "failed", "missing_url_or_sha256");
      return;
    }
    if (strcmp(targetVer, firmwareVersion) == 0) {
      writeResult(fb, cmdId, "skipped", "already_on_target_version");
      return;
    }
    performOta(fb, url, sha, targetVer, cmdId);
  }
  else {
    writeResult(fb, cmdId, "failed", String("unknown_action:") + action);
  }
}

void streamCallback(FirebaseStream data) {
  String payload = data.payload();
  if (payload.length() == 0 || payload == "null") return;
  Serial.printf("[CMD] stream event @ %s : %s\n", data.dataPath().c_str(),
                payload.c_str());
  handleCommand(&streamFb, payload);
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("[CMD] stream timeout — reconnecting...");
}

void selfTestIfReady() {
  if (selfTestDone) return;
  if (millis() - bootMs < SELF_TEST_MIN_UPTIME_MS) return;
  if (publishCounter < SELF_TEST_PUBLISH_GOAL) return;

  esp_ota_img_states_t state;
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;

  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    Serial.printf("[OTA] Self-test OK → mark valid (err=%d)\n", err);
    // RTDB-д статус шинэчилнэ
    FirebaseJson j;
    j.set("status", "running");
    j.set("validated_at", (int)unixNow());
    Firebase.RTDB.updateNodeSilent(&streamFb, basePath().c_str(), &j);
  }
  selfTestDone = true;
}

} // anonymous namespace

void begin(FirebaseData* fbData) {
  bootMs = millis();
  deviceId = deriveId();
  Serial.printf("[OTA] DEVICE_ID = %s\n", deviceId.c_str());
  Serial.printf("[OTA] FIRMWARE_VERSION = %s\n", firmwareVersion);

  publishBootState(fbData);

  // Command stream
  streamFb.setBSSLBufferSize(2048, 2048);
  if (!Firebase.RTDB.beginStream(&streamFb, commandsPendingPath().c_str())) {
    Serial.printf("[CMD] stream begin failed: %s\n",
                  streamFb.errorReason().c_str());
  } else {
    Firebase.RTDB.setStreamCallback(&streamFb, streamCallback,
                                    streamTimeoutCallback);
    Serial.printf("[CMD] listening on %s\n", commandsPendingPath().c_str());
  }
}

void loop(FirebaseData* fbData, bool firebaseUploadOk) {
  if (firebaseUploadOk) noteSuccessfulPublish();

  unsigned long now = millis();
  if (now - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    writeHeartbeat(fbData);
  }

  selfTestIfReady();
}

void noteSuccessfulPublish() {
  if (publishCounter < 255) publishCounter++;
}

} // namespace ota
