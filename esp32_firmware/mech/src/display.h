/*
 * display.h — GC9A01 дугуй дэлгэц рүү Serial логийг realtime толин тусгал
 * ----------------------------------------------------------------------
 *  Зорилго: USB serial monitor дээр гарч буй бүх логийг 240×240 GC9A01
 *  дэлгэцэд terminal маягаар шууд харуулна. Лог хадгалахгүй, flash/SPIFFS
 *  ашиглахгүй — зөвхөн realtime урсгал.
 *
 *  Ажиллах зарчим:
 *    - main.cpp дотор `#define Serial gLog` хийснээр бүх `Serial.print*`
 *      дуудлага TeeSerial руу орно. TeeSerial нь жинхэнэ ::Serial (USB) рүү
 *      бичээд зэрэг богино FreeRTOS stream buffer руу байтуудыг түлхэнэ.
 *    - Рендер нь core 0 дээрх тусдаа task-д явагдана (loop()-ийн core 1-г
 *      хэзээ ч блоклохгүй → Modbus/Firebase timing, watchdog аюулгүй).
 *    - feedBytes timeout=0 — buffer дүүрвэл шууд алгасна, хэзээ ч блоклохгүй.
 */
#pragma once
#include <Arduino.h>

namespace disp {
// GC9A01 init + core 0 рендер task асаана. Дэлгэц хариу өгөхгүй бол false
// буцаана (non-fatal — firmware хэвийн үргэлжилнэ). OTA-д аюулгүй.
bool begin();

// TeeSerial-ийн дуудах цэг — байтуудыг рендер task руу дамжуулна.
// Блоклохгүй; дэлгэц бэлэн биш бол чимээгүй no-op.
void feedBytes(const uint8_t *data, size_t len);
} // namespace disp

// Жинхэнэ ::Serial (USB CDC) рүү бичээд зэрэг дэлгэц рүү толин тусгана.
// main.cpp-д `#define Serial gLog` гэснээр Serial1 (Modbus)-д хүрэлгүй
// зөвхөн USB логийг барьж авна.
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
