#pragma once

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "RTClib.h"
#include <PicoMQTT.h>

extern RTC_DS3231 rtc;
extern PicoMQTT::Server mqtt;

// ─── Mutable schedule variables (loaded from SPIFFS, updated via MQTT) ───
int schedFeedHours[2]  = {DEFAULT_FEED_HOURS[0], DEFAULT_FEED_HOURS[1]};
int schedEggHours[2]   = {DEFAULT_EGG_HOURS[0], DEFAULT_EGG_HOURS[1]};
int schedWasteHours[2] = {DEFAULT_WASTE_HOURS[0], DEFAULT_WASTE_HOURS[1]};
int eggThreshold       = DEFAULT_EGG_THRESHOLD;

// ─── Mutable cycle timing variables (editable from dashboard) ───
unsigned long feedDistributeDuration = DEFAULT_FEED_DISTRIBUTE_DURATION;
unsigned long feedPauseDuration      = DEFAULT_FEED_PAUSE_DURATION;
unsigned long feedReverseDuration    = DEFAULT_FEED_REVERSE_DURATION;
unsigned long eggCollectDuration     = DEFAULT_EGG_COLLECT_DURATION;
unsigned long wasteCycleDuration     = DEFAULT_WASTE_CYCLE_DURATION;

void initStorage() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");
  
  if(!SPIFFS.exists(QUEUE_FILE)){
    File file = SPIFFS.open(QUEUE_FILE, FILE_WRITE);
    if(file){
      file.print("[]");
      file.close();
    }
  }
}

// ─── Offline Message Queue ───

void logOfflineMessage(String topic, String payload) {
  Serial.println("[STORAGE] Queuing offline message...");
  
  File file = SPIFFS.open(QUEUE_FILE, FILE_READ);
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

  file = SPIFFS.open(QUEUE_FILE, FILE_WRITE);
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
}

void syncOfflineQueue() {
  File file = SPIFFS.open(QUEUE_FILE, FILE_READ);
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
    file = SPIFFS.open(QUEUE_FILE, FILE_WRITE);
    if(file){
      file.print("[]");
      file.close();
      Serial.println("[SYNC] Offline queue cleared.");
    }
  }
}

// ─── Queue Helpers ───

int getQueueSize() {
  File file = SPIFFS.open(QUEUE_FILE, FILE_READ);
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
  File file = SPIFFS.open(QUEUE_FILE, FILE_WRITE);
  if (file) {
    file.print("[]");
    file.close();
    Serial.println("[STORAGE] Offline queue manually cleared.");
  }
}

// ─── Config Persistence (schedules + timings) ───

void saveScheduleConfig() {
  JsonDocument doc;
  JsonArray feed = doc["feed"].to<JsonArray>();
  feed.add(schedFeedHours[0]);
  feed.add(schedFeedHours[1]);

  JsonArray egg = doc["egg"].to<JsonArray>();
  egg.add(schedEggHours[0]);
  egg.add(schedEggHours[1]);

  JsonArray waste = doc["waste"].to<JsonArray>();
  waste.add(schedWasteHours[0]);
  waste.add(schedWasteHours[1]);

  doc["egg_threshold"] = eggThreshold;

  // Cycle timings (stored in milliseconds)
  doc["feed_distribute"] = feedDistributeDuration;
  doc["feed_pause"]      = feedPauseDuration;
  doc["feed_reverse"]    = feedReverseDuration;
  doc["egg_collect"]     = eggCollectDuration;
  doc["waste_cycle"]     = wasteCycleDuration;

  File file = SPIFFS.open(CONFIG_FILE, FILE_WRITE);
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.println("[CONFIG] Config saved to SPIFFS.");
  }
}

void loadScheduleConfig() {
  if (!SPIFFS.exists(CONFIG_FILE)) {
    Serial.println("[CONFIG] No config file found, using defaults.");
    return;
  }

  File file = SPIFFS.open(CONFIG_FILE, FILE_READ);
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

  if (doc["feed"].is<JsonArray>() && doc["feed"].as<JsonArray>().size() == 2) {
    schedFeedHours[0] = doc["feed"][0].as<int>();
    schedFeedHours[1] = doc["feed"][1].as<int>();
  }
  if (doc["egg"].is<JsonArray>() && doc["egg"].as<JsonArray>().size() == 2) {
    schedEggHours[0] = doc["egg"][0].as<int>();
    schedEggHours[1] = doc["egg"][1].as<int>();
  }
  if (doc["waste"].is<JsonArray>() && doc["waste"].as<JsonArray>().size() == 2) {
    schedWasteHours[0] = doc["waste"][0].as<int>();
    schedWasteHours[1] = doc["waste"][1].as<int>();
  }
  if (doc["egg_threshold"].is<int>()) {
    eggThreshold = doc["egg_threshold"].as<int>();
  }

  // Load cycle timings
  if (doc["feed_distribute"].is<unsigned long>()) feedDistributeDuration = doc["feed_distribute"].as<unsigned long>();
  if (doc["feed_pause"].is<unsigned long>())      feedPauseDuration      = doc["feed_pause"].as<unsigned long>();
  if (doc["feed_reverse"].is<unsigned long>())    feedReverseDuration    = doc["feed_reverse"].as<unsigned long>();
  if (doc["egg_collect"].is<unsigned long>())     eggCollectDuration     = doc["egg_collect"].as<unsigned long>();
  if (doc["waste_cycle"].is<unsigned long>())     wasteCycleDuration     = doc["waste_cycle"].as<unsigned long>();

  Serial.printf("[CONFIG] Loaded: Feed[%d,%d] Egg[%d,%d] Waste[%d,%d] Threshold=%d\n",
    schedFeedHours[0], schedFeedHours[1],
    schedEggHours[0], schedEggHours[1],
    schedWasteHours[0], schedWasteHours[1],
    eggThreshold);
  Serial.printf("[CONFIG] Timings(ms): FeedDist=%lu FeedPause=%lu FeedRev=%lu EggCol=%lu Waste=%lu\n",
    feedDistributeDuration, feedPauseDuration, feedReverseDuration, eggCollectDuration, wasteCycleDuration);
}

void publishCurrentConfig() {
  JsonDocument doc;
  
  JsonArray feed = doc["feed"].to<JsonArray>();
  feed.add(schedFeedHours[0]); feed.add(schedFeedHours[1]);
  JsonArray egg = doc["egg"].to<JsonArray>();
  egg.add(schedEggHours[0]); egg.add(schedEggHours[1]);
  JsonArray waste = doc["waste"].to<JsonArray>();
  waste.add(schedWasteHours[0]); waste.add(schedWasteHours[1]);
  doc["egg_threshold"] = eggThreshold;

  // Send timings in SECONDS for dashboard display
  doc["feed_distribute"] = feedDistributeDuration / 1000;
  doc["feed_pause"]      = feedPauseDuration / 1000;
  doc["feed_reverse"]    = feedReverseDuration / 1000;
  doc["egg_collect"]     = eggCollectDuration / 1000;
  doc["waste_cycle"]     = wasteCycleDuration / 1000;

  String payload;
  serializeJson(doc, payload);
  mqtt.publish(TOPIC_CONFIG_STAT, payload);
  Serial.println("[CONFIG] Published current config.");
}
