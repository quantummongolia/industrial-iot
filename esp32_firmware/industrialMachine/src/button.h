/*
 * button.h — BOOT товч (GPIO0)-ийн олон даралт (single/double/triple) таних.
 * ----------------------------------------------------------------------
 *  GPIO0 нь board дээрх BOOT товч (active-low, гадны pull-up-тай).
 *  poll() нь дарааллын ТӨГСГӨЛД (сүүлийн дарснаас хойш чимээгүй завсар өнгөрөхөд)
 *  нэг удаа Action буцаана:
 *    1 дарах        → SINGLE  (дараагийн зүйл рүү)
 *    2 хурдан дарах → DOUBLE  (сонгож орох)
 *    3+ хурдан      → TRIPLE  (гарах/буцах)
 */
#pragma once
#include <Arduino.h>

namespace btn {

enum Action { NONE, SINGLE, DOUBLE, TRIPLE };

void   begin();
Action poll();

} // namespace btn
