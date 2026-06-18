/*
 * display.cpp — SSD1306 128×64 I2C OLED рүү Serial логийг realtime толин тусгал
 *
 *  I2C холболт (ESP32-S3 N8R2):
 *    OLED SCL → GPIO 10
 *    OLED SDA → GPIO 11
 *    OLED VCC → 3V3, GND → GND
 *  ⚠️ USB host нь GPIO19/20 (USB D-/D+)-г эзэлдэг тул OLED-г 19/20 дээр тавьж
 *     БОЛОХГҮЙ. I2C-г өөр хос пинд (одоо 10/11) холбоно.
 *
 *  Дэлгэц: 0.92" SSD1306, addr 0x78 (8-bit) = 0x3C (7-bit). 180° эргүүлсэн (R2).
 *  Бүх лог (host + device serial) НЭГ terminal урсгалаар, 4x6 фонт, 10 мөр×32.
 *
 *  Concurrency: feedBytes()-г олон task/core зэрэг дуудаж болох тул мөр буферийг
 *  spinlock-оор хамгаалсан. Рендер нь зөвхөн үндсэн loop (core 1)-ээс дуудагдах
 *  бөгөөд Wire-ийг мөн core 1-д эхлүүлсэн тул I2C cross-core зөрчил гарахгүй.
 *  (mech нь SPI учир core 0 task ашигладаг — I2C-д энэ нь эрсдэлтэй.)
 */
#include "display.h"
#include <U8g2lib.h>
#include <Wire.h>

TeeSerial gLog;

namespace {
constexpr uint8_t DISP_SDA   = 11;
constexpr uint8_t DISP_SCL   = 10;
constexpr uint8_t DISP_ADDR7 = 0x3C;   // 7-bit (Wire ping)
constexpr uint8_t DISP_ADDR8 = 0x78;   // 8-bit (U8g2 setI2CAddress)

// Hardware-I2C, full-frame буфер. 180° эргүүлсэн (R2).
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, /*reset=*/U8X8_PIN_NONE);

// Terminal буфер — 4x6 фонт (хамгийн жижиг), мөр 6px → 10 мөр × 32 тэмдэгт.
constexpr int ROWS = 10;
constexpr int COLS = 32;
char lines[ROWS][COLS + 1];
int  curRow = 0;   // одоогийн бичиж буй мөр (дээрээс доош дүүрнэ)
int  curCol = 0;

bool ok = false;

// ── Цэс (MENU) горимын төлөв — зөвхөн core 1-ээс хүрнэ, lock хэрэггүй ──
disp::Mode          mode = disp::TERMINAL;
const char         *menuTitle = "";
const char *const  *menuItems = nullptr;
int                 menuCount = 0;
int                 menuSel = 0;

// feedBytes() олон task/core-оос дуудагдах тул буферийг spinlock-оор хамгаална.
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void clearBuf() {
  for (int i = 0; i < ROWS; i++) lines[i][0] = '\0';
  curRow = 0;
  curCol = 0;
}

// Шинэ мөр рүү шилжинэ. Доод мөрд хүрсэн бол бүх мөрийг 1-ээр дээш гүйлгэж,
// доод мөрд үлдэнэ — жинхэнэ terminal шиг мөр мөрөөр дээш гүйдэг.
void newline() {
  curCol = 0;
  if (curRow < ROWS - 1) {
    curRow++;
    return;
  }
  for (int i = 0; i < ROWS - 1; i++) strcpy(lines[i], lines[i + 1]);
  lines[ROWS - 1][0] = '\0';
}

// putChar нь mux барьсан үед л дуудагдана.
void putChar(uint8_t c) {
  if (c == '\r') return;
  if (c == '\n') { newline(); return; }
  if (c < 32 || c > 126) c = ' ';         // зөвхөн хэвлэгдэх ASCII
  if (curCol >= COLS) newline();          // мөр дүүрвэл шинэ мөр (wrap)
  lines[curRow][curCol++] = c;
  lines[curRow][curCol] = '\0';
}
} // namespace

namespace disp {

bool begin() {
  Wire.begin(DISP_SDA, DISP_SCL);
  Wire.setClock(400000);

  // Дэлгэц байгаа эсэхийг I2C ping-ээр шалгана. Cold boot/тэжээл тогтворжих
  // хүртэл хариу өгөхгүй байж болзошгүй тул хэдэн удаа давтана.
  bool found = false;
  for (int attempt = 0; attempt < 10 && !found; attempt++) {
    Wire.beginTransmission(DISP_ADDR7);
    if (Wire.endTransmission() == 0) { found = true; break; }
    delay(50);
  }
  if (!found) {
    ok = false;
    return false;
  }

  u8g2.setI2CAddress(DISP_ADDR8);
  u8g2.begin();
  u8g2.setFontMode(1);
  clearBuf();
  ok = true;

  u8g2.clearBuffer();
  u8g2.sendBuffer();                      // хоосон дэлгэц — лог тэр даруй урсана
  return true;
}

void feedBytes(const uint8_t *data, size_t len) {
  if (!ok || !data) return;
  portENTER_CRITICAL(&mux);
  for (size_t i = 0; i < len; i++) putChar(data[i]);
  portEXIT_CRITICAL(&mux);
}

void setMode(Mode m) { mode = m; }

void clear() {
  portENTER_CRITICAL(&mux);
  clearBuf();
  portEXIT_CRITICAL(&mux);
}

void setMenu(const char *title, const char *const *items, int count, int sel) {
  menuTitle = title ? title : "";
  menuItems = items;
  menuCount = count;
  menuSel   = sel;
}

namespace {
// MENU горим — гарчиг + мөрүүд, сонгогдсонд '>' заагч.
void renderMenu() {
  u8g2.drawStr(0, 5, menuTitle);
  for (int i = 0; i < menuCount && i < ROWS - 1; i++) {
    char row[COLS + 1];
    snprintf(row, sizeof(row), "%c %.*s", (i == menuSel ? '>' : ' '),
             COLS - 2, menuItems[i] ? menuItems[i] : "");
    u8g2.drawStr(0, 5 + (i + 1) * 6, row);
  }
}

// TERMINAL горим — лог буфер (богино локоор хуулж аваад зурна).
void renderTerminal() {
  char local[ROWS][COLS + 1];
  portENTER_CRITICAL(&mux);
  memcpy(local, lines, sizeof(local));
  portEXIT_CRITICAL(&mux);
  for (int i = 0; i < ROWS; i++) {
    u8g2.drawStr(0, 5 + i * 6, local[i]);
  }
}
} // namespace

void render() {
  if (!ok) return;

  u8g2.clearBuffer();
  u8g2.setDrawColor(1);                    // цагаан пиксел (хар дэвсгэр дээр)
  u8g2.setFont(u8g2_font_4x6_tr);         // хамгийн жижиг уншигдахуйц фонт
  if (mode == MENU) renderMenu();
  else              renderTerminal();
  u8g2.sendBuffer();
}

} // namespace disp
