/*
 * Метеостанция на ESP32-C6 + BME280
 * Подключение:
 *   BME280 VCC  -> ESP32-C6 3.3V
 *   BME280 GND  -> ESP32-C6 GND
 *   BME280 SDA  -> ESP32-C6 GPIO8
 *   BME280 SCL  -> ESP32-C6 GPIO9
 * 
 * Режимы работы:
 * 1. Обычный (веб-интерфейс): подключитесь к точке доступа "ESP32-Weather" (пароль: weather123)
 *    и откройте в браузере http://192.168.4.1
 * 2. Matter: устройство появляется как сенсор в умных домах (Google Home, Apple HomeKit, Alexa)
 *    Используйте QR-код или ручной код для подключения
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define I2C_SDA 8
#define I2C_SCL 9

Adafruit_BME280 bme;

// currrent sensor readings
float currentTemp = 0;
float currentHum = 0;
float currentPress = 0;

// ============ MODE SELECTION ============
// Uncomment one of the following defines to select mode:
// #define USE_WEB_SERVER   // Web interface mode (default)
// #define USE_MATTER       // Matter mode for smart home integration

// Default to web server if nothing is defined
#if !defined(USE_WEB_SERVER) && !defined(USE_MATTER)
#define USE_WEB_SERVER
#endif

// ============ WEB SERVER MODE ============
#if defined(USE_WEB_SERVER)

#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "ESP32-Weather";
const char* password = "weather123";

WebServer server(80);

const char INDEX_HTML[] PROGMEM = R"rawliterals(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-C6 Метеостанция</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 600px; margin: 40px auto; padding: 20px; text-align: center; }
    h1 { color: #333; }
    .data-card { background: #f5f5f5; border-radius: 10px; padding: 20px; margin: 15px 0; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
    .value { font-size: 2.5em; font-weight: bold; color: #2196F3; }
    .label { font-size: 0.9em; color: #666; text-transform: uppercase; }
    .units { font-size: 0.6em; color: #999; }
    .timestamp { color: #888; font-size: 0.9em; margin-top: 20px; }
    .status { color: #4CAF50; font-weight: bold; }
  </style>
</head>
<body>
  <h1>🌤️ ESP32-C6 Метеостанция</h1>
  <div class="status">Подключено к BME280</div>
  
  <div class="data-card">
    <div class="label">Температура</div>
    <div><span class="value" id="temp">--</span><span class="units">°C</span></div>
  </div>
  
  <div class="data-card">
    <div class="label">Влажность</div>
    <div><span class="value" id="hum">--</span><span class="units">%</span></div>
  </div>
  
  <div class="data-card">
    <div class="label">Давление</div>
    <div><span class="value" id="press">--</span><span class="units">гПа</span></div>
  </div>
  
  <div class="timestamp" id="timestamp">Обновлено: никогда</div>

  <script>
    function updateData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('temp').textContent = data.temperature.toFixed(1);
          document.getElementById('hum').textContent = data.humidity.toFixed(1);
          document.getElementById('press').textContent = data.pressure.toFixed(1);
          document.getElementById('timestamp').textContent = 'Обновлено: ' + new Date(data.timestamp * 1000).toLocaleTimeString();
        })
        .catch(error => {
          console.error('Ошибка получения данных:', error);
        });
    }
    
    updateData();
    setInterval(updateData, 1000);
  </script>
</body>
</html>
)rawliterals";

void handleRoot() {
  server.send(200, "text/html; charset=UTF-8", INDEX_HTML);
}

void handleData() {
  currentTemp = bme.readTemperature();
  currentHum = bme.readHumidity();
  currentPress = bme.readPressure() / 100.0F;

  String json = "{";
  json += "\"temperature\": " + String(currentTemp, 1);
  json += ", \"humidity\": " + String(currentHum, 1);
  json += ", \"pressure\": " + String(currentPress, 1);
  json += ", \"timestamp\": " + String(millis() / 1000);
  json += "}";

  server.send(200, "application/json; charset=UTF-8", json);
}

#endif // USE_WEB_SERVER

// ============ MATTER MODE ============
#if defined(USE_MATTER)

#include <esp_matter.h>
#include <esp_matter_console.h>

#define MATTER_PORT 5580

using namespace chip;
using namespace esp_matter;
using namespace chip::app::Clusters;

static uint8_t matter_qr_code[esp_matter::QRCode::kMaxLength];
static size_t matter_qr_code_len = 0;
static node_t *sensor_device = nullptr;

void temperature_read_callback(attribute_t *attribute, uint8_t endpoint) {
    esp_matter::attribute::update<float>(attribute, currentTemp, MatterValueType::FLOAT);
}

void humidity_read_callback(attribute_t *attribute, uint8_t endpoint) {
    uint16_t humidity_x100 = static_cast<uint16_t>(currentHum * 100);
    esp_matter::attribute::update<uint16_t>(attribute, humidity_x100, MatterValueType::UINT16);
}

void pressure_read_callback(attribute_t *attribute, uint8_t endpoint) {
    int16_t pressure_x10 = static_cast<int16_t>(currentPress * 10);
    esp_matter::attribute::update<int16_t>(attribute, pressure_x10, MatterValueType::INT16);
}

void create_matter_device() {
    sensor_device = node::create(nullptr, "ESP32-C6 Weather Sensor", DEVICE_TYPE_SENSOR);
    
    // Temperature Measurement cluster
    cluster_t *temperature_cluster = cluster::create(sensor_device, TEMPERATURE_MEASUREMENT_CLUSTER_ID);
    attribute_t *temperature_attr = attribute::create(temperature_cluster, TemperatureMeasurement::Attributes::MeasuredValue::Id);
    attribute::set_read_callback(temperature_attr, temperature_read_callback);
    attribute::set_default_value(temperature_attr, 0.0f, MatterValueType::FLOAT);
    cluster::set_attribute(temperature_cluster, TemperatureMeasurement::Attributes::MinMeasuredValue::Id, -40.0f, MatterValueType::FLOAT);
    cluster::set_attribute(temperature_cluster, TemperatureMeasurement::Attributes::MaxMeasuredValue::Id, 80.0f, MatterValueType::FLOAT);

    // Relative Humidity cluster
    cluster_t *humidity_cluster = cluster::create(sensor_device, RELATIVE_HUMIDITY_CLUSTER_ID);
    attribute_t *humidity_attr = attribute::create(humidity_cluster, RelativeHumidityMeasurement::Attributes::MeasuredValue::Id);
    attribute::set_read_callback(humidity_attr, humidity_read_callback);
    attribute::set_default_value(humidity_attr, (uint16_t)0, MatterValueType::UINT16);
    cluster::set_attribute(humidity_cluster, RelativeHumidityMeasurement::Attributes::MinMeasuredValue::Id, (uint16_t)0, MatterValueType::UINT16);
    cluster::set_attribute(humidity_cluster, RelativeHumidityMeasurement::Attributes::MaxMeasuredValue::Id, (uint16_t)10000, MatterValueType::UINT16);

    // Pressure Measurement cluster
    cluster_t *pressure_cluster = cluster::create(sensor_device, PRESSURE_MEASUREMENT_CLUSTER_ID);
    attribute_t *pressure_attr = attribute::create(pressure_cluster, PressureMeasurement::Attributes::MeasuredValue::Id);
    attribute::set_read_callback(pressure_attr, pressure_read_callback);
    attribute::set_default_value(pressure_attr, (int16_t)0, MatterValueType::INT16);
    cluster::set_attribute(pressure_cluster, PressureMeasurement::Attributes::MinMeasuredValue::Id, (int16_t)8000, MatterValueType::INT16);
    cluster::set_attribute(pressure_cluster, PressureMeasurement::Attributes::MaxMeasuredValue::Id, (int16_t)11000, MatterValueType::INT16);
    cluster::set_attribute(pressure_cluster, PressureMeasurement::Attributes::ScaledValue::Id, (int16_t)0, MatterValueType::INT16);
    cluster::set_attribute(pressure_cluster, PressureMeasurement::Attributes::Scale::Id, (int8_t)-1, MatterValueType::INT8); // Scale = -1 (divide by 10)

    node::set_active(sensor_device, true);
}

void init_matter() {
    matter::init(MATTER_PORT);
    create_matter_device();
    esp_matter::start();
    matter_qr_code_len = esp_matter::QRCode::generate(matter_qr_code, sizeof(matter_qr_code));
}

#endif // USE_MATTER

// ============ COMMON SETUP ============

void readSensors() {
    currentTemp = bme.readTemperature();
    currentHum = bme.readHumidity();
    currentPress = bme.readPressure() / 100.0F;
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    Serial.println("\n=== ESP32-C6 Weather Station ===");

    // Initialize I2C and BME280
    Wire.begin(I2C_SDA, I2C_SCL, 100000);

    if (!bme.begin(0x76, &Wire)) {
        Serial.println("ERROR: BME280 not found!");
        Serial.println("Check SDA/SCL connections and address (0x76 or 0x77)");
        while (1) delay(1000);
    }

    Serial.println("BME280 initialized successfully");

#if defined(USE_WEB_SERVER)
    // Web Server mode
    Serial.println("\nStarting Web Server mode...");
    
    WiFi.softAP(ssid, password);
    Serial.print("AP SSID: ");
    Serial.println(ssid);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.begin();
    
    Serial.println("Web server started on port 80");
    Serial.println("Open in browser: http://192.168.4.1");

#elif defined(USE_MATTER)
    // Matter mode
    Serial.println("\nStarting Matter mode...");
    init_matter();
    
    Serial.println("\n=== Matter Commissioning Info ===");
    Serial.println("1. Open your Matter controller app (Google Home, Apple Home, Alexa)");
    Serial.println("2. Scan the QR code or enter the manual pairing code");
    
    if (matter_qr_code_len > 0) {
        Serial.println("\nQR Code:");
        esp_matter::QRCode::print(matter_qr_code, matter_qr_code_len);
    }
    
    char manual_code[11] = {0};
    esp_matter::manual_pairing_code::generate(manual_code, sizeof(manual_code));
    Serial.print("\nManual Pairing Code: ");
    Serial.println(manual_code);
    Serial.println("\nDevice is ready for commissioning!");

#endif

    Serial.println("=====================================");
}

void loop() {
    static unsigned long last_sensor_read = 0;
    
    // Read sensors every 2 seconds
    if (millis() - last_sensor_read >= 2000) {
        readSensors();
        last_sensor_read = millis();
        
        Serial.printf("Temp: %.1f°C | Hum: %.1f%% | Press: %.1fhPa\n", 
                      currentTemp, currentHum, currentPress);
    }

#if defined(USE_WEB_SERVER)
    server.handleClient();
#elif defined(USE_MATTER)
    esp_matter::loop();
#endif
    
    delay(10);
}
