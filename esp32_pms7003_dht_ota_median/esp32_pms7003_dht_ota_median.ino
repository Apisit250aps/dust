#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <vector>
#include <algorithm>

// ⚡ กำหนดค่า WiFi
const char* ssid = "4G-UFI-A0B0";
const char* password = "1234567890";

// ⚡ กำหนดค่าเซ็นเซอร์ PMS7003 (ใช้ SoftwareSerial)
SoftwareSerial pmsSerial(16, 17);  // RX, TX
unsigned char buffer[32];

// ⚡ Arrays สำหรับเก็บค่าการวัด
std::vector<int> pm1_0_readings;
std::vector<int> pm2_5_readings;
std::vector<int> pm10_readings;
std::vector<float> temperature_readings;
std::vector<float> humidity_readings;

// ⚡ กำหนดค่าเซ็นเซอร์ DHT
#define DHTPIN 5
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ⚡ กำหนดค่าเซิร์ฟเวอร์
const char device[] = "67a6f291f572cc094d0d7b21";
String serverURL = String("https://sisaket-aqi.net/api/device/") + device;

// ⚡ ตัวแปรควบคุมเวลา
unsigned long lastReadTime = 0;
unsigned long lastSendTime = 0;

// ฟังก์ชันคำนวณค่า median สำหรับข้อมูลประเภท int
int calculateMedianInt(std::vector<int>& values) {
  if (values.empty()) return 0;

  std::sort(values.begin(), values.end());
  size_t size = values.size();

  if (size % 2 == 0) {
    return (values[size / 2 - 1] + values[size / 2]) / 2;
  } else {
    return values[size / 2];
  }
}

// ฟังก์ชันคำนวณค่า median สำหรับข้อมูลประเภท float
float calculateMedianFloat(std::vector<float>& values) {
  if (values.empty()) return 0.0f;

  std::sort(values.begin(), values.end());
  size_t size = values.size();

  if (size % 2 == 0) {
    return (values[size / 2 - 1] + values[size / 2]) / 2.0f;
  } else {
    return values[size / 2];
  }
}

void setup() {
  Serial.begin(115200);
  pmsSerial.begin(9600);
  dht.begin();

  // เชื่อมต่อ WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  ArduinoOTA.setHostname("ESP32-PMS7003-DHT-22_K");
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  unsigned long currentTime = millis();

  // 🔹 อ่านค่าทุก 1 นาที
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
    readDHT();
    sleepSensor();
  }

  // 🔹 ส่งค่า median ทุก 5 นาที
  if (currentTime - lastSendTime >= 1000 * 60 * 30) {
    lastSendTime = currentTime;
    sendMedianData();

    // เคลียร์ arrays หลังจากส่งข้อมูล
    pm1_0_readings.clear();
    pm2_5_readings.clear();
    pm10_readings.clear();
    temperature_readings.clear();
    humidity_readings.clear();
  }
}

void readPMS7003() {
  if (pmsSerial.available() >= 32) {
    for (int i = 0; i < 32; i++) {
      buffer[i] = pmsSerial.read();
    }

    if (buffer[0] == 0x42 && buffer[1] == 0x4D) {
      int pm1_0 = (buffer[10] << 8) + buffer[11];
      int pm2_5 = (buffer[12] << 8) + buffer[13];
      int pm10 = (buffer[14] << 8) + buffer[15];

      int pm1_0_final = (int)(pm1_0 / 2);
      int pm2_5_final = (int)(pm2_5 / 2);
      int pm10_final = (int)(pm10 / 2);
      Serial.printf("PM1.0: %d, PM2.5: %d, PM10: %d\n", pm1_0_final, pm2_5_final, pm10_final);

      // เพิ่มค่าลงใน arrays
      pm1_0_readings.push_back(pm1_0_final);
      pm2_5_readings.push_back(pm2_5_final);
      pm10_readings.push_back(pm10_final);
    } 
  }
  
}

void readDHT() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("❌ Error reading DHT22");
    return;
  }

  // เพิ่มค่าลงใน arrays
  humidity_readings.push_back(humidity);
  temperature_readings.push_back(temperature);

  Serial.printf("🌡 Temperature: %.2f°C, 💧 Humidity: %.2f%%\n", temperature, humidity);
}

void sendMedianData() {
  if (pm1_0_readings.empty()) return;

  // คำนวณค่า median แยกตามประเภทข้อมูล
  int median_pm1_0 = calculateMedianInt(pm1_0_readings);
  int median_pm2_5 = calculateMedianInt(pm2_5_readings);
  int median_pm10 = calculateMedianInt(pm10_readings);
  float median_humidity = calculateMedianFloat(humidity_readings);
  float median_temperature = calculateMedianFloat(temperature_readings);

  HTTPClient http;
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");

  String payload = String("{\"temperature\":") + median_temperature + ",\"humidity\": " + median_humidity + ",\"pm1\": " + median_pm1_0 + ",\"pm25\": " + median_pm2_5 + ",\"pm10\": " + median_pm10 + "}";

  int httpResponseCode = http.POST(payload);
  Serial.printf("📡 Median Data Sent, Response: %d\n", httpResponseCode);
  http.end();
}

void wakeSensor() {
  pmsSerial.write((uint8_t)0x42);
  pmsSerial.write((uint8_t)0x4D);
  pmsSerial.write((uint8_t)0xE4);
  pmsSerial.write((uint8_t)0x00);
  pmsSerial.write((uint8_t)0x01);
  pmsSerial.write((uint8_t)0x73);
}

void sleepSensor() {
  pmsSerial.write((uint8_t)0x42);
  pmsSerial.write((uint8_t)0x4D);
  pmsSerial.write((uint8_t)0xE4);
  pmsSerial.write((uint8_t)0x00);
  pmsSerial.write((uint8_t)0x00);
  pmsSerial.write((uint8_t)0x72);
}