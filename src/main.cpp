#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// WiFi配置
const char *ssid = "applepie";
const char *password = "sjdygzdx123";
const char *mqtt_server = "43.142.252.113";
const int mqtt_port = 1883;
const char *mqtt_user = "admin";
const char *mqtt_password = "public";

// 引脚定义
#define LED_RED D0
#define LED_GREEN D1
#define LED_BLUE D2
#define LIGHT_SENSOR_PIN A0
#define FAN_PIN D5

WiFiClient esp_client;
PubSubClient client(esp_client);

struct DeviceStatus {
  bool led_on = false;
  bool fan_on = false;
  int light_level = 0;
} device_status;

bool manual_led_control = true;
bool manual_fan_control = true;

String generateDeviceID(uint8_t pin) {
  String baseID = WiFi.macAddress();
  baseID.replace(":", "");
  return "dev_" + baseID + "_" + String(pin);
}

String light_id;
String fan_id;
String light_status_topic;
String fan_status_topic;

class DeviceManager {
public:
  static void initDevices() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    led_off();
    fan_off();
    pinMode(FAN_PIN, OUTPUT);
    pinMode(LIGHT_SENSOR_PIN, INPUT);

    device_status.led_on = false;
    device_status.fan_on = false;
  }

  static void setColor(bool red, bool green, bool blue) {
    digitalWrite(LED_RED, red ? HIGH : LOW);
    digitalWrite(LED_GREEN, green ? HIGH : LOW);
    digitalWrite(LED_BLUE, blue ? HIGH : LOW);
  }

  static void led_off() { setColor(true, true, true); }
  static void led_on() { setColor(false, false, false); }
  static void fan_on() { 
  analogWrite(FAN_PIN, 0); 
  Serial.println("风扇开启"); 
}
static void fan_off() { 
  analogWrite(FAN_PIN, 255); 
  Serial.println("风扇关闭"); 
}


  static void updateLightBySensor() {
    if (manual_led_control) return;
    int lightValue = analogRead(LIGHT_SENSOR_PIN);
    device_status.light_level = lightValue;
    Serial.print("光照值: ");
    Serial.println(lightValue);

    if (device_status.led_on && lightValue > 900) {
      led_off();
      device_status.led_on = false;
      Serial.println("光线强 关灯");
    } else if (!device_status.led_on && lightValue <= 900) {
      led_on();
      device_status.led_on = true;
      Serial.println("光线弱 打开灯");
    }
  }

  static void updateFanByLightSensor() {
  if (manual_fan_control) return;
  int lightValue = analogRead(LIGHT_SENSOR_PIN);
  device_status.light_level = lightValue;
  Serial.print("光照值: ");
  Serial.println(lightValue);
  if (lightValue <= 900 && !device_status.fan_on) {
    fan_on();
    device_status.fan_on = true;
    Serial.println("白天 开风扇");
  } else if (lightValue > 900 && device_status.fan_on) {
    fan_off();
    device_status.fan_on = false;
    Serial.println("晚上 关风扇");
  }
}
};

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());
}

void publishStatus() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = light_id;
  doc["device_type"] = 1;
  doc["status"] = device_status.led_on;
  doc["light_level"] = device_status.light_level;
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(light_status_topic.c_str(), buffer);
}

void publishFanStatus() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = fan_id;
  doc["fan_on"] = device_status.fan_on;
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(fan_status_topic.c_str(), buffer);
}

void handleCommand(const JsonObject &cmd, const String &topic) {
  String target_id = cmd["deviceId"] | "";
  String command = cmd["command"] | "";
  Serial.printf("Cmd Topic=%s ID=%s CMD=%s\n", topic.c_str(), target_id.c_str(), command.c_str());


  Serial.print("Light ID: ");
  Serial.println(light_id);
  Serial.print("Fan ID: ");
  Serial.println(fan_id);

  if (topic.startsWith("scene/")) {
  if (command == "nightlight_on") {
    manual_led_control = false;
    Serial.println("启用夜灯自动控制模式");
  } else if (command == "nightlight_off") {
    manual_led_control = true;
    DeviceManager::led_off();
    device_status.led_on = false;
    Serial.println("关闭夜灯，切换为手动模式");
  } else if (command == "autofan_on") {
    manual_fan_control = false;
    DeviceManager::updateFanByLightSensor();
    Serial.println("启用风扇自动控制模式");
  } else if (command == "autofan_off") {
    manual_led_control = true;
    manual_fan_control = true;
    DeviceManager::fan_off();
    device_status.fan_on = false;
    Serial.println("关闭风扇，切换为手动模式");
  }
  publishStatus();
  publishFanStatus();
  return;
}


  if (target_id == light_id) {
    if (command == "on" || command == "off") {
      manual_led_control = true;
      device_status.led_on = (command == "on");
      device_status.led_on ? DeviceManager::led_on() : DeviceManager::led_off();
      publishStatus();
    } else if (command == "get_status") {
      publishStatus();
    }
  } else if (target_id == fan_id) {
    if (command == "on" || command == "off") {
      manual_fan_control = true;
      device_status.fan_on = (command == "on");
      device_status.fan_on ? DeviceManager::fan_on() : DeviceManager::fan_off();
      publishFanStatus();
    } else if (command == "get_status") {
      publishFanStatus();
    }
  }
}

void callback(char *topic, byte *payload, unsigned int length) {
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, message)) {
    Serial.println("JSON解析错误");
    return;
  }
  handleCommand(doc.as<JsonObject>(), String(topic));
}

void reconnectMQTT() {
  while (!client.connected()) {
    String clientId = "ESP8266-" + WiFi.macAddress();
    clientId.replace(":", "");
    Serial.print("Connecting to MQTT...");
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");
      client.subscribe("device/+/control");
      client.subscribe("scene/+/control");
      publishStatus();
      publishFanStatus();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  DeviceManager::initDevices();
  setupWiFi();
  light_id = generateDeviceID(LED_RED);
  fan_id = generateDeviceID(FAN_PIN);
  light_status_topic = "device/" + light_id + "/status";
  fan_status_topic = "device/" + fan_id + "/status";
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  static unsigned long lastStatusTime = 0;
  static unsigned long lastSensorUpdate = 0;
  unsigned long now = millis();

  if (now - lastSensorUpdate > 1000) {
  if (!manual_led_control) DeviceManager::updateLightBySensor();
  if (!manual_fan_control) DeviceManager::updateFanByLightSensor();
  lastSensorUpdate = now;
}

  if (now - lastStatusTime > 20000) {
    publishStatus();
    publishFanStatus();
    lastStatusTime = now;
  }

  delay(10);
}