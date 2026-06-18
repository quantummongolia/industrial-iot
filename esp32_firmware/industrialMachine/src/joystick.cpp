/*
 * joystick.cpp — HW-504 joystick → 4 чиглэлийн товч.
 *
 *  ADC1 (12-bit, 0..4095). Гол төв ~2048. Хязгаар руу түлхэхэд 0 эсвэл 4095 рүү
 *  ойртоно. Тодорхой босго (LOW/HIGH)-оор чиглэл тогтооно. Edge-detect: төв рүү
 *  буцсаны дараа л дараагийн event-ийг зөвшөөрнө.
 *
 *  Чиглэл урвуу бол доорх UP/DOWN, LEFT/RIGHT-ийг сольж болно.
 */
#include "joystick.h"

namespace joy {
namespace {
constexpr uint8_t PIN_VRX = 4;   // зүүн/баруун
constexpr uint8_t PIN_VRY = 5;   // дээш/доош
constexpr uint8_t PIN_SW  = 6;   // товч (одоохондоо ашиглахгүй)

constexpr int LOW_TH  = 1000;    // үүнээс бага → нэг тал руу түлхсэн
constexpr int HIGH_TH = 3000;    // үүнээс их  → нөгөө тал руу түлхсэн

bool centered = true;            // сүүлийн уншилтад голдоо байсан эсэх

bool atRest(int x, int y) {
  return x > LOW_TH && x < HIGH_TH && y > LOW_TH && y < HIGH_TH;
}
} // namespace

void begin() {
  analogReadResolution(12);
  pinMode(PIN_SW, INPUT_PULLUP);
}

Dir poll() {
  int x = analogRead(PIN_VRX);
  int y = analogRead(PIN_VRY);

  if (atRest(x, y)) {            // төв рүү буцсан — дараагийн event-д бэлэн
    centered = true;
    return NONE;
  }
  if (!centered) return NONE;    // голдоо буцаагүй хэвээр — давталт хийхгүй

  Dir d = NONE;
  if (y < LOW_TH)       d = UP;
  else if (y > HIGH_TH) d = DOWN;
  else if (x < LOW_TH)  d = LEFT;
  else if (x > HIGH_TH) d = RIGHT;

  if (d != NONE) centered = false;
  return d;
}

bool swPressed() {
  return digitalRead(PIN_SW) == LOW;   // active-low (pullup)
}

} // namespace joy
