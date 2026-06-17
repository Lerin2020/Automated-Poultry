#pragma once
#include "config.h"
#include <DHT.h>
#include <PicoMQTT.h>

extern PicoMQTT::Server mqtt;
extern void publishMessage(const char* topic, const String& payload);
extern void logActivity(const char* subsystem, const char* event, const char* detail);

// ─── Timing variables (defined in storage.h) ───
extern unsigned long feedDistributeDuration;
extern unsigned long feedPauseDuration;
extern unsigned long feedReverseDuration;
extern unsigned long eggCollectDuration;
extern unsigned long wasteCycleDuration;

// DHT22 Sensor
DHT dht(PIN_DHT22, DHT22);
float lastTemp = 0.0;
float lastHumidity = 0.0;
bool dhtValid = false;

volatile int eggCount_L1 = 0;
volatile int eggCount_L2 = 0;

volatile unsigned long lastTrigger_L1 = 0;
volatile unsigned long lastTrigger_L2 = 0;

// Stop flags — set from MQTT callbacks
volatile bool feedStopRequested = false;
volatile bool eggStopRequested  = false;
volatile bool wasteStopRequested = false;

void IRAM_ATTR countEgg_L1() {
  // Reject EMI spikes: a real egg holds the NPN output LOW for many ms, so the
  // line must still read LOW when the ISR runs. A transient glitch will have
  // already bounced back HIGH and is ignored.
  if (digitalRead(PIN_EGG_IR1) != LOW) return;
  unsigned long now = millis();
  if (now - lastTrigger_L1 > EGG_DEBOUNCE_MS) {
    eggCount_L1++;
    lastTrigger_L1 = now;
  }
}

void IRAM_ATTR countEgg_L2() {
  if (digitalRead(PIN_EGG_IR2) != LOW) return;
  unsigned long now = millis();
  if (now - lastTrigger_L2 > EGG_DEBOUNCE_MS) {
    eggCount_L2++;
    lastTrigger_L2 = now;
  }
}

void initializeHardware() {
  // BTS7960 gantry motor — all LOW = coasting/off
  pinMode(PIN_FEED_LPWM, OUTPUT);  digitalWrite(PIN_FEED_LPWM, LOW);
  pinMode(PIN_FEED_RPWM, OUTPUT);  digitalWrite(PIN_FEED_RPWM, LOW);
  pinMode(PIN_FEED_EN,   OUTPUT);  digitalWrite(PIN_FEED_EN,   LOW);

  // Relays (active-LOW module): pre-load the output latch to RELAY_OFF
  // BEFORE switching to OUTPUT so the pin never momentarily drives the
  // relay on. digitalWrite-before-pinMode sets the latch; pinMode then
  // drives that level immediately.
  digitalWrite(PIN_AUGER_RELAY, RELAY_OFF); pinMode(PIN_AUGER_RELAY, OUTPUT); digitalWrite(PIN_AUGER_RELAY, RELAY_OFF);
  digitalWrite(PIN_EGG_MOTOR_L, RELAY_OFF); pinMode(PIN_EGG_MOTOR_L, OUTPUT); digitalWrite(PIN_EGG_MOTOR_L, RELAY_OFF);
  digitalWrite(PIN_EGG_MOTOR_R, RELAY_OFF); pinMode(PIN_EGG_MOTOR_R, OUTPUT); digitalWrite(PIN_EGG_MOTOR_R, RELAY_OFF);
  digitalWrite(PIN_WASTE_RELAY, RELAY_OFF); pinMode(PIN_WASTE_RELAY, OUTPUT); digitalWrite(PIN_WASTE_RELAY, RELAY_OFF);

  // Egg IR sensors
  pinMode(PIN_EGG_IR1, INPUT_PULLUP);
  pinMode(PIN_EGG_IR2, INPUT_PULLUP);

  dht.begin();
  Serial.println("[HW] All outputs initialised LOW. DHT22 on GPIO " + String(PIN_DHT22));
}

void readDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  if (isnan(h) || isnan(t)) {
    dhtValid = false;
    Serial.println("[DHT] Read failed.");
    return;
  }
  
  lastTemp = t;
  lastHumidity = h;
  dhtValid = true;
}

// =========================================================
//  NON-BLOCKING FEED CYCLE (state machine)
//  Auger + Gantry run SIMULTANEOUSLY during distribute
// =========================================================
enum FeedState { FEED_IDLE, FEED_DISTRIBUTING, FEED_PAUSE, FEED_REVERSE };
FeedState feedState = FEED_IDLE;
unsigned long feedStepStart = 0;

void haltFeedHardware() {
  digitalWrite(PIN_AUGER_RELAY, RELAY_OFF);
  digitalWrite(PIN_FEED_EN, LOW);
  digitalWrite(PIN_FEED_LPWM, LOW);
  digitalWrite(PIN_FEED_RPWM, LOW);
}

void startFeedingCycle() {
  if (feedState != FEED_IDLE) {
    Serial.println("[FEED] Already running, ignoring start");
    return;
  }
  feedStopRequested = false;
  feedState = FEED_DISTRIBUTING;
  feedStepStart = millis();
  
  // Auger ON + Gantry forward — simultaneously
  digitalWrite(PIN_AUGER_RELAY, RELAY_ON);
  digitalWrite(PIN_FEED_LPWM, HIGH);
  digitalWrite(PIN_FEED_RPWM, LOW);
  digitalWrite(PIN_FEED_EN, HIGH);
  
  publishMessage(TOPIC_FEED_STAT, "{\"state\": \"distributing\"}");
  logActivity("feed", "started", "");
  Serial.println("[FEED] === DISTRIBUTING === Auger ON + Gantry FWD");
}

void updateFeedCycle() {
  if (feedState == FEED_IDLE) return;
  
  if (feedStopRequested) {
    feedStopRequested = false;
    haltFeedHardware();
    feedState = FEED_IDLE;
    publishMessage(TOPIC_FEED_STAT, "{\"state\": \"idle\"}");
    logActivity("feed", "stopped", "user");
    Serial.println("[FEED] STOPPED by user");
    return;
  }
  
  unsigned long elapsed = millis() - feedStepStart;
  
  switch (feedState) {
    case FEED_DISTRIBUTING:
      if (elapsed >= feedDistributeDuration) {
        // Stop auger + stop gantry
        digitalWrite(PIN_AUGER_RELAY, RELAY_OFF);
        digitalWrite(PIN_FEED_EN, LOW);
        digitalWrite(PIN_FEED_LPWM, LOW);
        feedState = FEED_PAUSE;
        feedStepStart = millis();
        publishMessage(TOPIC_FEED_STAT, "{\"state\": \"pausing\"}");
        Serial.println("[FEED] Pause at far end");
      }
      break;
      
    case FEED_PAUSE:
      if (elapsed >= feedPauseDuration) {
        // Gantry reverse — return home
        digitalWrite(PIN_FEED_LPWM, LOW);
        digitalWrite(PIN_FEED_RPWM, HIGH);
        digitalWrite(PIN_FEED_EN, HIGH);
        feedState = FEED_REVERSE;
        feedStepStart = millis();
        publishMessage(TOPIC_FEED_STAT, "{\"state\": \"returning\"}");
        Serial.println("[FEED] Gantry REVERSE");
      }
      break;
      
    case FEED_REVERSE:
      if (elapsed >= feedReverseDuration) {
        haltFeedHardware();
        feedState = FEED_IDLE;
        publishMessage(TOPIC_FEED_STAT, "{\"state\": \"idle\"}");
        logActivity("feed", "complete", "");
        Serial.println("[FEED] === CYCLE COMPLETE ===");
      }
      break;
      
    default:
      break;
  }
}

// =========================================================
//  NON-BLOCKING EGG COLLECTION (state machine)
// =========================================================
extern int eggThreshold;

enum EggState { EGG_IDLE, EGG_COLLECTING };
EggState eggState = EGG_IDLE;
unsigned long eggStepStart = 0;
unsigned long eggLastPublish = 0;

void startEggCollection() {
  if (eggState != EGG_IDLE) {
    Serial.println("[EGG] Already running, ignoring start");
    return;
  }
  eggStopRequested = false;
  eggCount_L1 = 0;
  eggCount_L2 = 0;
  
  attachInterrupt(digitalPinToInterrupt(PIN_EGG_IR1), countEgg_L1, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_EGG_IR2), countEgg_L2, FALLING);
  
  digitalWrite(PIN_EGG_MOTOR_L, RELAY_ON);
  digitalWrite(PIN_EGG_MOTOR_R, RELAY_ON);

  eggState = EGG_COLLECTING;
  eggStepStart = millis();
  eggLastPublish = 0;
  
  publishMessage(TOPIC_EGG_STAT, "{\"state\": \"collecting\", \"eggs_l1\": 0, \"eggs_l2\": 0, \"total\": 0}");
  logActivity("egg", "started", "");
  Serial.println("[EGG] === COLLECTION STARTED ===");
}

void finishEggCollection(bool userStopped = false) {
  digitalWrite(PIN_EGG_MOTOR_L, RELAY_OFF);
  digitalWrite(PIN_EGG_MOTOR_R, RELAY_OFF);

  detachInterrupt(digitalPinToInterrupt(PIN_EGG_IR1));
  detachInterrupt(digitalPinToInterrupt(PIN_EGG_IR2));

  // Read volatile counters with interrupts disabled for safety
  noInterrupts();
  int l1 = eggCount_L1;
  int l2 = eggCount_L2;
  interrupts();
  int totalEggs = l1 + l2;

  if (!userStopped && totalEggs >= eggThreshold) {
    publishMessage(TOPIC_ALERTS, "{\"type\": \"egg_threshold\", \"message\": \"Egg yield threshold reached!\"}");
  }

  String payload = "{\"eggs_l1\": " + String(l1) + ", \"eggs_l2\": " + String(l2) + ", \"total\": " + String(totalEggs) + "}";
  publishMessage(TOPIC_EGG_DATA, payload);
  publishMessage(TOPIC_EGG_STAT, "{\"state\": \"idle\", \"eggs_l1\": " + String(l1) + ", \"eggs_l2\": " + String(l2) + ", \"total\": " + String(totalEggs) + "}");

  eggState = EGG_IDLE;
  char eggDetail[24];
  snprintf(eggDetail, sizeof(eggDetail), "l1=%d l2=%d total=%d", l1, l2, totalEggs);
  if (!userStopped) logActivity("egg", "complete", eggDetail);
  Serial.println("[EGG] === COLLECTION " + String(userStopped ? "STOPPED" : "COMPLETE") + " === Total: " + String(totalEggs));
}

void updateEggCollection() {
  if (eggState == EGG_IDLE) return;
  
  unsigned long elapsed = millis() - eggStepStart;
  
  if (eggStopRequested || elapsed >= eggCollectDuration) {
    bool stopped = eggStopRequested;
    if (stopped) logActivity("egg", "stopped", "user");
    eggStopRequested = false;
    finishEggCollection(stopped);
    return;
  }
  
  if (elapsed - eggLastPublish >= EGG_PUBLISH_INTERVAL) {
    eggLastPublish = elapsed;
    noInterrupts();
    int l1 = eggCount_L1;
    int l2 = eggCount_L2;
    interrupts();
    int liveTotal = l1 + l2;
    String livePayload = "{\"eggs_l1\": " + String(l1) + ", \"eggs_l2\": " + String(l2) + ", \"total\": " + String(liveTotal) + "}";
    publishMessage(TOPIC_EGG_DATA, livePayload);
  }
}

// =========================================================
//  NON-BLOCKING WASTE CYCLE (state machine)
// =========================================================
enum WasteState { WASTE_IDLE, WASTE_ACTIVE };
WasteState wasteState = WASTE_IDLE;
unsigned long wasteStepStart = 0;

void startWasteCycle() {
  if (wasteState != WASTE_IDLE) {
    Serial.println("[WASTE] Already running, ignoring start");
    return;
  }
  wasteStopRequested = false;
  
  digitalWrite(PIN_WASTE_RELAY, RELAY_ON);
  wasteState = WASTE_ACTIVE;
  wasteStepStart = millis();
  
  publishMessage(TOPIC_WASTE_STAT, "{\"state\": \"active\"}");
  logActivity("waste", "started", "");
  Serial.println("[WASTE] === CYCLE STARTED ===");
}

void updateWasteCycle() {
  if (wasteState == WASTE_IDLE) return;
  
  unsigned long elapsed = millis() - wasteStepStart;
  
  if (wasteStopRequested || elapsed >= wasteCycleDuration) {
    if (wasteStopRequested) logActivity("waste", "stopped", "user");
    else                    logActivity("waste", "complete", "");
    wasteStopRequested = false;
    digitalWrite(PIN_WASTE_RELAY, RELAY_OFF);
    wasteState = WASTE_IDLE;
    publishMessage(TOPIC_WASTE_STAT, "{\"state\": \"idle\"}");
    Serial.println("[WASTE] === CYCLE COMPLETE ===");
  }
}
