#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <DHT.h>

#define DHTPIN D2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define GREEN_LED D6
#define RED_LED   D7
#define TRIG_PIN  D0
#define ECHO_PIN  D1

const char* ssid     = "P";
const char* password = "pppppppp";

#define BOT_TOKEN "8272302286:AAGDHjg8xaVWAxOSwjo_7DWaJzcLNWGzSgE"
#define CHAT_ID   "6009634163"
#define LED       D7

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

unsigned long lastTimeBotRan = 0;
const unsigned long BOT_MTBS = 1000;
unsigned long lastSentTime = 0;
const unsigned long SEND_INTERVAL = 10000;

float temp;
float humi;

unsigned long lastCheckTime = 0;
const unsigned long checkInterval = 5000;

bool isFanOn = false;
bool isHeaterOn = false;
bool tempAlertSent = false;
bool humidityAlertSent = false;
bool waterAlertSent = false;
bool sendingEnabled = true;
float level_water;
float TankHeight = 40;

float getlevel_water(){
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  float level_water = TankHeight - (duration * 0.034 / 2) ;
  return level_water;
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED, OUTPUT);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  WiFi.begin(ssid, password);
  secured_client.setInsecure();

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");

  bot.sendMessage(CHAT_ID, "ESP8266 Bot ready", "");
  Serial.println("Bot ready!");
}

void loop() {

  if (millis() - lastCheckTime > checkInterval) {
    lastCheckTime = millis();

    temp = dht.readTemperature();
    humi = dht.readHumidity();
    level_water = getlevel_water();

    if (isnan(humi) || isnan(temp)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    Serial.print("Temp : ");
    Serial.println(temp);
    Serial.print("Humi : ");
    Serial.println(humi);
    Serial.print("Level Water : ");
    Serial.println(level_water);

    if (temp > 30) {
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED, LOW);
      if (!isFanOn) {
        bot.sendMessage(CHAT_ID, "Alert Temp High!!!! FAN ON(LED GREEN ON)", "");
        isFanOn = true;
        isHeaterOn = false;
      }
    } else if (temp < 25) {
      digitalWrite(RED_LED, HIGH);
      digitalWrite(GREEN_LED, LOW);
      if (!isHeaterOn) {
        bot.sendMessage(CHAT_ID, "Alert Temp Low!!!! Heater ON(LED RED ON)", "");
        isHeaterOn = true;
        isFanOn = false;
      }
    } else {
      digitalWrite(RED_LED, LOW);
      digitalWrite(GREEN_LED, LOW);
      isFanOn = false;
      isHeaterOn = false;
    }

    if (temp < 25 || temp > 30) {
      if (!tempAlertSent) {
        String msg = "⚠️Alert!!!! TempOutOfRange !!!! ";
        bot.sendMessage(CHAT_ID, msg, "");
        tempAlertSent = true;
      }
    } else {
      tempAlertSent = false;
    }

    if (humi < 60 || humi > 75) {
      if (!humidityAlertSent) {
        String msg = "💧 Alert!!!! HumiOutOfRange!!!!!!";
        bot.sendMessage(CHAT_ID, msg, "");
        humidityAlertSent = true;
      }
    } else {
      humidityAlertSent = false;
    }

    if (level_water < 5 && level_water > 0) {
      if (!waterAlertSent) {
        String msg = "🌊 Level Water less than 5 CM | level water is  " + String(level_water) + " cm";
        bot.sendMessage(CHAT_ID, msg, "");
        waterAlertSent = true;
      }
    } else {
      waterAlertSent = false;
    }
  }
}
