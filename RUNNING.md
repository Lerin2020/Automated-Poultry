# Running the Automated Poultry System

End-to-end guide: from a fresh clone of this repository to a live coop dashboard.

## How the system fits together

Everything runs **on the ESP32-S3** — there is no PC server and no cloud:

```
┌─────────────────────────── ESP32-S3 ───────────────────────────┐
│  Firmware (PlatformIO/Arduino)                                 │
│   • PicoMQTT broker     → port 1883 (TCP) + port 81 (WebSocket)│
│   • HTTP server         → port 80, serves dashboard from SPIFFS│
│   • State machines      → feed gantry, egg belts, waste relay  │
│   • DS3231 RTC + DHT22  → schedules + temp/humidity            │
└────────────────────────────────────────────────────────────────┘
            ▲ WiFi (mDNS: http://poultry.local)
            │
   Any phone/laptop browser → loads dashboard → connects back
   to the same ESP32 over MQTT-over-WebSocket (port 81)
```

The React dashboard is built on your PC, then uploaded into the ESP32's
SPIFFS flash. After that, the ESP32 is fully self-contained.

## Prerequisites

| Tool | Used for | Install |
|---|---|---|
| Node.js 18+ & npm | building the dashboard | https://nodejs.org |
| PlatformIO Core (or the VS Code extension) | building/flashing firmware | https://platformio.org/install |
| Git | version control | https://git-scm.com |
| USB driver for the ESP32-S3 devkit | flashing | usually automatic on Windows 11 |

Hardware: ESP32-S3 DevKitC-1, DS3231 RTC, DHT22, BTS7960 driver, relays and
sensors wired per `system_assembly_guide.md` and the pin map in
[firmware/src/config.h](firmware/src/config.h).

## 1. First-time setup

```powershell
git clone <your-repo-url> "Automated Poultry"
cd "Automated Poultry"

# WiFi credentials (secrets.h is gitignored on purpose)
Copy-Item firmware\src\secrets.example.h firmware\src\secrets.h
# Edit firmware\src\secrets.h and set WIFI_SSID / WIFI_PASS

# Dashboard dependencies
cd dashboard
npm install
cd ..
```

> The ESP32 only supports **2.4 GHz WiFi** — make sure the SSID in
> `secrets.h` is a 2.4 GHz network.

## 2. Build & flash the firmware

Plug the ESP32-S3 in via USB, then:

```powershell
cd firmware
pio run -t upload        # compile + flash firmware
pio device monitor       # watch serial output at 115200 baud
```

On boot you should see: SPIFFS mounted, config loaded, WiFi connected with
an IP address, `[mDNS] Reachable at http://poultry.local`, and
`[MQTT] PicoMQTT Broker started`.

If `pio` isn't on your PATH, use the full path:
`& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload`

## 3. Build & deploy the dashboard to the ESP32

From the **project root**:

```powershell
.\deploy_dashboard.ps1     # builds the React app, gzips it into firmware\data
cd firmware
pio run -t uploadfs        # writes firmware\data into the ESP32's SPIFFS
```

`uploadfs` uses the same USB connection as firmware flashing. Re-run these
two steps any time you change dashboard code.

## 4. Use it

1. Connect your phone/laptop to the **same WiFi network** as the ESP32.
2. Open **http://poultry.local** — the dashboard loads from the ESP32 and
   auto-connects to its MQTT broker. The header badge should read
   **ESP32 Online**.
3. If `poultry.local` doesn't resolve (some Android versions don't do mDNS):
   open `http://<ESP32-IP>` instead, and set the same IP via the link (🔗)
   icon in the dashboard header. The IP is printed on the serial monitor and
   shown in the Admin tab heartbeat.

What you can do from the dashboard:
- **Controls tab** — force-start/stop feeding, egg collection, and waste
  flush; live egg counts; temp/humidity strip; next scheduled run times.
- **Admin tab** — change schedule hours and cycle durations (persisted to
  the ESP32's flash), set the egg-yield alert threshold, view the heartbeat
  (heap, RSSI, RTC clock, queue size), and the live MQTT event log.
- The RTC is **auto-synced to your browser's local time** every time the
  dashboard connects — no manual clock setting needed.

Automatic behavior (no dashboard required once configured):
- Feed, egg, and waste cycles run at their scheduled hours (defaults:
  feed 7:00/17:00, eggs 8:00/20:00, waste 6:00/18:00).
- A heartbeat publishes every 30 s; a danger alert fires at ≥ 40 °C.
- Egg counters reset at midnight; egg data/alerts generated while WiFi is
  down are queued in flash and replayed on reconnect.

## 5. Day-to-day development

**Dashboard with hot reload** (no ESP32 flashing per change):

```powershell
cd dashboard
npm run dev
```

Open the Vite URL it prints, click the 🔗 icon, and point the broker host at
your ESP32's IP (or `poultry.local`). The dev server talks to the real
hardware over WebSocket. When you're happy, deploy with step 3.

**Firmware changes:** edit, then `pio run -t upload` again. Schedules and
timings saved from the dashboard live in SPIFFS (`/config.json`) and
survive reflashes (firmware upload doesn't erase the filesystem; `uploadfs`
rewrites it, but the deploy script preserves `config.json` in `firmware\data`).

## Troubleshooting

| Symptom | Fix |
|---|---|
| Dashboard says "Connecting..." forever | Phone and ESP32 must be on the same network; check the broker host via the 🔗 icon; verify port 81 isn't blocked. |
| `poultry.local` not found | Use the raw IP (see serial monitor); Android < 12 often lacks mDNS. |
| "Hardware Offline" badge | Broker is reachable but no heartbeat in a while — check serial monitor for crashes; heartbeat is every 30 s. |
| Temp shows `--` | DHT22 read failing — check wiring on GPIO 6 and the pull-up resistor. |
| `Couldn't find RTC` on serial | DS3231 wiring (SDA 8 / SCL 9) or power issue. |
| WiFi won't connect | 2.4 GHz only; credentials in `firmware/src/secrets.h`; it retries every 20 s forever. |
| `pio` not recognized | Use the full path shown in step 2, or add `%USERPROFILE%\.platformio\penv\Scripts` to PATH. |
| Upload fails / port busy | Close the serial monitor first; only one program can hold the COM port. |
