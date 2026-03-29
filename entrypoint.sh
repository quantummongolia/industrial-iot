#!/bin/bash
# ============================================================
#  ESP32 Compile & Upload скрипт
# ============================================================

set -e

SKETCH="/app/Flowmeter-iot"
FQBN="esp32:esp32:esp32"
BUILD_DIR="/app/build"

case "${1:-compile}" in
  compile)
    echo "========= Compile хийж байна... ========="
    arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR" "$SKETCH"
    echo ""
    echo "========= Compile амжилттай! ========="
    echo "Firmware: $BUILD_DIR/Flowmeter-iot.ino.bin"
    ls -lh "$BUILD_DIR/Flowmeter-iot.ino.bin"
    ;;

  upload)
    # USB порт автоматаар олох
    PORT="${2:-$(ls /dev/ttyUSB* /dev/cu.usbserial* 2>/dev/null | head -1)}"
    if [ -z "$PORT" ]; then
      echo "АЛДАА: USB порт олдсонгүй!"
      echo "Хэрэглэх: docker run --rm --device=/dev/ttyUSB0 flowmeter-iot upload [PORT]"
      exit 1
    fi
    echo "========= Compile & Upload хийж байна... ========="
    echo "Порт: $PORT"
    arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR" "$SKETCH"
    arduino-cli upload --fqbn "$FQBN" --port "$PORT" --input-dir "$BUILD_DIR"
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
    arduino-cli monitor --port "$PORT" --config baudrate=115200
    ;;

  *)
    echo "Хэрэглэх заавар:"
    echo "  compile  — Зөвхөн compile (анхдагч)"
    echo "  upload   — Compile + Upload ESP32 руу"
    echo "  monitor  — Serial Monitor нээх"
    ;;
esac
