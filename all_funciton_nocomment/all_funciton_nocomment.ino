#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>           
#include <UniversalTelegramBot.h>
#include <ESP8266HTTPClient.h>           
#include <WiFiClientSecureBearSSL.h>    
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <time.h>

#define DHTPIN   D2
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);
#define LED_RED D7

#define TRIG_PIN D0
#define ECHO_PIN D1

#define SDA_PIN  D4
#define SCL_PIN  D3
BH1750 lightMeter; 

const char* ssid     = "P";
const char* password = "pppppppp";

#define BOT_TOKEN "8272302286:AAGDHjg8xaVWAxOSwjo_7DWaJzcLNWGzSgE"
#define CHAT_ID   "6009634163"

WiFiClientSecure telegram_client;
UniversalTelegramBot bot(BOT_TOKEN, telegram_client);

const char* RTDB_HOST = "testexsamplefinaliot01-default-rtdb.asia-southeast1.firebasedatabase.app"; 
const char* DATABASE_SECRET = "I9xnnyp3W8Tqs4qm3hI012cOwopS6U91t3MLuZtD";

unsigned long lastSample = 0;
const unsigned long SAMPLE_EVERY_MS = 5000;

bool sentTempHigh = false;
bool sentHumiHigh = false;
bool sentDistLow  = false;
bool sentLuxLow   = false;
void BlynkLED(){
  for(int i=0; i<=10; i++){
    digitalWrite(LED_RED, HIGH);
    delay(1000);
    digitalWrite(LED_RED, LOW);
    delay(1000);
  }
}

void setupTime() {

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  for (int i=0; i<30 && time(nullptr) < 100000; ++i) { delay(500); }
}

float readDistance_cm() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long us = pulseIn(ECHO_PIN, HIGH, 30000UL);
  if (us == 0) return NAN;
  return (us * 0.0343f) / 2.0f;
}

bool firebasePUT(const String& url, const String& json) {
  std::unique_ptr<BearSSL::WiFiClientSecure> fb(new BearSSL::WiFiClientSecure());
  fb->setInsecure(); 

  HTTPClient https;
  if (!https.begin(*fb, url)) {
    Serial.println("[FB] begin() failed");
    return false;
  }
  https.addHeader("Content-Type", "application/json");
  int code = https.PUT(json);
  bool ok = (code >= 200 && code < 300);
  Serial.printf("[FB] PUT %s -> %d\n", url.c_str(), code);
  if (!ok) Serial.printf("[FB] resp: %s\n", https.getString().c_str());
  https.end();
  return ok;
}

void sendOnce(bool &flag, const String& msg) {
  if (!flag) {
    bot.sendMessage(CHAT_ID, msg, ""); 
    flag = true;
  }
}
void resetFlag(bool &flag) { if (flag) flag = false; }

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(LED_RED, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT); 
  digitalWrite(TRIG_PIN, LOW);

  dht.begin();

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 init failed. Check wiring/address.");
  } else {
    Serial.println("BH1750 ready");
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(" Connected!");

  telegram_client.setInsecure();
  bot.sendMessage(CHAT_ID, "ESP8266 ready", "");

  setupTime();
}

void loop() {
  if (millis() - lastSample < SAMPLE_EVERY_MS) return;
  lastSample = millis();

  float t   = dht.readTemperature();
  float h   = dht.readHumidity();
  float dcm = readDistance_cm();
  float lux = lightMeter.readLightLevel();
  if (lux < 0) lux = NAN;

  Serial.printf("T=%s C | H=%s %% | D=%s cm | L=%s lx\n",
    isnan(t) ? "-" : String(t,1).c_str(),
    isnan(h) ? "-" : String(h,1).c_str(),
    isnan(dcm) ? "-" : String(dcm,1).c_str(),
    isnan(lux) ? "-" : String(lux,1).c_str()
  );

  if (!isnan(t) && t > 30.0) {
    sendOnce(sentTempHigh, "Temperature high: " + String(t,1) + " C");
    
  } else {
    resetFlag(sentTempHigh);
  }

  if (!isnan(h) && h > 80.0) {
    sendOnce(sentHumiHigh, "Humidity high: " + String(h,1) + " %");
    BlynkLED();
  } else {
    resetFlag(sentHumiHigh);
  }

  if (!isnan(dcm) && dcm < 10.0) {
    sendOnce(sentDistLow, "Distance low: " + String(dcm,1) + " cm");
  } else {
    resetFlag(sentDistLow);
  }

  if (!isnan(lux) && lux < 10.0) {
    sendOnce(sentLuxLow, "Light low: " + String(lux,1) + " lx");
  } else {
    resetFlag(sentLuxLow);
  }

  time_t now = time(nullptr);
  unsigned long ts = (now > 100000) ? (unsigned long)now * 1000UL : millis();
  String base = String("https://") + RTDB_HOST;

  String urlLatest = base + "/latest.json?auth=" + DATABASE_SECRET;
  String urlHist   = base + "/history/" + String(ts) + ".json?auth=" + DATABASE_SECRET;

  String jsonLatest = "{";
  bool comma = false;
  if (!isnan(t))   { jsonLatest += "\"temperature\":" + String(t,1); comma = true; }
  if (!isnan(h))   { jsonLatest += (comma?",":"") + String("\"humidity\":") + String(h,1); comma = true; }
  jsonLatest       += (comma?",":"") + String("\"distance\":") + (isnan(dcm)? String("null") : String(dcm,1)); comma = true;
  jsonLatest       += (comma?",":"") + String("\"lux\":")     + (isnan(lux)? String("null") : String(lux,1));
  jsonLatest       += ",\"ts\":" + String(ts) + "}";

  String jsonHist = "{";
  comma = false;
  if (!isnan(t))   { jsonHist += "\"temperature\":" + String(t,1); comma = true; }
  if (!isnan(h))   { jsonHist += (comma?",":"") + String("\"humidity\":") + String(h,1); comma = true; }
  jsonHist         += (comma?",":"") + String("\"distance\":") + (isnan(dcm)? String("null") : String(dcm,1)); comma = true;
  jsonHist         += (comma?",":"") + String("\"lux\":")      + (isnan(lux)? String("null") : String(lux,1));
  jsonHist         += "}";

  bool a = firebasePUT(urlLatest, jsonLatest);
  bool b = firebasePUT(urlHist,   jsonHist);
  if (a && b) {
    Serial.println("[OK] Firebase latest + history updated");
  }
}
