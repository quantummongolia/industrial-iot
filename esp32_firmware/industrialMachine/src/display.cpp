/*
 * display.cpp — SSD1306 128×64 I2C OLED terminal (U8g2).
 *
 *  I2C холболт (ESP32-S3 N8R2):
 *    OLED SCL → GPIO 10
 *    OLED SDA → GPIO 11
 *    OLED VCC → 3V3, GND → GND
 *  ⚠️ USB host нь GPIO19/20 (USB D-/D+)-г эзэлдэг тул OLED-г 19/20 дээр тавьж
 *     БОЛОХГҮЙ. I2C-г өөр хос пинд (одоо 10/11) холбоно. Пин солих бол доош.
 *
 *  Дэлгэц: 0.92" SSD1306, I2C addr 0x78 (8-bit бичих) = 0x3C (7-bit).
 */
#include "display.h"
#include <U8g2lib.h>
#include <Wire.h>

namespace disp {
namespace {

constexpr uint8_t DISP_SDA   = 11;
constexpr uint8_t DISP_SCL   = 10;
constexpr uint8_t DISP_ADDR7 = 0x3C;   // 7-bit (Wire ping)
constexpr uint8_t DISP_ADDR8 = 0x78;   // 8-bit (U8g2 setI2CAddress)

// Hardware-I2C, full-frame буфер. Wire-ийн SDA/SCL-ийг begin()-д тохируулна.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

bool      ok = false;
bool      connected = false;
char      statusLine[26] = "";

// Terminal буфер — доод мөр рүү бичээд дүүрэхэд дээш гүйлгэнэ.
// 128×64, 5x7 font (мөрийн өндөр 8px) → 8 мөр × ~25 тэмдэгт.
constexpr int ROWS = 8;
constexpr int COLS = 25;
char lines[ROWS][COLS + 1];
int  curCol = 0;

void clearBuf() {
  for (int i = 0; i < ROWS; i++) lines[i][0] = '\0';
  curCol = 0;
}

void scrollUp() {
  for (int i = 0; i < ROWS - 1; i++) strcpy(lines[i], lines[i + 1]);
  lines[ROWS - 1][0] = '\0';
  curCol = 0;
}

void putChar(char c) {
  if (c == '\n') { scrollUp(); return; }
  if (c == '\r') return;
  if (c == '\t') c = ' ';
  if (c < 32 || c > 126) return;          // зөвхөн хэвлэгдэх ASCII
  if (curCol >= COLS) scrollUp();         // мөр дүүрвэл шинэ мөр
  lines[ROWS - 1][curCol++] = c;
  lines[ROWS - 1][curCol] = '\0';
}

} // namespace

bool begin() {
  Wire.begin(DISP_SDA, DISP_SCL);
  Wire.setClock(400000);

  // Дэлгэц байгаа эсэхийг I2C ping-ээр шалгана — олдохгүй бол non-fatal.
  Wire.beginTransmission(DISP_ADDR7);
  if (Wire.endTransmission() != 0) {
    ok = false;
    return false;
  }

  u8g2.setI2CAddress(DISP_ADDR8);         // U8g2 8-bit хаяг хүлээнэ (0x78)
  u8g2.begin();
  u8g2.setFontMode(1);
  ok = true;
  clearBuf();

  // Splash
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x13B_tr);
  u8g2.drawStr(8, 28, "industrial");
  u8g2.drawStr(28, 44, "Machine");
  u8g2.sendBuffer();
  return true;
}

void feed(const uint8_t* data, size_t len) {
  if (!ok || !data) return;
  for (size_t i = 0; i < len; i++) putChar((char)data[i]);
}

void setConnected(bool c) { connected = c; }

void setStatus(const char* line) {
  if (!line) { statusLine[0] = '\0'; return; }
  strncpy(statusLine, line, sizeof(statusLine) - 1);
  statusLine[sizeof(statusLine) - 1] = '\0';
}

void render() {
  if (!ok) return;
  u8g2.clearBuffer();

  if (!connected) {
    // Залгаагүй дэлгэц — төвд "NOT CONNECTED", доор статус (хувилбар/IP).
    u8g2.setFont(u8g2_font_7x13B_tr);
    const char* msg = "NOT CONNECTED";
    int w = u8g2.getStrWidth(msg);
    u8g2.drawStr((128 - w) / 2, 34, msg);
    if (statusLine[0]) {
      u8g2.setFont(u8g2_font_5x7_tf);
      int sw = u8g2.getStrWidth(statusLine);
      u8g2.drawStr((128 - sw) / 2, 52, statusLine);
    }
  } else {
    // Монитор горим — terminal мөрүүд (5x7 font, 8 мөр).
    u8g2.setFont(u8g2_font_5x7_tf);
    for (int i = 0; i < ROWS; i++) {
      u8g2.drawStr(0, 7 + i * 8, lines[i]);
    }
  }

  u8g2.sendBuffer();
}

} // namespace disp
