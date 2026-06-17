# PoultryMG: System Assembly & Deployment Guide

This document serves as your master blueprint for taking the PoultryMG system from raw materials and source code into a fully functioning, physical automated poultry farm.

---

## Part 1: Physical Assembly of the Feeding System

The feeding system relies on a gravity-fed gantry mechanism traversing over the cage troughs.

### Required Materials
- 2x 12V High-Torque DC Motor (Wiper motor or linear actuator style) for the gantry
- 1x BTS7960 43A Motor Driver (for precision gantry PWM)
- 1x Auger / screw-conveyor motor (relay-driven, controls feed flow from the hopper)
- 1x 4-Channel 5V Relay Module, **active-LOW** (drives auger, both egg belts, and waste motor)
- 1x ESP32-S3 Development Board
- 1x DS3231 RTC Module
- 1x DHT22 Temperature & Humidity Sensor
- 2x E18-D80NK NPN Proximity Sensors (Yellow type)
- 1x 10kΩ Resistor (pull-up for DHT22 data line)
- 2x 4.7kΩ–10kΩ Resistors (pull-ups for the two proximity-sensor signal lines)
- Hopper rig and PVC piping (2" inner diameter)

### Step 1: Constructing the Track & Gantry
1. **Mount the Track**: Install a rigid linear rail or C-channel beam exactly centered above the feeding troughs spanning the length of your poultry cages.
2. **Mount the Drive Motor**: Attach the 12V DC motor to the gantry sled. Secure the drive mechanism (e.g., rack-and-pinion or heavy-duty timing belt) traversing the length of the track.
3. **Calibrate Transit Time**: Ensure the mechanical gearing allows the sled to travel from the "Home" position to the "End" position in roughly 10 seconds. 

### Step 2: Assembling the Hopper & Auger
1. **Mount the Hopper**: Construct a hopper system (minimum 5kg capacity) above the "Home" parking position of the gantry.
2. **Install the Auger**: Fit a screw-conveyor (auger) motor at the hopper discharge throat to meter feed. Wire the auger motor through **Relay CH4 (`GPIO 4`)** — the firmware runs the auger ON together with the gantry during the distribute phase, then stops it before the gantry returns home.
3. **Align the Pipes**: Attach 3" ID PVC pipes descending from the hopper/auger outlet. They must terminate exactly 1-2 inches directly above the gantry sled's receiving funnel when the sled is parked at Home.

### Step 3: Egg Collection Conveyors
1. **Mount Two Wiper Motors**: Install one wiper motor per cage layer to drive the egg collection belt.
2. **Wire to Relay Module**: Connect the Layer 1 motor to **Relay CH1** and Layer 2 motor to **Relay CH2**. Both belts run simultaneously during collection.
3. **Install Proximity Sensors**: Mount one E18-D80NK NPN sensor at the end of each belt to count eggs as they roll past.
   - **Brown wire** → 5V, **Blue wire** → GND, **Black wire (signal)** → GPIO pin.
   - **Layer 1 signal → `GPIO 47`**, **Layer 2 signal → `GPIO 5`**. (Do not use GPIO 48 — it is the onboard RGB LED.)
   - **Noise hardening (required for reliable counting):** the E18-D80NK output is open-collector NPN. Add a **4.7kΩ–10kΩ external pull-up from each signal line to 3.3V** — the ESP32's internal pull-up is too weak and long sensor runs will pick up motor/relay EMI and produce phantom counts. For very noisy installs, also add a **100nF capacitor from signal to GND** (forms an RC low-pass filter).
   - Keep sensor wires **short and routed away from motor/relay power leads**, and ensure the sensor's 5V supply shares a **common ground** with the ESP32. A floating or unconnected signal pin will self-trigger.

### Step 4: Waste Conveyor
1. **Mount Waste Motor**: Install a wiper motor beneath the cage droppings tray.
2. **Wire to Relay Module**: Connect to **Relay CH3**.

### Step 5: DHT22 Temperature & Humidity Sensor
1. **Placement**: Mount inside the coop at bird height, away from direct sunlight and water sources.
2. **Wiring**:

```
DHT22 Pin 1 (VCC)  →  3.3V
DHT22 Pin 2 (DATA) →  GPIO 6  +  10kΩ resistor bridging DATA to 3.3V
DHT22 Pin 3 (NC)   →  Not connected
DHT22 Pin 4 (GND)  →  GND
```

> [!CAUTION]
> The 10kΩ pull-up resistor between the DATA pin and 3.3V is **required**. Without it, the sensor will return `NaN` readings and the dashboard will show a "DHT22 sensor read failed" warning.

---

## Part 2: Electronics & Wiring

> [!CAUTION] 
> The BTS7960 driver controls up to 43A. Ensure you power the drive loops with a dedicated 12V high-amperage power supply. NEVER run standard servo power or 12V load power directly through the ESP32 pins. Use a common ground across all components.

### ESP32-S3 Pin Mapping

Connect your jumper cables exactly as defined in `firmware/src/config.h`:

| Component | Pin Type | ESP32-S3 GPIO | Function |
| :--- | :--- | :--- | :--- |
| **DS3231 RTC Module** | I2C Data | `GPIO 8` | I2C SDA |
| | I2C Clock | `GPIO 9` | I2C SCL |
| **BTS7960 Driver** | PWM | `GPIO 14` | Forward / Left PWM (`PIN_FEED_LPWM`) |
| | PWM | `GPIO 16` | Reverse / Right PWM (`PIN_FEED_RPWM`) |
| | Digital | `GPIO 15` | Master Driver Enable (`PIN_FEED_EN`) |
| **DHT22 Sensor** | Digital | `GPIO 6` | Temperature & Humidity Data (10kΩ pull-up to 3.3V) |
| **NPN Proximity Sensors** | Digital In | `GPIO 47` | Layer 1 Egg Counter (`PIN_EGG_IR1`, Black wire → GPIO) |
| | Digital In | `GPIO 5` | Layer 2 Egg Counter (`PIN_EGG_IR2`, Black wire → GPIO) |
| **Relay Block (4-Channel, active-LOW)** | Digital Out | `GPIO 12` | CH1: Egg Conveyor Layer 1 (`PIN_EGG_MOTOR_L`) |
| | Digital Out | `GPIO 13` | CH2: Egg Conveyor Layer 2 (`PIN_EGG_MOTOR_R`) |
| | Digital Out | `GPIO 21` | CH3: Waste Conveyor (`PIN_WASTE_RELAY`) |
| | Digital Out | `GPIO 4` | CH4: Auger Motor — feed flow (`PIN_AUGER_RELAY`) |

> [!WARNING]
> **Do NOT use `GPIO 48` for any sensor.** On the ESP32-S3-DevKitC-1, GPIO 48 is hard-wired to the onboard addressable RGB LED. Wiring a proximity sensor there will drive the LED with random colors and the firmware will never read it. The Layer 2 sensor belongs on **`GPIO 5`**.

> [!NOTE]
> The relay module is configured **active-LOW** (`RELAY_ON = LOW` in `config.h`). Set the module's trigger jumper to the **L** (low-level) position. Active-LOW modules are boot-safe — relays stay de-energized while the ESP32's pins float HIGH during reset.

### Power Supply Requirements

| Rail | Voltage | Feeds |
|------|---------|-------|
| Main 12V (≥5A) | 12V DC | BTS7960 motor driver, wiper motors via relay |
| ESP32 USB | 5V | ESP32-S3 dev board |
| Relay VCC | 5V | Relay coils + opto inputs (dedicated 5V supply recommended; a separate supply sharing only GND prevents coil-switching noise from resetting the ESP32) |
| Sensor VCC | 5V | E18-D80NK proximity sensors (share common GND with ESP32) |

> [!IMPORTANT]
> All GND rails must be **connected together** (common ground). Floating grounds cause erratic relay switching and I2C bus failures.

---

## Part 3: Flashing the Firmware

The microcontroller logic runs on an RTOS environment managed by PlatformIO.

### Step 1: Toolchain Preparation
1. Install **VS Code**.
2. Go to the Extensions tab and install **PlatformIO IDE**.
3. Re-plug your USB cable directly to the ESP32-S3 (use the `UART`/`COM` port, not the secondary OTG port).

### Step 2: Network Configuration
1. Open the file `firmware/src/config.h`.
2. Update the Wi-Fi credentials to match your router:
```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
```

### Step 3: Build & Upload
1. In VS Code, go to **File → Open Folder** and select strictly the `firmware/` directory from the repository.
2. At the bottom blue status bar, click the **Checkmark (✔)** to compile the C++ binaries.
3. Upon success, click the **Right Arrow (→)** to flash the firmware directly to the ESP32-S3 embedded flash memory.
4. Click the **Plug icon (🔌)** to open the Serial Monitor (Baud rate `115200`). You should see output like:

```
SPIFFS mounted successfully
[CONFIG] Loaded: Feed[7,17] Egg[8,20] Waste[6,18] Threshold=50
[HW] DHT22 initialized on GPIO 6
Connecting to WiFi...
WiFi connected! IP: 192.168.x.x
Attempting MQTT connection...connected
[CONFIG] Published current config.
```

> [!NOTE]
> If the DHT22 is not wired yet, you will see `[DHT] Read failed.` in the serial output. This is normal — the system continues operating without the temperature sensor.

---

## Part 4: Running the UI Dashboard

> [!TIP]
> The Web Interface is built on React + Vite. Ensure you have Node.js (v18+) installed on your PC or Raspberry Pi.

1. Open a new terminal.
2. Navigate into the UI directory:
   ```bash
   cd dashboard/
   ```
3. *(First time only)* Install the node modules:
   ```bash
   npm install
   ```
4. Start the development server:
   ```bash
   npm run dev
   ```

### System Verification
The dashboard is live-wired to `broker.hivemq.com` over WSS port 8884. Once the ESP32 is powered and connected to WiFi, the top-right status badge will change from **"Hardware Offline"** to **"ESP32 Online"**.

Press **Force Feed** on the Feeding System card and watch the rig physically drive the sequence!

---

## Part 5: Using the Admin Panel

The dashboard has two tabs: **Controls** (the main operations view) and **Admin** (system configuration and monitoring).

### System Health
- **Temperature & Humidity**: Live readings from the DHT22 sensor, updated every 30 seconds. The temperature gauge turns **red and pulses** when it reaches 40°C or above.
- **WiFi Signal**: RSSI strength displayed as a percentage gauge with raw dBm value.
- **Free Heap**: Available ESP32 RAM — useful for detecting memory leaks.
- **RTC Clock**: A live ticking clock synced from the ESP32's DS3231 RTC. Ticks locally between heartbeats.
- **Offline Queue**: Shows how many MQTT messages are queued in SPIFFS waiting for reconnection. The **Clear** button wipes the queue manually.

### Schedule Configuration
All automation schedules can be changed **without re-flashing firmware**:

1. Use the hour pickers to set two times per subsystem (feeding, egg collection, waste flush).
2. Adjust the **Egg Alert Threshold** slider (10–200 eggs).
3. Click **Push to ESP32** — the config is sent over MQTT and saved to SPIFFS on the ESP32.
4. Schedules **persist across reboots** (stored in `/config.json` on the ESP32's flash).

### Event Log
- Captures all MQTT events (except noisy heartbeats) in a scrollable, color-coded table.
- Persisted in your browser's `localStorage` (survives page refreshes, max 100 entries).
- **Export CSV** downloads the log as a spreadsheet-ready file.

### Alerts
- **Temperature alerts**: Auto-fired when the DHT22 reads ≥40°C. Resets when temp drops below 38°C (2°C hysteresis to prevent spam).
- **Egg threshold alerts**: Fired when a single collection cycle exceeds the configured threshold.
- A red **badge counter** appears on the Admin tab when alerts arrive while you're on the Controls tab.

---

## MQTT Topic Reference

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `poultry/feed/cmd` | Dashboard → ESP | `{"action":"start"}` or `{"action":"stop"}` |
| `poultry/feed/status` | ESP → Dashboard | `{"state":"cycling"}` or `{"state":"idle"}` |
| `poultry/egg/cmd` | Dashboard → ESP | Start/stop egg collection |
| `poultry/egg/status` | ESP → Dashboard | Collection state + final counts |
| `poultry/egg/data` | ESP → Dashboard | Live egg counts during collection |
| `poultry/waste/cmd` | Dashboard → ESP | Start/stop waste flush |
| `poultry/waste/status` | ESP → Dashboard | Waste cycle state |
| `poultry/system/status` | ESP → Dashboard | Heartbeat: heap, RSSI, temp, humidity, RTC, queue |
| `poultry/alerts` | ESP → Dashboard | Temperature and egg threshold alerts |
| `poultry/config/cmd` | Dashboard → ESP | `update_schedule`, `clear_queue`, `get_config` |
| `poultry/config/status` | ESP → Dashboard | Current schedule configuration |
