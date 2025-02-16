#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <HTTPClient.h>
#include <DHT.h>

// ⚡ กำหนดค่า WiFi
const char *ssid = "Really?";
const char *password = "113333555555";

// ⚡ กำหนดค่าเซ็นเซอร์ PMS7003 (ใช้ SoftwareSerial)
SoftwareSerial pmsSerial(16, 17);  // RX, TX
unsigned char buffer[32];
int pm1_0_total = 0, pm2_5_total = 0, pm10_total = 0;
int readings_count = 0;

// ⚡ กำหนดค่าเซ็นเซอร์ DHT
#define DHTPIN 4       // ขา GPIO ที่ต่อกับ DHT
#define DHTTYPE DHT11  // เปลี่ยนเป็น DHT11 ถ้าใช้ DHT11
DHT dht(DHTPIN, DHTTYPE);
float total_temperature = 0.0, total_humidity = 0.0;

// ⚡ กำหนดค่าเซิร์ฟเวอร์
const char device[] = "67a6f291f572cc094d0d7b21";
String serverURL = String("https://sisaket-aqi.net/api/device/") + device;

// ⚡ ตัวแปรควบคุมเวลา
unsigned long lastReadTime = 0;
unsigned long lastSendTime = 0;

void setup() {
  Serial.begin(115200);
  pmsSerial.begin(9600);
  dht.begin();  // เริ่มต้นเซ็นเซอร์ DHT

  // เชื่อมต่อ WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());  // Print the local IP address
  ArduinoOTA.setHostname("ESP32-PMS7003-DHT-11");
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  unsigned long currentTime = millis();

  // 🔹 อ่านค่าฝุ่นและอุณหภูมิ/ความชื้นทุก 1 นาที
  if (currentTime - lastReadTime >= 1000 * 60) {
    lastReadTime = currentTime;
    wakeSensor();

    // รอ 30 วินาทีให้ PMS7003 พร้อมทำงาน
    unsigned long waitStart = millis();
    while (millis() - waitStart < 30000) {
      ArduinoOTA.handle();
      delay(1);
    }

    readPMS7003();
    readDHT();  // อ่านค่า DHT22/DHT11
    sleepSensor();
  }

  // 🔹 ส่งข้อมูลเฉลี่ยไปยังเซิร์ฟเวอร์ทุก 30 นาที
  if (currentTime - lastSendTime >= 1000 * 60 * 5) {
    lastSendTime = currentTime;
    sendAveragedData();
    pm1_0_total = 0;
    pm2_5_total = 0;
    pm10_total = 0;
    readings_count = 0;
  }
  readings_count++;
}

// ✅ อ่านค่าฝุ่นจาก PMS7003
void readPMS7003() {
  if (pmsSerial.available() >= 32) {
    for (int i = 0; i < 32; i++) {
      buffer[i] = pmsSerial.read();
    }

    if (buffer[0] == 0x42 && buffer[1] == 0x4D) {
      int pm1_0 = (buffer[10] << 8) + buffer[11];
      int pm2_5 = (buffer[12] << 8) + buffer[13];
      int pm10 = (buffer[14] << 8) + buffer[15];

      Serial.printf("PM1.0: %d, PM2.5: %d, PM10: %d\n", pm1_0, pm2_5, pm10);
      pm1_0_total += pm1_0;
      pm2_5_total += pm2_5;
      pm10_total += pm10;
      
    }
  }
}

// ✅ อ่านค่าอุณหภูมิและความชื้นจาก DHT
void readDHT() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("❌ Error reading DHT22");
    return;
  }

  total_humidity += humidity;
  total_temperature += temperature;

  Serial.printf("🌡 Temperature: %.2f°C, 💧 Humidity: %.2f%%\n", temperature, humidity);
}

// ✅ ส่งข้อมูลไปยังเซิร์ฟเวอร์
void sendAveragedData() {
  if (readings_count == 0) return;

  int avg_pm1_0 = pm1_0_total / readings_count;
  int avg_pm2_5 = pm2_5_total / readings_count;
  int avg_pm10 = pm10_total / readings_count;
  float avg_humidity = total_humidity / readings_count;
  float avg_temperature = total_temperature / readings_count;

  HTTPClient http;
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");

  String payload = String("{\"temperature\":") + avg_temperature + ",\"humidity\": " + avg_humidity + ",\"pm1\": " + avg_pm1_0 + ",\"pm25\": " + avg_pm2_5 + ",\"pm10\": " + avg_pm10 + "}";

  int httpResponseCode = http.POST(payload);
  Serial.printf("📡 Data Sent, Response: %d\n", httpResponseCode);
  http.end();
}

// ✅ ปลุกเซ็นเซอร์ PMS7003
void wakeSensor() {
  pmsSerial.write((uint8_t)0x42);
  pmsSerial.write((uint8_t)0x4D);
  pmsSerial.write((uint8_t)0xE4);
  pmsSerial.write((uint8_t)0x00);
  pmsSerial.write((uint8_t)0x01);
  pmsSerial.write((uint8_t)0x73);
}

// ✅ ปิดเซ็นเซอร์ PMS7003
void sleepSensor() {
  pmsSerial.write((uint8_t)0x42);
  pmsSerial.write((uint8_t)0x4D);
  pmsSerial.write((uint8_t)0xE4);
  pmsSerial.write((uint8_t)0x00);
  pmsSerial.write((uint8_t)0x00);
  pmsSerial.write((uint8_t)0x72);
}
