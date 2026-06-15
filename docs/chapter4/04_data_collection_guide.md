# Chapter 4 — Data Collection Guide

How to gather the real measurements for each figure/table, using the system
you already built. Most data comes free from MQTT — log it rather than writing
numbers by hand.

## Set up a data logger first (30 minutes, pays for everything)
The firmware already publishes everything you need. On a laptop on the same
WiFi, log all topics to a file:

```powershell
# Install mosquitto clients (or use MQTT Explorer GUI)
mosquitto_sub -h poultry.local -t "poultry/#" -v | Tee-Object -FilePath mqtt_log.txt
```

Leave it running for your whole data-collection period. The 30-second
heartbeat on `poultry/system/status` alone gives you: temperature, humidity,
free heap, WiFi RSSI, uptime, RTC time → Figures 4.4, 4.6 and Tables 4.7, 4.9.

## Per-figure procedures

**Egg counting accuracy (Figs 4.1, 4.2; Table 4.5)**
Start an egg-collection cycle, place a known number of eggs (or egg-sized
balls) on the belt at realistic spacing, compare the dashboard's final count
to the true number. 5+ trials per layer with varying quantities (10–30).

**Debounce histogram (Fig 11 idea)**
From the MQTT log, take timestamps of `poultry/egg/data` live updates during
a run; compute gaps between consecutive count increments.

**Cycle timing (Fig 4.3; Table 4.6)**
Phone stopwatch (or video) from relay click-on to click-off for each phase.
3–5 repetitions each. The status messages in the log also carry state
transitions you can timestamp.

**Feed uniformity (Fig 16 idea)**
Lay paper segments (e.g. 5 equal zones) along the trough, run one feed cycle,
weigh the feed in each zone with a kitchen scale.

**Temperature/humidity (Fig 4.4; Table 4.9)**
From the heartbeat log, average readings per hour over ≥ 24 h. For validation,
place any reference thermometer beside the DHT22 and record both at 5–10
moments across a day.

**Schedule accuracy (Table 4.7; Fig 21 idea)**
Set schedules a few minutes ahead, watch the log for the cycle-start status
message, compare its timestamp to the scheduled hour. Repeat across 2–3 days
of normal scheduled runs. Also verify the 10-minute grace period blocks an
immediate re-trigger.

**System stability (Fig 4.6)**
Run continuously ≥ 48 h; extract free heap and RSSI from each heartbeat in
the log. Flat heap line = no memory leak (strong result).

**Offline resilience (Fig 4.9; Table 4.8)**
Power off the router for 5/15/30/60 min while generating events (run egg
cycles, trigger a temp alert with a hairdryer near the DHT22 — careful, or
lower the threshold temporarily). Power router back on; count replayed
messages in the dashboard event log vs. events you generated.

**Command latency (Fig 4.7)**
Slow-motion phone video capturing both the screen tap and the relay LED/click;
count frames between them. 10 samples per action.

**Dashboard load time (Fig 25 idea)**
Browser DevTools → Network tab → reload `http://poultry.local`, read total
load time. Repeat on phone (remote debugging or a stopwatch on first paint).

**Power (Table 4.10; Figs 29–30 ideas)**
Multimeter in series on the supply (or a cheap USB/inline power meter) in each
state: idle, feed running, belts running, conveyor running. Multiply by the
daily schedule durations for Wh/day.

**Labour comparison (Fig 4.8)**
Time yourself (or the farmer) doing each task manually for one day; automated
side is just supervision time. Be honest — examiners ask how you got these.

## Practical tips
- Collect for at least 3–7 days so daily-yield and environment charts have a
  real story.
- Record raw data in a spreadsheet as you go (date, time, trial, value); keep
  it as an appendix — examiners sometimes ask for raw data.
- Photograph every test setup; those photos become Chapter 4 figures too.
- After replacing the sample numbers in `charts/generate_charts.py`, re-run it
  once: `python charts/generate_charts.py` — all PNGs regenerate at 300 dpi.
