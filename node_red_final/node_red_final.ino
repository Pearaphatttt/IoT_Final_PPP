#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>                     // ใช้สร้าง client แบบ HTTPS (TLS)
#include <UniversalTelegramBot.h>   
#include <PubSubClient.h>
#include <DHT.h>

// ==== CONFIG ====
const char* mqttServer = "broker.hivemq.com";
const int   mqttPort   = 1883;

const char* WIFI_NAME     = "P";
const char* WIFI_PASSWORD = "pppppppp";
#define BOT_TOKEN "8272302286:AAGDHjg8xaVWAxOSwjo_7DWaJzcLNWGzSgE"        // โทเคนบอทที่ได้จาก BotFather 
#define CHAT_ID   "6009634163"   
// MQTT Topics
const char* TOPIC_TEMP = "peeraphat2233/temperature";
const char* TOPIC_HUMI = "peeraphat2233/humi";
// ==== LED ==== 
#define LED_RED D7
#define LED_GREEN D6
#define LED_YELLOW D0
// ==== DHT ====
#define DHT_PIN  D2
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

WiFiClient espClient;
PubSubClient mqtt(espClient);

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.println("Message arrived [" + String(topic) + "]: " + message);

  // ตัวอย่างการควบคุมไฟ
  if (String(topic) == "peeraphat-led123") {
    if (message == "Red_ON") digitalWrite(LED_RED, HIGH);
    else if (message == "Red_OFF") digitalWrite(LED_RED, LOW);

    else if (message == "Green_ON") digitalWrite(LED_GREEN, HIGH);
    else if (message == "Green_OFF") digitalWrite(LED_GREEN, LOW);

    else if (message == "Yellow_ON") digitalWrite(LED_YELLOW, HIGH);
    else if (message == "Yellow_OFF") digitalWrite(LED_YELLOW, LOW);
  }
}

// ---- helpers ----
void wifiReconnect() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting WiFi...");
  }
  Serial.println("WiFi connected! IP: " + WiFi.localIP().toString());
}

void mqttReconnect() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT ...");
    if (mqtt.connect("TS-VAS")) {   // เปลี่ยน clientID ถ้ามีหลายบอร์ด
      Serial.println("connected");
    } else {
      Serial.print("failed rc=");
      Serial.print(mqtt.state());
      Serial.println(" retry in 1s");
      delay(1000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello, ESP8266!");
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  WiFi.mode(WIFI_STA);
  wifiReconnect();

  mqtt.setServer(mqttServer, mqttPort);
  mqtt.setCallback(callback);
  mqttReconnect();

  dht.begin();
  mqtt.subscribe("peeraphat-led123");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) wifiReconnect();
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  // อ่านแบบไม่บล็อกทุก ~2.5 วินาที (DHT22 จำกัด ~0.5Hz)
  static unsigned long lastRead = 0;
  const unsigned long READ_INTERVAL_MS = 2500;

  if (millis() - lastRead >= READ_INTERVAL_MS) {
    lastRead = millis();

    float temperature = dht.readTemperature(); // °C
    float humi        = dht.readHumidity();    // %

    if (isnan(temperature) || isnan(humi)) {
      Serial.println("DHT read failed, skip this round.");
      return;
    }

    // ส่ง MQTT (retain = true)
    String payloadTemp = String(temperature, 2);
    mqtt.publish(TOPIC_TEMP, payloadTemp.c_str(), true);

    String payloadHumi = String(humi, 2);
    mqtt.publish(TOPIC_HUMI, payloadHumi.c_str(), true);

    Serial.printf("Temp: %.2f C, Humi: %.2f %%\n", temperature, humi);
  }
}
