#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include "RTClib.h"
#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include "config.h"

RTC_DS3231 rtc;

// ─── PicoMQTT Broker: TCP + WebSocket ───
::WiFiServer tcp_server(MQTT_PORT);
::WiFiServer ws_underlying(WEBSOCKET_PORT);
PicoWebsocket::Server<::WiFiServer> ws_server(ws_underlying);
PicoMQTT::Server mqtt(tcp_server, ws_server);

// ─── HTTP Web Server (serves dashboard from SPIFFS) ───
WebServer webServer(80);

#include "storage.h"

// Publish helper — always available since we ARE the broker.
// Data/alert messages are queued to SPIFFS while WiFi is down so the
// dashboard can replay them after reconnect.
void logOfflineMessage(String topic, String payload);

void publishMessage(const char* topic, const String& payload) {
  if (WiFi.status() != WL_CONNECTED &&
      (strcmp(topic, TOPIC_EGG_DATA) == 0 || strcmp(topic, TOPIC_ALERTS) == 0)) {
    logOfflineMessage(topic, payload);
    return;
  }
  mqtt.publish(topic, payload);
}

#include "hardware.h"

// System Timing States
int lastDailyResetDay = -1;
unsigned long lastFeedEpoch = 0;
unsigned long lastEggEpoch = 0;
unsigned long lastWasteEpoch = 0;
unsigned long lastHeartbeatEpoch = 0;
bool tempDangerAlertSent = false;

// Command request flags — set in MQTT callbacks, executed in loop()
// NEVER call blocking functions inside PicoMQTT callbacks!
volatile bool feedStartRequested = false;
volatile bool eggStartRequested = false;
volatile bool wasteStartRequested = false;

// WiFi reconnection tracking
unsigned long lastWifiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 20000;

void setupWiFi() {
  Serial.println("Connecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
    
    // Start mDNS so dashboard can find us at poultry.local
    if (MDNS.begin(MDNS_HOSTNAME)) {
      Serial.printf("[mDNS] Reachable at http://%s.local\n", MDNS_HOSTNAME);
      MDNS.addService("mqtt", "tcp", MQTT_PORT);
      MDNS.addService("ws", "tcp", WEBSOCKET_PORT);
    } else {
      Serial.println("[mDNS] Failed to start.");
    }
  } else {
    Serial.println("WiFi failed. Will keep retrying.");
  }
  lastWifiAttempt = millis();
}

// Non-blocking: kick off a reconnect attempt and check the result on later
// loop passes — never stall the loop while motor cycles are running.
void reconnectWiFi() {
  static bool wifiWasDown = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiWasDown) {
      wifiWasDown = false;
      Serial.print("WiFi reconnected! IP: ");
      Serial.println(WiFi.localIP());
      MDNS.begin(MDNS_HOSTNAME);
      syncOfflineQueue();
    }
    return;
  }

  wifiWasDown = true;
  unsigned long now = millis();
  if (now - lastWifiAttempt < WIFI_RETRY_INTERVAL) return;
  lastWifiAttempt = now;

  Serial.println("WiFi lost. Reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void handleConfigCommand(const String& message) {
  JsonDocument doc;
  if (deserializeJson(doc, message)) {
    Serial.println("[CONFIG] Failed to parse config command.");
    return;
  }
  
  String action = doc["action"].as<String>();
  
  if (action == "update_schedule") {
    if (doc["feed"].is<JsonArray>() && doc["feed"].as<JsonArray>().size() == 2) {
      schedFeedHours[0] = constrain(doc["feed"][0].as<int>(), 0, 23);
      schedFeedHours[1] = constrain(doc["feed"][1].as<int>(), 0, 23);
    }
    if (doc["egg"].is<JsonArray>() && doc["egg"].as<JsonArray>().size() == 2) {
      schedEggHours[0] = constrain(doc["egg"][0].as<int>(), 0, 23);
      schedEggHours[1] = constrain(doc["egg"][1].as<int>(), 0, 23);
    }
    if (doc["waste"].is<JsonArray>() && doc["waste"].as<JsonArray>().size() == 2) {
      schedWasteHours[0] = constrain(doc["waste"][0].as<int>(), 0, 23);
      schedWasteHours[1] = constrain(doc["waste"][1].as<int>(), 0, 23);
    }
    if (doc["egg_threshold"].is<int>()) {
      eggThreshold = max(1, doc["egg_threshold"].as<int>());
    }
    
    saveScheduleConfig();
    publishCurrentConfig();
    
    Serial.printf("[CONFIG] Updated: Feed[%d,%d] Egg[%d,%d] Waste[%d,%d] Threshold=%d\n",
      schedFeedHours[0], schedFeedHours[1],
      schedEggHours[0], schedEggHours[1],
      schedWasteHours[0], schedWasteHours[1],
      eggThreshold);
  }
  else if (action == "clear_queue") {
    clearOfflineQueue();
  }
  else if (action == "get_config") {
    publishCurrentConfig();
  }
  else if (action == "sync_rtc") {
    if (doc["epoch"].is<unsigned long>()) {
      unsigned long epoch = doc["epoch"].as<unsigned long>();
      rtc.adjust(DateTime(epoch));
      DateTime now = rtc.now();
      Serial.printf("[RTC] Clock synced to: %04d-%02d-%02d %02d:%02d:%02d\n",
        now.year(), now.month(), now.day(),
        now.hour(), now.minute(), now.second());
    }
  }
  else if (action == "update_timings") {
    // Dashboard sends values in SECONDS — convert to milliseconds.
    // Clamp to 1–600s so a bad value can't run a motor indefinitely.
    if (doc["feed_distribute"].is<int>()) feedDistributeDuration = constrain(doc["feed_distribute"].as<int>(), 1, 600) * 1000UL;
    if (doc["feed_pause"].is<int>())      feedPauseDuration      = constrain(doc["feed_pause"].as<int>(), 1, 600) * 1000UL;
    if (doc["feed_reverse"].is<int>())    feedReverseDuration    = constrain(doc["feed_reverse"].as<int>(), 1, 600) * 1000UL;
    if (doc["egg_collect"].is<int>())     eggCollectDuration     = constrain(doc["egg_collect"].as<int>(), 1, 600) * 1000UL;
    if (doc["waste_cycle"].is<int>())     wasteCycleDuration     = constrain(doc["waste_cycle"].as<int>(), 1, 600) * 1000UL;

    // Gantry speed (0–100%) and auger run mode (0–3)
    if (doc["feed_speed"].is<int>())        feedSpeedPct       = constrain(doc["feed_speed"].as<int>(), 0, 100);
    if (doc["feed_return_speed"].is<int>()) feedReturnSpeedPct = constrain(doc["feed_return_speed"].as<int>(), 0, 100);
    if (doc["auger_mode"].is<int>())        augerMode          = constrain(doc["auger_mode"].as<int>(), 0, 3);

    saveScheduleConfig();
    publishCurrentConfig();

    Serial.printf("[CONFIG] Timings updated(ms): FeedDist=%lu FeedPause=%lu FeedRev=%lu EggCol=%lu Waste=%lu | Speed=%d%% Return=%d%% AugerMode=%d\n",
      feedDistributeDuration, feedPauseDuration, feedReverseDuration, eggCollectDuration, wasteCycleDuration,
      feedSpeedPct, feedReturnSpeedPct, augerMode);
  }
}

void setup() {
  Serial.begin(115200);
  
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting to compile time as fallback.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Initialize systems
  initStorage();
  loadScheduleConfig();
  initializeHardware();

  // Seed epochs with boot time, then restore any persisted values that are
  // more recent — prevents double-triggering after a reboot mid-hour.
  unsigned long bootEpoch = rtc.now().unixtime();
  lastFeedEpoch  = bootEpoch;
  lastEggEpoch   = bootEpoch;
  lastWasteEpoch = bootEpoch;
  loadLastRunEpochs(bootEpoch);
  
  setupWiFi();
  
  // ─── PicoMQTT Subscriptions ───
  // Copy payload to buffer first (Boat Interface pattern — payload ptr may not be stable)
  
  mqtt.subscribe(TOPIC_FEED_CMD, [](const char* topic, const char* payload) {
    char buf[128];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    Serial.printf("[MQTT-RX] %s -> %s\n", topic, buf);
    if (strstr(buf, "stop")) {
      feedStopRequested = true;
      Serial.println("[CMD] Feed STOP");
    } else if (strstr(buf, "start")) {
      feedStartRequested = true;
      Serial.println("[CMD] Feed START");
    }
  });

  mqtt.subscribe(TOPIC_EGG_CMD, [](const char* topic, const char* payload) {
    char buf[128];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    Serial.printf("[MQTT-RX] %s -> %s\n", topic, buf);
    if (strstr(buf, "stop")) {
      eggStopRequested = true;
      Serial.println("[CMD] Egg STOP");
    } else if (strstr(buf, "start")) {
      eggStartRequested = true;
      Serial.println("[CMD] Egg START");
    }
  });

  mqtt.subscribe(TOPIC_WASTE_CMD, [](const char* topic, const char* payload) {
    char buf[128];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    Serial.printf("[MQTT-RX] %s -> %s\n", topic, buf);
    if (strstr(buf, "stop")) {
      wasteStopRequested = true;
      Serial.println("[CMD] Waste STOP");
    } else if (strstr(buf, "start")) {
      wasteStartRequested = true;
      Serial.println("[CMD] Waste START");
    }
  });

  mqtt.subscribe(TOPIC_CONFIG_CMD, [](const char* topic, const char* payload) {
    char buf[384];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    Serial.printf("[MQTT-RX] %s -> %s\n", topic, buf);
    handleConfigCommand(String(buf));
  });

  // Start the MQTT broker
  mqtt.begin();
  Serial.println("[MQTT] PicoMQTT Broker started");
  Serial.printf("[MQTT] MQTT on port %d, WebSocket on port %d\n", MQTT_PORT, WEBSOCKET_PORT);

  // ─── HTTP Web Server: serve dashboard from SPIFFS ───
  webServer.onNotFound([]() {
    // Try to serve the requested file from SPIFFS
    String path = webServer.uri();
    if (path.endsWith("/")) path += "index.html";
    
    // Content type mapping
    String contentType = "text/plain";
    if (path.endsWith(".html"))      contentType = "text/html";
    else if (path.endsWith(".css"))  contentType = "text/css";
    else if (path.endsWith(".js"))   contentType = "application/javascript";
    else if (path.endsWith(".png"))  contentType = "image/png";
    else if (path.endsWith(".svg"))  contentType = "image/svg+xml";
    else if (path.endsWith(".ico"))  contentType = "image/x-icon";
    else if (path.endsWith(".json")) contentType = "application/json";
    
    // Try gzipped version first (much smaller)
    if (SPIFFS.exists(path + ".gz")) {
      File file = SPIFFS.open(path + ".gz", "r");
      webServer.streamFile(file, contentType);
      file.close();
      return;
    }
    
    if (SPIFFS.exists(path)) {
      File file = SPIFFS.open(path, "r");
      webServer.streamFile(file, contentType);
      file.close();
      return;
    }
    
    // SPA fallback: serve index.html for client-side routing
    if (SPIFFS.exists("/index.html")) {
      File file = SPIFFS.open("/index.html", "r");
      webServer.streamFile(file, "text/html");
      file.close();
      return;
    }
    
    webServer.send(404, "text/plain", "File not found");
  });
  // Activity log download endpoint — GET /log serves /activity_log.json
  webServer.on("/log", HTTP_GET, []() {
    if (SPIFFS.exists(LOG_FILE)) {
      File file = SPIFFS.open(LOG_FILE, "r");
      webServer.sendHeader("Content-Disposition", "attachment; filename=poultry_activity_log.json");
      webServer.streamFile(file, "application/json");
      file.close();
    } else {
      webServer.send(200, "application/json", "[]");
    }
  });

  webServer.begin();
  Serial.printf("[HTTP] Dashboard at http://%s.local (port 80)\n", MDNS_HOSTNAME);
}

void loop() {
  reconnectWiFi();
  
  // PicoMQTT handles all broker traffic + WebSocket connections
  mqtt.loop();
  
  // HTTP server handles dashboard file requests
  webServer.handleClient();
  
  // Process command requests (set by MQTT callbacks)
  if (feedStartRequested) {
    feedStartRequested = false;
    lastFeedEpoch = rtc.now().unixtime();
    saveLastRunEpochs();
    startFeedingCycle();
  }
  if (eggStartRequested) {
    eggStartRequested = false;
    lastEggEpoch = rtc.now().unixtime();
    saveLastRunEpochs();
    startEggCollection();
  }
  if (wasteStartRequested) {
    wasteStartRequested = false;
    lastWasteEpoch = rtc.now().unixtime();
    saveLastRunEpochs();
    startWasteCycle();
  }
  
  // Run non-blocking state machines (never block, just check timers)
  updateFeedCycle();
  updateEggCollection();
  updateWasteCycle();
  
  DateTime now = rtc.now();
  
  // Daily Reset (Midnight) — zero the dashboard's egg counters without
  // touching the cycle "state" field (the dashboard merges status JSON).
  if (now.day() != lastDailyResetDay && now.hour() == 0) {
    lastDailyResetDay = now.day();
    publishMessage(TOPIC_EGG_DATA, "{\"eggs_l1\": 0, \"eggs_l2\": 0, \"total\": 0}");
  }

  // Dynamic Feeding Schedule — skip if cycle already active
  if (feedState == FEED_IDLE &&
      (now.hour() == schedFeedHours[0] || now.hour() == schedFeedHours[1]) &&
      (now.unixtime() - lastFeedEpoch > GRACE_PERIOD_SEC)) {
    lastFeedEpoch = now.unixtime();
    saveLastRunEpochs();
    startFeedingCycle();
  }

  // Dynamic Egg Collection Schedule — skip if cycle already active
  if (eggState == EGG_IDLE &&
      (now.hour() == schedEggHours[0] || now.hour() == schedEggHours[1]) &&
      (now.unixtime() - lastEggEpoch > GRACE_PERIOD_SEC)) {
    lastEggEpoch = now.unixtime();
    saveLastRunEpochs();
    startEggCollection();
  }

  // Dynamic Waste Management Schedule — skip if cycle already active
  if (wasteState == WASTE_IDLE &&
      (now.hour() == schedWasteHours[0] || now.hour() == schedWasteHours[1]) &&
      (now.unixtime() - lastWasteEpoch > GRACE_PERIOD_SEC)) {
    lastWasteEpoch = now.unixtime();
    saveLastRunEpochs();
    startWasteCycle();
  }
  
  // System Heartbeat
  if (now.unixtime() - lastHeartbeatEpoch >= HEARTBEAT_INTERVAL_SEC) {
    lastHeartbeatEpoch = now.unixtime();
    
    readDHT();
    
    char rtcBuf[25];
    snprintf(rtcBuf, sizeof(rtcBuf), "%04d-%02d-%02dT%02d:%02d:%02d",
      now.year(), now.month(), now.day(),
      now.hour(), now.minute(), now.second());
    
    JsonDocument hbDoc;
    hbDoc["status"]   = "online";
    hbDoc["heap"]     = ESP.getFreeHeap();
    hbDoc["rssi"]     = WiFi.RSSI();
    hbDoc["temp"]     = serialized(String(lastTemp, 1));
    hbDoc["humidity"] = serialized(String(lastHumidity, 1));
    hbDoc["rtc"]      = rtcBuf;
    hbDoc["queue"]    = getQueueSize();
    hbDoc["dht_ok"]   = dhtValid;
    hbDoc["ip"]       = WiFi.localIP().toString();

    String hbPayload;
    serializeJson(hbDoc, hbPayload);
    mqtt.publish(TOPIC_SYSTEM_STAT, hbPayload);
    
    // Temperature danger alert
    if (dhtValid && lastTemp >= TEMP_DANGER && !tempDangerAlertSent) {
      String alertPayload = "{\"type\":\"temperature\",\"message\":\"DANGER: Coop temperature " + 
        String(lastTemp, 1) + "°C!\",\"value\":" + String(lastTemp, 1) + "}";
      publishMessage(TOPIC_ALERTS, alertPayload);
      tempDangerAlertSent = true;
    }
    if (dhtValid && lastTemp < TEMP_DANGER - 2.0) {
      tempDangerAlertSent = false;
    }
  }
  
}
