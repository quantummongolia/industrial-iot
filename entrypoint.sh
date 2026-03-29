#!/bin/bash
# ============================================================
#  ESP32 PlatformIO Compile & Upload скрипт
# ============================================================

set -e

case "${1:-compile}" in
  compile)
    echo "========= Compile хийж байна... ========="
    pio run
    echo ""
    echo "========= Compile амжилттай! ========="
    ls -lh .pio/build/esp32dev/firmware.bin
    ;;

  upload)
    PORT="${2:-$(ls /dev/ttyUSB* /dev/cu.usbserial* 2>/dev/null | head -1)}"
    if [ -z "$PORT" ]; then
      echo "АЛДАА: USB порт олдсонгүй!"
      echo "Хэрэглэх: docker run --rm --device=/dev/ttyUSB0 flowmeter-iot upload [PORT]"
      exit 1
    fi
    echo "========= Compile & Upload хийж байна... ========="
    echo "Порт: $PORT"
    pio run -t upload --upload-port "$PORT"
    echo ""
    echo "========= Upload амжилттай! ========="
    ;;

  monitor)
    PORT="${2:-$(ls /dev/ttyUSB* /dev/cu.usbserial* 2>/dev/null | head -1)}"
    if [ -z "$PORT" ]; then
      echo "АЛДАА: USB порт олдсонгүй!"
      exit 1
    fi
    echo "========= Serial Monitor (115200 baud) ========="
    pio device monitor --port "$PORT" --baud 115200
    ;;

  *)
    echo "Хэрэглэх заавар:"
    echo "  compile  — Зөвхөн compile (анхдагч)"
    echo "  upload   — Compile + Upload ESP32 руу"
    echo "  monitor  — Serial Monitor нээх"
    ;;
esac
