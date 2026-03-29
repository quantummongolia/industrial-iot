# Flowmeter IoT

ESP32 + RS485 урсгал хэмжигч → Firebase Realtime Database → Вэб Dashboard

## Бүтэц

```
Flowmeter-iot/
├── esp32_firmware/       # ESP32 C++ код (Arduino)
│   └── main.cpp
├── web_dashboard/        # Firebase вэб хуудас (HTML, JS, CSS)
│   ├── index.html
│   └── firebase.json
├── .gitignore
├── Dockerfile
├── entrypoint.sh
└── README.md
```

## Тоног төхөөрөмж

| Эд анги | Тайлбар |
|---------|---------|
| ESP32 DevKit | Гол хянагч |
| MAX485 модуль | RS485 хөрвүүлэгч |
| Урсгал хэмжигч | Modbus RTU (Slave ID 1, 9600 8N1) |

### Холболтын схем

```
MAX485 RE/DE → GPIO 4
MAX485 RX2   → GPIO 16
MAX485 TX2   → GPIO 17
```

## ESP32 firmware

### Arduino IDE-р upload хийх

1. Arduino IDE нээнэ
2. `esp32_firmware/main.cpp` файлыг нээнэ
3. Board: **ESP32 Dev Module** сонгоно
4. Wi-Fi болон Firebase тохиргоогоо оруулна
5. Upload дарна

### Docker-р compile хийх

```bash
docker build -t flowmeter-iot .
docker run --rm flowmeter-iot
```

## Вэб Dashboard

1. `web_dashboard/index.html` дотор Firebase тохиргоогоо оруулна
2. Хөтөч дээр шууд нээж болно, эсвэл Firebase Hosting руу deploy хийнэ:

```bash
cd web_dashboard
firebase deploy --only hosting
```
