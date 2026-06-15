# Chapter 4 — Table Templates

Copy these into your document and fill in real values. Number them Table 4.1,
4.2, … in the order they appear.

## Table 4.1 — Pin assignment (section 4.2.1)
(From `firmware/src/config.h`.)

| GPIO | Function | Component |
|---|---|---|
| 14 | Gantry forward PWM | BTS7960 LPWM |
| 16 | Gantry reverse PWM | BTS7960 RPWM |
| 15 | Gantry enable | BTS7960 EN |
| 4 | Auger motor | Relay |
| 12 | Egg belt — Layer 1 | Relay |
| 13 | Egg belt — Layer 2 | Relay |
| 47 | Egg sensor — Layer 1 | E18-D80NK NPN |
| 5 | Egg sensor — Layer 2 | E18-D80NK NPN |
| 21 | Waste conveyor | Relay |
| 6 | Temp/humidity data | DHT22 |
| 8 / 9 | I²C SDA / SCL | DS3231 RTC |

## Table 4.2 — Bill of materials and cost (4.2.1)

| # | Component | Qty | Unit cost | Subtotal |
|---|---|---|---|---|
| 1 | ESP32-S3 DevKitC-1 | 1 | | |
| 2 | BTS7960 motor driver | 1 | | |
| 3 | Relay module | 4 ch | | |
| 4 | E18-D80NK proximity sensor | 2 | | |
| 5 | DHT22 sensor | 1 | | |
| 6 | DS3231 RTC module | 1 | | |
| 7 | DC motors (gantry, auger, belts ×2, conveyor) | 5 | | |
| 8 | Power supply | | | |
| 9 | Frame, belts, mechanical parts | | | |
| | **Total** | | | |

## Table 4.3 — MQTT topic structure (4.2.4)

| Topic | Direction | Payload / purpose |
|---|---|---|
| poultry/feed/cmd, egg/cmd, waste/cmd | dashboard → ESP | {"action":"start"\|"stop"} |
| poultry/feed/status, egg/status, waste/status | ESP → dashboard | cycle state |
| poultry/egg/data | ESP → dashboard | live + final egg counts |
| poultry/system/status | ESP → dashboard | 30 s heartbeat (heap, RSSI, temp, RTC) |
| poultry/alerts | ESP → dashboard | temperature / egg-threshold alerts |
| poultry/config/cmd, config/status | both | schedules, durations, RTC sync |

## Table 4.4 — Functional test cases (one per subsystem, 4.3.1–4.3.5)

| # | Test case | Procedure | Expected result | Observed result | Remark |
|---|---|---|---|---|---|
| 1 | Manual feed start | Press Start Feed on dashboard | Auger + gantry run 10 s, pause 1 s, reverse 10 s | | Pass/Fail |
| 2 | Mid-cycle stop | Press Stop during distribute | All feed motors stop immediately | | |
| 3 | … | | | | |

## Table 4.5 — Egg counting accuracy trials (4.4.1)

| Trial | Eggs passed | Counted L1 | Counted L2 | Accuracy L1 (%) | Accuracy L2 (%) |
|---|---|---|---|---|---|
| 1 | | | | | |
| … | | | | | |
| **Mean** | | | | | |

## Table 4.6 — Cycle timing repeatability (4.4.2)

| Cycle phase | Configured (s) | Measured mean (s) | Std dev (s) | Error (%) |
|---|---|---|---|---|
| Feed distribute | 10.0 | | | |
| Feed pause | 1.0 | | | |
| Feed reverse | 10.0 | | | |
| Egg collection | 10.0 | | | |
| Waste cycle | 8.0 | | | |

## Table 4.7 — Schedule firing log (4.3.5)

| Date | Subsystem | Scheduled hour | Actual start time | Deviation | Re-trigger blocked? |
|---|---|---|---|---|---|

## Table 4.8 — Offline resilience tests (4.4.4)

| Test | Outage duration | Events generated | Events queued | Events replayed | Data loss |
|---|---|---|---|---|---|

## Table 4.9 — DHT22 validation (4.3.4)

| Reading # | DHT22 temp (°C) | Reference temp (°C) | Error (°C) | DHT22 RH (%) | Reference RH (%) |
|---|---|---|---|---|---|

## Table 4.10 — Power consumption (4.4.6)

| State | Current (A) | Voltage (V) | Power (W) | Duration/day | Energy (Wh/day) |
|---|---|---|---|---|---|
| Idle (ESP32 + dashboard) | | | | 24 h | |
| Feeding cycle | | | | 2 × 21 s | |
| Egg collection | | | | 2 × 10 s | |
| Waste cycle | | | | 2 × 8 s | |

## Table 4.11 — Comparison with existing systems (4.5)

| Feature | This system | Commercial system A | Related work B |
|---|---|---|---|
| Cost | | | |
| Works offline (no cloud) | ✓ | | |
| Egg counting per layer | ✓ | | |
| Scheduling | ✓ | | |
| Environment alerts | ✓ | | |
| Web dashboard (self-hosted) | ✓ | | |

## Table 4.12 — Objectives achievement summary (4.5)

| Objective (Chapter 1) | Evidence (figure/table) | Status |
|---|---|---|
| Automated feeding | Fig 4.3, Table 4.4 | Achieved |
| Egg collection + counting | Figs 4.1–4.2, Table 4.5 | Achieved |
| Waste management | Table 4.4 | Achieved |
| Environment monitoring + alerts | Fig 4.4, Table 4.9 | Achieved |
| Local web dashboard control | Screenshots, Fig 4.7 | Achieved |
