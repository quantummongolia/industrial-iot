#!/bin/bash
# ============================================================
#  ESP32 PlatformIO Compile & Upload Script
#  Usage: ./entrypoint.sh [compile|upload|monitor] [PORT]
#  Default action: compile
# ============================================================

set -e  # Exit immediately if any command fails

# Switch on the first argument (default to "compile" if none provided)
case "${1:-compile}" in
  compile)
    # Compile the ESP32 firmware using PlatformIO without uploading
    echo "========= Compiling firmware... ========="
    pio run  # Run PlatformIO build process
    echo ""
    echo "========= Compile successful! ========="
    # Display the compiled firmware binary file size
    ls -lh .pio/build/esp32dev/firmware.bin
    ;;

  upload)
    # Auto-detect USB serial port if not provided as second argument
    PORT="${2:-$(ls /dev/ttyUSB* /dev/cu.usbserial* 2>/dev/null | head -1)}"
    if [ -z "$PORT" ]; then
      # Error: no USB serial port found
      echo "ERROR: No USB port found!"
      echo "Usage: docker run --rm --device=/dev/ttyUSB0 flowmeter-iot upload [PORT]"
      exit 1
    fi
    # Compile and upload firmware to ESP32 via detected serial port
    echo "========= Compiling & Uploading firmware... ========="
    echo "Port: $PORT"
    pio run -t upload --upload-port "$PORT"  # PlatformIO compile + upload to target port
    echo ""
    echo "========= Upload successful! ========="
    ;;

  monitor)
    # Open serial monitor to view ESP32 debug output in real-time
    PORT="${2:-$(ls /dev/ttyUSB* /dev/cu.usbserial* 2>/dev/null | head -1)}"
    if [ -z "$PORT" ]; then
      # Error: no USB serial port found
      echo "ERROR: No USB port found!"
      exit 1
    fi
    echo "========= Serial Monitor (115200 baud) ========="
    # Start PlatformIO serial monitor at 115200 baud rate
    pio device monitor --port "$PORT" --baud 115200
    ;;

  *)
    # Display usage help for unknown commands
    echo "Usage:"
    echo "  compile  — Compile only (default)"
    echo "  upload   — Compile + Upload to ESP32"
    echo "  monitor  — Open Serial Monitor"
    ;;
esac
