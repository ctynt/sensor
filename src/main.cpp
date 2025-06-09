#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include <DHT.h> // 使用Adafruit的DHT库

// WiFi配置
const char *ssid = "applepie";
const char *password = "sjdygzdx123";
const char *mqtt_server = "43.142.252.113";
const int mqtt_port = 1883;
const char *mqtt_user = "admin";
const char *mqtt_password = "public";

// 引脚定义
#define LED_RED D0
#define LED_GREEN D2
#define LED_BLUE D3
#define LIGHT_SENSOR_PIN A0
#define FAN_PIN D4
#define PIR_SENSOR_PIN D5 // 人体红外传感器
#define BUZZER_PIN D6      // 有源蜂鸣器 (与风扇共用D5引脚)

#define DHTPIN D1    // DHT数据引脚（根据实际连接修改）
#define DHTTYPE DHT11 // 传感器类型（DHT11/DHT22）

DHT dht(DHTPIN, DHTTYPE);

WiFiClient esp_client;
PubSubClient client(esp_client);

struct DeviceStatus {
  bool led_on = false;
  bool fan_on = false;
  bool buzzer_on = false;
  bool dht_on = false;
  bool sensor_on = false;
  bool pir_on = false;

  int light_level = 0;  
  int alarm_state = 0;  
  float humidity = 0;
  float temperature = 0;
  
  bool led_status = false;
  bool fan_status = false;
  bool buzzer_status = false;
  bool dht_status = false;
  bool pir_status = false;
  bool sensor_status = false;
} device_status;

bool manual_led_control = true;
bool manual_fan_control = true;
bool manual_buzzer_control = true;  

String generateDeviceID(uint8_t pin) {
  String baseID = WiFi.macAddress();
  baseID.replace(":", "");
  return "dev_" + baseID + "_" + String(pin);
}

String light_id;
String fan_id;
String buzzer_id; 
String dht_id;
String pir_id;
String sensor_id;

String light_status_topic;
String fan_status_topic;
String buzzer_status_topic; 
String dht_status_topic;
String pir_status_topic;
String sensor_status_topic;

class DeviceManager {
public:
  static void initDevices() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    led_off();
    fan_off();
    buzzer_off();
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(FAN_PIN, OUTPUT);
    pinMode(PIR_SENSOR_PIN, INPUT); 

    device_status.led_on = false;
    device_status.fan_on = false;
    device_status.buzzer_on = false;
    device_status.dht_on = false;
    device_status.sensor_on = false;
    device_status.pir_on = false;
  }

  static void setColor(bool red, bool green, bool blue) {
    digitalWrite(LED_RED, red ? HIGH : LOW);
    digitalWrite(LED_GREEN, green ? HIGH : LOW);
    digitalWrite(LED_BLUE, blue ? HIGH : LOW);
  }

  static void led_off() { 
    setColor(true, true, true);
    device_status.led_on = false; 
  }
  static void led_on() {
     setColor(false, false, false); 
     device_status.led_on = true;
  }
  static void fan_on() { 
    analogWrite(FAN_PIN, 0); 
    device_status.fan_on = true;
  }
  static void fan_off() { 
    analogWrite(FAN_PIN, 255); 
    device_status.fan_on = false;
  }

  static void buzzer_on()  {
  digitalWrite(BUZZER_PIN, LOW);   
  device_status.buzzer_on = true;  
}

static void buzzer_off() {
  digitalWrite(BUZZER_PIN, HIGH);   
  device_status.buzzer_on = false;  
}


  static void updateLightBySensor() {
    if (manual_led_control) return;
    int lightValue = analogRead(LIGHT_SENSOR_PIN);
    device_status.sensor_on = true;
    device_status.light_level = lightValue;
    Serial.print("光照值: ");
    Serial.println(lightValue);

    if (device_status.led_on && lightValue > 900) {
      led_off();
      Serial.println("光线强 关灯");
    } else if (!device_status.led_on && lightValue <= 900) {
      led_on();
      Serial.println("光线弱 打开灯");
    }
  }

  static void updateFanByLightSensor() {
    if (manual_fan_control) return;
    int lightValue = analogRead(LIGHT_SENSOR_PIN);
    device_status.sensor_on = true;
    device_status.light_level = lightValue;
    Serial.print("光照值: ");
    Serial.println(lightValue);
    if (lightValue <= 900 && !device_status.fan_on) {
      fan_on();
      Serial.println("白天 开风扇");
    } else if (lightValue > 900 && device_status.fan_on) {
      fan_off();
      Serial.println("晚上 关风扇");
    }
  }

  static void checkMotionSensor() {
  if (manual_buzzer_control) return;  

  int motion = digitalRead(PIR_SENSOR_PIN);
  device_status.pir_on = true;
  device_status.alarm_state = motion;
  Serial.println("priValue");
  Serial.println(motion);
  if ((motion == HIGH) && !device_status.buzzer_on) {   
    buzzer_on();                              
  }
  else if (!motion && device_status.buzzer_on) {
    buzzer_off();                            
  }
}

static void updateLightByEnv() {
  if (manual_led_control) return;

  float humidityValue = dht.readHumidity();
  float temperatureValue = dht.readTemperature();
  device_status.humidity = humidityValue;
  device_status.temperature = temperatureValue;
  
  device_status.dht_on = true;
  // 校验
  if (isnan(humidityValue) || isnan(temperatureValue)) {
    Serial.println("读取温湿度失败！");
  } else {
    Serial.print("温度: ");
    Serial.print(temperatureValue);
    Serial.print("°C  湿度: ");
    Serial.print(humidityValue);
    Serial.println("%");

    if (temperatureValue < 30) {

        Serial.println("温度过高，启动风扇...");
        DeviceManager::fan_on();
    } else {
        Serial.println("温度正常，关闭风扇...");
        DeviceManager::fan_off();
    }
  }

  // 读取光照强度
  int lightValue = analogRead(LIGHT_SENSOR_PIN);
  Serial.print("光照强度: ");
  Serial.println(lightValue);
  device_status.sensor_on = true;
  device_status.light_level=lightValue;
  if (lightValue >= 900) {
      Serial.println("环境光照较暗，开启补光LED");
      DeviceManager::led_on();
  } else {
      Serial.println("环境光照充足，关闭补光LED");
      DeviceManager::led_off();
  }

  delay(1000); // 可选：用于节流更新频率
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

void publishLightStatus() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = light_id;
  doc["device_type"] = 1;
  doc["device_name"] = "灯";
  doc["isSwitched"] = device_status.led_on;
  doc["status"] = 1;
  doc["light_level"] = device_status.light_level;
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(light_status_topic.c_str(), buffer);
}

void publishFanStatus() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = fan_id;
  doc["device_type"] = 2;
  doc["device_name"] = "风扇";
  doc["isSwitched"] = device_status.fan_on;
  doc["status"] = 1;
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(fan_status_topic.c_str(), buffer);
}

void publishAlarmStatus() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = buzzer_id;
  doc["device_type"] = 3;
  doc["device_name"] = "蜂鸣器";
  doc["isSwitched"] = device_status.buzzer_on;
  doc["status"] = 1;
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(buzzer_status_topic.c_str(), buffer);
}

void publishTemStatus() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = dht_id;
  doc["device_type"] = 4;
  doc["device_name"] = "温湿度传感器";
  doc["isSwitched"] = device_status.dht_on;
  doc["status"] = 1;
  doc["humidity"] = device_status.humidity;
  doc["temperature"] = device_status.temperature;
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(dht_status_topic.c_str(), buffer);
}

void publishPirStatus() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = pir_id;
  doc["device_type"] = 5;
  doc["device_name"] = "人体红外";
  doc["isSwitched"] = device_status.pir_on;
  doc["alarm_state"] = device_status.alarm_state;
  doc["status"] = 1;
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(pir_status_topic.c_str(), buffer);
}

void publishSensorStatus() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = sensor_id;
  doc["device_type"] = 6;
  doc["device_name"] = "光敏";
  doc["isSwitched"] = device_status.sensor_on;
  doc["light_level"] = device_status.light_level;
  doc["status"] = 1;
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(pir_status_topic.c_str(), buffer);
}

void publishLightOffline() {
  StaticJsonDocument<128> doc;
  doc["device_id"] = light_id;
  doc["device_type"] = 1;
  doc["device_name"] = "灯";
  doc["status"] = 0;
  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(light_status_topic.c_str(), buffer);
}

void publishFanOffline() {
  StaticJsonDocument<128> doc;
  doc["device_id"] = fan_id;
  doc["device_type"] = 2;
  doc["device_name"] = "风扇";
  doc["status"] = 0;
  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(fan_status_topic.c_str(), buffer);
}

void publishAlarmOffline() {
  StaticJsonDocument<128> doc;
  doc["device_id"] = buzzer_id;
  doc["device_type"] = 3;
  doc["device_name"] = "蜂鸣器";
  doc["status"] = 0;
  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(buzzer_status_topic.c_str(), buffer);
}

void publishTemOffline() {
  StaticJsonDocument<128> doc;
  doc["device_id"] = dht_id;
  doc["device_type"] = 4;
  doc["device_name"] = "温湿度传感器";
  doc["status"] = 0;
  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(dht_status_topic.c_str(), buffer);
}

void publishPirOffline() {
  StaticJsonDocument<128> doc;
  doc["device_id"] = pir_id;
  doc["device_type"] = 5;
  doc["device_name"] = "人体红外";
  doc["status"] = 0;
  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(pir_status_topic.c_str(), buffer);
}

void publishSensorOffline() {
  StaticJsonDocument<128> doc;
  doc["device_id"] = sensor_id;
  doc["device_type"] = 6;
  doc["device_name"] = "光敏";
  doc["status"] = 0;
  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(sensor_status_topic.c_str(), buffer);
}


void handleCommand(const JsonObject &cmd, const String &topic) {
  String target_id = cmd["deviceId"] | "";
  String command = cmd["command"] | "";
  Serial.printf("Cmd Topic=%s ID=%s CMD=%s\n", topic.c_str(), target_id.c_str(), command.c_str());

  if (topic.startsWith("scene/")) {
    if (command == "nightlight_on") {
      manual_led_control = false;
      DeviceManager::updateLightBySensor();
      Serial.println("启用夜灯自动控制模式");
    } else if (command == "nightlight_off") {
      manual_led_control = true;
      DeviceManager::led_off();
      device_status.sensor_on = false;
      Serial.println("关闭夜灯，切换为手动模式");
    } else if (command == "autofan_on") {
      manual_led_control = false;
      manual_fan_control = false;
      DeviceManager::updateFanByLightSensor();
      Serial.println("启用风扇自动控制模式");
    } else if (command == "autofan_off") {
      manual_led_control = true;
      manual_fan_control = true;
      DeviceManager::fan_off();
      device_status.sensor_on = false;
      Serial.println("关闭风扇，切换为手动模式");
    } else if (command == "autoalarm_on") {
      // 切换到自动报警模式
      manual_buzzer_control = false;
      Serial.println("人体移动将自动触发报警");
      DeviceManager::checkMotionSensor();
    } else if (command == "autoalarm_off") {
      manual_buzzer_control = true;
      device_status.pir_on = false;
      DeviceManager::buzzer_off();                       
      Serial.println("已切换到手动报警模式");
    } else if (command == "autolight_on") {
       manual_led_control = false;
       manual_fan_control = false;
       DeviceManager::updateLightByEnv();
       Serial.println("已切换到自动环境补光");
    } else if (command == "autolight_off") {
       manual_led_control = true;
       manual_fan_control = true;
       DeviceManager::fan_off();
       DeviceManager::led_off();
       device_status.dht_on=false;
       device_status.sensor_on=false;
       Serial.println("已切换到手动环境补光");
    }

    publishLightStatus();
    publishFanStatus();
    publishAlarmStatus();
    publishTemStatus();
    publishPirStatus();
    publishSensorStatus();
    return;
  }

  if (target_id == light_id) {
    if (command == "on" || command == "off") {
      manual_led_control = true;
      device_status.led_on = (command == "on");
      device_status.led_on ? DeviceManager::led_on() : DeviceManager::led_off();
      publishLightStatus();
    } else if (command == "get_status") {
      publishLightStatus();
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
  } else if (target_id == buzzer_id) {
    if (command == "on" || command == "off") {
        manual_buzzer_control = true;
        device_status.buzzer_on = (command == "on");
        device_status.buzzer_on ? DeviceManager::buzzer_on() : DeviceManager::buzzer_off();
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


bool mqttPreviouslyConnected = false;

void reconnectMQTT() {
  // 如果之前连接过但现在断开，则说明是重连 —— 先发送所有设备下线状态
  if (mqttPreviouslyConnected && !client.connected()) {
    Serial.println("MQTT断开，发布设备下线状态...");
    publishLightOffline();
    publishFanOffline();
    publishAlarmOffline();
    publishTemOffline();
    publishPirOffline();
    publishSensorOffline();
    mqttPreviouslyConnected = false;
  }

  while (!client.connected()) {
    String clientId = "ESP8266-" + WiFi.macAddress();
    clientId.replace(":", "");
    Serial.print("Connecting to MQTT...");
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");
      mqttPreviouslyConnected = true;

      client.subscribe("device/+/control");
      client.subscribe("scene/+/control");

      // 发布上线状态
      publishLightStatus();
      publishFanStatus();
      publishAlarmStatus();
      publishTemStatus();
      publishPirStatus();
      publishSensorStatus();
      
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
  dht.begin();
  light_id = generateDeviceID(LED_RED);
  fan_id = generateDeviceID(FAN_PIN);
  buzzer_id = generateDeviceID(BUZZER_PIN);
  dht_id = generateDeviceID(DHTPIN);
  pir_id = generateDeviceID(PIR_SENSOR_PIN);
  sensor_id =generateDeviceID(LIGHT_SENSOR_PIN);
  
  Serial.println("设备ID生成：");
  Serial.print("Light ID: ");
  Serial.println(light_id);
  Serial.print("Fan ID: ");
  Serial.println(fan_id);
  Serial.print("Buzzer ID: ");
  Serial.println(buzzer_id);
  Serial.print("DHT ID: ");
  Serial.println(dht_id);
  Serial.print("PIR ID: ");
  Serial.println(pir_id);
  Serial.print("Light Sensor ID: ");
  Serial.println(sensor_id);

  
  light_status_topic = "device/" + light_id + "/status";
  fan_status_topic = "device/" + fan_id + "/status";
  buzzer_status_topic = "device/" + buzzer_id + "/status";
  dht_status_topic = "device/" + dht_id + "/status";
  pir_status_topic = "device/" + pir_id + "/status";
  sensor_status_topic = "device/" + sensor_id + "/status";

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  static unsigned long lastStatusTime = 0;
  static unsigned long lastSensorUpdate = 0;
  static unsigned long lastPirCheck = 0;
  unsigned long now = millis();

  // 每10秒上报设备状态
  if (now - lastStatusTime >= 10000) {
      publishLightStatus();
      publishFanStatus();
      publishAlarmStatus();
      publishTemStatus();
      publishPirStatus();
      publishSensorStatus();
    lastStatusTime = now;
  }

  // 每2秒更新光敏传感器状态并控制灯光/风扇（自动模式下）
if (now - lastSensorUpdate >= 2000) {
  if (!manual_led_control || !manual_fan_control) {
    // 若是环境补光模式下，仅用环境光逻辑控制
    DeviceManager::updateLightByEnv();
  } else {
    // 若是夜灯/风扇自动控制
    DeviceManager::updateLightBySensor();
    DeviceManager::updateFanByLightSensor();
  }
  lastSensorUpdate = now;
}


  // 每1秒检测是否有人体移动（自动模式下）
  if (now - lastPirCheck >= 1000) {
    DeviceManager::checkMotionSensor();
    lastPirCheck = now;
  }
}

 