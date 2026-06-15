"""
Chapter 4 figure generator — Automated Poultry Cage System (ESP32-S3)

ALL DATA BELOW IS SAMPLE/PLACEHOLDER DATA.
Replace each block marked  # <-- REPLACE WITH YOUR DATA  with your real
measurements, then re-run:   python generate_charts.py
All figures are saved as 300-dpi PNGs in this folder, ready to insert
into your Chapter 4 document.
"""

import matplotlib.pyplot as plt
import numpy as np

plt.rcParams.update({
    "figure.dpi": 100,
    "savefig.dpi": 300,
    "font.size": 11,
    "axes.grid": True,
    "grid.alpha": 0.3,
})

OUT = __file__.rsplit("\\", 1)[0] if "\\" in __file__ else "."


def save(fig, name):
    path = f"{OUT}/{name}.png"
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    print(f"saved {path}")


# ============================================================
# Figure 4.1 — Egg counting accuracy per trial (both layers)
# ============================================================
trials = [1, 2, 3, 4, 5]                               # <-- REPLACE WITH YOUR DATA
actual_eggs   = [10, 15, 20, 25, 30]                   # eggs physically passed
counted_L1    = [10, 15, 19, 25, 29]                   # sensor count, layer 1
counted_L2    = [10, 14, 20, 24, 30]                   # sensor count, layer 2

x = np.arange(len(trials))
w = 0.27
fig, ax = plt.subplots(figsize=(8, 4.5))
ax.bar(x - w, actual_eggs, w, label="Actual eggs", color="#4d4d4d")
ax.bar(x,     counted_L1,  w, label="Counted — Layer 1", color="#1f77b4")
ax.bar(x + w, counted_L2,  w, label="Counted — Layer 2", color="#ff7f0e")
ax.set_xticks(x, [f"Trial {t}" for t in trials])
ax.set_ylabel("Number of eggs")
ax.set_title("Egg Counting Accuracy: Actual vs. Sensor Count per Trial")
ax.legend()
save(fig, "fig4_1_egg_count_accuracy")

# ============================================================
# Figure 4.2 — Egg counting accuracy percentage per trial
# ============================================================
acc_L1 = [100 * c / a for c, a in zip(counted_L1, actual_eggs)]
acc_L2 = [100 * c / a for c, a in zip(counted_L2, actual_eggs)]

fig, ax = plt.subplots(figsize=(8, 4.5))
ax.plot(trials, acc_L1, "o-", label="Layer 1", color="#1f77b4")
ax.plot(trials, acc_L2, "s-", label="Layer 2", color="#ff7f0e")
ax.axhline(100, color="green", ls="--", lw=1, label="Ideal (100%)")
mean_acc = np.mean(acc_L1 + acc_L2)
ax.axhline(mean_acc, color="red", ls=":", lw=1.5,
           label=f"Mean accuracy ({mean_acc:.1f}%)")
ax.set_xticks(trials, [f"Trial {t}" for t in trials])
ax.set_ylabel("Counting accuracy (%)")
ax.set_ylim(85, 105)
ax.set_title("Egg Counting Accuracy (%) per Trial — 200 ms Debounce")
ax.legend()
save(fig, "fig4_2_egg_accuracy_percent")

# ============================================================
# Figure 4.3 — Cycle timing: configured vs. measured durations
# ============================================================
cycles = ["Feed\ndistribute", "Feed\npause", "Feed\nreverse",
          "Egg\ncollection", "Waste\ncycle"]
configured = [10.0, 1.0, 10.0, 10.0, 8.0]              # seconds (firmware defaults)
measured   = [10.1, 1.0, 10.1, 10.0, 8.1]              # <-- REPLACE WITH YOUR DATA (mean)
meas_err   = [0.08, 0.02, 0.07, 0.05, 0.06]            # <-- REPLACE (std dev over trials)

x = np.arange(len(cycles))
fig, ax = plt.subplots(figsize=(8, 4.5))
ax.bar(x - 0.18, configured, 0.36, label="Configured duration", color="#4d4d4d")
ax.bar(x + 0.18, measured, 0.36, yerr=meas_err, capsize=4,
       label="Measured duration (mean ± SD)", color="#2ca02c")
ax.set_xticks(x, cycles)
ax.set_ylabel("Duration (seconds)")
ax.set_title("Cycle Timing Repeatability: Configured vs. Measured")
ax.legend()
save(fig, "fig4_3_cycle_timing")

# ============================================================
# Figure 4.4 — Temperature & humidity over the monitoring period
# ============================================================
hours = np.arange(0, 24, 1)                            # <-- REPLACE WITH YOUR DATA
temperature = 26 + 6 * np.exp(-((hours - 14) ** 2) / 18)   # sample diurnal curve
humidity = 70 - 15 * np.exp(-((hours - 14) ** 2) / 20)     # sample inverse curve

fig, ax1 = plt.subplots(figsize=(8, 4.5))
ax1.plot(hours, temperature, "o-", color="#d62728", label="Temperature")
ax1.axhline(40, color="red", ls="--", lw=1.5, label="Danger threshold (40 °C)")
ax1.set_xlabel("Time of day (hour)")
ax1.set_ylabel("Temperature (°C)", color="#d62728")
ax1.set_ylim(15, 45)
ax2 = ax1.twinx()
ax2.plot(hours, humidity, "s-", color="#1f77b4", label="Humidity")
ax2.set_ylabel("Relative humidity (%)", color="#1f77b4")
ax2.set_ylim(30, 100)
ax2.grid(False)
lines1, labels1 = ax1.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax1.legend(lines1 + lines2, labels1 + labels2, loc="upper left")
ax1.set_title("Cage Temperature and Humidity (DHT22) over 24 Hours")
save(fig, "fig4_4_temp_humidity")

# ============================================================
# Figure 4.5 — Daily egg yield vs. alert threshold
# ============================================================
days = ["Day 1", "Day 2", "Day 3", "Day 4", "Day 5", "Day 6", "Day 7"]
yield_per_day = [46, 52, 49, 55, 51, 48, 53]           # <-- REPLACE WITH YOUR DATA
threshold = 50                                          # firmware DEFAULT_EGG_THRESHOLD

fig, ax = plt.subplots(figsize=(8, 4.5))
colors = ["#2ca02c" if y >= threshold else "#ff7f0e" for y in yield_per_day]
ax.bar(days, yield_per_day, color=colors)
ax.axhline(threshold, color="red", ls="--", lw=1.5,
           label=f"Yield alert threshold ({threshold} eggs)")
ax.set_ylabel("Eggs collected per day")
ax.set_title("Daily Egg Yield During Data Collection Period")
ax.legend()
save(fig, "fig4_5_daily_yield")

# ============================================================
# Figure 4.6 — System stability: free heap & WiFi RSSI (heartbeat)
# ============================================================
t_hours = np.arange(0, 48, 0.5)                        # <-- REPLACE WITH YOUR DATA
rng = np.random.default_rng(42)
free_heap = 210 + rng.normal(0, 4, len(t_hours))       # kB, from heartbeat
rssi = -58 + rng.normal(0, 3, len(t_hours))            # dBm, from heartbeat

fig, ax1 = plt.subplots(figsize=(8, 4.5))
ax1.plot(t_hours, free_heap, color="#2ca02c", lw=1, label="Free heap")
ax1.set_xlabel("Uptime (hours)")
ax1.set_ylabel("Free heap (kB)", color="#2ca02c")
ax1.set_ylim(0, 280)
ax2 = ax1.twinx()
ax2.plot(t_hours, rssi, color="#9467bd", lw=1, label="WiFi RSSI")
ax2.set_ylabel("WiFi RSSI (dBm)", color="#9467bd")
ax2.set_ylim(-90, -30)
ax2.grid(False)
lines1, labels1 = ax1.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax1.legend(lines1 + lines2, labels1 + labels2, loc="lower left")
ax1.set_title("System Stability over 48-Hour Continuous Run (30 s Heartbeat)")
save(fig, "fig4_6_system_stability")

# ============================================================
# Figure 4.7 — Command-to-action response latency
# ============================================================
actions = ["Feed\nstart", "Feed\nstop", "Egg\nstart", "Egg\nstop", "Waste\nstart"]
latency_ms = [120, 110, 115, 105, 118]                 # <-- REPLACE WITH YOUR DATA (mean)
latency_err = [25, 20, 22, 18, 24]                     # <-- REPLACE (std dev)

fig, ax = plt.subplots(figsize=(8, 4.5))
ax.bar(actions, latency_ms, yerr=latency_err, capsize=4, color="#1f77b4")
ax.set_ylabel("Latency (ms)")
ax.set_title("Dashboard Command → Hardware Action Latency (MQTT over WebSocket)")
save(fig, "fig4_7_command_latency")

# ============================================================
# Figure 4.8 — Manual vs. automated daily labour time
# ============================================================
tasks = ["Feeding", "Egg collection", "Waste removal", "Monitoring"]
manual_min    = [40, 35, 30, 20]                       # <-- REPLACE WITH YOUR DATA
automated_min = [2, 3, 1, 2]                           # supervision time only

x = np.arange(len(tasks))
fig, ax = plt.subplots(figsize=(8, 4.5))
ax.bar(x - 0.18, manual_min, 0.36, label="Manual operation", color="#d62728")
ax.bar(x + 0.18, automated_min, 0.36, label="Automated system", color="#2ca02c")
ax.set_xticks(x, tasks)
ax.set_ylabel("Time spent per day (minutes)")
total_saved = sum(manual_min) - sum(automated_min)
ax.set_title(f"Daily Labour: Manual vs. Automated (≈ {total_saved} min/day saved)")
ax.legend()
save(fig, "fig4_8_labour_comparison")

# ============================================================
# Figure 4.9 — Offline resilience test: queued vs. replayed events
# ============================================================
outage_tests = ["Test 1\n(5 min)", "Test 2\n(15 min)", "Test 3\n(30 min)",
                "Test 4\n(60 min)"]
events_queued   = [3, 8, 14, 27]                       # <-- REPLACE WITH YOUR DATA
events_replayed = [3, 8, 14, 27]                       # events recovered on reconnect

x = np.arange(len(outage_tests))
fig, ax = plt.subplots(figsize=(8, 4.5))
ax.bar(x - 0.18, events_queued, 0.36, label="Events queued during outage",
       color="#ff7f0e")
ax.bar(x + 0.18, events_replayed, 0.36, label="Events replayed on reconnect",
       color="#2ca02c")
ax.axhline(50, color="red", ls="--", lw=1, label="Queue capacity (50)")
ax.set_xticks(x, outage_tests)
ax.set_ylabel("Number of events")
ax.set_title("Offline Resilience: WiFi Outage Queue and Replay Test")
ax.legend()
save(fig, "fig4_9_offline_resilience")

print("\nDone — 9 figures generated.")
print("REMINDER: these contain SAMPLE data. Edit the blocks marked")
print("'REPLACE WITH YOUR DATA' with your real measurements and re-run.")
