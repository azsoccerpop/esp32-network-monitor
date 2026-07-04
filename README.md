# Network Monitor Project

This project is a network monitoring tool built for the ESP32 microcontroller using PlatformIO. It features an OLED display, web interface, and asynchronous ping monitoring of multiple hosts.

---

## Hardware Overview

- **Microcontroller:** ESP32-WROOM-32D
- **Display:** SH1106 OLED (I2C, 128x64 resolution)
- **Network:** WiFi 2.4GHz
- **Filesystem:** LittleFS
- **Web Server:** ESPAsyncWebServer

### OLED Wiring

- SDA → GPIO 21
- SCL → GPIO 22
- VCC → 3.3V
- GND → GND
- I2C Address → 0x3C

---

## Features

- Add and manage multiple hosts to monitor via web interface
- Periodic asynchronous pinging of hosts with status and latency
- OLED display shows network status with scrolling and alert modes
- Adjustable OLED brightness with persistence
- JSON status endpoint with auto-refresh

---

## Project Structure

network-monitor/
│
├── src/
│   └── main.cpp
│
├── include/
│   └── *.h (class headers)
│
├── lib/
│   ├── DisplayManager/
│   ├── HostMonitor/
│   └── WebInterface/
│
├── data/
│   ├── hosts.json
│   ├── settings.json
│   ├── index.html
│   ├── script.js
│   └── style.css
│
└── platformio.ini


---

## Usage

1. Build and upload the project using PlatformIO.
2. Connect the ESP32 to a WiFi network.
3. Access the web interface at the ESP32's IP address.
4. Add hosts to monitor and adjust brightness.
5. View network status on the OLED display.

---

## PlatformIO Configuration

- Board: esp32dev or esp32-wroom-32
- Libraries:
  - U8g2 or SH1106 driver
  - ESPAsyncWebServer
  - AsyncTCP
  - AsyncPing
  - ArduinoJSON
  - LittleFS

---

## Coding Conventions

- Use classes: DisplayManager, HostMonitor, WebInterface
- Non-blocking loops and millis() timers
- AsyncPing callbacks for pinging
- ESPAsyncWebServer for HTTP endpoints
- Modular code structure
- Include all necessary #include statements
- PlatformIO-compatible Arduino code

---

## Notes

- This project is standalone and does not interfere with other ESP32 projects.
- All code must compile without modification.

---

## Contact

For questions or contributions, please open an issue or pull request on the repository.
