/*
 * display.h — SSD1306 128×64 I2C OLED рүү Serial логийг realtime толин тусгал
 * ----------------------------------------------------------------------
 *  Зорилго: UART debug дээр гарч буй БҮХ логийг (boot, USB таних, WiFi,
 *  [USB] connected, цаашлаад монитор хийж буй device-ийн serial) OLED дээр
 *  terminal маягаар шууд харуулна. Лог хадгалахгүй — зөвхөн realtime урсгал.
 *
 *  Ажиллах зарчим (mech-тэй ижил):
 *    - main.cpp дотор `#define Serial gLog` хийснээр бүх `Serial.print*`
 *      дуудлага TeeSerial руу орно. TeeSerial нь жинхэнэ ::Serial (UART0) рүү
 *      бичээд зэрэг богино FreeRTOS stream buffer руу байтуудыг түлхэнэ.
 *    - Рендер нь core 0 дээрх тусдаа task-д явагдана (loop()-ийн core 1-г
 *      хэзээ ч блоклохгүй).
 *    - feedBytes timeout=0 — buffer дүүрвэл шууд алгасна, хэзээ ч блоклохгүй.
 */
#pragma once
#include <Arduino.h>

namespace disp {

// Дэлгэцийн горим: TERMINAL = Serial лог урсгал, MENU = сонголтын цэс.
enum Mode { TERMINAL, MENU };

// SSD1306 init. Дэлгэц хариу өгөхгүй бол false (non-fatal — firmware үргэлжилнэ).
bool begin();

// TeeSerial-ийн дуудах цэг — байтуудыг terminal буферт нэмнэ. Олон task/core-оос
// дуудагдаж болох тул spinlock-оор хамгаалсан (thread-safe). Блоклохгүй.
void feedBytes(const uint8_t *data, size_t len);

// Идэвхтэй горим сонгоно (зөвхөн core 1-ээс дуудна).
void setMode(Mode m);

// Terminal буферийг цэвэрлэнэ (шинэ үйлдэл эхлэхэд). Thread-safe.
void clear();

// MENU горимд харуулах цэс: гарчиг, мөрүүд, сонгогдсон индекс. (core 1).
void setMenu(const char *title, const char *const *items, int count, int sel);

// Идэвхтэй горимоор OLED-д зурна. Үндсэн loop (core 1)-ээс тогтмол дуудна.
void render();
} // namespace disp

// Жинхэнэ ::Serial (UART0) рүү бичээд зэрэг дэлгэц рүү толин тусгана.
// main.cpp-д `#define Serial gLog` гэснээр бүх лог OLED дээр гарна.
class TeeSerial : public Print {
public:
  void begin(unsigned long baud) { Serial.begin(baud); }
  size_t write(uint8_t c) override {
    size_t r = Serial.write(c);
    disp::feedBytes(&c, 1);
    return r;
  }
  size_t write(const uint8_t *buf, size_t size) override {
    size_t r = Serial.write(buf, size);
    disp::feedBytes(buf, size);
    return r;
  }
  using Print::write;
};

extern TeeSerial gLog;
