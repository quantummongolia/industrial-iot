/*
 * ============================================================
 *  ESP32-S3 industrialMachine — USB Serial Monitor + WiFi Tools
 * ============================================================
 *
 *  2 горимтой жижиг "OS":
 *   • Device ESP залгаатай   → MONITOR: түүний serial-ийг OLED дээр хэвлэнэ.
 *   • Device ESP залгаагүй    → MENU: BOOT товчоор WiFi оношлогооны цэс сонгоно.
 *
 *  Удирдлага — BOOT товч ба joystick ЗЭРЭГЦЭЭ (аль нэгээр нь):
 *    BOOT 1 дарах / joystick доош  → дараагийн зүйл (joystick дээш → өмнөх)
 *    BOOT 2 хурдан / joystick баруун → сонгож ажиллуулах
 *    BOOT 3 хурдан / joystick зүүн   → буцах
 *
 *  Холболт:
 *    USB-C (native, GPIO19/20) → монитор хийх ESP-ийн USB порт
 *    OLED  SCL→GPIO10, SDA→GPIO11, VCC/GND→3V3/GND
 *
 *  Анхаар: ARDUINO_USB_CDC_ON_BOOT=0 тул native USB нь HOST, `Serial` нь UART0.
 *  `#define Serial gLog` тул бүх лог OLED terminal дээр мөн гарна.
 */

#include "display.h"
#include "button.h"
#include "joystick.h"
#include "wifitools.h"
#include <Arduino.h>
#include <USBHostSerial.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0-dev"
#endif

// БҮХ Serial.print* → TeeSerial → UART0 + OLED дэлгэц.
#define Serial gLog

namespace cfg {
constexpr uint32_t MONITOR_BAUD = 115200;
constexpr uint32_t RENDER_MS = 33;            // OLED refresh (~30fps)
} // namespace cfg

// ── USB Host serial ──
USBHostSerial hostSerial;

// ── Цэс ──
const char *const MENU_ITEMS[] = {
    "Signal",     // 1) сигналын хүч
    "Connect",    // 2) холбогдох тест
    "Data",       // 3) өгөгдөл илгээх/хүлээн авах
    "Speed",      // 4) хурдны тест
};
constexpr int MENU_N = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

// ── Аппын горим ──
enum AppMode { MENU, RUNNING, MONITOR };
AppMode appMode = MENU;
int     menuSel = 0;

unsigned long lastRender = 0;
bool          lastUsbState = false;

// USBHostSerial-ийн дотоод лог → ЗӨВХӨН UART0 (Serial0), дэлгэц рүү ОРУУЛАХГҮЙ.
// (gLog/tee-г тойрно.) Ингэснээр "VCP open... CDC" спам цэс/үйлдлийг бохирдуулахгүй.
// Бодит device-ийн serial болон [USB] connected event нь loop-д хэвээр харагдана.
void usbHostLogger(const char *msg) { Serial0.printf("USB: %s\n", msg); }

void showMenu() {
  disp::setMenu("== WiFi Tools ==", MENU_ITEMS, MENU_N, menuSel);
  disp::setMode(disp::MENU);
}

// Сонгосон үйлдлийг ажиллуулна (блоклож гүйцэтгэнэ, явцаа өөрөө рендерлэнэ).
void runSelected() {
  disp::clear();
  disp::setMode(disp::TERMINAL);
  disp::render();
  switch (menuSel) {
    case 0: wifitools::signalCheck();  break;
    case 1: wifitools::connectTest();  break;
    case 2: wifitools::dataTest();     break;
    case 3: wifitools::speedTest();    break;
  }
  Serial.printf("[< back]\n");
  appMode = RUNNING;
}

// Нэгдсэн навигаци — BOOT товч БА joystick хоёуланг зэрэг уншина (аль нэгээр).
// ХОЁУЛАНГ нь loop бүрд дуудах ёстой (debounce/edge төлвөө шинэчилнэ).
enum Nav { N_NONE, N_PREV, N_NEXT, N_ENTER, N_BACK };
Nav readNav() {
  Nav fromJoy = N_NONE;
  switch (joy::poll()) {
    case joy::UP:    fromJoy = N_PREV;  break;
    case joy::DOWN:  fromJoy = N_NEXT;  break;
    case joy::RIGHT: fromJoy = N_ENTER; break;
    case joy::LEFT:  fromJoy = N_BACK;  break;
    default: break;
  }
  Nav fromBtn = N_NONE;
  switch (btn::poll()) {
    case btn::SINGLE: fromBtn = N_NEXT;  break;
    case btn::DOUBLE: fromBtn = N_ENTER; break;
    case btn::TRIPLE: fromBtn = N_BACK;  break;
    default: break;
  }
  return fromJoy != N_NONE ? fromJoy : fromBtn;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  disp::begin();
  btn::begin();
  joy::begin();

  Serial.printf("industrialMachine v%s\n", FIRMWARE_VERSION);

  hostSerial.setLogger(usbHostLogger);
  hostSerial.begin(cfg::MONITOR_BAUD, 0, 0, 8);

  // Device залгаагүй бол цэснээс эхэлнэ.
  showMenu();
}

void loop() {
  unsigned long now = millis();

  // ── USB device холбогдох/салах ──
  bool usb = (bool)hostSerial;
  if (usb != lastUsbState) {
    lastUsbState = usb;
    if (usb) {
      appMode = MONITOR;
      disp::clear();
      disp::setMode(disp::TERMINAL);
      Serial.printf("[USB] connected\n");
    } else {
      Serial.printf("[USB] disconnected\n");
      appMode = MENU;
      showMenu();
    }
  }

  // ── MONITOR: device-ийн serial-ийг урсгана ──
  if (appMode == MONITOR && usb) {
    uint8_t buf[128];
    size_t got;
    while ((got = hostSerial.read(buf, sizeof(buf))) > 0) {
      Serial.write(buf, got);     // → UART0 + OLED (tee)
      if (got < sizeof(buf)) break;
    }
  }

  // ── MENU / RUNNING: BOOT товч + joystick (нэгдсэн навигаци) ──
  Nav nav = readNav();
  if (appMode == MENU) {
    switch (nav) {
      case N_PREV:  menuSel = (menuSel - 1 + MENU_N) % MENU_N; showMenu(); break;
      case N_NEXT:  menuSel = (menuSel + 1) % MENU_N;          showMenu(); break;
      case N_ENTER: runSelected(); break;
      default: break;
    }
  } else if (appMode == RUNNING) {
    if (nav == N_BACK) {                        // гарах → цэс рүү
      appMode = MENU;
      showMenu();
    }
  }

  // ── OLED рендер ──
  if (now - lastRender >= cfg::RENDER_MS) {
    lastRender = now;
    disp::render();
  }
}
