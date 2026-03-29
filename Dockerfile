# ============================================================
#  ESP32 Урсгал Хэмжигч — Compile & Upload
# ============================================================
#
#  Image бүтээх:
#    docker build -t flowmeter-iot .
#
#  Compile хийх:
#    docker run --rm flowmeter-iot
#
#  ESP32 руу upload:
#    docker run --rm --device=/dev/ttyUSB0 flowmeter-iot upload
#
# ============================================================

FROM python:3.11-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl ca-certificates && rm -rf /var/lib/apt/lists/*

# Arduino CLI
RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh -s -- --dest /usr/local/bin

# ESP32 core + сангууд
RUN arduino-cli config init \
    && arduino-cli config add board_manager.additional_urls \
       https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json \
    && arduino-cli core update-index \
    && arduino-cli core install esp32:esp32 \
    && arduino-cli lib install "ModbusMaster" \
    && arduino-cli lib install "Firebase Arduino Client Library for ESP8266 and ESP32"

WORKDIR /app
COPY esp32_firmware/main.cpp ./Flowmeter-iot/Flowmeter-iot.ino
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
CMD ["compile"]
