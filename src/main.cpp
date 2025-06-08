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
#define PIR_SENSOR_PIN D6  // 人体红外传感器
#define BUZZER_PIN D5      // 有源蜂鸣器 (与风扇共用D5引脚)

WiFiClient esp_client;
PubSubClient client(esp_client);

struct DeviceStatus {
  bool led_on = false;
  bool fan_on = false;
  int light_level = 0;
  bool alarm_active = false;  // 报警状态
  bool motion_detected = false;  // 人体移动检测状态
} device_status;

bool manual_led_control = true;
bool manual_fan_control = true;
bool manual_alarm_control = true;  // 报警功能启用状态

String generateDeviceID(uint8_t pin) {
  String baseID = WiFi.macAddress();
  baseID.replace(":", "");
  return "dev_" + baseID + "_" + String(pin);
}

String light_id;
String fan_id;
String alarm_id;  // 报警设备ID
String light_status_topic;
String fan_status_topic;
String alarm_status_topic;  // 报警状态主题

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
    pinMode(PIR_SENSOR_PIN, INPUT);  // 初始化人体红外传感器

    device_status.led_on = false;
    device_status.fan_on = false;
    device_status.alarm_active = false;
    device_status.motion_detected = false;
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

  static void buzzer_on() {
    analogWrite(BUZZER_PIN, 0);  // 蜂鸣器开启
    Serial.println("蜂鸣器开启");
  }

  static void buzzer_off() {
    analogWrite(BUZZER_PIN, 255);  // 蜂鸣器关闭
    Serial.println("蜂鸣器关闭");
  }

  static void alarm_on() {
    setColor(false, true, true);  // 只亮红灯
    buzzer_on();
    device_status.alarm_active = true;
    Serial.println("报警开启");
  }

  static void alarm_off() {
    led_off();
    buzzer_off();
    device_status.alarm_active = false;
    Serial.println("报警关闭");
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

  static void checkMotionSensor() {
    if (manual_alarm_control) return;
    
    int pirValue = digitalRead(PIR_SENSOR_PIN);
    bool motion = (pirValue == HIGH);
    
    if (motion && !device_status.motion_detected) {
      device_status.motion_detected = true;
      alarm_on();
      Serial.println("检测到人体移动，触发报警!");
    } else if (!motion && device_status.motion_detected) {
      device_status.motion_detected = false;
      // 这里可以选择是否自动关闭报警
      alarm_off();
      Serial.println("未检测到移动，报警停止");
    }
  }

  static void flashRedLED() {
    if (device_status.alarm_active) {
      static bool red_state = false;
      red_state = !red_state;
      digitalWrite(LED_RED, red_state ? LOW : HIGH);  // 红灯闪烁
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

void publishAlarmStatus() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = alarm_id;
  doc["alarm_active"] = device_status.alarm_active;
  doc["motion_detected"] = device_status.motion_detected;
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(alarm_status_topic.c_str(), buffer);
}

void handleCommand(const JsonObject &cmd, const String &topic) {
  String target_id = cmd["deviceId"] | "";
  String command = cmd["command"] | "";
  Serial.printf("Cmd Topic=%s ID=%s CMD=%s\n", topic.c_str(), target_id.c_str(), command.c_str());


  Serial.print("Light ID: ");
  Serial.println(light_id);
  Serial.print("Fan ID: ");
  Serial.println(fan_id);
  Serial.print("Alarm ID: ");
  Serial.println(alarm_id);

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
    } else if (command == "autoalarm_on") {
      // 切换到自动报警模式
      manual_alarm_control = false;
      Serial.println("已切换到自动报警模式 - 人体移动将自动触发报警");
      // 重置状态
      device_status.alarm_active = false;
      device_status.motion_detected = false;
      DeviceManager::alarm_off();
    } else if (command == "autoalarm_off") {
      // 切换到手动报警模式并关闭当前报警
      manual_alarm_control = true;
      DeviceManager::alarm_off();
      device_status.alarm_active = false;
      device_status.motion_detected = true;
      Serial.println("已切换到手动报警模式 - 人体移动不会触发报警");
    }
    publishStatus();
    publishFanStatus();
    publishAlarmStatus();
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
  } else if (target_id == alarm_id) {
    if (command == "on" || command == "off") {
        manual_alarm_control = true;
        device_status.alarm_active = (command == "on");
        device_status.alarm_active ? DeviceManager::alarm_on() : DeviceManager::alarm_off();
      publishAlarmStatus();
    } else if (command == "get_status") {
      publishAlarmStatus();
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
      publishAlarmStatus();
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
  alarm_id = generateDeviceID(PIR_SENSOR_PIN);
  light_status_topic = "device/" + light_id + "/status";
  fan_status_topic = "device/" + fan_id + "/status";
  alarm_status_topic = "device/" + alarm_id + "/status";
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  // 打印调试信息
  Serial.println("系统初始化完成");
  Serial.println("人体传感器测试程序");
  Serial.print("人体传感器引脚: ");
  Serial.println(PIR_SENSOR_PIN);
  Serial.print("蜂鸣器引脚: ");
  Serial.println(BUZZER_PIN);
  
  // 先手动测试一下报警功能
  Serial.println("测试报警功能...");
  DeviceManager::alarm_on();
  delay(1000);
  DeviceManager::alarm_off();
  Serial.println("报警测试完成");
  
  // 默认为手动模式（不自动报警）
  manual_alarm_control = true;
  Serial.println("当前为手动报警模式，使用scene/alarm_on命令可切换到自动报警模式");
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  static unsigned long lastStatusTime = 0;
  static unsigned long lastSensorUpdate = 0;
  static unsigned long lastAlarmFlash = 0;
  static unsigned long lastPirPrint = 0;
  unsigned long now = millis();

  if (now - lastSensorUpdate > 1000) {
    if (!manual_led_control) DeviceManager::updateLightBySensor();
    if (!manual_fan_control) DeviceManager::updateFanByLightSensor();
    if (!manual_alarm_control) DeviceManager::checkMotionSensor();
    lastSensorUpdate = now;
  }

  // 报警时红灯闪烁控制
  if (now - lastAlarmFlash > 300 && device_status.alarm_active) {
    DeviceManager::flashRedLED();
    lastAlarmFlash = now;
  }

  if (now - lastStatusTime > 20000 || device_status.alarm_active) {
    publishStatus();
    publishFanStatus();
    publishAlarmStatus();  // 发布报警状态
    lastStatusTime = now;
  }

  delay(10);
}