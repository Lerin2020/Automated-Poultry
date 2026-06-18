#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "RTClib.h"
#include <PicoMQTT.h>

extern RTC_DS3231 rtc;
extern PicoMQTT::Server mqtt;

// ─── Mutable schedule variables (loaded from filesystem, updated via MQTT) ───
// Each is up to MAX_SCHED_SLOTS slots of minute-of-day (0–1439); -1 = unused.
int schedFeed[MAX_SCHED_SLOTS]  = {DEFAULT_FEED_SLOTS[0],  DEFAULT_FEED_SLOTS[1],  DEFAULT_FEED_SLOTS[2]};
int schedEgg[MAX_SCHED_SLOTS]   = {DEFAULT_EGG_SLOTS[0],   DEFAULT_EGG_SLOTS[1],   DEFAULT_EGG_SLOTS[2]};
int schedWaste[MAX_SCHED_SLOTS] = {DEFAULT_WASTE_SLOTS[0], DEFAULT_WASTE_SLOTS[1], DEFAULT_WASTE_SLOTS[2]};
int eggThreshold       = DEFAULT_EGG_THRESHOLD;

// ── Schedule slot helpers ──
// Config file stores compact minute-of-day ints; MQTT uses [hour,minute] pairs
// for dashboard friendliness. These convert between the two and the internal
// arrays. Unused slots (-1) are skipped on the way out.
inline void slotsToMinutesJson(JsonArray arr, const int* slots) {
  for (int i = 0; i < MAX_SCHED_SLOTS; i++) if (slots[i] >= 0) arr.add(slots[i]);
}
inline void minutesJsonToSlots(JsonArrayConst arr, int* slots) {
  for (int i = 0; i < MAX_SCHED_SLOTS; i++) slots[i] = -1;
  int idx = 0;
  for (JsonVariantConst v : arr)
    if (idx < MAX_SCHED_SLOTS && v.is<int>()) slots[idx++] = constrain(v.as<int>(), 0, 1439);
}
inline void slotsToPairsJson(JsonArray arr, const int* slots) {
  for (int i = 0; i < MAX_SCHED_SLOTS; i++) if (slots[i] >= 0) {
    JsonArray s = arr.add<JsonArray>();
    s.add(slots[i] / 60);
    s.add(slots[i] % 60);
  }
}
inline void pairsJsonToSlots(JsonArrayConst arr, int* slots) {
  for (int i = 0; i < MAX_SCHED_SLOTS; i++) slots[i] = -1;
  int idx = 0;
  for (JsonVariantConst v : arr) {
    if (idx >= MAX_SCHED_SLOTS) break;
    if (v.is<JsonArrayConst>() && v.as<JsonArrayConst>().size() == 2) {
      int h = constrain(v[0].as<int>(), 0, 23);
      int m = constrain(v[1].as<int>(), 0, 59);
      slots[idx++] = h * 60 + m;
    }
  }
}
// True if any active slot equals the current hour:minute.
inline bool slotMatches(const int* slots, int hour, int minute) {
  int mod = hour * 60 + minute;
  for (int i = 0; i < MAX_SCHED_SLOTS; i++) if (slots[i] >= 0 && slots[i] == mod) return true;
  return false;
}

// ─── Mutable cycle timing variables (editable from dashboard) ───
unsigned long feedDistributeDuration = DEFAULT_FEED_DISTRIBUTE_DURATION;
unsigned long feedPauseDuration      = DEFAULT_FEED_PAUSE_DURATION;
unsigned long feedReverseDuration    = DEFAULT_FEED_REVERSE_DURATION;
unsigned long eggCollectDuration     = DEFAULT_EGG_COLLECT_DURATION;
unsigned long wasteCycleDuration     = DEFAULT_WASTE_CYCLE_DURATION;

// ─── Gantry speed + auger behaviour (editable from dashboard) ───
int feedSpeedPct       = DEFAULT_FEED_SPEED;        // gantry forward duty %
int feedReturnSpeedPct = DEFAULT_FEED_RETURN_SPEED; // gantry reverse duty %
int augerMode          = DEFAULT_AUGER_MODE;        // see AUGER_* in config.h

void initStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully");
  
  if(!LittleFS.exists(QUEUE_FILE)){
    File file = LittleFS.open(QUEUE_FILE, FILE_WRITE);
    if(file){
      file.print("[]");
      file.close();
    }
  }
}

// ─── Offline Message Queue ───

void logOfflineMessage(String topic, String payload) {
  Serial.println("[STORAGE] Queuing offline message...");
  
  File file = LittleFS.open(QUEUE_FILE, FILE_READ);
  if (!file) return;

  size_t size = file.size();
  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);
  file.close();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf.get(), size);
  if (error) {
    Serial.println("Failed to parse queue file");
    return;
  }

  JsonArray q = doc.as<JsonArray>();
  
  if(q.size() >= MAX_QUEUE_SIZE) {
    q.remove(0);
  }

  JsonObject newMsg = q.add<JsonObject>();
  newMsg["timestamp"] = rtc.now().unixtime();
  newMsg["topic"] = topic;
  newMsg["payload"] = payload;

  file = LittleFS.open(QUEUE_FILE, FILE_WRITE);
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
}

void syncOfflineQueue() {
  File file = LittleFS.open(QUEUE_FILE, FILE_READ);
  if (!file) return;

  size_t size = file.size();
  if(size < 5) {
     file.close();
     return;
  }

  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);
  file.close();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf.get(), size);

  if (!error) {
    JsonArray q = doc.as<JsonArray>();
    
    for (JsonObject v : q) {
      String topic = v["topic"].as<String>();
      String payload = v["payload"].as<String>();
      
      Serial.println("[SYNC] Dispatching queued event: " + topic);
      mqtt.publish(topic, payload);
      delay(10);
    }

    // Clear after sync
    file = LittleFS.open(QUEUE_FILE, FILE_WRITE);
    if(file){
      file.print("[]");
      file.close();
      Serial.println("[SYNC] Offline queue cleared.");
    }
  }
}

// ─── Queue Helpers ───

int getQueueSize() {
  File file = LittleFS.open(QUEUE_FILE, FILE_READ);
  if (!file) return 0;

  size_t size = file.size();
  if (size < 5) { file.close(); return 0; }

  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);
  file.close();

  JsonDocument doc;
  if (deserializeJson(doc, buf.get(), size)) return 0;
  return doc.as<JsonArray>().size();
}

void clearOfflineQueue() {
  File file = LittleFS.open(QUEUE_FILE, FILE_WRITE);
  if (file) {
    file.print("[]");
    file.close();
    Serial.println("[STORAGE] Offline queue manually cleared.");
  }
}

// ─── Config Persistence (schedules + timings) ───

void saveScheduleConfig() {
  JsonDocument doc;
  // Schedules stored as compact minute-of-day arrays (active slots only)
  slotsToMinutesJson(doc["feed"].to<JsonArray>(),  schedFeed);
  slotsToMinutesJson(doc["egg"].to<JsonArray>(),   schedEgg);
  slotsToMinutesJson(doc["waste"].to<JsonArray>(), schedWaste);

  doc["egg_threshold"] = eggThreshold;

  // Cycle timings (stored in milliseconds)
  doc["feed_distribute"] = feedDistributeDuration;
  doc["feed_pause"]      = feedPauseDuration;
  doc["feed_reverse"]    = feedReverseDuration;
  doc["egg_collect"]     = eggCollectDuration;
  doc["waste_cycle"]     = wasteCycleDuration;

  // Gantry speed + auger behaviour
  doc["feed_speed"]        = feedSpeedPct;
  doc["feed_return_speed"] = feedReturnSpeedPct;
  doc["auger_mode"]        = augerMode;

  File file = LittleFS.open(CONFIG_FILE, FILE_WRITE);
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.println("[CONFIG] Config saved to LittleFS.");
  }
}

void loadScheduleConfig() {
  if (!LittleFS.exists(CONFIG_FILE)) {
    Serial.println("[CONFIG] No config file found, using defaults.");
    return;
  }

  File file = LittleFS.open(CONFIG_FILE, FILE_READ);
  if (!file) return;

  size_t size = file.size();
  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);
  file.close();

  JsonDocument doc;
  if (deserializeJson(doc, buf.get(), size)) {
    Serial.println("[CONFIG] Failed to parse config file.");
    return;
  }

  if (doc["feed"].is<JsonArray>())  minutesJsonToSlots(doc["feed"].as<JsonArrayConst>(),  schedFeed);
  if (doc["egg"].is<JsonArray>())   minutesJsonToSlots(doc["egg"].as<JsonArrayConst>(),   schedEgg);
  if (doc["waste"].is<JsonArray>()) minutesJsonToSlots(doc["waste"].as<JsonArrayConst>(), schedWaste);
  if (doc["egg_threshold"].is<int>()) {
    eggThreshold = doc["egg_threshold"].as<int>();
  }

  // Load cycle timings
  if (doc["feed_distribute"].is<unsigned long>()) feedDistributeDuration = doc["feed_distribute"].as<unsigned long>();
  if (doc["feed_pause"].is<unsigned long>())      feedPauseDuration      = doc["feed_pause"].as<unsigned long>();
  if (doc["feed_reverse"].is<unsigned long>())    feedReverseDuration    = doc["feed_reverse"].as<unsigned long>();
  if (doc["egg_collect"].is<unsigned long>())     eggCollectDuration     = doc["egg_collect"].as<unsigned long>();
  if (doc["waste_cycle"].is<unsigned long>())     wasteCycleDuration     = doc["waste_cycle"].as<unsigned long>();

  // Gantry speed + auger behaviour
  if (doc["feed_speed"].is<int>())        feedSpeedPct       = constrain(doc["feed_speed"].as<int>(), 0, 100);
  if (doc["feed_return_speed"].is<int>()) feedReturnSpeedPct = constrain(doc["feed_return_speed"].as<int>(), 0, 100);
  if (doc["auger_mode"].is<int>())        augerMode          = constrain(doc["auger_mode"].as<int>(), 0, 3);

  Serial.printf("[CONFIG] Loaded slots(min-of-day): Feed[%d,%d,%d] Egg[%d,%d,%d] Waste[%d,%d,%d] Threshold=%d\n",
    schedFeed[0], schedFeed[1], schedFeed[2],
    schedEgg[0], schedEgg[1], schedEgg[2],
    schedWaste[0], schedWaste[1], schedWaste[2],
    eggThreshold);
  Serial.printf("[CONFIG] Timings(ms): FeedDist=%lu FeedPause=%lu FeedRev=%lu EggCol=%lu Waste=%lu\n",
    feedDistributeDuration, feedPauseDuration, feedReverseDuration, eggCollectDuration, wasteCycleDuration);
}

// ─── Activity Log ───

#define LOG_FILE  "/activity_log.json"
const int MAX_LOG_ENTRIES = 200;

void logActivity(const char* subsystem, const char* event, const char* detail = "") {
  // Read existing log
  JsonDocument doc;
  File file = LittleFS.open(LOG_FILE, FILE_READ);
  if (file && file.size() > 4) {
    size_t sz = file.size();
    std::unique_ptr<char[]> buf(new char[sz]);
    file.readBytes(buf.get(), sz);
    file.close();
    if (deserializeJson(doc, buf.get(), sz)) doc.to<JsonArray>();
  } else {
    if (file) file.close();
    doc.to<JsonArray>();
  }

  JsonArray arr = doc.as<JsonArray>();
  while ((int)arr.size() >= MAX_LOG_ENTRIES) arr.remove(0);

  DateTime now = rtc.now();
  char ts[25];
  snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d",
    now.year(), now.month(), now.day(),
    now.hour(), now.minute(), now.second());

  JsonObject entry = arr.add<JsonObject>();
  entry["ts"]  = ts;
  entry["sub"] = subsystem;
  entry["evt"] = event;
  if (strlen(detail) > 0) entry["det"] = detail;

  file = LittleFS.open(LOG_FILE, FILE_WRITE);
  if (file) { serializeJson(doc, file); file.close(); }
}

// ─── Last-run epoch persistence ───
// Saves/loads the three "last ran at" timestamps so a reboot mid-hour
// doesn't re-trigger a cycle that already ran before the reset.

#define EPOCH_FILE "/epochs.json"

extern unsigned long lastFeedEpoch;
extern unsigned long lastEggEpoch;
extern unsigned long lastWasteEpoch;

void saveLastRunEpochs() {
  JsonDocument doc;
  doc["feed"]  = lastFeedEpoch;
  doc["egg"]   = lastEggEpoch;
  doc["waste"] = lastWasteEpoch;
  File file = LittleFS.open(EPOCH_FILE, FILE_WRITE);
  if (file) { serializeJson(doc, file); file.close(); }
}

void loadLastRunEpochs(unsigned long bootEpoch) {
  if (!LittleFS.exists(EPOCH_FILE)) return;
  File file = LittleFS.open(EPOCH_FILE, FILE_READ);
  if (!file) return;
  size_t size = file.size();
  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);
  file.close();
  JsonDocument doc;
  if (deserializeJson(doc, buf.get(), size)) return;
  // Only restore if the stored epoch is more recent than boot — if the
  // clock was reset or the file is stale, fall back to bootEpoch.
  if (doc["feed"].is<unsigned long>()  && doc["feed"].as<unsigned long>()  > bootEpoch - 86400)
    lastFeedEpoch  = doc["feed"].as<unsigned long>();
  if (doc["egg"].is<unsigned long>()   && doc["egg"].as<unsigned long>()   > bootEpoch - 86400)
    lastEggEpoch   = doc["egg"].as<unsigned long>();
  if (doc["waste"].is<unsigned long>() && doc["waste"].as<unsigned long>() > bootEpoch - 86400)
    lastWasteEpoch = doc["waste"].as<unsigned long>();
  Serial.printf("[EPOCHS] Restored: Feed=%lu Egg=%lu Waste=%lu\n",
    lastFeedEpoch, lastEggEpoch, lastWasteEpoch);
}

void publishCurrentConfig() {
  JsonDocument doc;
  
  // Schedules sent as arrays of [hour, minute] pairs (active slots only)
  slotsToPairsJson(doc["feed"].to<JsonArray>(),  schedFeed);
  slotsToPairsJson(doc["egg"].to<JsonArray>(),   schedEgg);
  slotsToPairsJson(doc["waste"].to<JsonArray>(), schedWaste);
  doc["egg_threshold"] = eggThreshold;

  // Send timings in SECONDS for dashboard display
  doc["feed_distribute"] = feedDistributeDuration / 1000;
  doc["feed_pause"]      = feedPauseDuration / 1000;
  doc["feed_reverse"]    = feedReverseDuration / 1000;
  doc["egg_collect"]     = eggCollectDuration / 1000;
  doc["waste_cycle"]     = wasteCycleDuration / 1000;

  // Gantry speed + auger behaviour (sent as-is)
  doc["feed_speed"]        = feedSpeedPct;
  doc["feed_return_speed"] = feedReturnSpeedPct;
  doc["auger_mode"]        = augerMode;

  String payload;
  serializeJson(doc, payload);
  mqtt.publish(TOPIC_CONFIG_STAT, payload);
  Serial.println("[CONFIG] Published current config.");
}
