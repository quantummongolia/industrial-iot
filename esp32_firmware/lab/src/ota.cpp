#include "ota.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mbedtls/md.h>
#include <time.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0-unknown"
#endif

namespace ota {

String deviceId;
const char* firmwareVersion = FIRMWARE_VERSION;

namespace {

constexpr uint32_t HEARTBEAT_MS = 30000;
constexpr uint32_t SELF_TEST_MIN_UPTIME_MS = 30000;
constexpr uint8_t  SELF_TEST_PUBLISH_GOAL = 3;
constexpr uint32_t OTA_TASK_STACK = 8192;
constexpr uint8_t  OTA_RETRY_MAX = 1;       // network алдаанд нэг удаа дахин оролдох
constexpr uint8_t  BOOT_FAILURE_THRESHOLD = 3;  // Энэ тооноос илүү дараалан self-test амжилтгүй → app-level rollback

FirebaseData      streamFb;
unsigned long     lastHeartbeat = 0;
unsigned long     bootMs = 0;
uint8_t           publishCounter = 0;
bool              selfTestDone = false;

// OTA task ↔ main loop хооронд солилцох state. Volatile тул main loop
// нэмэлт sync хэрэггүйгээр read хийнэ.
struct OtaProgress {
  volatile bool     active        = false;   // task ажиллаж байна уу
  volatile uint8_t  progressPct   = 0;       // 0..100
  volatile uint32_t lastUpdateMs  = 0;       // main loop publish dedup
  char              stage[32]     = "";      // "downloading" / "verifying" / ...
  char              message[96]   = "";
  char              cmdId[40]     = "";
  char              targetVersion[24] = "";
} progress;

struct OtaJob {
  char url[256];
  char sha256[80];
  char version[24];
  char cmdId[40];
};
QueueHandle_t otaQueue = nullptr;
TaskHandle_t  otaTaskHandle = nullptr;

// Команд стримийг main loop руу дамжуулах queue. Stream task нь Firebase/NVS-д ОГТ
// хүрэхгүй — зөвхөн ирсэн payload-ыг энд хийнэ. handleCommand-ийг main loop (fbData-ийн
// цорын ганц эзэн) ажиллуулна. Ингэснээр нэг FirebaseData объектыг 2 task зэрэг барих
// race (TLS/heap corruption → panic)-аас бүрэн сэргийлнэ.
struct PendingCmd { char payload[768]; };
QueueHandle_t cmdQueue = nullptr;

// Сүүлд Firebase-д амжилттай бичсэн агшин (ms). 0 = хараахан холбогдоогүй.
// main loop-ийн "Firebase delivery watchdog"-д ашиглана (token хүчинтэй ч TCP үхсэн
// "дүлий" төлвийг барина).
volatile unsigned long lastFbContactMs = 0;

uint32_t unixNow() {
  time_t t = time(nullptr);
  return (t > 1700000000) ? (uint32_t)t : 0;
}

String basePath()            { return "/devices/" + deviceId; }
String commandsPendingPath() { return "/commands/" + deviceId + "/pending"; }
String commandResultPath(const String& cmdId) {
  return "/commands/" + deviceId + "/result/" + cmdId;
}

String deriveId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof(buf), "lab_%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

void writeStageDirect(FirebaseData* fb, const char* stage, int pct,
                      const char* message, const char* cmdId) {
  FirebaseJson j;
  j.set("stage", stage);
  j.set("progress", pct);
  j.set("message", message);
  j.set("ts", (int)unixNow());
  j.set("cmd_id", cmdId);
  Firebase.RTDB.updateNodeSilent(fb, (basePath() + "/ota_status").c_str(), &j);
}

void writeResultDirect(FirebaseData* fb, const String& cmdId, const String& status,
                       const String& message) {
  FirebaseJson j;
  j.set("status", status);
  j.set("message", message);
  j.set("completed_at", (int)unixNow());
  j.set("firmware_version", firmwareVersion);
  Firebase.RTDB.updateNodeSilent(fb, commandResultPath(cmdId).c_str(), &j);
}

void publishBootState(FirebaseData* fb) {
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
  j.set("family", "lab");
  j.set("firmware", firmwareVersion);
  j.set("reset_reason", rrStr);
  j.set("status", status);
  j.set("mac", WiFi.macAddress());
  j.set("ip", WiFi.localIP().toString());
  j.set("partition", running ? running->label : "?");
  // Firebase server timestamp — NTP-аас хамаардаггүй, корпорейт WiFi-д ч ажиллана
  j.set("boot_time/.sv", "timestamp");
  j.set("last_heartbeat/.sv", "timestamp");
  Firebase.RTDB.updateNodeSilent(fb, basePath().c_str(), &j);

  // OTA state-machine cleanup — амжилттай boot хийсэн бол ota_status-ыг шинэчилнэ
  FirebaseJson ota;
  if (status == "validating") {
    ota.set("stage", "validating");
    ota.set("progress", 100);
    ota.set("message", String("Validating ") + firmwareVersion);
  } else if (status == "rolled_back") {
    ota.set("stage", "rolled_back");
    ota.set("progress", 0);
    ota.set("message", String("Reverted to ") + firmwareVersion);
  } else {
    ota.set("stage", "idle");
    ota.set("progress", 0);
    ota.set("message", "");
  }
  ota.set("ts/.sv", "timestamp");
  ota.set("cmd_id", "");
  Firebase.RTDB.updateNodeSilent(fb, (basePath() + "/ota_status").c_str(), &ota);
}

void writeHeartbeat(FirebaseData* fb) {
  FirebaseJson j;
  // Firebase server timestamp (ms) — ESP32-ийн NTP синхрончлогдоогүй ч ажиллана.
  // Сервер бичих үеийн өөрийн цагийг автомат бөглөнө.
  j.set("family", "lab");
  j.set("last_heartbeat/.sv", "timestamp");
  j.set("uptime_s", (int)(millis() / 1000));
  j.set("free_heap", (int)ESP.getFreeHeap());
  j.set("rssi", WiFi.RSSI());
  if (Firebase.RTDB.updateNodeSilent(fb, basePath().c_str(), &j))
    lastFbContactMs = millis();  // delivery watchdog-ийн "Firebase амьд" тэмдэг
}

// OTA task callback — HTTPUpdate-аас бүх 1-2KB block-ын дараа дуудагдана.
// Зөвхөн volatile state шинэчилнэ; жинхэнэ RTDB бичилт main loop-аар хийгдэнэ.
void onUpdateProgress(int cur, int total) {
  if (total <= 0) return;
  uint8_t pct = (uint8_t)((cur * 100) / total);
  if (pct != progress.progressPct) {
    progress.progressPct = pct;
  }
  // Task-ийн өөрийнхөө WDT нэмж буцаан өгөх хэрэгцээгүй — task өөрөө WDT-д ороогүй.
}

// FreeRTOS task — Core 0 дээр ажиллана. Main loop (Core 1) хөндөгдөхгүй.
void otaTask(void* param) {
  OtaJob job;
  for (;;) {
    if (xQueueReceive(otaQueue, &job, portMAX_DELAY) != pdTRUE) continue;

    progress.active = true;
    progress.progressPct = 0;
    strncpy(progress.cmdId, job.cmdId, sizeof(progress.cmdId) - 1);
    strncpy(progress.targetVersion, job.version, sizeof(progress.targetVersion) - 1);
    snprintf(progress.stage, sizeof(progress.stage), "downloading");
    snprintf(progress.message, sizeof(progress.message),
             "Татаж байна: %s", job.version);

    Serial.printf("[OTA-Task] Start: %s -> %s\n", firmwareVersion, job.version);

    HTTPUpdateResult res = HTTP_UPDATE_FAILED;
    for (uint8_t attempt = 0; attempt <= OTA_RETRY_MAX; ++attempt) {
      if (attempt > 0) {
        Serial.printf("[OTA-Task] Retry %u/%u\n", attempt, OTA_RETRY_MAX);
        snprintf(progress.message, sizeof(progress.message), "Дахин оролдож байна (%u/%u)", attempt, OTA_RETRY_MAX);
        progress.progressPct = 0;
        vTaskDelay(pdMS_TO_TICKS(2000));
      }

      WiFiClientSecure client;
      client.setInsecure();
      client.setTimeout(15);  // секунд

      httpUpdate.rebootOnUpdate(false);
      httpUpdate.onProgress(onUpdateProgress);
      httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

      res = httpUpdate.update(client, job.url, firmwareVersion);
      if (res == HTTP_UPDATE_OK) break;
    }

    if (res == HTTP_UPDATE_FAILED) {
      snprintf(progress.stage, sizeof(progress.stage), "failed");
      snprintf(progress.message, sizeof(progress.message), "Татах амжилтгүй: %s",
               httpUpdate.getLastErrorString().c_str());
      Serial.printf("[OTA-Task] %s\n", progress.message);
      progress.active = false;
      progress.progressPct = 0;
      continue;
    }

    if (res == HTTP_UPDATE_NO_UPDATES) {
      snprintf(progress.stage, sizeof(progress.stage), "skipped");
      snprintf(progress.message, sizeof(progress.message), "Шинэчлэлт хэрэггүй");
      progress.active = false;
      continue;
    }

    snprintf(progress.stage, sizeof(progress.stage), "rebooting");
    progress.progressPct = 100;
    snprintf(progress.message, sizeof(progress.message), "Шинэ firmware бичигдлээ — restart...");
    Serial.println("[OTA-Task] Reboot pending...");

    // Main loop progress publish хийх боломж олгох
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP.restart();
  }
}

// Main loop OTA progress өөрчлөгдөхөд RTDB-руу publish хийнэ.
void publishProgressIfChanged(FirebaseData* fb) {
  static uint8_t lastPublishedPct = 255;
  static char lastPublishedStage[32] = "";

  if (!progress.active && strlen(progress.stage) == 0) return;  // юу ч идэвхгүй

  bool stageChanged = strcmp(progress.stage, lastPublishedStage) != 0;
  bool pctChanged = (progress.progressPct >= lastPublishedPct + 5) ||
                    progress.progressPct == 100 ||
                    progress.progressPct == 0;

  if (!stageChanged && !pctChanged) return;

  writeStageDirect(fb, progress.stage, progress.progressPct, progress.message, progress.cmdId);

  lastPublishedPct = progress.progressPct;
  strncpy(lastPublishedStage, progress.stage, sizeof(lastPublishedStage) - 1);

  // Stage "failed" эсвэл "skipped" болсон бол command result-руу бичнэ
  if (strcmp(progress.stage, "failed") == 0) {
    writeResultDirect(fb, progress.cmdId, "failed", progress.message);
    progress.stage[0] = '\0';  // дахин publish хийхгүй
  } else if (strcmp(progress.stage, "skipped") == 0) {
    writeResultDirect(fb, progress.cmdId, "skipped", progress.message);
    progress.stage[0] = '\0';
  }
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
  writeResultDirect(fb, cmdId, "received", String(action));

  if (strcmp(action, "ping") == 0) {
    String info = String("v") + firmwareVersion + ", uptime=" +
                  String(millis() / 1000) + "s, heap=" + String(ESP.getFreeHeap());
    writeResultDirect(fb, cmdId, "completed", info);
  }
  else if (strcmp(action, "reboot") == 0) {
    writeResultDirect(fb, cmdId, "completed", "reboot_now");
    delay(500);
    ESP.restart();
  }
  else if (strcmp(action, "update") == 0) {
    const char* url       = doc["url"]     | "";
    const char* sha       = doc["sha256"]  | "";
    const char* targetVer = doc["version"] | "?";
    if (strlen(url) == 0 || strlen(sha) != 64) {
      writeResultDirect(fb, cmdId, "failed", "missing_url_or_sha256");
      return;
    }
    if (strcmp(targetVer, firmwareVersion) == 0) {
      writeResultDirect(fb, cmdId, "skipped", "already_on_target_version");
      return;
    }
    if (progress.active) {
      writeResultDirect(fb, cmdId, "rejected", "ota_already_in_progress");
      return;
    }

    OtaJob job{};
    strncpy(job.url, url, sizeof(job.url) - 1);
    strncpy(job.sha256, sha, sizeof(job.sha256) - 1);
    strncpy(job.version, targetVer, sizeof(job.version) - 1);
    strncpy(job.cmdId, cmdId, sizeof(job.cmdId) - 1);
    if (xQueueSend(otaQueue, &job, 0) != pdTRUE) {
      writeResultDirect(fb, cmdId, "failed", "queue_full");
      return;
    }
    writeResultDirect(fb, cmdId, "queued", "OTA task queued");
  }
  else {
    writeResultDirect(fb, cmdId, "failed", String("unknown_action:") + action);
  }
}

void streamCallback(FirebaseStream data) {
  String payload = data.payload();
  if (payload.length() == 0 || payload == "null") return;
  Serial.printf("[CMD] stream event @ %s\n", data.dataPath().c_str());
  // ВАЖНО: энэ нь Firebase санги доторх стрим task-аас дуудагдана. Энд Firebase эсвэл
  // NVS-д ХҮРВЭЛ streamFb-г main loop-той зэрэг барьж race → corruption → panic үүснэ.
  // Тиймээс зөвхөн payload-ыг queue-д тавиад гарна; боловсруулалтыг main loop хийнэ.
  if (!cmdQueue) return;
  PendingCmd pc;
  strncpy(pc.payload, payload.c_str(), sizeof(pc.payload) - 1);
  pc.payload[sizeof(pc.payload) - 1] = '\0';
  if (xQueueSend(cmdQueue, &pc, 0) != pdTRUE)
    Serial.println("[CMD] queue full — command dropped");
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("[CMD] stream timeout — reconnecting...");
}

void selfTestIfReady(FirebaseData* fb) {
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
    FirebaseJson j;
    j.set("status", "running");
    j.set("validated_at/.sv", "timestamp");  // Firebase server timestamp (NTP-аас хамаардаггүй)
    if (Firebase.RTDB.updateNodeSilent(fb, basePath().c_str(), &j))
      lastFbContactMs = millis();

    // OTA state-machine: validating → completed
    FirebaseJson ota;
    ota.set("stage", "completed");
    ota.set("progress", 100);
    ota.set("message", String("Updated to ") + firmwareVersion);
    ota.set("ts/.sv", "timestamp");
    ota.set("cmd_id", "");
    Firebase.RTDB.updateNodeSilent(fb, (basePath() + "/ota_status").c_str(), &ota);
  }

  // App-level boot counter цэвэрлэх — энэ boot self-test амжилттай давсан
  {
    Preferences p;
    p.begin("ota", false);
    if (p.getUChar("boot_fail", 0) != 0) {
      p.putUChar("boot_fail", 0);
      Serial.println("[OTA] boot_fail counter cleared");
    }
    p.end();
  }
  selfTestDone = true;
}

// Boot эхэнд дуудна — boot count counter-ийг зөвхөн PENDING_VERIFY state-д буюу
// сүүлд OTA-аар суулгасан firmware-д шалгана. Validated (ESP_OTA_IMG_VALID)
// app-ийг "fail" гэж тоолохгүй — өмнө амжилттай ажилласан firmware дахин
// reboot хийгээд rollback гарах эрсдэл арилна.
void checkAppLevelRollback() {
  esp_ota_img_states_t state;
  const esp_partition_t* running = esp_ota_get_running_partition();
  bool isPending = (running && esp_ota_get_state_partition(running, &state) == ESP_OK
                    && state == ESP_OTA_IMG_PENDING_VERIFY);

  Preferences p;
  p.begin("ota", false);

  if (!isPending) {
    // Validated firmware ажиллаж байна — counter цэвэрлэе (өмнөх firmware-ын үлдсэн утга)
    if (p.getUChar("boot_fail", 0) != 0) {
      p.putUChar("boot_fail", 0);
      Serial.println("[OTA] Running validated firmware — boot_fail cleared");
    }
    p.end();
    return;
  }

  uint8_t failCount = p.getUChar("boot_fail", 0) + 1;
  p.putUChar("boot_fail", failCount);
  Serial.printf("[OTA] PENDING_VERIFY boot #%u (threshold %u)\n",
                failCount, BOOT_FAILURE_THRESHOLD);

  if (failCount >= BOOT_FAILURE_THRESHOLD) {
    const esp_partition_t* prev = esp_ota_get_next_update_partition(NULL);
    if (prev) {
      Serial.printf("[OTA] App-level rollback → switching to %s\n", prev->label);
      esp_ota_set_boot_partition(prev);
      p.putUChar("boot_fail", 0);
      p.end();
      delay(500);
      ESP.restart();
    } else {
      Serial.println("[OTA] App-level rollback failed: no other partition");
      p.end();
    }
  } else {
    p.end();
  }
}

} // anonymous namespace

void begin(FirebaseData* fbData) {
  bootMs = millis();
  deviceId = deriveId();
  Serial.printf("[OTA] DEVICE_ID = %s\n", deviceId.c_str());
  Serial.printf("[OTA] FIRMWARE_VERSION = %s\n", firmwareVersion);

  // App-level rollback шалгалт — boot fail counter threshold давсан бол өмнөх
  // partition руу switch хийгээд ESP.restart() дуудна (энэ функцээс буцахгүй).
  checkAppLevelRollback();

  publishBootState(fbData);
  lastFbContactMs = millis();  // boot publish = эхний амжилттай контакт

  // OTA task + queue үүсгэх (нэг л удаа)
  if (!otaQueue) {
    otaQueue = xQueueCreate(2, sizeof(OtaJob));
    cmdQueue = xQueueCreate(4, sizeof(PendingCmd)); // стрим task → main loop команд
    xTaskCreatePinnedToCore(otaTask, "ota", OTA_TASK_STACK, nullptr,
                            1 /* priority */, &otaTaskHandle, 0 /* Core 0 */);
    Serial.println("[OTA] Background task spawned on Core 0");
  }

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
  if (firebaseUploadOk && publishCounter < 255) publishCounter++;

  // Стрим task-аас ирсэн командуудыг ЭНД (main loop, fbData-ийн цорын ганц эзэн)
  // боловсруулна. handleCommand нь NVS dedup + үр дүнгийн бичилт + reboot/update-ийг
  // бүгдийг main loop контекстэд гүйцэтгэнэ → cross-task FirebaseData хандалт байхгүй.
  if (cmdQueue) {
    PendingCmd pc;
    while (xQueueReceive(cmdQueue, &pc, 0) == pdTRUE)
      handleCommand(fbData, String(pc.payload));
  }

  // OTA progress publish — main loop-аас бичих учир Firebase нь нэг л task-аас хандана
  publishProgressIfChanged(fbData);

  unsigned long now = millis();
  if (now - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    writeHeartbeat(fbData);
  }

  selfTestIfReady(fbData);
}

bool isUpdating() {
  return progress.active;
}

unsigned long lastContactMs() { return lastFbContactMs; }

void noteFbSuccess() { lastFbContactMs = millis(); }

} // namespace ota
