#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TaskScheduler.h>

// Pin Definitions
#define DHT_PIN 33        // DHT22 data pin
#define GP2Y10_PIN 34     // GP2Y10 analog input pin
#define GP2Y10_LED_PIN 4  // GP2Y10 LED control pin
#define STATUS_LED_PIN 2  // Built-in LED for status indication

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1  // Reset pin (not used, set to -1)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi Configuration
const char* ssid = "4G-UFI-54C2";
const char* password = "1234567890";
const char* SERVER_URL = "http://128.199.84.24:8000/api/device/678b630f969c66c0ea3c359f";

// Task Scheduler
Scheduler taskScheduler;
Task readSensorTask(60000, TASK_FOREVER, &readSensors); // Read every 1 minute
Task sendDataTask(3600000, TASK_FOREVER, &sendData);    // Send every 60 minutes

// Sensor setup
DHT dht(DHT_PIN, DHT22);

// Variables for sensor readings
volatile float temperature = NAN;
volatile float humidity = NAN;
volatile float dustDensity = NAN;

// Variables for storing data
float hourlyTemperatureSum = 0;
float hourlyHumiditySum = 0;
float hourlyDustSum = 0;
int readingsCount = 0;

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
  if (!display.begin(SSD1306_PAGEADDR, 0x3C)) {
    Serial.println("Failed to initialize OLED!");
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  // Connect to WiFi
  connectToWiFi();

  // Add tasks to scheduler
  taskScheduler.addTask(readSensorTask);
  taskScheduler.addTask(sendDataTask);
  readSensorTask.enable();
  sendDataTask.enable();
}

void loop() {
  taskScheduler.execute();
}

void readSensors() {
  // Read DHT sensor
  float newTemp = dht.readTemperature();
  float newHumidity = dht.readHumidity();

  // Read Dust sensor
  float newDustDensity = readDustDensity();

  // Validate readings
  if (!isnan(newTemp) && !isnan(newHumidity) && newDustDensity >= 0) {
    temperature = newTemp;
    humidity = newHumidity;
    dustDensity = newDustDensity;

    hourlyTemperatureSum += temperature;
    hourlyHumiditySum += humidity;
    hourlyDustSum += dustDensity;
    readingsCount++;

    // Display data
    displayData(temperature, humidity, dustDensity);
    Serial.printf("Temp: %.2f C, Humidity: %.2f%%, Dust: %.2f ug/m3\n",
                  temperature, humidity, dustDensity);
  } else {
    Serial.println("Error reading sensors!");
  }
}

void sendData() {
  if (readingsCount == 0) return; // Avoid division by zero

  // Calculate averages
  float avgTemp = hourlyTemperatureSum / readingsCount;
  float avgHumidity = hourlyHumiditySum / readingsCount;
  float avgDust = hourlyDustSum / readingsCount;

  // Prepare JSON payload
  String payload = "{";
  payload += "\"temperature\":" + String(avgTemp, 2) + ",";
  payload += "\"humidity\":" + String(avgHumidity, 2) + ",";
  payload += "\"dust\":" + String(avgDust, 2);
  payload += "}";

  // Reset hourly sums and count
  hourlyTemperatureSum = 0;
  hourlyHumiditySum = 0;
  hourlyDustSum = 0;
  readingsCount = 0;

  // Send data to server
  sendHTTPRequestAsync(SERVER_URL, payload.c_str());
}

float readDustDensity() {
  digitalWrite(GP2Y10_LED_PIN, LOW);
  delayMicroseconds(280);
  int dustValue = analogRead(GP2Y10_PIN);
  delayMicroseconds(40);
  digitalWrite(GP2Y10_LED_PIN, HIGH);

  float voltage = dustValue * (3.3 / 4095.0);
  float dustDensity = (0.1 * voltage - 0.1) * 1000.0;

  return dustDensity > 0 ? dustDensity : 0;
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
  blinkLED(3);
}

void sendHTTPRequestAsync(const char* url, const char* payload) {
  HTTPClient http;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.printf("Error code: %d\n", httpResponseCode);
  }

  http.end();
}

void blinkLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(100);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(100);
  }
}
