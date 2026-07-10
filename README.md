# Automated Poultry System

ESP32-S3-based cage automation — feeding, egg collection, waste management, and environment monitoring controlled from a self-hosted web dashboard. No cloud, no PC server required.

## Stack

- **Firmware** — PlatformIO / Arduino framework, ESP32-S3 DevKitC-1
- **Dashboard** — React + Vite, served directly from the ESP32's LittleFS
- **Broker** — PicoMQTT running on-device (TCP port 1883, WebSocket port 81)

## Hardware

| Component | Purpose |
|---|---|
| ESP32-S3 DevKitC-1 | Main controller |
| DS3231 RTC | Timekeeping (I2C SDA=GPIO8, SCL=GPIO9) |
| DHT22 | Temperature & humidity (GPIO6, 10kΩ pull-up) |
| BTS7960 driver | Gantry motor PWM (LPWM=GPIO14, RPWM=GPIO16, EN=GPIO15) |
| 4-ch relay module (active-LOW) | Auger=GPIO4, Egg L1=GPIO12, Egg L2=GPIO13, Waste=GPIO21 |
| 2× E18-D80NK NPN proximity sensors | Egg counters — Layer 1=GPIO47, Layer 2=GPIO5 |

## Setup

```powershell

# Flash firmware
cd firmware
pio run -t upload

# Build dashboard and upload to LittleFS
cd ..
.\deploy_dashboard.ps1
cd firmware
pio run -t uploadfs
```

Open **http://poultry.local** from any device on the same WiFi network.

## Layout

```
firmware/
  src/
    config.h    Pin map, MQTT topics, timing defaults
    hardware.h  Motor state machines, relay control, sensor ISRs
    storage.h   LittleFS persistence, activity log, offline queue
    main.cpp    WiFi, MQTT broker, HTTP server, schedule loop
    secrets.h   WiFi credentials (gitignored)
dashboard/      React + Vite source
deploy_dashboard.ps1  Builds and gzips dashboard into firmware/data
```
