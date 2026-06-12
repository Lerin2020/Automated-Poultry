# Complete Automated Poultry Cage System Implementation Plan

## Overview
A comprehensive two-part plan covering:
1. **Part A: Software/Firmware Implementation** — ESP32-S3 firmware with three independent subsystems (waste management, egg collection, gravity-fed feeding), MQTT communication, local data logging, and cloud sync to Google Sheets
2. **Part B: Mechanical Rig Specifications** — Fixed hopper, side-to-side feed delivery system with optimal pipe diameter, height, materials, and engineering calculations

## System Architecture
```mermaid
graph TD
    ESP["ESP32-S3 Controller"] --> |BTS7960 PWM| M1["Gantry Drive Motor"]
    ESP --> |PWM| S1["Hopper Servo 1 (Chute A)"]
    ESP --> |PWM| S2["Hopper Servo 2 (Chute B)"]
    ESP --> |Relay Control| M2["Waste Conveyor Motor"]
    ESP --> |Relay Control| M3["Egg Collection Motors"]
    
    IR1["NPN Proximity Sensor 1 (E18-D80NK)"] --> |Interrupt (FALLING)| ESP
    IR2["NPN Proximity Sensor 2 (E18-D80NK)"] --> |Interrupt (FALLING)| ESP
    RTC["DS3231 RTC Module"] <--> |I2C SDA/SCL| ESP
    
    ESP <--> |MQTT over WiFi| Broker["HiveMQ Broker"]
    Broker <--> Web["React Dashboard"]
    ESP --> |REST API| GSheets["Google Sheets Logs"]
```

---


## Part A: Software/Firmware Implementation

### Step 1: Design Firmware Architecture

Create three independent modules in ESP32-S3:

**Waste Management Module:**
- DC motor relay control on GPIO (to be assigned)
- Schedule: 6:00 AM & 6:00 PM
- Runtime: 30–60 seconds per activation
- Status tracking: active/idle/failed

**Egg Collection Module:**
- Two wiper motors (separate GPIO pins for direction control)
- Two IR sensors for pulse counting (GPIO pins to be assigned)
- Schedule: 8:00 AM & 8:00 PM
- Egg count tracking: cumulative per layer
- Status tracking: motor status, sensor health

**Feeding Module:**
- Wiper motor relay for side-to-side motion (GPIO to be assigned)
- Schedule: 7:00 AM & 5:00 PM
- Timed cycles with multiple passes (3–5 passes per feeding)
- Target portion calculation from bird count and age range
- Status tracking: motor status, dispensed amount vs. target

**Parameter Storage (NVS):**
- Bird count (1–1000)
- Age range (0–4, mapping to 60–110g/day)
- Portion grams per cycle (calculated: daily_g ÷ 2)
- Cycle duration baseline (in seconds, calibrated empirically)
- Waste management runtime
- Feeding pass count and dwell time
- Schedule overrides (start time adjustments)

**Timing Implementation:**
- Use millis()-based timers for all scheduling (not delay())
- Check schedule every 1000ms (1-second intervals)
- Track last execution time per subsystem to prevent duplicate triggers
- Implement grace period (300–600 seconds) to prevent re-triggering within same hour

```cpp
// Pseudo-code structure
unsigned long lastFeedTime = 0;
unsigned long lastEggTime = 0;
unsigned long lastWasteTime = 0;
unsigned long lastDailyReset = 0;
const unsigned long GRACE_PERIOD = 600000; // 10 minutes
const unsigned long DAILY_PERIOD = 86400000; // 24 hours

void loop() {
  unsigned long now = millis();
  
  // Daily counter reset (e.g., midnight)
  if (now - lastDailyReset > DAILY_PERIOD) {
    lastDailyReset = now;
    eggCount_L1 = 0;
    eggCount_L2 = 0;
    publishMQTT("poultry/egg/status", "{\"state\": \"counter_reset\"}");
  }
  
  // Check feeding schedule (7 AM, 5 PM)
  if (shouldFeedNow(now) && (now - lastFeedTime) > GRACE_PERIOD) {
    lastFeedTime = now;
    executeFeedingCycle();
  }
  
  // Check egg collection (8 AM, 8 PM)
  if (shouldCollectEggsNow(now) && (now - lastEggTime) > GRACE_PERIOD) {
    lastEggTime = now;
    executeEggCollection();
  }
  
  // Check waste management (6 AM, 6 PM)
  if (shouldCleanWasteNow(now) && (now - lastWasteTime) > GRACE_PERIOD) {
    lastWasteTime = now;
    executeWasteManagement();
  }
}
```

---

### Step 2: Implement MQTT Pub/Sub System

**Broker Configuration:**
- Broker: HiveMQ (broker.hivemq.com) or self-hosted
- Port: 1883 (unencrypted) or 8883 (TLS)
- Keep-alive: 60 seconds
- Reconnect backoff: 5-second initial, exponential up to 300 seconds
- Message QoS: 1 (at-least-once delivery)

**Topic Structure:**

```
Command Topics (Dashboard → ESP32):
  poultry/feed/cmd           → {"action": "start", "passes": 3, "dwell": 3}
  poultry/egg/cmd            → {"action": "start"}
  poultry/waste/cmd          → {"action": "start", "duration": 45}
  poultry/settings/cmd       → {"birds": 50, "ageRange": 2, "cycleSeconds": 45}

Telemetry Topics (ESP32 → Dashboard):
  poultry/feed/status        → {"state": "cycling", "pass": 2/3, "motorRuntime": 5}
  poultry/feed/data          → {"timestamp": "2026-04-10T14:30:00Z", "target_g": 55, "actual_g": 52, "status": "success"}
  poultry/egg/status         → {"state": "idle", "count_layer1": 15, "count_layer2": 12}
  poultry/egg/data           → {"timestamp": "2026-04-10T08:15:00Z", "eggs_l1": 18, "eggs_l2": 16}
  poultry/waste/status       → {"state": "idle", "lastClean": "2026-04-10T18:00:00Z"}
  poultry/system/heartbeat   → {"device_id": "esp32-01", "wifi_rssi": -65, "uptime_s": 864000}
  poultry/system/offline     → {"queue_size": 12, "oldest_entry": "2026-04-10T10:30:00Z"}
```

**Message Queueing During Offline:**
- Store JSON messages to SPIFFS when MQTT connection fails
- Max queue size: 50 messages (circular buffer)
- Sync queue when WiFi/MQTT reconnects
- Include timestamp with each queued message

```cpp
// Pseudo-code for message queueing
struct QueuedMessage {
  unsigned long timestamp;
  String topic;
  String payload;
};

QueuedMessage messageQueue[50];
int queueIndex = 0;

void publishOrQueue(String topic, String payload) {
  if (client.connected()) {
    client.publish(topic.c_str(), payload.c_str(), 1);
  } else {
    messageQueue[queueIndex % 50] = {millis(), topic, payload};
    queueIndex++;
  }
}

void syncQueueWhenOnline() {
  if (client.connected() && queueIndex > 0) {
    for (int i = 0; i < min(queueIndex, 50); i++) {
      client.publish(messageQueue[i].topic.c_str(), messageQueue[i].payload.c_str(), 1);
    }
    queueIndex = 0;
  }
}
```

**Auto-Reconnect Logic:**
- Check connection every 10 seconds
- Implement exponential backoff (5s, 10s, 20s, 40s, 80s, 160s, 300s max)
- Reset backoff on successful connection
- Publish heartbeat every 60 seconds to monitor device health

---

### Step 3: Code Motor Control Sequences

**Waste Management Motor Control:**
```cpp
#define WASTE_MOTOR_PIN 26  // Relay pin
#define WASTE_DURATION 45   // seconds (configurable via MQTT)

void executeWasteManagement() {
  Serial.println("[WASTE] Starting waste management cycle");
  publishMQTT("poultry/waste/status", "{\"state\": \"active\"}");
  
  digitalWrite(WASTE_MOTOR_PIN, HIGH);  // Motor ON
  unsigned long startTime = millis();
  
  while (millis() - startTime < WASTE_DURATION * 1000) {
    if (!checkWiFi()) {
      publishMQTT("poultry/waste/status", "{\"state\": \"offline_active\"}");
    }
    delay(100);  // Check every 100ms
  }
  
  digitalWrite(WASTE_MOTOR_PIN, LOW);   // Motor OFF
  publishMQTT("poultry/waste/status", "{\"state\": \"idle\", \"lastClean\": \"" + getCurrentTimestamp() + "\"}");
  logToStorage("waste", "completed", 1);
}
```

**Egg Collection Sequence:**
```cpp
#define EGG_MOTOR_L_PIN 12   // Left wiper motor (forward)
#define EGG_MOTOR_R_PIN 13   // Right wiper motor (reverse)
#define EGG_SENSOR_L_PIN 34  // IR sensor Layer 1
#define EGG_SENSOR_R_PIN 35  // IR sensor Layer 2

volatile int eggCount_L1 = 0;
volatile int eggCount_L2 = 0;

void IRAM_ATTR countEgg_L1() {
  eggCount_L1++;
}

void IRAM_ATTR countEgg_L2() {
  eggCount_L2++;
}

void executeEggCollection() {
  Serial.println("[EGG] Starting egg collection cycle");
  
  attachInterrupt(digitalPinToInterrupt(EGG_SENSOR_L_PIN), countEgg_L1, FALLING);
  attachInterrupt(digitalPinToInterrupt(EGG_SENSOR_R_PIN), countEgg_L2, FALLING);
  
  publishMQTT("poultry/egg/status", "{\"state\": \"collecting\"}");
  
  // Activate wiper motors (Option: Sequential or Simultaneous)
  // Option 1: Sequential
  digitalWrite(EGG_MOTOR_L_PIN, HIGH);  // Left motor ON
  delay(5000);                           // 5 seconds
  digitalWrite(EGG_MOTOR_L_PIN, LOW);
  
  digitalWrite(EGG_MOTOR_R_PIN, HIGH);  // Right motor ON
  delay(5000);
  digitalWrite(EGG_MOTOR_R_PIN, LOW);
  
  detachInterrupt(digitalPinToInterrupt(EGG_SENSOR_L_PIN));
  detachInterrupt(digitalPinToInterrupt(EGG_SENSOR_R_PIN));
  
  int totalEggs = eggCount_L1 + eggCount_L2;
  
  if (totalEggs >= 50) {
    publishMQTT("poultry/alerts", "{\"type\": \"egg_threshold\", \"message\": \"50 eggs collected today!\"}");
  }
  
  String payload = "{\"timestamp\": \"" + getCurrentTimestamp() + "\", \"eggs_l1\": " + String(eggCount_L1) + ", \"eggs_l2\": " + String(eggCount_L2) + ", \"total\": " + String(totalEggs) + "}";
  publishMQTT("poultry/egg/data", payload);
  publishMQTT("poultry/egg/status", "{\"state\": \"idle\", \"total\": " + String(totalEggs) + "}");
  
  logToStorage("eggs", String(totalEggs), 2);
}
```

**Feeding Cycle with Multiple Passes:**
```cpp
#include <ESP32Servo.h>

#define BTS_LPWM 14    // Forward PWM
#define BTS_RPWM 27    // Reverse PWM
#define BTS_EN 15      // Enable Pin (tied to L_EN & R_EN)
#define SERVO1_PIN 4   // Hopper Servo 1
#define SERVO2_PIN 5   // Hopper Servo 2

const int SERVO_OPEN = 90;
const int SERVO_CLOSED = 0;

Servo hopperServo1;
Servo hopperServo2;

// From poultry system: feedPerBird array
const int feedPerBird[] = {60, 85, 105, 110, 109};  // grams/day by age

void initializeServos() {
  hopperServo1.attach(SERVO1_PIN);
  hopperServo2.attach(SERVO2_PIN);
  hopperServo1.write(SERVO_CLOSED);
  hopperServo2.write(SERVO_CLOSED);
}

void executeFeedingCycle() {
  // Load parameters from NVS
  int numBirds = preferences.getInt("birds", 50);
  int ageRange = preferences.getInt("ageRange", 2);
  
  // Calculate target
  int dailyGrams = numBirds * feedPerBird[ageRange];
  int targetGrams = dailyGrams / 2;  // 2 cycles per day
  
  Serial.println("[FEED] Starting feeding cycle");
  Serial.println("Birds: " + String(numBirds) + ", Age: " + String(ageRange) + ", Target: " + String(targetGrams) + "g");
  
  publishMQTT("poultry/feed/status", "{\"state\": \"cycling\", \"target_g\": " + String(targetGrams) + "}");
  
  // Open Hopper Servos to dispense feed
  Serial.println("Opening Hopper Servos...");
  hopperServo1.write(SERVO_OPEN);
  hopperServo2.write(SERVO_OPEN);
  delay(500); // Allow servos to mechanically open
  
  // Move Clockwise (Traverse Out)
  Serial.println("Moving Clockwise...");
  digitalWrite(BTS_LPWM, HIGH);
  digitalWrite(BTS_RPWM, LOW);
  digitalWrite(BTS_EN, HIGH);
  delay(10000);  // 10 seconds traverse
  
  // Stop & Dwell
  digitalWrite(BTS_EN, LOW);
  digitalWrite(BTS_LPWM, LOW);
  delay(1000);  
  
  publishMQTT("poultry/feed/status", "{\"state\": \"cycling\", \"pass\": \"returning\"}");
  
  // Move Anti-Clockwise (Traverse Return)
  Serial.println("Moving Anti-Clockwise...");
  digitalWrite(BTS_LPWM, LOW);
  digitalWrite(BTS_RPWM, HIGH);
  digitalWrite(BTS_EN, HIGH);
  delay(10000);  // 10 seconds traverse
  
  // Close Servos & Hard Stop
  Serial.println("Closing Servos and Stopping...");
  hopperServo1.write(SERVO_CLOSED);
  hopperServo2.write(SERVO_CLOSED);
  digitalWrite(BTS_EN, LOW);
  digitalWrite(BTS_RPWM, LOW);
  
  String payload = "{\"timestamp\": \"" + getCurrentTimestamp() + "\", \"target_g\": " + String(targetGrams) + ", \"status\": \"success\"}";
  publishMQTT("poultry/feed/data", payload);
  publishMQTT("poultry/feed/status", "{\"state\": \"idle\"}");
  
  logToStorage("feed", String(targetGrams), 0);
}
```



---

### Step 5: Add Data Logging & Cloud Sync

**Local SPIFFS Storage (JSON Format):**
```cpp
#include <SPIFFS.h>
#include <ArduinoJson.h>

const char* LOG_FILE = "/poultry_log.json";
const int MAX_LOG_ENTRIES = 500;

struct LogEntry {
  unsigned long timestamp;
  String subsystem;  // "feed", "eggs", "waste"
  String target;
  String actual;
  String status;
};

void logToStorage(String subsystem, String actual, int subsystemId) {
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  
  // Read existing log
  File file = SPIFFS.open(LOG_FILE, "r");
  DynamicJsonDocument doc(10240);  // 10KB buffer
  
  if (file && file.size() > 0) {
    deserializeJson(doc, file);
    file.close();
  }
  
  // Add new entry
  JsonObject entry = doc.createNestedObject();
  entry["timestamp"] = getCurrentTimestamp();
  entry["subsystem"] = subsystem;
  entry["actual"] = actual;
  entry["status"] = "logged";
  
  // Manage size (keep latest 500 entries)
  if (doc.size() > MAX_LOG_ENTRIES) {
    // Remove oldest entry
    doc.remove(0);
  }
  
  // Write back
  file = SPIFFS.open(LOG_FILE, "w");
  serializeJson(doc, file);
  file.close();
  
  Serial.println("[LOG] Stored: " + subsystem);
}
```

**Google Sheets API Sync:**
```cpp
// Requires: WiFiClientSecure, HTTPClient libraries
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

const char* GOOGLE_SHEETS_URL = "https://sheets.googleapis.com/v4/spreadsheets/{SPREADSHEET_ID}/values/{SHEET_NAME}:append?key={API_KEY}";

void syncLogsToGoogleSheets() {
  if (!WiFi.isConnected()) {
    Serial.println("[SYNC] WiFi not connected");
    return;
  }
  
  // Read local logs
  File file = SPIFFS.open(LOG_FILE, "r");
  if (!file) {
    Serial.println("[SYNC] No log file found");
    return;
  }
  
  DynamicJsonDocument doc(10240);
  deserializeJson(doc, file);
  file.close();
  
  // Prepare batch for Google Sheets
  String requestBody = "{\"values\": [";
  for (JsonObject entry : doc.as<JsonArray>()) {
    requestBody += "[\"" + String(entry["timestamp"].as<String>()) + "\", \"" + entry["subsystem"].as<String>() + "\", \"" + entry["actual"].as<String>() + "\"],";
  }
  requestBody += "]}";
  
  // Send HTTP POST
  HTTPClient http;
  http.begin(GOOGLE_SHEETS_URL);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(requestBody);
  if (httpResponseCode == 200) {
    Serial.println("[SYNC] Successfully synced to Google Sheets");
    // Clear local log after successful sync
    SPIFFS.remove(LOG_FILE);
  } else {
    Serial.println("[SYNC] Failed: " + String(httpResponseCode));
  }
  
  http.end();
}
```

**Offline Indicator & Sync Trigger:**
```cpp
unsigned long lastSyncTime = 0;
const unsigned long SYNC_INTERVAL = 3600000;  // 1 hour

void checkAndSync() {
  unsigned long now = millis();
  
  if (WiFi.isConnected() && (now - lastSyncTime > SYNC_INTERVAL)) {
    lastSyncTime = now;
    syncLogsToGoogleSheets();
    publishMQTT("poultry/system/sync", "{\"status\": \"success\"}");
  } else if (!WiFi.isConnected()) {
    publishMQTT("poultry/system/offline", "{\"queue_size\": " + String(getQueueSize()) + "}");
  }
}
```

---

### Step 6: Build React Web Dashboard

**Component Structure:**

```
src/
├── components/
│   ├── FeedingControl.tsx      // Number of birds, age, manual trigger, cycle duration
│   ├── EggCollection.tsx        // Live egg count, manual trigger, collection history
│   ├── OperationHistory.tsx     // Table of all operations with timestamps
│   ├── CloudSync.tsx            // Google Sheets sync status, offline indicator
│   └── Settings.tsx             // WiFi config, MQTT broker, schedule adjustments
├── hooks/
│   └── useMQTT.ts              // MQTT connection, pub/sub, auto-reconnect
├── services/
│   └── api.ts                  // REST calls for settings, history
└── App.tsx                     // Main layout, state management
```

**FeedingControl Component (React):**
```tsx
import React, { useState, useEffect } from 'react';
import { useMQTT } from '../hooks/useMQTT';

interface FeedingState {
  state: 'idle' | 'cycling';
  target_g: number;
  actual_g?: number;
  pass?: number;
  total_passes?: number;
}

export const FeedingControl: React.FC = () => {
  const { publish, subscribe } = useMQTT();
  const [birds, setBirds] = useState(50);
  const [ageRange, setAgeRange] = useState(2);
  const [feedingState, setFeedingState] = useState<FeedingState>({ state: 'idle', target_g: 0 });
  const [cycleSeconds, setCycleSeconds] = useState(45);
  
  const feedPerBird = [60, 85, 105, 110, 109];  // grams/day by age
  const dailyGrams = birds * feedPerBird[ageRange];
  const targetPerCycle = dailyGrams / 2;
  
  useEffect(() => {
    const unsubscribe = subscribe('poultry/feed/status', (message) => {
      setFeedingState(JSON.parse(message.toString()));
    });
    return unsubscribe;
  }, []);
  
  const handleManualFeed = () => {
    publish('poultry/feed/cmd', JSON.stringify({
      action: 'start',
      passes: 3,
      dwell: 3,
      cycleSeconds
    }));
  };
  
  const handleSaveSettings = () => {
    publish('poultry/settings/cmd', JSON.stringify({
      birds,
      ageRange,
      cycleSeconds
    }));
  };
  
  return (
    <div className="p-6 bg-white rounded-lg shadow-md">
      <h2 className="text-2xl font-bold mb-4">Feeding System</h2>
      
      <div className="grid grid-cols-2 gap-4 mb-6">
        <div>
          <label className="block text-sm font-medium">Number of Birds</label>
          <input
            type="number"
            value={birds}
            onChange={(e) => setBirds(parseInt(e.target.value))}
            min="1"
            max="1000"
            className="w-full px-3 py-2 border rounded-md"
          />
        </div>
        
        <div>
          <label className="block text-sm font-medium">Age Range</label>
          <select
            value={ageRange}
            onChange={(e) => setAgeRange(parseInt(e.target.value))}
            className="w-full px-3 py-2 border rounded-md"
          >
            <option value={0}>12-16 weeks (60g/day)</option>
            <option value={1}>17-20 weeks (85g/day)</option>
            <option value={2}>21-30 weeks (105g/day)</option>
            <option value={3}>31-52 weeks (110g/day)</option>
            <option value={4}>52+ weeks (109g/day)</option>
          </select>
        </div>
      </div>
      
      <div className="bg-blue-50 p-4 rounded mb-6">
        <p className="text-sm"><strong>Daily Total:</strong> {dailyGrams}g</p>
        <p className="text-sm"><strong>Per Cycle:</strong> {targetPerCycle}g (7 AM & 5 PM)</p>
        <p className="text-sm"><strong>Weekly:</strong> {dailyGrams * 7}g</p>
      </div>
      
      <div className="mb-6">
        <label className="block text-sm font-medium mb-2">Cycle Duration (seconds)</label>
        <input
          type="number"
          value={cycleSeconds}
          onChange={(e) => setCycleSeconds(parseInt(e.target.value))}
          min="10"
          max="120"
          className="w-full px-3 py-2 border rounded-md"
        />
        <small className="text-gray-500">Empirically calibrated based on pellet type and flow rate</small>
      </div>
      
      <div className="flex gap-3 mb-6">
        <button
          onClick={handleManualFeed}
          className="flex-1 px-4 py-2 bg-green-500 text-white rounded-md hover:bg-green-600"
          disabled={feedingState.state === 'cycling'}
        >
          {feedingState.state === 'cycling' ? 'Feeding...' : 'Feed Now'}
        </button>
        <button
          onClick={handleSaveSettings}
          className="flex-1 px-4 py-2 bg-blue-500 text-white rounded-md hover:bg-blue-600"
        >
          Save Settings
        </button>
      </div>
      
      {feedingState.state === 'cycling' && (
        <div className="bg-yellow-50 p-4 rounded">
          <p className="text-sm"><strong>Status:</strong> Cycling</p>
          {feedingState.pass && (
            <p className="text-sm"><strong>Progress:</strong> Pass {feedingState.pass}/{feedingState.total_passes}</p>
          )}
          <p className="text-sm"><strong>Target:</strong> {feedingState.target_g}g</p>
        </div>
      )}
      
      {feedingState.state === 'idle' && feedingState.actual_g !== undefined && (
        <div className="bg-green-50 p-4 rounded">
          <p className="text-sm"><strong>Last Feeding:</strong> {feedingState.actual_g}g dispensed</p>
          <p className="text-sm text-green-600">✓ Completed</p>
        </div>
      )}
    </div>
  );
};
```

**WeightMonitor Component:**
```tsx
export const WeightMonitor: React.FC = () => {
  const { subscribe } = useMQTT();
  const [weights, setWeights] = useState({
    before_l1_g: 0,
    after_l1_g: 0,
    before_l2_g: 0,
    after_l2_g: 0
  });
  
  useEffect(() => {
    const unsubscribe = subscribe('poultry/weight/data', (message) => {
      setWeights(JSON.parse(message.toString()));
    });
    return unsubscribe;
  }, []);
  
  return (
    <div className="grid grid-cols-2 gap-4">
      <div className="p-4 bg-gray-50 rounded">
        <h3 className="font-bold mb-2">Layer 1</h3>
        <p className="text-sm">Before: {weights.before_l1_g}g</p>
        <p className="text-sm">After: {weights.after_l1_g}g</p>
        <p className="text-sm font-bold text-green-600">Dispensed: {weights.before_l1_g - weights.after_l1_g}g</p>
      </div>
      
      <div className="p-4 bg-gray-50 rounded">
        <h3 className="font-bold mb-2">Layer 2</h3>
        <p className="text-sm">Before: {weights.before_l2_g}g</p>
        <p className="text-sm">After: {weights.after_l2_g}g</p>
        <p className="text-sm font-bold text-green-600">Dispensed: {weights.before_l2_g - weights.after_l2_g}g</p>
      </div>
    </div>
  );
};
```

**useMQTT Hook:**
```typescript
import { useEffect, useRef, useCallback } from 'react';
import * as Paho from 'paho-mqtt';

export const useMQTT = () => {
  const clientRef = useRef<Paho.Client | null>(null);
  const subscribersRef = useRef<Map<string, (message: Paho.Message) => void>>(new Map());
  
  useEffect(() => {
    const client = new Paho.Client('broker.hivemq.com', 8883, 'poultry-dashboard-' + Math.random());
    
    client.onConnectionLost = () => {
      console.log('MQTT connection lost');
      setTimeout(() => client.connect({ onSuccess: () => console.log('Reconnected') }), 5000);
    };
    
    client.onMessageArrived = (message) => {
      const callback = subscribersRef.current.get(message.destinationName);
      if (callback) callback(message);
    };
    
    client.connect({
      onSuccess: () => console.log('MQTT connected'),
      useSSL: true,
      reconnect: true
    });
    
    clientRef.current = client;
    
    return () => {
      if (clientRef.current) clientRef.current.disconnect();
    };
  }, []);
  
  const publish = useCallback((topic: string, message: string) => {
    if (clientRef.current?.isConnected()) {
      clientRef.current.send(topic, message, 1);
    } else {
      console.warn('MQTT not connected');
    }
  }, []);
  
  const subscribe = useCallback((topic: string, callback: (message: Paho.Message) => void) => {
    subscribersRef.current.set(topic, callback);
    clientRef.current?.subscribe(topic);
    
    return () => {
      subscribersRef.current.delete(topic);
      clientRef.current?.unsubscribe(topic);
    };
  }, []);
  
  return { publish, subscribe, client: clientRef.current };
};
```

---

## Part B: Mechanical Rig Specifications

### Hopper Specifications

| Parameter | Specification | Rationale |
|-----------|---------------|-----------|
| **Material** | Food-grade stainless steel (304/316) or powder-coated mild steel | Corrosion resistance, food safety; avoid galvanized (zinc risk) |
| **Shape** | Rectangular with sloped bottom (35–45° angle) | Prevents bridging/arching of pellets; gravity-fed |
| **Capacity** | 5–10 kg (estimate: 2× daily feed requirement) | Safe margin; reduces refill frequency |
| **Access Port** | Top-mounted hinged lid (12" × 12" minimum) | Easy refilling and inspection |
| **Outlet Diameter** | Match pipe diameter (3" ID recommended) | Consistent flow; no restrictions |
| **Wall Thickness** | 1.5–2.5 mm (14–16 gauge steel) | Adequate strength; minimize weight |
| **Supports** | Welded angles or brackets to frame | Rigid mounting; no vibration |

**Hopper Capacity Calculation (Example: 50 birds, Peak Production):**
```
Daily feed: 50 birds × 110g/day = 5,500g (5.5 kg)
Per cycle: 5.5 kg ÷ 2 cycles = 2.75 kg
Hopper capacity: 7.5–10 kg (3–4 cycles between refills)
```

**Hopper Dimensions (50-bird system, 10 kg capacity):**
- Width: 16 inches (406 mm)
- Depth: 12 inches (305 mm)
- Height: 20 inches (508 mm) total; sloped bottom
- Bottom slope angle: 40°
- Outlet hole: 3" diameter (76 mm)
- Wall thickness: 2 mm (14 gauge mild steel)

---

### Elevation & Support Frame

| Parameter | Specification | Rationale |
|-----------|---------------|-----------|
| **Mount Height** | **28–36 inches (71–91 cm) above trough top** | Optimal gravity flow; prevents birds from reaching hopper |
| **Frame Material** | Structural steel tubing (1.5" × 1.5" × 0.12") or wood (4×4 posts) | Strength-to-weight ratio; easy fabrication |
| **Frame Dimensions** | 24" W × 24" D × 40" H | Adequate space for hopper + wiper motor rig |
| **Stability** | Diagonal bracing + weight distribution | Prevents tipping when hopper is full (5–10 kg) |
| **Vibration Isolation** | Rubber isolation pads under frame feet | Reduces motor vibration transmission to cage |

**Load Calculation:**
```
Hopper weight (empty): ~15 kg (stainless) or ~10 kg (mild steel)
Feed weight (full): 10 kg
Wiper motor rig: ~5 kg
Total load: ~30 kg
Frame safety factor: 3× (design for 90 kg)
```

**Frame Assembly Dimensions:**
- Vertical posts: 4 pieces, 1.5" × 1.5" × 40" height
- Top beam: 1.5" × 1.5" × 24" (front-to-back)
- Side beams: 1.5" × 1.5" × 24" (left-to-right)
- Diagonal bracing: 1" × 1" angle iron, all four corners
- Welded joints throughout; hardware bolts only for disassembly points

---

### Pipe Specifications for Gravity Feed

| Parameter | Specification | Rationale |
|-----------|---------------|-----------|
| **Pipe Diameter** | **2.5–3 inches (64–76 mm) ID** | Allow pellets to flow without jamming; minimal pressure drop |
| **Pipe Material** | PVC Schedule 40 (non-food) or galvanized steel | Lightweight, affordable, durable; avoid aluminum (oxidation) |
| **Pipe Length** | ~24–36 inches from hopper outlet to trough | Balance gravity flow; prevent settling |
| **Wall Thickness** | 0.113" (Schedule 40 PVC) | Standard; adequate rigidity |
| **Joint Type** | Slip-fit couplings or welded (steel) | Allow easy disassembly for cleaning |
| **Slope** | Vertical or slight 5–10° forward angle | Promote gravity flow; prevent bridging |
| **Interior Surface** | Smooth; avoid sharp bends | Reduce friction; improve flow rate |

**Flow Rate Physics:**

Using gravity flow formula:
$$q = C_d A \sqrt{2g(h_1 - h_2)}$$

Where:
- $q$ = volumetric flow rate (m³/s)
- $C_d$ = discharge coefficient (~0.61 for smooth pipe)
- $A$ = pipe cross-sectional area (m²)
- $g$ = 9.81 m/s²
- $h_1$ = hopper height above trough (m)
- $h_2$ = trough height (reference level)

**Example Calculation (3" PVC, 30" elevation):**
```
A = π(0.038)² = 0.00454 m²
Δh = 0.76 m (30 inches)
q = 0.61 × 0.00454 × √(2 × 9.81 × 0.76)
q = 0.00278 × 3.86 ≈ 0.0107 m³/s
Flow rate: ~10.7 liters/minute or 170 grams/second (for 15.8 g/L pellet density)
```

**Note:** Actual flow depends on pellet size, density, and moisture. **Test empirically with your feed type.**

**Pipe Dimensions (3" Schedule 40 PVC):**
- Outside diameter: 3.5 inches (88.9 mm)
- Inside diameter: 3.068 inches (77.9 mm)
- Wall thickness: 0.216 inches (5.5 mm)
- Weight: 0.79 lb/ft (1.18 kg/m)

---

### Flow Control Gates (Optional but Recommended)

| Component | Specification | Purpose |
|-----------|---------------|---------|
| **Gate Type** | Butterfly valve (3" mount) or slide gate | Adjustable flow control; manual override |
| **Actuation** | Manual (lever) or servo-driven (optional) | Fine-grain portion control |
| **Position** | Immediately below hopper outlet | Regulate flow before trough entry |
| **Material** | Stainless steel disc + seat | Food-safe; durable |
| **Control** | Lever arm (0–90° rotation) | Fully open = max flow; 45° = half flow |

**Butterfly Valve Specs (3" bore):**
- Type: Wafer or lug body
- Pressure rating: 250 PSI minimum
- Flow range: 0–100%
- Cost estimate: $25–50 for manual, $60–120 for motorized

---

### Feed Delivery to Two Layers

| Configuration | Pros | Cons |
|---------------|------|------|
| **Single pipe with Y-splitter** | Simple; uses one motor control | Both layers feed simultaneously; less control |
| **Dual pipes (separate outlets)** | Independent per-layer control; fine-grain dosing | More complex plumbing; requires servo gates |
| **Horizontal chute with side-to-side sweep** | Even distribution; uses existing wiper motor rig | Requires precise timing calibration |

**Recommended: Horizontal chute with side-to-side sweep** (matches your current design)

**Implementation:**
- Single 3" pipe from hopper descends vertically
- At trough height, pipe connects to 12–16" wide horizontal chute
- Chute has open bottom (3" wide slot) allowing pellets to fall into trough below
- Wiper motor rig slides chute side-to-side (left to right, return)
- Pellets distribute evenly across trough width with 3–5 passes per cycle

---

### Gantry Drive Motor & Motion Control

| Parameter | Specification | Rationale |
|-----------|---------------|-----------|
| **Motor Type** | Geared DC Motor (12V/24V) | Drives the hopper cart along the floor rails |
| **Motor Driver** | **BTS7960 43A High Power** | Handles start/stall currents of moving a heavy hopper |
| **Motion Profile** | Pure Time-Based | Clockwise traverse (10s), Anticlockwise return (10s), Stop |
| **Drive Mechanics** | Drive wheel (friction) on rail or cable/pulley loop | Provide linear motion to vertical hopper pillar |
| **Structure** | Full Traveling Hopper | Entire vertical hopper tower rests on parallel floor rails |

**Motion Sequence Timing (21-second complete cycle):**

```
├─ 0s: Motor Clockwise (Traverse Outwards)
├─ 10s: Stop (1-second dwell/brake)
├─ 11s: Motor Anti-Clockwise (Traverse Return)
└─ 21s: Stop (Return to idle)
```

**GPIO Pin Configuration (Recommended):**
```
Waste Motor:        GPIO 26 (Relay)
Egg Motor L:        GPIO 12 (Forward)
Egg Motor R:        GPIO 13 (Reverse)
Feed Motor LPWM:    GPIO 14 (BTS7960 Forward)
Feed Motor RPWM:    GPIO 27 (BTS7960 Reverse)
Feed Motor EN:      GPIO 15 (BTS7960 ENABLE)
Hopper Servo 1:     GPIO 4  (PWM control mechanism)
Hopper Servo 2:     GPIO 5  (PWM control mechanism)
RTC SDA:            GPIO 8  (I2C to DS3231)
RTC SCL:            GPIO 9  (I2C to DS3231)
IR Sensor L1:       GPIO 36 (Digital input w/ interrupt)
IR Sensor L2:       GPIO 39 (Digital input w/ interrupt)
```

---

### Structural Assembly & Materials List

**For 50-bird system (estimated costs USD):**

| Material | Quantity | Size | Cost Est. | Notes |
|----------|----------|------|-----------|-------|
| **Structural Steel Tubing** | 16 ft | 1.5"×1.5" | $80–120 | Frame posts and crossbeams |
| **Welded Angle (Bracing)** | 8 ft | 1"×1" | $20–30 | Diagonal supports |
| **Stainless Steel Hopper** | 1 | Custom | $150–300 | Or mild steel + coating ($80–150) |
| **PVC Pipe (Schedule 40)** | 3 ft | 3" ID | $25–40 | Main feed delivery |
| **PVC Couplings** | 2 | 3" slip-fit | $10–15 | Joint connectors |
| **Butterfly Valve** | 1 | 3" | $20–50 | Flow control (optional) |
| **Wiper Motor (12V)** | 2 | Standard | $30–60 | Used automotive or new |
| **Motor Relay Module** | 2 | 12V, 30A | $10–20 | Switching control |
| **Hardware** | 1 set | Bolts, nuts, washers | $15–25 | Assembly fasteners |
| **Paint/Coating** | 1 | Food-safe epoxy | $30–50 | Corrosion protection |
| **TOTAL** | | | **$410–765** | Excluding electronics & labor |

---

## Further Considerations & Refinements

### 1. Pellet Specifications & Flow Testing

- **What pellet size** (2mm, 3mm, 4mm diameter)?
- **Feed density** (g/cm³)? Affects flow rate and portion calculations.
- **Moisture content** (%)? Affects bridging tendency in hopper/pipe.

**Recommended:** Test flow rate with actual pellets before finalizing pipe diameter.

**Flow Test Procedure:**
1. Fill hopper with 5 kg of pellets
2. Open flow gate fully
3. Measure grams dispensed per second for 10 seconds
4. Calculate empirical flow rate (g/s)
5. Adjust cycle duration (in firmware) to hit target grams per feeding

---

### 2. Feeding Precision Options

**Time-Based (±10% accuracy):**
- Motor runtime = calibrated grams/second × target grams
- Pros: Simple, no sensors needed (other than validation)
- Cons: Varies with pellet moisture, temperature

**Weight-Based Verification (Optional Future Scope):**
- Can be added via HX711 + load cells to empirically measure trough weight.
- Current plan operates solely on time-based motor calibration.

**Hybrid (Recommended):**
- Time-based primary control + weight-based verification
- Firmware adjusts future cycle duration if actual ≠ target

---

### 3. Temperature & Corrosion Protection

**Outdoor Installation:**
- Use stainless steel hopper (304 or 316)
- Galvanized steel frame (avoid aluminum)
- Sealed connectors for electrical (IP67 minimum)

**Indoor Installation:**
- Mild steel + food-safe epoxy coating acceptable
- Paint hopper with non-toxic enamel
- Standard connectors (IP54 acceptable)

---

### 4. Maintenance & Access

**Design for Cleaning:**
- Removable hopper lid (hinged or bolted)
- Cleanout ports on pipe every 12 inches (optional)
- Trough access without removing frame

**Design for Adjustment:**
- Butterfly valve position (flow rate tuning)
- Motor speed/timing (via firmware MQTT commands)

---

### 5. Failsafe Mechanisms

**Mechanical Failsafes:**
- Gravity return: If motors fail, hopper descends or pipe remains open (safe state)
- Emergency stop button: Manual cutoff for all motors

**Firmware Failsafes:**
- 30–60 second cycle timeout (stop if motor doesn't complete)
- Weight sensor alarm: If actual < 80% of target, log as "failed" and retry
- MQTT timeout: If broker unreachable >10 minutes, switch to local-only mode
- Offline storage: Queue logs locally, sync when online

---

### 6. Expansion & Scalability

**For 100+ birds:**
- Increase hopper capacity to 15–20 kg
- Use larger pipe (3.5–4" ID)
- Dual chutes (one per layer) with independent motors

**For 200+ birds:**
- Separate feed system per layer
- Dedicated motors/relays per layer
- Proportional flow valves (servo-controlled) for fine-tuning

