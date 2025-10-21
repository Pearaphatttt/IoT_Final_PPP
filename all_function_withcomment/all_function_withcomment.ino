#include <ESP8266WiFi.h>                 // Wi-Fi สำหรับ ESP8266
#include <WiFiClientSecure.h>            // TLS สำหรับ Telegram
#include <UniversalTelegramBot.h>        // ไลบรารี Telegram Bot
#include <ESP8266HTTPClient.h>           // HTTPClient สำหรับเรียก REST Firebase
#include <WiFiClientSecureBearSSL.h>     // TLS client สำหรับ Firebase
#include <DHT.h>                         // ไลบรารี DHT22
#include <Wire.h>                        // I2C สำหรับ BH1750
#include <BH1750.h>                      // ไลบรารี BH1750 (วัดแสง)
#include <time.h>                        // NTP time

#define DHTPIN   D2                      // ขา DATA ของ DHT22 ใช้ D2
#define DHTTYPE  DHT22                   // ประเภทเซนเซอร์ DHT
DHT dht(DHTPIN, DHTTYPE);                // สร้างอ็อบเจ็กต์ DHT

#define LED_RED  D7                      // ไฟ/รีเลย์แจ้งเตือน (ตัวอย่าง) ใช้ D7

#define TRIG_PIN D0                      // Ultrasonic: TRIG = D0
#define ECHO_PIN D1                      // Ultrasonic: ECHO = D1 (ผ่านตัวแบ่งแรงดัน)

#define SDA_PIN  D4                      // BH1750: SDA = D4
#define SCL_PIN  D3                      // BH1750: SCL = D3
BH1750 lightMeter;                       // อ็อบเจ็กต์วัดแสง (ค่าเริ่มต้นที่อยู่ 0x23)

const char* ssid     = "P";              // ชื่อ Wi-Fi
const char* password = "pppppppp";       // รหัสผ่าน Wi-Fi

#define BOT_TOKEN "8272302286:AAGDHjg8xaVWAxOSwjo_7DWaJzcLNWGzSgE"  // โทเคน Telegram Bot
#define CHAT_ID   "6009634163"                                     // Chat ID ปลายทาง

WiFiClientSecure telegram_client;         // TLS client สำหรับ Telegram
UniversalTelegramBot bot(BOT_TOKEN, telegram_client); // ตัวบอท

const char* RTDB_HOST = "testexsamplefinaliot01-default-rtdb.asia-southeast1.firebasedatabase.app"; // โฮสต์ RTDB (ไม่ใส่ https://) 
const char* DATABASE_SECRET = "I9xnnyp3W8Tqs4qm3hI012cOwopS6U91t3MLuZtD";                           // Database secret (เดโม)

unsigned long lastSample = 0;            // เวลาอ่านครั้งล่าสุด (ms)
const unsigned long SAMPLE_EVERY_MS = 5000; // อ่านทุก 5 วินาที

// ธงกันสแปมการแจ้งเตือน (ส่งครั้งเดียวเมื่อเข้าเงื่อนไข แล้วรีเซ็ตเมื่อกลับปกติ)
bool sentTempHigh = false;               // เตือนอุณหภูมิสูง
bool sentHumiHigh = false;               // เตือนความชื้นสูง
bool sentDistLow  = false;               // เตือนระยะต่ำ
bool sentLuxLow   = false;               // เตือนแสงต่ำ

void BlynkLED(){                         // ฟังก์ชันกระพริบไฟเป็นการแจ้งเตือนภายในสถานที่
  for(int i=0; i<=10; i++){              // กระพริบ 11 รอบ
    digitalWrite(LED_RED, HIGH);         // ติด
    delay(1000);                         // 1 วินาที
    digitalWrite(LED_RED, LOW);          // ดับ
    delay(1000);                         // 1 วินาที
  }
}

void setupTime() {                        // ตั้งค่า NTP เพื่อได้เวลาจริง (UTC+7)
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // ตั้ง timezone +7
  for (int i=0; i<30 && time(nullptr) < 100000; ++i) { delay(500); } // รอเวลาเซ็ต
}

float readDistance_cm() {                 // อ่านระยะทางจาก Ultrasonic (cm)
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);  // เคลียร์พัลส์
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);// TRIG 10µs
  digitalWrite(TRIG_PIN, LOW);                         // จบพัลส์

  unsigned long us = pulseIn(ECHO_PIN, HIGH, 30000UL); // วัดเวลาที่ ECHO=HIGH (timeout 30ms)
  if (us == 0) return NAN;                  // ถ้าเกินระยะ/ไม่สะท้อน คืน NAN
  return (us * 0.0343f) / 2.0f;             // ระยะ(cm) = เวลา(µs)*0.0343/2 (ไป-กลับ)
}

bool firebasePUT(const String& url, const String& json) { // PUT ข้อมูลขึ้น RTDB ด้วย REST
  std::unique_ptr<BearSSL::WiFiClientSecure> fb(new BearSSL::WiFiClientSecure()); // TLS client
  fb->setInsecure();                          // เดโม: ไม่ตรวจ cert (โปรดักชันควร pin cert)

  HTTPClient https;                           // ตัวช่วย HTTP
  if (!https.begin(*fb, url)) {               // เปิดการเชื่อมต่อไปยัง URL
    Serial.println("[FB] begin() failed");    // เริ่มไม่สำเร็จ
    return false;                             // คืน false
  }
  https.addHeader("Content-Type", "application/json"); // กำหนด header
  int code = https.PUT(json);                 // ส่งคำสั่ง PUT พร้อมเพย์โหลด JSON
  bool ok = (code >= 200 && code < 300);      // สำเร็จเมื่อรหัส 2xx
  Serial.printf("[FB] PUT %s -> %d\n", url.c_str(), code); // พิมพ์สถานะ
  if (!ok) Serial.printf("[FB] resp: %s\n", https.getString().c_str()); // แสดง response เมื่อผิดพลาด
  https.end();                                // ปิดการเชื่อมต่อ
  return ok;                                  // คืนผลลัพธ์
}

void sendOnce(bool &flag, const String& msg) { // ส่งข้อความ Telegram หนึ่งครั้งต่อสถานะ
  if (!flag) {                                 // ถ้ายังไม่เคยส่งในสถานะนี้
    bot.sendMessage(CHAT_ID, msg, "");         // ส่งข้อความ (ไม่มี markdown)
    flag = true;                               // ตั้งธงว่าถูกส่งแล้ว
  }
}
void resetFlag(bool &flag) { if (flag) flag = false; } // รีเซ็ตธงเมื่อค่ากลับสู่ปกติ

void setup() {
  Serial.begin(115200);                        // เปิด Serial สำหรับดีบัก
  delay(200);                                  // หน่วงเล็กน้อย
  pinMode(LED_RED, OUTPUT);                    // ตั้งขาไฟเป็นเอาต์พุต

  pinMode(TRIG_PIN, OUTPUT);                   // TRIG เป็นเอาต์พุต
  pinMode(ECHO_PIN, INPUT);                    // ECHO เป็นอินพุต (ผ่านตัวแบ่งแรงดัน)
  digitalWrite(TRIG_PIN, LOW);                 // เคลียร์ TRIG

  dht.begin();                                 // เริ่มต้น DHT22

  Wire.begin(SDA_PIN, SCL_PIN);                // เริ่ม I2C (SDA=D4, SCL=D3)
  if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) { // เริ่ม BH1750 โหมดละเอียดต่อเนื่อง
    Serial.println("BH1750 init failed. Check wiring/address."); // แจ้งถ้าล้มเหลว
  } else {
    Serial.println("BH1750 ready");            // พร้อมใช้งาน
  }

  WiFi.begin(ssid, password);                  // ต่อ Wi-Fi
  Serial.print("Connecting WiFi");             // แจ้งสถานะ
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); } // รอเชื่อมต่อ
  Serial.println(" Connected!");               // เชื่อมสำเร็จ

  telegram_client.setInsecure();               // เดโม: ไม่ตรวจ cert Telegram
  bot.sendMessage(CHAT_ID, "ESP8266 ready", ""); // แจ้งเริ่มพร้อมใช้งาน

  setupTime();                                 // ตั้งค่า NTP สำหรับ timestamp
}

void loop() {
  if (millis() - lastSample < SAMPLE_EVERY_MS) return; // ยังไม่ถึงรอบอ่าน ขอกลับก่อน
  lastSample = millis();                      // ปรับเวลารอบล่าสุด

  float t   = dht.readTemperature();          // อ่านอุณหภูมิ (°C)
  float h   = dht.readHumidity();             // อ่านความชื้น (%RH)
  float dcm = readDistance_cm();              // อ่านระยะทาง (cm)
  float lux = lightMeter.readLightLevel();    // อ่านความสว่าง (lx)
  if (lux < 0) lux = NAN;                     // บางครั้ง BH1750 อาจคืนค่าติดลบ ให้ถือว่าอ่านผิด

  Serial.printf("T=%s C | H=%s %% | D=%s cm | L=%s lx\n", // สรุปค่าที่อ่านได้
    isnan(t) ? "-" : String(t,1).c_str(),
    isnan(h) ? "-" : String(h,1).c_str(),
    isnan(dcm) ? "-" : String(dcm,1).c_str(),
    isnan(lux) ? "-" : String(lux,1).c_str()
  );

  // ===== เงื่อนไขแจ้งเตือน Telegram (ไม่มีอีโมจิ) =====
  if (!isnan(t) && t > 30.0) {                // ถ้าอุณหภูมิ > 30°C
    sendOnce(sentTempHigh, "Temperature high: " + String(t,1) + " C"); // ส่งครั้งเดียวต่อสถานะ
  } else {
    resetFlag(sentTempHigh);                  // กลับสู่ปกติ รีเซ็ตให้พร้อมแจ้งครั้งต่อไป
  }

  if (!isnan(h) && h > 80.0) {                // ถ้าความชื้น > 80%RH
    sendOnce(sentHumiHigh, "Humidity high: " + String(h,1) + " %"); // ส่งเตือน
    BlynkLED();                               // กระพริบไฟเป็นสัญญาณภายใน
  } else {
    resetFlag(sentHumiHigh);                  // รีเซ็ตธง
  }

  if (!isnan(dcm) && dcm < 10.0) {            // ถ้าระยะทาง < 10 cm
    sendOnce(sentDistLow, "Distance low: " + String(dcm,1) + " cm"); // ส่งเตือน
  } else {
    resetFlag(sentDistLow);                   // รีเซ็ตธง
  }

  if (!isnan(lux) && lux < 10.0) {            // ถ้าแสง < 10 lux
    sendOnce(sentLuxLow, "Light low: " + String(lux,1) + " lx"); // ส่งเตือน
  } else {
    resetFlag(sentLuxLow);                    // รีเซ็ตธง
  }

  // ===== อัปเดตขึ้น Firebase (latest + history) =====
  time_t now = time(nullptr);                 // เวลาปัจจุบัน (วินาที)
  unsigned long ts = (now > 100000) ? (unsigned long)now * 1000UL : millis(); // timestamp เป็น ms
  String base = String("https://") + RTDB_HOST; // โครงสร้าง URL พื้นฐาน

  String urlLatest = base + "/latest.json?auth=" + DATABASE_SECRET;          // ปลายทางเก็บค่าสุดท้าย
  String urlHist   = base + "/history/" + String(ts) + ".json?auth=" + DATABASE_SECRET; // ปลายทางเก็บประวัติ

  String jsonLatest = "{";                    // สร้างเพย์โหลด JSON ของ /latest
  bool comma = false;                         // ตัวช่วยใส่คอมมา
  if (!isnan(t))   { jsonLatest += "\"temperature\":" + String(t,1); comma = true; } // temp
  if (!isnan(h))   { jsonLatest += (comma?",":"") + String("\"humidity\":") + String(h,1); comma = true; } // hum
  jsonLatest       += (comma?",":"") + String("\"distance\":") + (isnan(dcm)? String("null") : String(dcm,1)); comma = true; // dist
  jsonLatest       += (comma?",":"") + String("\"lux\":")     + (isnan(lux)? String("null") : String(lux,1)); // lux
  jsonLatest       += ",\"ts\":" + String(ts) + "}";           // time stamp

  String jsonHist = "{";                      // สร้างเพย์โหลด JSON ของ /history/<ts>
  comma = false;                              // รีเซ็ตตัวช่วยคอมมา
  if (!isnan(t))   { jsonHist += "\"temperature\":" + String(t,1); comma = true; } // temp
  if (!isnan(h))   { jsonHist += (comma?",":"") + String("\"humidity\":") + String(h,1); comma = true; } // hum
  jsonHist         += (comma?",":"") + String("\"distance\":") + (isnan(dcm)? String("null") : String(dcm,1)); comma = true; // dist
  jsonHist         += (comma?",":"") + String("\"lux\":")      + (isnan(lux)? String("null") : String(lux,1)); // lux
  jsonHist         += "}";                     // ปิด JSON

  bool a = firebasePUT(urlLatest, jsonLatest); // PUT /latest
  bool b = firebasePUT(urlHist,   jsonHist);   // PUT /history/<ts>
  if (a && b) {
    Serial.println("[OK] Firebase latest + history updated"); // รายงานผล
  }
}
