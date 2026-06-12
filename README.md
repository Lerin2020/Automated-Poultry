# Automated Poultry System 🐔

A self-contained poultry cage automation system running entirely on an
**ESP32-S3** — automated feeding, egg collection with live counting, waste
management, and environment monitoring, controlled from a web dashboard
served by the ESP32 itself. No PC server, no cloud, no internet required.

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![Firmware](https://img.shields.io/badge/firmware-PlatformIO%20%2F%20Arduino-orange)
![Dashboard](https://img.shields.io/badge/dashboard-React%20%2B%20Vite-61dafb)

## Features

- **Feeding system** — gantry cart (BTS7960-driven) distributes feed while an
  auger relay runs; distribute → pause → return cycle, fully non-blocking.
- **Egg collection** — two belt motors with NPN proximity sensors counting
  eggs per layer in real time (interrupt-driven, debounced), with a
  configurable daily yield alert.
- **Waste management** — timed conveyor flush cycle.
- **Scheduling** — each subsystem runs twice daily at hours you set from the
  dashboard; persisted to flash, backed by a DS3231 RTC that auto-syncs to
  your browser's clock.
- **Environment monitoring** — DHT22 temp/humidity with a high-temperature
  danger alert (≥ 40 °C).
- **Web dashboard** — React app served from the ESP32's SPIFFS at
  `http://poultry.local`; manual start/stop, live egg counts, schedule and
  cycle-duration editing, system heartbeat, and a live MQTT event log.
- **Offline resilience** — egg data and alerts generated while WiFi is down
  are queued in flash and replayed on reconnect.

## Architecture

```
┌─────────────────────────── ESP32-S3 ───────────────────────────┐
│  • PicoMQTT broker      → port 1883 (TCP) + port 81 (WebSocket)│
│  • HTTP server          → port 80, serves dashboard from SPIFFS│
│  • Non-blocking state machines → feed / eggs / waste           │
│  • DS3231 RTC + DHT22   → schedules + environment              │
└────────────────────────────────────────────────────────────────┘
            ▲ WiFi (mDNS: http://poultry.local)
            │
   Phone/laptop browser → loads dashboard → MQTT over WebSocket
```

The ESP32 **is** the MQTT broker (PicoMQTT) — the dashboard connects back to
the same device it was served from.

## Repository layout

| Path | Contents |
|---|---|
| [firmware/](firmware/) | PlatformIO project (Arduino framework, ESP32-S3 DevKitC-1) |
| [firmware/src/config.h](firmware/src/config.h) | Pin map, MQTT topics, defaults |
| [firmware/src/hardware.h](firmware/src/hardware.h) | Motor state machines, sensors |
| [firmware/src/storage.h](firmware/src/storage.h) | SPIFFS config persistence + offline queue |
| [dashboard/](dashboard/) | React + Vite dashboard |
| [RUNNING.md](RUNNING.md) | **Full setup & operations guide** |
| [system_assembly_guide.md](system_assembly_guide.md) | Mechanical/wiring assembly |
| [docs/original_implementation_plan.md](docs/original_implementation_plan.md) | Original design document (historical) |
| [deploy_dashboard.ps1](deploy_dashboard.ps1) | Builds + gzips dashboard into `firmware/data` |

## Quick start

```powershell
# 1. WiFi credentials (gitignored)
Copy-Item firmware\src\secrets.example.h firmware\src\secrets.h
#    → edit secrets.h with your 2.4 GHz SSID/password

# 2. Flash firmware
cd firmware
pio run -t upload

# 3. Build & upload the dashboard to SPIFFS
cd ..
cd dashboard; npm install; cd ..
.\deploy_dashboard.ps1
cd firmware
pio run -t uploadfs
```

Then open **http://poultry.local** from any device on the same WiFi.
See [RUNNING.md](RUNNING.md) for the full guide, development workflow, and
troubleshooting.

## MQTT topics

| Topic | Direction | Purpose |
|---|---|---|
| `poultry/feed/cmd` · `egg/cmd` · `waste/cmd` | dashboard → ESP | `{"action":"start"\|"stop"}` |
| `poultry/feed/status` · `egg/status` · `waste/status` | ESP → dashboard | cycle state |
| `poultry/egg/data` | ESP → dashboard | live + final egg counts |
| `poultry/system/status` | ESP → dashboard | 30 s heartbeat (heap, RSSI, temp, RTC…) |
| `poultry/alerts` | ESP → dashboard | temperature / egg-threshold alerts |
| `poultry/config/cmd` / `config/status` | both | schedule, timings, RTC sync |

## Hardware

ESP32-S3 DevKitC-1 · DS3231 RTC · DHT22 · BTS7960 motor driver (gantry) ·
relays (auger, 2× egg belts, waste conveyor) · 2× E18-D80NK NPN proximity
sensors. Full pin map in [firmware/src/config.h](firmware/src/config.h);
wiring in [system_assembly_guide.md](system_assembly_guide.md).

## Security note

The broker has no authentication — anyone on your WiFi can send commands.
Fine for a private home network; if the network is shared, see the MQTT
auth discussion in the project issues/history before deploying.
