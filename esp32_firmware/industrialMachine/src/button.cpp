/*
 * button.cpp — BOOT товч (GPIO0) олон даралт таних.
 *
 *  Debounce + multi-click цонх. Дарах бүрд тоолно; сүүлийн дарснаас хойш GAP_MS
 *  чимээгүй өнгөрвөл тоогоор нь Action гаргана (1=SINGLE, 2=DOUBLE, 3+=TRIPLE).
 */
#include "button.h"

namespace btn {
namespace {
constexpr uint8_t  PIN        = 0;     // BOOT товч (active-low)
constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t GAP_MS      = 400;  // дараалал дуусгах чимээгүй завсар

int      lastReading = HIGH;
int      stableState = HIGH;
uint32_t lastChange  = 0;
int      pressCount  = 0;
uint32_t lastPress   = 0;
} // namespace

void begin() {
  pinMode(PIN, INPUT_PULLUP);
}

Action poll() {
  uint32_t now = millis();
  int r = digitalRead(PIN);

  // Debounce — төлөв тогтвортой DEBOUNCE_MS байж байж хүлээн зөвшөөрнө.
  if (r != lastReading) {
    lastChange = now;
    lastReading = r;
  }
  if (now - lastChange > DEBOUNCE_MS && r != stableState) {
    stableState = r;
    if (stableState == LOW) {        // дарагдсан (falling edge)
      pressCount++;
      lastPress = now;
    }
  }

  // Дараалал дуусав уу — товч сулласан, сүүлийн дарснаас GAP_MS өнгөрсөн.
  if (pressCount > 0 && stableState == HIGH && now - lastPress > GAP_MS) {
    Action a = (pressCount == 1) ? SINGLE
             : (pressCount == 2) ? DOUBLE
                                 : TRIPLE;
    pressCount = 0;
    return a;
  }
  return NONE;
}

} // namespace btn
