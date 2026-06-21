/*
 * display.cpp — GC9A01 (240×240) дугуй дэлгэц рүү Serial логийг толин тусгал
 * ------------------------------------------------------------------------
 *  Драйвер сан: Arduino_GFX (moononournation) — GC9A01-д хөнгөн, найдвартай,
 *  тохиргоог кодод хийдэг тул OTA-д аюулгүй.
 *
 *  Холболт (ESP32-S3-Zero ↔ GC9A01 модуль, дэлгэцийн талаас pin зүүн талд):
 *    SCL → GPIO 1  (SPI clock / SCK)
 *    SDA → GPIO 2  (SPI data  / MOSI)
 *    DC  → GPIO 3
 *    CS  → GPIO 7
 *    RST → GPIO 8
 *    VCC/GND → 3V3 / GND   (backlight модульд тэжээлээс шууд асна — BL pin алга)
 */
#include "display.h"
#include <Arduino_GFX_Library.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

TeeSerial gLog;

namespace {
// ── GC9A01 pinout ───────────────────────────────────────────────────────
constexpr int8_t PIN_SCK = 1;  // SCL
constexpr int8_t PIN_MOSI = 2; // SDA
constexpr int8_t PIN_DC = 3;
constexpr int8_t PIN_CS = 7;
constexpr int8_t PIN_RST = 8;

// Чиглэл: 0..3. Дэлгэцийн талаас харахад pin ЗҮҮН талд байрласан.
// GC9A01-ийн native (rotation 0) нь pin доод талд = босоо. Pin зүүн талд тул
// агуулгыг 90° CCW эргүүлэх ROTATION 3 нь босоо зөв уншигдана.
// Хэрэв урвуу/толин харагдвал хөрш утга (0 эсвэл 2)-ыг туршина.
constexpr uint8_t ROTATION = 3;

// Текст — built-in GLCD фонт (6×8px суурь), textSize 2 → 12×16px.
constexpr int16_t SCR_W = 240;
constexpr int16_t SCR_H = 240;
constexpr uint8_t TEXT_SIZE = 2; // 12×16 тэмдэгт: нэг мөрөнд 20 ширхэг, нийт 15 мөр
constexpr int16_t CH_W = 6 * TEXT_SIZE; // тэмдэгтийн өргөн (12px)
constexpr int16_t CH_H = 8 * TEXT_SIZE; // мөрийн өндөр (16px)

// Terminal мөр буфер — industrialMachine-тэй ижил зарчмаар мөр мөрөөр дээш
// гүйлгэнэ (хуудсаар арчихгүй). SCR_W/CH_W тэмдэгт/мөр, SCR_H/CH_H мөр.
constexpr int COLS = SCR_W / CH_W;
constexpr int ROWS = SCR_H / CH_H;
char lines[ROWS][COLS + 1];

// RGB565 өнгө — Arduino_GFX-ийн хувилбараас (BLACK/RGB565_BLACK) хамаарахгүй
// байхын тулд шууд тодорхойлов.
constexpr uint16_t COL_BLACK = 0x0000;
constexpr uint16_t COL_WHITE = 0xFFFF;

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
StreamBufferHandle_t sb = nullptr;

int curRow = 0; // одоо бичиж буй мөр (дээрээс доош дүүрнэ)
int curCol = 0;

void clearBuf() {
  for (int i = 0; i < ROWS; i++) lines[i][0] = '\0';
  curRow = 0;
  curCol = 0;
}

// Шинэ мөр. Доод мөрд хүрсэн бол бүх мөрийг 1-ээр дээш гүйлгэж, доод мөрийг
// хоослоно — terminal шиг мөр мөрөөр дээш гүйдэг (хуудсаар арчихгүй).
void newline() {
  curCol = 0;
  if (curRow < ROWS - 1) {
    curRow++;
    return;
  }
  for (int i = 0; i < ROWS - 1; i++) strcpy(lines[i], lines[i + 1]);
  lines[ROWS - 1][0] = '\0';
}

void putChar(uint8_t c) {
  if (c == '\r') return;
  if (c == '\n') { newline(); return; }
  if (c < 32 || c > 126) c = ' '; // зөвхөн хэвлэгдэх ASCII
  if (curCol >= COLS) newline();   // мөр дүүрвэл доош гүйнэ (wrap)
  lines[curRow][curCol++] = c;
  lines[curRow][curCol] = '\0';
}

// Буферээс бүх дэлгэцийг дахин зурна. Мөр бүрийг COLS өргөнд хоосон зайгаар
// гүйцээх тул хуучин агуулга opaque (хар) дэвсгэрээр дарагдаж арилна —
// fillScreen хэрэггүй, анивчаа бага.
void redraw() {
  char row[COLS + 1];
  for (int r = 0; r < ROWS; r++) {
    int len = (int)strlen(lines[r]);
    for (int c = 0; c < COLS; c++) row[c] = (c < len) ? lines[r][c] : ' ';
    row[COLS] = '\0';
    gfx->setCursor(0, r * CH_H);
    gfx->print(row);
  }
}

// Core 0 дээр ажиллах рендер task. stream buffer-аас байт татаж зурна.
// 50ms timeout-той receive нь IDLE0-г өлсгөхгүй (WDT-д бүртгээгүй).
void renderTask(void *) {
  uint8_t buf[64];
  for (;;) {
    size_t n = xStreamBufferReceive(sb, buf, sizeof(buf), pdMS_TO_TICKS(50));
    if (n == 0) continue;                 // юу ч ирээгүй — дахин зурахгүй
    for (size_t i = 0; i < n; i++) putChar(buf[i]);
    // Нэг бөөн дэх үлдсэн байтуудыг шавхаад нэг л удаа redraw().
    while ((n = xStreamBufferReceive(sb, buf, sizeof(buf), 0)) > 0)
      for (size_t i = 0; i < n; i++) putChar(buf[i]);
    redraw();
  }
}
} // namespace

namespace disp {

bool begin() {
  bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, GFX_NOT_DEFINED);
  gfx = new Arduino_GC9A01(bus, PIN_RST, ROTATION, true /* IPS */);

  // Дэлгэц хариу өгөхгүй бол non-fatal — sb null хэвээр тул feedBytes no-op,
  // firmware Modbus/Firebase-ээ хэвийн үргэлжлүүлнэ.
  if (!gfx->begin())
    return false;

  gfx->fillScreen(COL_BLACK);
  gfx->setTextSize(TEXT_SIZE);
  gfx->setTextColor(COL_WHITE, COL_BLACK); // цагаан текст, хар дэвсгэр
  gfx->setTextWrap(false);
  clearBuf();

  // Богино урсгал буфер (1KB) — лог хадгалах биш, core1→core0 дамжуулагч.
  sb = xStreamBufferCreate(1024, 1);
  if (!sb)
    return false;

  // Core 0 — loop()-ийн core 1-г блоклохгүй.
  xTaskCreatePinnedToCore(renderTask, "dispTask", 4096, nullptr, 1, nullptr, 0);
  return true;
}

void feedBytes(const uint8_t *data, size_t len) {
  if (!sb)
    return;                               // дэлгэц бэлэн биш — чимээгүй алгасна
  xStreamBufferSend(sb, data, len, 0);    // timeout 0 — дүүрвэл алгасна, блоклохгүй
}

} // namespace disp
