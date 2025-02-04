#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <HardwareSerial.h>

// Pin configurations
struct Pins {
    static const uint8_t DHT = 4;
    static const uint8_t PMS_RX = 16;
    static const uint8_t PMS_TX = 17;
    static const uint8_t STATUS_LED = 2;
};

// Network configuration
struct NetworkConfig {
    static const char* SSID;
    static const char* PASSWORD;
    static const char* SERVER_URL;
};

const char* NetworkConfig::SSID = "4G-UFI-54C2";
const char* NetworkConfig::PASSWORD = "1234567890";
const char* NetworkConfig::SERVER_URL = "http://128.199.84.24:8000/api/device/678b630f969c66c0ea3c359f";

// Timing constants
struct Timing {
    static const unsigned long READING_INTERVAL = 60000;
    static const unsigned long DISPLAY_INTERVAL = 1000;
};

// Sensor readings structure
struct SensorData {
    float temperature;
    float humidity;
    float pm1;
    float pm25;
    float pm10;
    bool valid;

    SensorData(float t, float h, float p1, float p25, float p10) :
        temperature(t), humidity(h), pm1(p1), pm25(p25), pm10(p10) {
        valid = !isnan(t) && !isnan(h) && p1 >= 0 && p25 >= 0 && p10 >= 0;
    }
};

// Global objects
DHT dht(Pins::DHT, DHT11);
HardwareSerial pmSerial(2);

// Function to calculate median
float calculateMedian(float arr[], int size) {
    // Sorting array
    for (int i = 0; i < size - 1; i++) {
        for (int j = i + 1; j < size; j++) {
            if (arr[i] > arr[j]) {
                float temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
    }
    return arr[size / 2]; // Return the middle value
}

// Read PM values from PMS9103M and find the median
SensorData readSensors() {
    const int SAMPLES = 5;  // Number of readings for median calculation
    float tempSamples[SAMPLES];
    float humiditySamples[SAMPLES];
    float pm1Samples[SAMPLES];
    float pm25Samples[SAMPLES];
    float pm10Samples[SAMPLES];

    for (int i = 0; i < SAMPLES; i++) {
        tempSamples[i] = dht.readTemperature();
        humiditySamples[i] = dht.readHumidity();
        float pm1 = 0, pm25 = 0, pm10 = 0;

        if (pmSerial.available()) {
            byte buffer[32];
            int index = 0;
            while (pmSerial.available() && index < 32) {
                buffer[index++] = pmSerial.read();
            }

            if (index >= 10 && buffer[0] == 0x42 && buffer[1] == 0x4D) {
                pm1 = ((buffer[4] << 8) | buffer[5]) * 10;
                pm25 = ((buffer[6] << 8) | buffer[7]) * 10;
                pm10 = ((buffer[8] << 8) | buffer[9]) * 10;
            }
        }

        pm1Samples[i] = pm1;
        pm25Samples[i] = pm25;
        pm10Samples[i] = pm10;

        delay(200); // Short delay between readings
    }

    // Compute median
    float tempMedian = calculateMedian(tempSamples, SAMPLES);
    float humidityMedian = calculateMedian(humiditySamples, SAMPLES);
    float pm1Median = calculateMedian(pm1Samples, SAMPLES);
    float pm25Median = calculateMedian(pm25Samples, SAMPLES);
    float pm10Median = calculateMedian(pm10Samples, SAMPLES);

    return SensorData(tempMedian, humidityMedian, pm1Median, pm25Median, pm10Median);
}

void connectToWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(NetworkConfig::SSID, NetworkConfig::PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        digitalWrite(Pins::STATUS_LED, !digitalRead(Pins::STATUS_LED));
    }

    digitalWrite(Pins::STATUS_LED, HIGH);
    Serial.println("\nConnected to WiFi!");
}

bool sendToServer(const SensorData& data) {
    String payload = "{";
    payload += "\"temperature\":" + String(data.temperature, 2) + ",";
    payload += "\"humidity\":" + String(data.humidity, 2) + ",";
    payload += "\"pm1\":" + String(data.pm1, 2) + ",";
    payload += "\"pm25\":" + String(data.pm25, 2) + ",";
    payload += "\"pm10\":" + String(data.pm10, 2) + "}";

    HTTPClient http;
    http.begin(NetworkConfig::SERVER_URL);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(payload);
    bool success = httpCode > 0;

    if (success) {
        Serial.printf("HTTP Response: %d\n", httpCode);
        digitalWrite(Pins::STATUS_LED, HIGH);
    } else {
        Serial.printf("HTTP Error: %d\n", httpCode);
        digitalWrite(Pins::STATUS_LED, LOW);
    }
    http.end();
    return success;
}

void displayReadings(const SensorData& data) {
    Serial.println("\n=========================================");
    Serial.printf("Temperature: %.1f°C\n", data.temperature);
    Serial.printf("Humidity   : %.1f%%\n", data.humidity);
    Serial.printf("PM1.0      : %.1f µg/m³\n", data.pm1);
    Serial.printf("PM2.5      : %.1f µg/m³\n", data.pm25);
    Serial.printf("PM10       : %.1f µg/m³\n", data.pm10);
    Serial.println("=========================================");
}

void setup() {
    Serial.begin(115200);
    pmSerial.begin(9600, SERIAL_8N1, Pins::PMS_RX, Pins::PMS_TX);
    pinMode(Pins::STATUS_LED, OUTPUT);
    digitalWrite(Pins::STATUS_LED, LOW);
    dht.begin();
    connectToWiFi();
}

void loop() {
    static unsigned long lastReadingTime = 0;
    static unsigned long lastDisplayTime = 0;
    unsigned long currentTime = millis();

    if (currentTime - lastDisplayTime >= Timing::DISPLAY_INTERVAL) {
        lastDisplayTime = currentTime;
        displayReadings(readSensors());
    }

    if (currentTime - lastReadingTime >= Timing::READING_INTERVAL) {
        lastReadingTime = currentTime;
        SensorData currentData = readSensors();
        if (currentData.valid) {
            sendToServer(currentData);
        }
    }
}
