/*
 * ================================================================
 * ESP32 #1 — SEC 1 (Safety) + SEC 4 (Appliances)
 * ================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ═══════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "r6166b11.ala.us-east-1.emqxsl.com";
const int   MQTT_PORT = 8883;
const char* MQTT_USER = "industry";
const char* MQTT_PASS = "industry";
const char* MQTT_CID  = "industry-esp1";

// ═══════════════════════════════════════════════════════════════
// PINS
// ═══════════════════════════════════════════════════════════════
#define PIN_RELAY1      18    // Active LOW
#define PIN_RELAY2      19    // Active LOW
#define PIN_VIBRATE     4     // D4 vibration sensor
#define PIN_DOOR_DS1    34    // Door sensor
#define PIN_ONEWIRE     23    // DS18B20 temperature
#define PIN_VOLTAGE     35    // Voltage fluctuation monitoring

// ═══════════════════════════════════════════════════════════════
// SENSORS
// ═══════════════════════════════════════════════════════════════
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

// ═══════════════════════════════════════════════════════════════
// THRESHOLDS
// ═══════════════════════════════════════════════════════════════
#define TEMP_ALERT       55.0
#define TEMP_WARN        45.0
#define PUBLISH_EVERY    3000

// ═══════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════
WiFiClientSecure wifiSecure;
PubSubClient mqttClient(wifiSecure);

bool relay1State = false;
bool relay2State = false;
bool priorityEnable = false;
bool sec4Enable = false;
bool inSafetyAlert = false;

unsigned long lastPublish = 0;

// ═══════════════════════════════════════════════════════════════
// RELAY CONTROL
// ═══════════════════════════════════════════════════════════════
void setRelay(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LOW : HIGH);
}

// ═══════════════════════════════════════════════════════════════
// SAFETY ALERT SYSTEM
// ═══════════════════════════════════════════════════════════════
void triggerSafetyAlert(const char* reason) {
  if (!inSafetyAlert) {
    inSafetyAlert = true;
    Serial.printf("[🚨 SAFETY ALERT] %s\n", reason);
    
    char alertMsg[128];
    snprintf(alertMsg, 128, "ALERT:%s", reason);
    mqttClient.publish("industry/esp1/safety_alert", alertMsg, true);
    
    // Auto-activate safety relays
    setRelay(PIN_RELAY1, true);
    setRelay(PIN_RELAY2, true);
    relay1State = relay2State = true;
    mqttClient.publish("industry/esp1/relay18/state", "1", true);
    mqttClient.publish("industry/esp1/relay19/state", "1", true);
  }
}

void clearSafetyAlert() {
  if (inSafetyAlert) {
    inSafetyAlert = false;
    Serial.println("[✅ SAFETY] Alert cleared");
    mqttClient.publish("industry/esp1/safety_alert", "CLEAR", true);
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

  // Priority enable (SEC1)
  if (t == "industry/esp1/priority_enable") {
    priorityEnable = (v == "1");
    Serial.printf("[⚡ PRIORITY] SEC1 = %s\n", priorityEnable ? "ENABLED" : "DISABLED");
    
    if (!priorityEnable) {
      setRelay(PIN_RELAY1, false);
      setRelay(PIN_RELAY2, false);
      relay1State = relay2State = false;
      mqttClient.publish("industry/esp1/relay18/state", "0", true);
      mqttClient.publish("industry/esp1/relay19/state", "0", true);
    }
  }

  // SEC4 enable (Appliances - only EB)
  if (t == "industry/esp1/sec4_enable") {
    sec4Enable = (v == "1");
    mqttClient.publish("industry/esp1/appliance_status", 
                       sec4Enable ? "ONLINE" : "OFFLINE", true);
    Serial.printf("[🏠 APPLIANCES] SEC4 = %s\n", sec4Enable ? "ENABLED" : "DISABLED");
  }

  // Safety Relay 18
  if (t == "industry/esp1/relay18/set") {
    if (!priorityEnable) {
      Serial.println("[❌ BLOCKED] SEC1 not enabled");
      return;
    }
    relay1State = (v == "1");
    setRelay(PIN_RELAY1, relay1State);
    mqttClient.publish("industry/esp1/relay18/state", relay1State ? "1" : "0", true);
    Serial.printf("[🔌 RELAY] PIN18 → %s\n", relay1State ? "ON" : "OFF");
  }

  // Safety Relay 19
  if (t == "industry/esp1/relay19/set") {
    if (!priorityEnable) {
      Serial.println("[❌ BLOCKED] SEC1 not enabled");
      return;
    }
    relay2State = (v == "1");
    setRelay(PIN_RELAY2, relay2State);
    mqttClient.publish("industry/esp1/relay19/state", relay2State ? "1" : "0", true);
    Serial.printf("[🔌 RELAY] PIN19 → %s\n", relay2State ? "ON" : "OFF");
  }

  // Appliance 18 (SEC4)
  if (t == "industry/esp1/app18/set") {
    if (!sec4Enable) {
      Serial.println("[❌ BLOCKED] SEC4 not enabled");
      return;
    }
    relay1State = (v == "1");
    setRelay(PIN_RELAY1, relay1State);
    mqttClient.publish("industry/esp1/relay18/state", relay1State ? "1" : "0", true);
  }

  // Appliance 19 (SEC4)
  if (t == "industry/esp1/app19/set") {
    if (!sec4Enable) {
      Serial.println("[❌ BLOCKED] SEC4 not enabled");
      return;
    }
    relay2State = (v == "1");
    setRelay(PIN_RELAY2, relay2State);
    mqttClient.publish("industry/esp1/relay19/state", relay2State ? "1" : "0", true);
  }
}

// ═══════════════════════════════════════════════════════════════
// SENSOR PUBLISHING
// ═══════════════════════════════════════════════════════════════
void publishSensors() {
  // Temperature
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  
  if (temp != DEVICE_DISCONNECTED_C) {
    char buf[16];
    dtostrf(temp, 4, 1, buf);
    mqttClient.publish("industry/esp1/temperature", buf, true);
    
    if (temp >= TEMP_ALERT) {
      triggerSafetyAlert("HIGH_TEMP");
    } else if (temp < TEMP_WARN && inSafetyAlert) {
      clearSafetyAlert();
    }
    
    Serial.printf("[🌡️ TEMP] %.1f°C\n", temp);
  } else {
    mqttClient.publish("industry/esp1/temperature", "ERR", false);
  }

  // Vibration (active LOW)
  bool vib = !digitalRead(PIN_VIBRATE);
  mqttClient.publish("industry/esp1/vibration", vib ? "ALERT" : "OK", true);
  if (vib) Serial.println("[📳 VIBRATION] ALERT");

  // Door Sensor
  bool door = !digitalRead(PIN_DOOR_DS1);
  mqttClient.publish("industry/esp1/ds1", door ? "OPEN" : "CLOSED", true);
  if (door) Serial.println("[🚪 DOOR] OPEN");

  // Voltage Fluctuation
  int adc = analogRead(PIN_VOLTAGE);
  float volt = adc * (3.3 / 4095.0) * 2;
  bool fluctHigh = (volt < 1.5 || volt > 2.8);
  mqttClient.publish("industry/esp1/fluctuation", fluctHigh ? "HIGH" : "NORMAL", true);
  
  if (fluctHigh) {
    triggerSafetyAlert("VOLTAGE_FLUCTUATION");
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
  
  Serial.println("\n[✅ WiFi] Connected: " + WiFi.localIP().toString());
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
                          "industry/esp1/status", 1, true, "OFFLINE")) {
      Serial.println(" ✅ Connected!");
      
      mqttClient.publish("industry/esp1/status", "ONLINE", true);
      
      // Subscribe to topics
      mqttClient.subscribe("industry/esp1/relay18/set", 1);
      mqttClient.subscribe("industry/esp1/relay19/set", 1);
      mqttClient.subscribe("industry/esp1/app18/set", 1);
      mqttClient.subscribe("industry/esp1/app19/set", 1);
      mqttClient.subscribe("industry/esp1/priority_enable", 1);
      mqttClient.subscribe("industry/esp1/sec4_enable", 1);
      
    } else {
      Serial.printf(" ❌ Failed rc=%d — retry in 3s\n", mqttClient.state());
      delay(3000);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔═══════════════════════════════════════════════════╗");
  Serial.println("║  ESP32 #1 — Safety (SEC1) + Appliances (SEC4)   ║");
  Serial.println("╚═══════════════════════════════════════════════════╝");

  // Pin setup
  pinMode(PIN_RELAY1, OUTPUT);
  pinMode(PIN_RELAY2, OUTPUT);
  pinMode(PIN_VIBRATE, INPUT_PULLUP);
  pinMode(PIN_DOOR_DS1, INPUT_PULLUP);

  // Default: relays OFF
  setRelay(PIN_RELAY1, false);
  setRelay(PIN_RELAY2, false);

  sensors.begin();
  
  connectWiFi();
  connectMQTT();

  Serial.println("[✅ INIT] ESP1 ready — waiting for retained MQTT messages...");
  delay(1500);
}

// ═══════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqttClient.connected()) connectMQTT();
  
  mqttClient.loop();

  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_EVERY) {
    lastPublish = now;
    if (mqttClient.connected()) {
      publishSensors();
    }
  }

  delay(10);
}