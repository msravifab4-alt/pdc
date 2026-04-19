/*
 * ================================================================
 * ESP32 #2 — SEC 2 (Critical / 24x7 Industrial Machines)
 * ================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// ═══════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "r6166b11.ala.us-east-1.emqxsl.com";
const int   MQTT_PORT = 8883;
const char* MQTT_USER = "industry";
const char* MQTT_PASS = "industry";
const char* MQTT_CID  = "industry-esp2";

// ═══════════════════════════════════════════════════════════════
// PINS
// ═══════════════════════════════════════════════════════════════
#define PIN_RELAY1  16  // Critical Machine 1
#define PIN_RELAY2  17  // Critical Machine 2

// ═══════════════════════════════════════════════════════════════
// CONFIG
// ═══════════════════════════════════════════════════════════════
#define HEARTBEAT_EVERY  5000

// ═══════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════
WiFiClientSecure wifiSecure;
PubSubClient mqttClient(wifiSecure);

bool relay1State = false;
bool relay2State = false;
bool priorityEnable = false;

unsigned long startTime = 0;
unsigned long lastHbeat = 0;

// ═══════════════════════════════════════════════════════════════
// RELAY CONTROL
// ═══════════════════════════════════════════════════════════════
void setRelay(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LOW : HIGH);
}

// ═══════════════════════════════════════════════════════════════
// MQTT CALLBACK
// ═══════════════════════════════════════════════════════════════
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t(topic);
  char msg[64];
  memset(msg, 0, 64);
  for (unsigned int i = 0; i < min(length, (unsigned int)63); i++) {
    msg[i] = payload[i];
  }
  String v(msg);
  
  Serial.printf("[📥 MQTT] %s → %s\n", topic, msg);

  // Priority enable
  if (t == "industry/esp2/priority_enable") {
    bool wasEnabled = priorityEnable;
    priorityEnable = (v == "1");
    
    Serial.printf("[⚡ PRIORITY] SEC2 = %s\n", priorityEnable ? "ENABLED" : "DISABLED");

    if (!priorityEnable && wasEnabled) {
      Serial.println("[🚨 SEC2] Priority lost — emergency shutdown");
      
      setRelay(PIN_RELAY1, false);
      setRelay(PIN_RELAY2, false);
      relay1State = relay2State = false;
      
      mqttClient.publish("industry/esp2/relay16/state", "0", true);
      mqttClient.publish("industry/esp2/relay17/state", "0", true);
      mqttClient.publish("industry/esp2/status", "STANDBY_NO_PRIORITY", true);
      
    } else if (priorityEnable && !wasEnabled) {
      Serial.println("[✅ SEC2] Priority restored — machines ready");
      mqttClient.publish("industry/esp2/status", "READY", true);
    }
  }

  // Relay 16
  if (t == "industry/esp2/relay16/set") {
    if (!priorityEnable) {
      Serial.println("[❌ BLOCKED] SEC2 not enabled");
      mqttClient.publish("industry/esp2/alert", "BLOCKED:sec2_not_enabled", false);
      return;
    }
    
    relay1State = (v == "1");
    setRelay(PIN_RELAY1, relay1State);
    mqttClient.publish("industry/esp2/relay16/state", relay1State ? "1" : "0", true);
    Serial.printf("[🔌 RELAY] PIN16 → %s\n", relay1State ? "ON" : "OFF");
    
    if (relay1State) startTime = millis();
  }

  // Relay 17
  if (t == "industry/esp2/relay17/set") {
    if (!priorityEnable) {
      Serial.println("[❌ BLOCKED] SEC2 not enabled");
      mqttClient.publish("industry/esp2/alert", "BLOCKED:sec2_not_enabled", false);
      return;
    }
    
    relay2State = (v == "1");
    setRelay(PIN_RELAY2, relay2State);
    mqttClient.publish("industry/esp2/relay17/state", relay2State ? "1" : "0", true);
    Serial.printf("[🔌 RELAY] PIN17 → %s\n", relay2State ? "ON" : "OFF");
  }
}

// ═══════════════════════════════════════════════════════════════
// WIFI CONNECTION
// ═══════════════════════════════════════════════════════════════
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[📡 WiFi] Connecting");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n[✅ WiFi] " + WiFi.localIP().toString());
}

// ═══════════════════════════════════════════════════════════════
// MQTT CONNECTION
// ═══════════════════════════════════════════════════════════════
void connectMQTT() {
  wifiSecure.setInsecure();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);

  while (!mqttClient.connected()) {
    Serial.print("[🔌 MQTT] Connecting...");
    
    if (mqttClient.connect(MQTT_CID, MQTT_USER, MQTT_PASS,
                          "industry/esp2/status", 1, true, "OFFLINE")) {
      Serial.println(" ✅ OK!");
      
      mqttClient.publish("industry/esp2/status", "ONLINE", true);
      
      mqttClient.subscribe("industry/esp2/relay16/set", 1);
      mqttClient.subscribe("industry/esp2/relay17/set", 1);
      mqttClient.subscribe("industry/esp2/priority_enable", 1);
      
    } else {
      Serial.printf(" ❌ Failed rc=%d, retry...\n", mqttClient.state());
      delay(3000);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// HEARTBEAT
// ═══════════════════════════════════════════════════════════════
void sendHeartbeat() {
  const char* status = priorityEnable ? 
                      (relay1State || relay2State ? "RUNNING" : "IDLE") : 
                      "STANDBY";
  
  mqttClient.publish("industry/esp2/status", status, true);

  // Uptime
  unsigned long upSec = millis() / 1000;
  char upStr[32];
  snprintf(upStr, 32, "%02lu:%02lu:%02lu", 
           upSec / 3600, (upSec % 3600) / 60, upSec % 60);
  mqttClient.publish("industry/esp2/uptime", upStr, true);

  // States
  mqttClient.publish("industry/esp2/relay16/state", relay1State ? "1" : "0", true);
  mqttClient.publish("industry/esp2/relay17/state", relay2State ? "1" : "0", true);
}

// ═══════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔═══════════════════════════════════════════════════╗");
  Serial.println("║  ESP32 #2 — Critical Machines (SEC2)            ║");
  Serial.println("╚═══════════════════════════════════════════════════╝");

  pinMode(PIN_RELAY1, OUTPUT);
  pinMode(PIN_RELAY2, OUTPUT);
  
  setRelay(PIN_RELAY1, false);
  setRelay(PIN_RELAY2, false);

  connectWiFi();
  connectMQTT();

  // Wait for retained messages
  unsigned long t = millis();
  while (millis() - t < 1500) {
    mqttClient.loop();
    delay(10);
  }

  Serial.printf("[✅ INIT] Priority: %s\n", 
                priorityEnable ? "YES" : "NO (waiting for EB/GEN2/GEN3)");
}

// ═══════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqttClient.connected()) connectMQTT();
  
  mqttClient.loop();

  unsigned long now = millis();
  if (now - lastHbeat >= HEARTBEAT_EVERY) {
    lastHbeat = now;
    sendHeartbeat();
  }

  delay(10);
}