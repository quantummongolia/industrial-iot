# ============================================================
#  ESP32 Урсгал Хэмжигч — PlatformIO Compile & Upload
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

# PlatformIO суулгах
RUN pip install --no-cache-dir platformio

WORKDIR /app

# PlatformIO тохиргоо болон код хуулах
COPY esp32_firmware/platformio.ini ./platformio.ini
COPY esp32_firmware/src/main.cpp   ./src/main.cpp

# Сангууд болон toolchain-г урьдчилж татах (cache-д хадгална)
RUN pio pkg install

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
CMD ["compile"]
