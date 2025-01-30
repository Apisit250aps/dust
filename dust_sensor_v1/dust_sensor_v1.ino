#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pin Definitions
#define DHT_PIN 33         // DHT22 data pin
#define GP2Y10_PIN A6      // GP2Y10 analog input pin
#define GP2Y10_LED_PIN 4   // GP2Y10 LED control pin
#define STATUS_LED_PIN 2   // Built-in LED for status indication

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1      // Reset pin (not used, set to -1)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi Configuration
const char* ssid = "4G-UFI-54C2";
const char* password = "1234567890";
const char* SERVER_URL = "http://128.199.84.24:8000/api/device/678b630f969c66c0ea3c359f";

// Constants
#define READINGS_PER_HOUR 60    // Number of readings to average (1 reading per minute)
#define READING_INTERVAL 60000  // Time between readings (1 minute in milliseconds)

// Sensor setup
DHT dht(DHT_PIN, DHT22);

// Arrays for storing hourly readings
float temperatureReadings[READINGS_PER_HOUR];
float humidityReadings[READINGS_PER_HOUR];
float dustReadings[READINGS_PER_HOUR];
int currentReadingIndex = 0;
bool arrayFull = false;

// Function prototypes
void connectToWiFi();
bool sendHTTPRequest(const char* url, const char* payload);
float readDustDensity();
void addReading(float temp, float humidity, float dust);
bool shouldSendData();
String calculateAverages();
void blinkLED(int times);
void displayData(float temp, float humidity, float dust);

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  Serial.println("ESP32 Environmental Monitor Starting...");

  // Configure pins
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(GP2Y10_LED_PIN, OUTPUT);
  digitalWrite(GP2Y10_LED_PIN, HIGH);  // Turn off GP2Y10 LED initially

  // Initialize sensors
  dht.begin();

  // Initialize OLED display
  if (!display.begin(SSD1306_PAGEADDR, OLED_RESET)) {
    Serial.println("SSD1306 allocation failed");
    while (true); // Stop if OLED initialization fails
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  // Connect to WiFi
  connectToWiFi();
}

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    connectToWiFi();
  }

  // Read sensor data
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  float dustDensity = readDustDensity();

  // Check if readings are valid
  if (!isnan(temperature) && !isnan(humidity) && dustDensity >= 0) {
    // Add readings to arrays
    addReading(temperature, humidity, dustDensity);

    // Print and display current readings
    Serial.printf("Temperature: %.2f°C, Humidity: %.2f%%, Dust: %.2f µg/m³\n",
                  temperature, humidity, dustDensity);
    displayData(temperature, humidity, dustDensity);

    // Check if it's time to send data
    if (shouldSendData()) {
      String payload = calculateAverages();
      Serial.println("Sending data to server: " + payload);

      if (sendHTTPRequest(SERVER_URL, payload.c_str())) {
        blinkLED(2);  // Indicate successful transmission
      } else {
        blinkLED(5);  // Indicate transmission failure
      }
    }
  } else {
    Serial.println("Error reading sensors!");
    blinkLED(1);  // Indicate sensor error
  }

  delay(READING_INTERVAL);
}

void displayData(float temp, float humidity, float dust) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("Temp: %.2f C\n", temp);
  display.printf("Humidity: %.2f %%\n", humidity);
  display.printf("Dust: %.2f ug/m3\n", dust);
  display.display();
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    blinkLED(1);
  }
  
  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  blinkLED(3);  // Indicate successful connection
}

bool sendHTTPRequest(const char* url, const char* payload) {
  HTTPClient http;
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(payload);
  
  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println(response);
    http.end();
    return true;
  } else {
    Serial.printf("Error code: %d\n", httpResponseCode);
    http.end();
    return false;
  }
}

float readDustDensity() {
  digitalWrite(GP2Y10_LED_PIN, LOW);  // Turn on IR LED
  delayMicroseconds(280);             // Wait for LED stabilization
  int dustValue = analogRead(GP2Y10_PIN);
  delayMicroseconds(40);
  digitalWrite(GP2Y10_LED_PIN, HIGH);  // Turn off IR LED

  // Convert reading to dust density
  float voltage = dustValue * (3.3 / 4095.0);           // ESP32 uses 3.3V reference
  float dustDensity = (0.17 * voltage - 0.1) * 1000.0;  // Convert to µg/m³

  return dustDensity > 0 ? dustDensity : 0;
}

void addReading(float temp, float humidity, float dust) {
  temperatureReadings[currentReadingIndex] = temp;
  humidityReadings[currentReadingIndex] = humidity;
  dustReadings[currentReadingIndex] = dust;

  currentReadingIndex++;
  if (currentReadingIndex >= READINGS_PER_HOUR) {
    currentReadingIndex = 0;
    arrayFull = true;
  }
}

bool shouldSendData() {
  return arrayFull && currentReadingIndex == 0;
}

String calculateAverages() {
  float tempSum = 0, humiditySum = 0, dustSum = 0;
  int count = arrayFull ? READINGS_PER_HOUR : currentReadingIndex;

  for (int i = 0; i < count; i++) {
    tempSum += temperatureReadings[i];
    humiditySum += humidityReadings[i];
    dustSum += dustReadings[i];
  }

  float tempAvg = tempSum / count;
  float humidityAvg = humiditySum / count;
  float dustAvg = dustSum / count;

  return "{\"temperature\":" + String(tempAvg, 2) + ",\"humidity\":" + String(humidityAvg, 2) + ",\"dust\":" + String(dustAvg, 2) + "}";
}

void blinkLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(100);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(100);
  }
}
