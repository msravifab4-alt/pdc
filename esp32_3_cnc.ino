/*
 * ================================================================
 * ESP32 #3 — SEC 3 (CNC & Packing Machines — Optional)
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
const char* MQTT_CID  = "industry-esp3";

// ═══════════════════════════════════════════════════════════════
// PINS
// ═══════════════════════════════════════════════════════════════
#define PIN_CNC      16  // CNC Machine
#define PIN_PACKING  17  // Packing Machine

// ═══════════════════════════════════════════════════════════════
// CONFIG
// ═══════════════════════════════════════════════════════════════
#define HEARTBEAT_EVERY  5000

// ═══════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════
WiFiClientSecure wifiSecure;
PubSubClient mqttClient(wifiSecure);

bool cncState = false;
bool packState = false;
bool priorityEnable = false;

unsigned long lastHbeat = 0;

// ═══════════════════════════════════════════════════════════════
// RELAY CONTROL
// ═══════════════════════════════════════════════════════════════
void setRelay(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LOW : HIGH);
}

// ═══════════════════════════════════════════════════════════════
// GRACEFUL SHUTDOWN
// ═══════════════════════════════════════════════════════════════
void shutdownMachines(const char* reason) {
  if (cncState || packState) {
    Serial.printf("[🚨 SEC3] SHUTDOWN: %s\n", reason);
    
    // Ramp-down delay for CNC safety
    delay(500);
    
    setRelay(PIN_CNC, false);
    setRelay(PIN_PACKING, false);
    cncState = packState = false;
    
    mqttClient.publish("industry/esp3/relay16/state", "0", true);
    mqttClient.publish("industry/esp3/relay17/state", "0", true);
    mqttClient.publish("industry/esp3/cnc", "OFF", true);
    mqttClient.publish("industry/esp3/packing", "OFF", true);
    
    char msg[64];
    snprintf(msg, 64, "SHUTDOWN:%s", reason);
    mqttClient.publish("industry/esp3/status", msg, true);
  }
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
  if (t == "industry/esp3/priority_enable") {
    bool wasEnabled = priorityEnable;
    priorityEnable = (v == "1");
    
    Serial.printf("[⚡ PRIORITY] SEC3 = %s\n", priorityEnable ? "ENABLED" : "DISABLED");

    if (!priorityEnable && wasEnabled) {
      shutdownMachines("NO_PRIORITY");
    } else if (priorityEnable && !wasEnabled) {
      mqttClient.publish("industry/esp3/status", "READY", true);
      Serial.println("[✅ SEC3] Priority restored — CNC/Pack ready");
    }
    return;
  }

  // CNC Relay
  if (t == "industry/esp3/relay16/set") {
    if (!priorityEnable) {
      Serial.println("[❌ BLOCKED] SEC3 not enabled");
      mqttClient.publish("industry/esp3/alert", 
                        "BLOCKED:sec3_not_enabled:need_GEN3_or_EB", false);
      return;
    }
    
    cncState = (v == "1");
    setRelay(PIN_CNC, cncState);
    mqttClient.publish("industry/esp3/relay16/state", cncState ? "1" : "0", true);
    mqttClient.publish("industry/esp3/cnc", cncState ? "ON" : "OFF", true);
    Serial.printf("[🔌 CNC] PIN16 → %s\n", cncState ? "ON" : "OFF");
  }

  // Packing Relay
  if (t == "industry/esp3/relay17/set") {
    if (!priorityEnable) {
      Serial.println("[❌ BLOCKED] SEC3 not enabled");
      mqttClient.publish("industry/esp3/alert", 
                        "BLOCKED:sec3_not_enabled:need_GEN3_or_EB", false);
      return;
    }
    
    packState = (v == "1");
    setRelay(PIN_PACKING, packState);
    mqttClient.publish("industry/esp3/relay17/state", packState ? "1" : "0", true);
    mqttClient.publish("industry/esp3/packing", packState ? "ON" : "OFF", true);
    Serial.printf("[🔌 PACKING] PIN17 → %s\n", packState ? "ON" : "OFF");
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
                          "industry/esp3/status", 1, true, "OFFLINE")) {
      Serial.println(" ✅ OK!");
      
      mqttClient.publish("industry/esp3/status", "ONLINE", true);
      
      mqttClient.subscribe("industry/esp3/relay16/set", 1);
      mqttClient.subscribe("industry/esp3/relay17/set", 1);
      mqttClient.subscribe("industry/esp3/priority_enable", 1);
      
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
  const char* status;
  
  if (!priorityEnable) {
    status = "STANDBY_NO_POWER";
  } else if (cncState || packState) {
    status = "RUNNING";
  } else {
    status = "IDLE";
  }
  
  mqttClient.publish("industry/esp3/status", status, true);
  mqttClient.publish("industry/esp3/cnc", cncState ? "ON" : "OFF", true);
  mqttClient.publish("industry/esp3/packing", packState ? "ON" : "OFF", true);
  mqttClient.publish("industry/esp3/relay16/state", cncState ? "1" : "0", true);
  mqttClient.publish("industry/esp3/relay17/state", packState ? "1" : "0", true);
}

// ═══════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔═══════════════════════════════════════════════════╗");
  Serial.println("║  ESP32 #3 — CNC & Packing Machines (SEC3)       ║");
  Serial.println("╚═══════════════════════════════════════════════════╝");

  pinMode(PIN_CNC, OUTPUT);
  pinMode(PIN_PACKING, OUTPUT);
  
  setRelay(PIN_CNC, false);
  setRelay(PIN_PACKING, false);

  connectWiFi();
  connectMQTT();

  // Wait for retained messages
  unsigned long t = millis();
  while (millis() - t < 1500) {
    mqttClient.loop();
    delay(10);
  }

  Serial.printf("[✅ INIT] Priority: %s\n", 
                priorityEnable ? "YES (GEN3/EB)" : "NO");
  Serial.println("[ℹ️ INFO] SEC3 requires GEN3 (800kW) or EB for operation");
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