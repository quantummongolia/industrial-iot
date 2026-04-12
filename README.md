# Industrial IoT monitoring system

ESP32 + RS485 flowmeter → Firebase Realtime Database → Вэб Dashboard

## Structure

```
Flowmeter-iot/
├── esp32_firmware/       # ESP32 C++ code (Arduino)
│   └── main.cpp
├── web_dashboard/        # Firebase website (HTML, JS, CSS)
│   ├── index.html
│   └── firebase.json
├── .gitignore
├── Dockerfile
├── entrypoint.sh
└── README.md
```

## devices and modules

| Device | Description |
|---------|---------|
| ESP32 DevKit | Main controller |
| MAX485 модуль | RS485 converter |
| Flowmeter | Modbus RTU (Slave ID 1, 9600 8N1) |

### Connection diagram

```
MAX485 RE/DE → GPIO 4
MAX485 RX2   → GPIO 16
MAX485 TX2   → GPIO 17
```

## ESP32 firmware

### Arduino IDE upload

1. Arduino IDE 
2. `esp32_firmware/main.cpp` open
3. Board: **ESP32 Dev Module** select
4. Wi-Fi and Firebase configuration
5. Upload

### Docker compile

```bash
docker build -t flowmeter-iot .
docker run --rm flowmeter-iot
```

## Web Dashboard

1. `web_dashboard/index.html` Firebase configuration
2. Open in browser, or deploy to Firebase Hosting:

```bash
cd web_dashboard
firebase deploy --only hosting
```
