#include <ESP8266WiFi.h>                          // ใช้คลาส/ฟังก์ชันสำหรับเชื่อม Wi-Fi บน ESP8266
#include <WiFiClientSecure.h>                     // ใช้สร้าง client แบบ HTTPS (TLS)
#include <UniversalTelegramBot.h>                 // ไลบรารี Telegram Bot (อาศัย ArduinoJson ภายใน)
#include <DHT.h>                                  // ไลบรารีอ่านเซ็นเซอร์ DHT (อุณหภูมิ/ความชื้น)

#define DHTPIN D2                                 // ระบุขาที่ต่อ DATA ของ DHT ไว้ที่ D2 (GPIO4)
#define DHTTYPE DHT22                             // ระบุชนิดเซ็นเซอร์เป็น DHT22 (ถ้าเป็น DHT11 ให้เปลี่ยน)
DHT dht(DHTPIN, DHTTYPE);                         // สร้างวัตถุ DHT ด้วยพินและชนิดที่กำหนด

#define GREEN_LED D6                              // กำหนดพินไฟเขียว (แทน “พัดลม”) อยู่ที่ D6 (GPIO12)
#define RED_LED   D7                              // กำหนดพินไฟแดง (แทน “ฮีตเตอร์”) อยู่ที่ D7 (GPIO13)
#define TRIG_PIN  D0                              // พิน TRIG ของอัลตราโซนิก ใช้ D0 (GPIO16) (เป็น output)
#define ECHO_PIN  D1                              // พิน ECHO ของอัลตราโซนิก ใช้ D1 (GPIO5) (เป็น input)

// Wi-Fi
const char* ssid     = "P";                       // ชื่อเครือข่าย Wi-Fi ที่จะเชื่อมต่อ
const char* password = "pppppppp";                // รหัสผ่าน Wi-Fi

// Telegram
#define BOT_TOKEN "8272302286:AAGDHjg8xaVWAxOSwjo_7DWaJzcLNWGzSgE"        // โทเคนบอทที่ได้จาก BotFather 
#define CHAT_ID   "6009634163"                 // Chat ID ปลายทางที่ต้องการส่งข้อความ
#define LED       D7                               // สัญลักษณ์ LED (ซ้ำกับ RED_LED) ใช้เป็นตัวอย่างตั้ง MODE

WiFiClientSecure secured_client;                   // สร้าง client แบบ TLS/SSL สำหรับคุยกับ Telegram (HTTPS)
UniversalTelegramBot bot(BOT_TOKEN, secured_client); // สร้างอ็อบเจ็กต์บอท ผูกกับ client และ token

// Interval settings
unsigned long lastTimeBotRan = 0;                  // ตัวแปรเก็บเวลา “ครั้งล่าสุดที่เช็คบอท” (ยังไม่ได้ใช้จริงในโค้ดนี้)
const unsigned long BOT_MTBS = 1000;               // ค่าช่วงเวลาระหว่างเช็คข้อความบอท (ms) (ยังไม่ได้ใช้จริง)
unsigned long lastSentTime = 0;                    // เวลา “ครั้งล่าสุดที่ส่งข้อความ” (ยังไม่ได้ใช้จริง)
const unsigned long SEND_INTERVAL = 10000;         // ระยะห่างส่งข้อความซ้ำ (ms) (ยังไม่ได้ใช้จริง)

float temp;                                        // ตัวแปรเก็บค่าอุณหภูมิ (°C)
float humi;                                        // ตัวแปรเก็บค่าความชื้น (%RH)

// --- Variables for Timing and State Control (Add these) ---
unsigned long lastCheckTime = 0;                   // เวลา “ครั้งล่าสุดที่อ่านเซ็นเซอร์/ประมวลผล”
const unsigned long checkInterval = 5000;          // อ่านเซ็นเซอร์ทุก 5 วินาที (ms)

// State flags to prevent spamming alerts
bool isFanOn = false;                               // สถานะ “พัดลมเปิดอยู่ไหม” (ใช้กันส่งข้อความซ้ำ)
bool isHeaterOn = false;                            // สถานะ “ฮีตเตอร์เปิดอยู่ไหม”
bool tempAlertSent = false;                         // เคยส่ง “เตือนอุณหภูมิผิดปกติ” ไปรึยัง (กันสแปม)
bool humidityAlertSent = false;                     // เคยส่ง “เตือนความชื้นผิดปกติ” ไปรึยัง
bool waterAlertSent = false;                        // เคยส่ง “เตือนระดับน้ำต่ำ” ไปรึยัง
bool sendingEnabled = true;                         // ธงเปิด/ปิดการส่ง (ในโค้ดนี้ยังไม่ถูกใช้ควบคุมจริง)
float level_water;                                  // ตัวแปรเก็บ “ระดับน้ำที่คำนวณได้” (cm)
float TankHeight = 40;                              // ความสูงถัง (cm) สมมุติว่าจุดเซ็นเซอร์อยู่บนหัวถัง

float getlevel_water(){                             // ฟังก์ชันวัดระดับน้ำจากอัลตราโซนิกแล้วคืนค่าเป็นเซนติเมตร
  digitalWrite(TRIG_PIN, LOW);                      // เคลียร์ TRIG ให้ LOW สั้น ๆ (เตรียมยิงพัลส์)
  delayMicroseconds(2);                             // หน่วง 2 ไมโครวินาทีตามสเปก
  digitalWrite(TRIG_PIN, HIGH);                     // ให้ TRIG เป็น HIGH เพื่อเริ่มยิงคลื่น
  delayMicroseconds(10);                            // ค้าง HIGH 10 ไมโครวินาที (ตามสเปก HC-SR04)
  digitalWrite(TRIG_PIN, LOW);                      // ดึงกลับ LOW เพื่อจบพัลส์

  long duration = pulseIn(ECHO_PIN, HIGH);          // วัดเวลาที่ ECHO เป็น HIGH (ไมโครวินาที) — *ไม่มี timeout*
  float level_water = TankHeight - (duration * 0.034 / 2) ; // ระยะ (cm) ≈ duration*0.034/2 → ระดับน้ำ = สูงถัง − ระยะ
  return level_water;                               // คืนค่าระดับน้ำ (cm)
}

void setup() {
  Serial.begin(115200);                             // เปิด Serial ที่ 115200 สำหรับดู log/ดีบัก
  dht.begin();                                      // เริ่มต้น DHT ให้พร้อมอ่านค่า

  pinMode(RED_LED, OUTPUT);                         // ตั้งพินไฟแดงเป็นเอาต์พุต
  pinMode(GREEN_LED, OUTPUT);                       // ตั้งพินไฟเขียวเป็นเอาต์พุต
  pinMode(TRIG_PIN, OUTPUT);                        // TRIG ของอัลตราโซนิกเป็นเอาต์พุต
  pinMode(ECHO_PIN, INPUT);                         // ECHO ของอัลตราโซนิกเป็นอินพุต (ควรลดแรงดัน 5V→3.3V)
  pinMode(LED, OUTPUT);                             // ตั้งพิน LED (D7) เป็นเอาต์พุต (ซ้ำกับ RED_LED แต่ไม่กระทบ)
  digitalWrite(RED_LED, LOW);                       // ดับไฟแดงเริ่มต้น
  digitalWrite(GREEN_LED, LOW);                     // ดับไฟเขียวเริ่มต้น

  WiFi.begin(ssid, password);                       // เริ่มเชื่อมต่อเครือข่าย Wi-Fi
  secured_client.setInsecure();                     // ปิดการตรวจใบรับรอง TLS (ง่ายขึ้น แต่ไม่ปลอดภัยสำหรับโปรดักชัน)

  Serial.print("Connecting WiFi");                  // พิมพ์สถานะกำลังเชื่อม Wi-Fi
  while (WiFi.status() != WL_CONNECTED) {           // ลูปรอจนกว่าจะเชื่อมต่อได้
    delay(500);                                     // หน่วงครึ่งวินาที
    Serial.print(".");                              // พิมพ์จุดคืบหน้า
  }
  Serial.println(" Connected!");                    // แจ้งเชื่อมต่อสำเร็จ

  bot.sendMessage(CHAT_ID, "ESP8266 Bot ready", ""); // ส่งข้อความเข้า Telegram แจ้งว่า “พร้อมใช้งาน”
  Serial.println("Bot ready!");                     // พิมพ์ log ลง Serial
}

void loop() {

  if (millis() - lastCheckTime > checkInterval) {   // ถ้าถึงรอบเวลาอ่าน/ประมวลผล (ทุก 5 วินาที)
    lastCheckTime = millis();                       // อัปเดตเวลา “ครั้งล่าสุด”

    temp = dht.readTemperature();                   // อ่านอุณหภูมิ (°C) จาก DHT
    humi = dht.readHumidity();                      // อ่านความชื้นสัมพัทธ์ (%) จาก DHT
    level_water = getlevel_water();                 // คำนวณระดับน้ำ (cm) จากอัลตราโซนิก

    if (isnan(humi) || isnan(temp)) {               // ถ้าอ่าน DHT พลาด (ได้ NaN)
      Serial.println("Failed to read from DHT sensor!"); // แจ้งเตือนใน Serial
      return;                                       // ออกจาก loop() รอบนี้ (ข้ามส่วนเงื่อนไขด้านล่าง)
    }

    Serial.print("Temp : ");                        // พิมพ์ค่า Temp ลง Serial
    Serial.println(temp);
    Serial.print("Humi : ");                        // พิมพ์ค่า Humi ลง Serial
    Serial.println(humi);
    Serial.print("Level Water : ");                 // พิมพ์ระดับน้ำลง Serial
    Serial.println(level_water);

    if (temp > 30) {                                // เงื่อนไขอุณหภูมิสูงเกิน 30°C
      digitalWrite(GREEN_LED, HIGH);                // เปิด “พัดลม” (ไฟเขียวติด)
      digitalWrite(RED_LED, LOW);                   // ปิด “ฮีตเตอร์” (ไฟแดงดับ)
      if (!isFanOn) {                               // ถ้าพัดลมยังไม่ถูกบันทึกว่า ON (กันข้อความซ้ำ)
        bot.sendMessage(CHAT_ID, "Alert Temp High!!!! FAN ON(LED GREEN ON)", ""); // ส่งเตือนครั้งแรก
        isFanOn = true;                             // ตั้งสถานะว่าพัดลมเปิดแล้ว
        isHeaterOn = false;                         // และฮีตเตอร์ไม่เปิด
      }
    } else if (temp < 25) {                         // เงื่อนไขอุณหภูมิต่ำกว่า 25°C
      digitalWrite(RED_LED, HIGH);                  // เปิด “ฮีตเตอร์” (ไฟแดงติด)
      digitalWrite(GREEN_LED, LOW);                 // ปิด “พัดลม” (ไฟเขียวดับ)
      if (!isHeaterOn) {                            // ถ้ายังไม่เคยแจ้งว่าเปิดฮีตเตอร์
        bot.sendMessage(CHAT_ID, "Alert Temp Low!!!! Heater ON(LED RED ON)", ""); // ส่งเตือนครั้งแรก
        isHeaterOn = true;                          // บันทึกสถานะฮีตเตอร์เปิด
        isFanOn = false;                            // และพัดลมไม่เปิด
      }
    } else {                                        // กรณีอุณหภูมิอยู่ในช่วงปกติ 25–30°C
      digitalWrite(RED_LED, LOW);                   // ดับไฟแดง
      digitalWrite(GREEN_LED, LOW);                 // ดับไฟเขียว
      isFanOn = false;                              // รีเซ็ตสถานะพัดลม
      isHeaterOn = false;                           // รีเซ็ตสถานะฮีตเตอร์
    }

    // 2. แจ้งเตือนเมื่อค่าผิดปกติ (Alerts)
    // แจ้งเตือนอุณหภูมิ
    if (temp < 25 || temp > 30) {                   // ถ้าอุณหภูมินอกช่วงปกติ
      if (!tempAlertSent) {                         // และยังไม่เคยส่งเตือน
        String msg = "⚠️Alert!!!! TempOutOfRange !!!! "; // สร้างข้อความเตือนแบบสั้น
        bot.sendMessage(CHAT_ID, msg, "");          // ส่งเข้า Telegram
        tempAlertSent = true;                       // ตั้งธงว่ามีการเตือนไปแล้ว
      }
    } else {                                        // ถ้ากลับเข้าช่วงปกติ
      tempAlertSent = false;                        // รีเซ็ตธง เพื่อพร้อมเตือนใหม่รอบหน้าถ้าหลุดช่วงอีก
    }

    // แจ้งเตือนความชื้น
    if (humi < 60 || humi > 75) {                   // ถ้าความชื้นนอกช่วง 60–75%
      if (!humidityAlertSent) {                     // และยังไม่เคยส่งเตือนในสถานะนี้
        String msg = "💧 Alert!!!! HumiOutOfRange!!!!!!"; // ข้อความเตือนความชื้น
        bot.sendMessage(CHAT_ID, msg, "");          // ส่งเข้า Telegram
        humidityAlertSent = true;                   // ตั้งธงว่าเตือนไปแล้ว
      }
    } else {                                        // ถ้าความชื้นกลับมาปกติ
      humidityAlertSent = false;                    // รีเซ็ตธง
    }

    // แจ้งเตือนระดับน้ำ
    if (level_water < 5 && level_water > 0) {       // ถ้าระดับน้ำต่ำกว่า 5 cm และ >0 (กันค่าเพี้ยน)
      if (!waterAlertSent) {                        // และยังไม่เคยส่งเตือนในสถานะนี้
        String msg = "🌊 Level Water less than 5 CM | level water is  " + String(level_water) + " cm"; // สร้างข้อความเตือน
        bot.sendMessage(CHAT_ID, msg, "");          // ส่งเข้า Telegram
        waterAlertSent = true;                      // ตั้งธงว่าเตือนไปแล้ว
      }
    } else {                                        // ถ้าระดับน้ำไม่ต่ำกว่า 5 cm หรืออ่านได้ 0/ค่าว่าง
      waterAlertSent = false;                       // รีเซ็ตธง
    }
  }
}
