#include <WiFi.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <GP2YDustSensor.h>

// Pin Definitions
#define DHT_PIN 33        // DHT22 data pin
#define GP2Y10_LED_PIN 4  // GP2Y10 LED control pin
#define STATUS_LED_PIN 2  // Built-in LED for status indication

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi Configuration
const char* ssid = "4G-UFI-54C2";
const char* password = "1234567890";
const char* SERVER_URL = "http://128.199.84.24:8000/api/device/678b630f969c66c0ea3c359f";

// Constants
#define READINGS_PER_HOUR 60

// Sensor setup
DHT dht(DHT_PIN, DHT22);
GP2YDustSensor dustSensor(GP2Y10_LED_PIN, GP2Y10_LED_PIN);

// Data storage
float temperatureReadings[READINGS_PER_HOUR];
float humidityReadings[READINGS_PER_HOUR];
float dustReadings[READINGS_PER_HOUR];
int currentReadingIndex = 0;
bool arrayFull = false;

// Timing variables
hw_timer_t* timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool readyToRead = false;

// Function prototypes
void connectToWiFi();
bool sendHTTPRequest(const char* url, const char* payload);
void IRAM_ATTR onTimer();
void readSensors();
void displayData(float temp, float humidity, float dust);
void addReading(float temp, float humidity, float dust);
bool shouldSendData();
String calculateAverages();
void blinkLED(int times);

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  Serial.println("ESP32 Environmental Monitor Starting...");

  // Configure pins
  pinMode(STATUS_LED_PIN, OUTPUT);

  // Initialize sensors
  dht.begin();
  dustSensor.begin();

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

  // Setup timer interrupt for 1 minute intervals
  timer = timerBegin(0, 80, true); // 80 prescaler for 1us ticks
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 60000000, true); // 1 minute
  timerAlarmEnable(timer);
}

void loop() {
  // Check if timer triggered a read
  if (readyToRead) {
    portENTER_CRITICAL(&timerMux);
    readyToRead = false;
    portEXIT_CRITICAL(&timerMux);

    readSensors();

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
  }
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

bool sendHTTPRequest(const char* url, const char* payload) {
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(payload);
  http.end();

  return httpResponseCode > 0;
}

void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  readyToRead = true;
  portEXIT_CRITICAL_ISR(&timerMux);
}

void readSensors() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  float dustDensity = dustSensor.getDustDensity();

  if (!isnan(temperature) && !isnan(humidity) && dustDensity >= 0) {
    addReading(temperature, humidity, dustDensity);
    displayData(temperature, humidity, dustDensity);
    Serial.printf("Temperature: %.2f°C, Humidity: %.2f%%, Dust: %.2fµg/m³\n",
                  temperature, humidity, dustDensity);
  } else {
    Serial.println("Error reading sensors!");
  }
}

void displayData(float temp, float humidity, float dust) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("Temp: %.2f C\n", temp);
  display.printf("Humidity: %.2f %%\n", humidity);
  display.printf("Dust: %.2f ug/m3\n", dust);
  display.display();
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
