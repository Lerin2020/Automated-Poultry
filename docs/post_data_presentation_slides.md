# Post-Data Presentation — Slide Outline
## Automated Poultry Cage System (ESP32-S3)

---

### Slide 1 — Title
- Project title: **Design and Implementation of an Automated Poultry Cage Management System**
- Your name, registration number, supervisor, department, date
- One hero photo of the assembled cage/prototype

### Slide 2 — Recap: Problem Statement
- Manual feeding, egg collection and waste removal are labour-intensive, inconsistent, and error-prone
- Small-scale farmers lack affordable automation (commercial systems are expensive and cloud-dependent)
- Need: a low-cost, offline-capable, self-contained automation system

### Slide 3 — Objectives (and status ✓)
- ✓ Automate feed distribution on a schedule
- ✓ Automate egg collection with real-time egg counting
- ✓ Automate waste removal
- ✓ Monitor cage environment (temperature/humidity) with alerts
- ✓ Provide local web dashboard for monitoring & control — no internet/cloud required

### Slide 4 — System Overview (block diagram)
- ESP32-S3 at the centre: runs MQTT broker (PicoMQTT), HTTP server, and all control logic
- Subsystems: gantry feed cart + auger, 2× egg belts + proximity sensors, waste conveyor, DHT22, DS3231 RTC
- User devices connect over WiFi → `http://poultry.local` (mDNS), dashboard talks MQTT over WebSocket
- Key point: **the ESP32 is both the controller and the server** — no PC, no cloud

### Slide 5 — Hardware Implementation
- ESP32-S3 DevKitC-1 microcontroller
- BTS7960 H-bridge → gantry feed cart (forward/reverse)
- 4× relays → auger, 2 egg belts, waste conveyor
- 2× E18-D80NK NPN proximity sensors → egg counting (one per layer)
- DHT22 (temp/humidity), DS3231 RTC (timekeeping through power loss)
- Photo of wiring / control box here

### Slide 6 — Software Implementation
- Firmware: Arduino/PlatformIO, **non-blocking state machines** per subsystem
- Feeding cycle: distribute (auger + gantry forward, 10 s) → pause (1 s) → reverse home (10 s)
- Egg counting: interrupt-driven with 200 ms debounce; live counts published every 1 s
- Scheduling: each subsystem runs twice daily at user-set hours; 10-min anti-retrigger grace period
- Dashboard: React + Vite, served from ESP32 flash (SPIFFS), gzipped

### Slide 7 — Key Design Features (what makes it robust)
- **Offline resilience**: egg data & alerts queued to flash during WiFi outage, replayed on reconnect (queue up to 50 events)
- **Persistence**: schedules and cycle durations saved to SPIFFS, survive reboot
- **RTC auto-sync**: DS3231 syncs to the browser clock — no NTP/internet needed
- **Alerts**: high temperature (≥ 40 °C) and daily egg-yield threshold

### Slide 8 — Dashboard Demo
- Screenshots: live egg counts, manual start/stop, schedule editing, heartbeat panel, event log
- (If presenting live: open `http://poultry.local`, trigger an egg-collection cycle)

### Slide 9 — Testing & Results: Functional Tests
- Table: subsystem | test performed | expected | observed | pass/fail
  - Feed cycle timing, gantry return-to-home, auger relay
  - Egg count accuracy (eggs passed vs. counted)
  - Waste cycle duration
  - Schedule firing at set hours, no double-trigger
  - DHT22 readings vs. reference thermometer

### Slide 10 — Testing & Results: Data Collected
- Egg-count accuracy: trials vs. % accuracy (debounce effectiveness)
- Cycle repeatability: measured vs. configured durations
- Temperature/humidity log over the data-collection period
- Network: dashboard load time, MQTT latency, offline-queue replay verification
- Heartbeat data: free heap, WiFi RSSI, uptime (system stability)

### Slide 11 — Discussion of Results
- Did each objective get met? Reference results from slides 9–10
- Accuracy/limitations: sensor placement sensitivity, fixed-duration (open-loop) cycles, single-network dependency
- Comparison with manual operation: time saved per day, consistency

### Slide 12 — Challenges & Solutions
- e.g. egg double-counting → interrupt debounce (200 ms)
- Power loss losing schedules → SPIFFS persistence + RTC battery backup
- No internet on site → ESP32 self-hosts broker + dashboard
- WiFi dropouts → offline queue with replay

### Slide 13 — Conclusion
- A fully self-contained, low-cost poultry automation system was designed, built and validated
- All objectives achieved; system runs autonomously on schedules with remote monitoring

### Slide 14 — Recommendations / Future Work
- Closed-loop control (limit switches for gantry, feed-level sensing)
- MQTT authentication for shared networks
- Camera-based egg/health monitoring; historical data logging & analytics
- Battery/solar backup; scaling to multiple cages

### Slide 15 — Q&A / Acknowledgements
