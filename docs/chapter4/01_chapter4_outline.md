# Chapter 4 — Results and Discussion: Outline

> Automated Poultry Cage Management System (ESP32-S3)
> Cross-references: figure ideas in [02_figures_and_charts.md](02_figures_and_charts.md),
> table templates in [03_tables.md](03_tables.md),
> data collection methods in [04_data_collection_guide.md](04_data_collection_guide.md).

## 4.1 Introduction
One paragraph: this chapter presents the results of implementing and testing the
automated poultry cage system — hardware build, firmware, dashboard, functional
tests, and performance evaluation — followed by a discussion of findings against
the project objectives.

## 4.2 System Implementation Results
### 4.2.1 Hardware Implementation
- Photos: assembled cage, gantry feed cart, egg belts, waste conveyor, control box
- Final pin-assignment table (from `firmware/src/config.h`)
- Bill of materials with costs

### 4.2.2 Firmware Implementation
- Non-blocking state machines per subsystem (feed / egg / waste)
- Feed cycle: distribute (10 s) → pause (1 s) → reverse home (10 s)
- Interrupt-driven egg counting with 200 ms debounce
- Scheduler: twice-daily per subsystem, 10-minute anti-retrigger grace period
- State-machine / flow diagrams

### 4.2.3 Web Dashboard Implementation
- Screenshots: live egg counts, manual controls, schedule editor, heartbeat
  panel, MQTT event log
- Served from ESP32 SPIFFS at http://poultry.local (no internet required)

### 4.2.4 Communication Architecture
- ESP32 as self-hosted MQTT broker (PicoMQTT): TCP 1883 + WebSocket 81
- MQTT topic structure table
- Offline queue (flash, 50-event capacity) with replay on reconnect

## 4.3 Functional Testing Results
(One subsection per subsystem; each gets a test-case table: test, expected,
observed, remark.)
### 4.3.1 Feeding System
### 4.3.2 Egg Collection and Counting
### 4.3.3 Waste Management
### 4.3.4 Environmental Monitoring (DHT22 + 40 °C alert)
### 4.3.5 Scheduling, RTC and Persistence (reboot survival, browser clock sync)

## 4.4 Performance Evaluation
### 4.4.1 Egg Counting Accuracy
### 4.4.2 Cycle Timing Repeatability
### 4.4.3 System Stability (heartbeat: uptime, free heap, WiFi RSSI)
### 4.4.4 Offline Resilience (WiFi-outage queue & replay)
### 4.4.5 Response Time (dashboard load, command-to-action latency)
### 4.4.6 Power Consumption and Cost Analysis (optional but impressive)

## 4.5 Discussion of Findings
- Objective-by-objective: was each met? Cite the figures/tables as evidence
- Comparison with manual operation (labour time, consistency)
- Comparison with related/commercial systems (cost, offline capability)
- Limitations: open-loop fixed-duration cycles, no MQTT authentication,
  single-WiFi dependency, sensor placement sensitivity

## 4.6 Summary
Closing paragraph bridging to Chapter 5 (Conclusion and Recommendations).

---
**Writing tips**
- Every figure and table must be numbered, captioned, and referenced in the text
  ("As shown in Figure 4.2, …") — never dropped in unexplained.
- Section 4.4 is where examiners look for measured numbers; prioritise egg-count
  accuracy and the environmental log.
- Report failures honestly and explain the fix — it reads as rigour, not weakness.
