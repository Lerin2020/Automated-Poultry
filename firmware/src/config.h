#pragma once
#include <Arduino.h>

// ==========================================
// GPIO PIN CONFIGURATION (ESP32-S3)
// ==========================================

// Gantry Feed System
#define PIN_FEED_LPWM    14   // BTS7960 Forward
#define PIN_FEED_RPWM    16   // BTS7960 Reverse
#define PIN_FEED_EN      15   // BTS7960 Enable
#define PIN_AUGER_RELAY  4    // Auger Motor Relay (feed flow control)

// Egg Collection System
#define PIN_EGG_MOTOR_L  12   // Layer 1 Belt Relay
#define PIN_EGG_MOTOR_R  13   // Layer 2 Belt Relay
#define PIN_EGG_IR1      47   // NPN Proximity Sensor Layer 1
#define PIN_EGG_IR2      5   // NPN Proximity Sensor Layer 2

// Waste Management
#define PIN_WASTE_RELAY  21   // Waste Conveyor Relay

// DHT22 Temperature & Humidity Sensor
#define PIN_DHT22        6    // Data pin (10kΩ pull-up to 3.3V)

// RTC DS3231
#define PIN_I2C_SDA      8    // RTC SDA
#define PIN_I2C_SCL      9    // RTC SCL

// ==========================================
// NETWORK CONFIGURATION
// ==========================================
#include "secrets.h"   // defines WIFI_SSID / WIFI_PASS — not committed to git

// Local MQTT Broker (PicoMQTT on this ESP32)
const int MQTT_PORT = 1883;         // Standard MQTT for other devices
const int WEBSOCKET_PORT = 81;      // WebSocket for browser dashboard
const char* MDNS_HOSTNAME = "poultry";  // Reachable at poultry.local

// MQTT Topics — Operations
#define TOPIC_FEED_CMD    "poultry/feed/cmd"
#define TOPIC_FEED_STAT   "poultry/feed/status"

#define TOPIC_EGG_CMD     "poultry/egg/cmd"
#define TOPIC_EGG_STAT    "poultry/egg/status"
#define TOPIC_EGG_DATA    "poultry/egg/data"

#define TOPIC_WASTE_CMD   "poultry/waste/cmd"
#define TOPIC_WASTE_STAT  "poultry/waste/status"

// MQTT Topics — System & Admin
#define TOPIC_SYSTEM_STAT "poultry/system/status"
#define TOPIC_ALERTS      "poultry/alerts"
#define TOPIC_CONFIG_CMD  "poultry/config/cmd"
#define TOPIC_CONFIG_STAT "poultry/config/status"

// ==========================================
// THRESHOLDS & SETTINGS
// ==========================================
const unsigned long GRACE_PERIOD_SEC = 600;      // 10 mins anti-retrigger buffer (compared against unixtime deltas)

// ==========================================
// CYCLE TIMING DEFAULTS (milliseconds)
// Runtime values live in storage.h — editable from dashboard
// ==========================================

// Feeding Cycle (auger + gantry run together during distribute)
const unsigned long DEFAULT_FEED_DISTRIBUTE_DURATION = 10000;  // Auger ON + Gantry forward (10s)
const unsigned long DEFAULT_FEED_PAUSE_DURATION      = 1000;   // Pause at far end before reversing (1s)
const unsigned long DEFAULT_FEED_REVERSE_DURATION    = 10000;  // Gantry returns home (10s)

// Egg Collection
const unsigned long DEFAULT_EGG_COLLECT_DURATION  = 10000;  // How long belts run (10s)
const unsigned long EGG_PUBLISH_INTERVAL          = 1000;   // Live count publish rate (1s) — not user-editable

// Waste Cycle
const unsigned long DEFAULT_WASTE_CYCLE_DURATION  = 8000;   // How long waste conveyor runs (8s)

// System
const unsigned long HEARTBEAT_INTERVAL_SEC = 30;    // Seconds between heartbeats

// Default schedules (can be overridden via MQTT and persisted to SPIFFS)
const int DEFAULT_FEED_HOURS[] = {7, 17};
const int DEFAULT_EGG_HOURS[]  = {8, 20};
const int DEFAULT_WASTE_HOURS[] = {6, 18};
const int DEFAULT_EGG_THRESHOLD = 50;

const float TEMP_DANGER = 40.0;   // Celsius — triggers danger alert

const unsigned long EGG_DEBOUNCE_MS = 200;

// Storage
#define QUEUE_FILE  "/offline_queue.json"
#define CONFIG_FILE "/config.json"
const int MAX_QUEUE_SIZE = 50;
