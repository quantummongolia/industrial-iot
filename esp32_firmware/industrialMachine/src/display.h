/*
 * display.h — 0.92" I2C OLED (SSD1306, addr 0x3C) дээр монитор хийж буй
 * төхөөрөмжийн serial гаралтыг terminal маягаар realtime харуулна.
 * ----------------------------------------------------------------------
 *  • USB-аар төхөөрөмж залгагдсан үед түүний serial текстийг гүйлгэж харуулна.
 *  • Залгаагүй (USB host device байхгүй) үед "NOT CONNECTED" + хувилбар/IP.
 *  • Дэлгэц олдохгүй бол begin() false буцаана — firmware non-fatal үргэлжилнэ.
 *
 *  Бүх API нь main loop (Core 1)-ээс л дуудагдана — дотроо нэмэлт sync хийхгүй.
 */
#pragma once
#include <Arduino.h>

namespace disp {

// SSD1306 128×32 init. Олдохгүй бол false (non-fatal).
bool begin();

// Монитор хийж буй төхөөрөмжөөс ирсэн байтуудыг terminal буферт нэмнэ.
void feed(const uint8_t* data, size_t len);

// USB төхөөрөмж залгагдсан эсэх — false бол "NOT CONNECTED" дэлгэц.
void setConnected(bool connected);

// "NOT CONNECTED" дэлгэцийн доод мөрд гарах статус (ж: хувилбар, IP).
void setStatus(const char* line);

// Одоогийн төлвийг OLED-д зурна. main loop-аас тогтмол (~80мс) дуудна.
void render();

} // namespace disp
