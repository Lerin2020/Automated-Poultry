# PoultryMG: System Assembly & Deployment Guide

This document serves as your master blueprint for taking the PoultryMG system from raw materials and source code into a fully functioning, physical automated poultry farm.

---

## Part 1: Physical Assembly of the Feeding System

The feeding system relies on a gravity-fed gantry mechanism traversing over the cage troughs.

### Required Materials
- 1x 12V High-Torque DC Motor (Wiper motor or linear actuator style)
- 1x BTS7960 43A Motor Driver (for precision gantry PWM)
- 2x Standard Servos (e.g., MG996R)
- 1x 4-Channel 5V Relay Module (for ON/OFF Waste & Egg Wiper Motors)
- 1x ESP32-S3 Development Board
- 1x DS3231 RTC Module
- 1x DHT22 Temperature & Humidity Sensor
- 2x E18-D80NK NPN Proximity Sensors (Yellow type)
- 1x 10kΩ Resistor (pull-up for DHT22 data line)
- Custom Dual-Hopper rig and PVC piping (3" inner diameter)

### Step 1: Constructing the Track & Gantry
1. **Mount the Track**: Install a rigid linear rail or C-channel beam exactly centered above the feeding troughs spanning the length of your poultry cages.
2. **Mount the Drive Motor**: Attach the 12V DC motor to the gantry sled. Secure the drive mechanism (e.g., rack-and-pinion or heavy-duty timing belt) traversing the length of the track.
3. **Calibrate Transit Time**: Ensure the mechanical gearing allows the sled to travel from the "Home" position to the "End" position in roughly 10 seconds. 

### Step 2: Assembling the Hopper & Chutes
1. **Mount the Hoppers**: Construct a dual-chamber hopper system (minimum 5kg capacity each) above the "Home" parking position of the gantry.
2. **Install the Servo Gates**: Wire `Servo 1` and `Servo 2` directly to the bottom discharge throats of the hoppers. 
   - A 0° (`SERVO_CLOSED`) rotation must fully obstruct the chute with a sliding plate.
   - A 90° (`SERVO_OPEN`) rotation must pull the plate back, dumping feed.
3. **Align the Pipes**: Attach 3" ID PVC pipes descending from the hopper chutes. They must terminate exactly 1-2 inches directly above the gantry sled's receiving funnels when the sled is parked at Home.

### Step 3: Egg Collection Conveyors
1. **Mount Two Wiper Motors**: Install one wiper motor per cage layer to drive the egg collection belt.
2. **Wire to Relay Module**: Connect the Layer 1 motor to **Relay CH1** and Layer 2 motor to **Relay CH2**. Both belts run simultaneously during collection.
3. **Install Proximity Sensors**: Mount one E18-D80NK NPN sensor at the end of each belt to count eggs as they roll past.
   - **Brown wire** → 5V, **Blue wire** → GND, **Black wire (signal)** → GPIO pin.

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
| | Digital | `GPIO 15` | Master Driver Enable |
| **Hopper Gates** | PWM | `GPIO 4` | Hopper Servo 1 |
| | PWM | `GPIO 5` | Hopper Servo 2 |
| **DHT22 Sensor** | Digital | `GPIO 6` | Temperature & Humidity Data (10kΩ pull-up to 3.3V) |
| **NPN Proximity Sensors** | Digital In | `GPIO 47` | Layer 1 Egg Counter (Black wire → GPIO) |
| | Digital In | `GPIO 48` | Layer 2 Egg Counter (Black wire → GPIO) |
| **Relay Block (4-Channel)** | Digital Out | `GPIO 12` | CH1: Egg Conveyor Layer 1 |
| | Digital Out | `GPIO 13` | CH2: Egg Conveyor Layer 2 |
| | Digital Out | `GPIO 21` | CH3: Waste Conveyor |
| | Digital Out | `N/A` | CH4: Available Spare |

### Power Supply Requirements

| Rail | Voltage | Feeds |
|------|---------|-------|
| Main 12V (≥5A) | 12V DC | BTS7960 motor driver, wiper motors via relay |
| ESP32 USB | 5V | ESP32-S3 dev board |
| Relay VCC | 5V | Relay coils (can share ESP32 5V pin if <500mA total) |
| Servo VCC | 5V–6V | MG996R servos (separate BEC recommended for high-torque servos) |

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
