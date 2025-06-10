#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <math.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SEALEVELPRESSURE_HPA (1013.25)
#define R_FIXED 10000.0f
#define R_LUX10 10000.0f
#define GAMMA 0.6f

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA_PIN 21
#define OLED_SCL_PIN 22
#define OLED_I2C_ADDRESS 0x3C

#define LAMP_PIN 2
#define LEDC_CHANNEL 0
#define LEDC_TIMER 0
#define LEDC_FREQ 5000
#define LEDC_RES 8

uint32_t BME_delay = 2000; // ms
uint32_t BME_tick = 0;
float temperature = 0.0f, humidity = 0.0f, pressure = 0.0f;

uint32_t photoresistor_delay = 1000; // ms
uint32_t photoresistor_tick = 0;
uint16_t adc_raw = 0;
uint32_t lux = 0;

uint32_t send_mqtt_delay = 5000; // ms
uint32_t send_mqtt_tick = 0;

Adafruit_BME280 bme;
AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);

const char *ssid = "TP-Link_DFC6";
const char *password = "86650076";

const char *mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>ESP32 Measurement Dashboard</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #e3eaf1; margin: 0; padding: 0; display: flex; flex-direction: column; align-items: center; }
    h2 { margin-top: 30px; color: #222; }
    .card { background: white; box-shadow: 0 4px 10px rgba(0,0,0,0.1); border-radius: 10px; padding: 20px 30px; max-width: 400px; width: 90%; margin-bottom: 20px; }
    .value { font-size: 18px; margin: 10px 0; display: grid; grid-template-columns: 1fr 1fr auto; align-items: center; border-bottom: 1px solid #eee; padding-bottom: 6px; column-gap: 10px; }
    .label { font-weight: bold; color: #555; }
    .data { color: #000; }
    .setting-block { margin-top: 20px; }
    label { display: block; text-align: left; margin-bottom: 5px; color: #444; }
    input[type=number] { width: 100%; padding: 6px; border: 1px solid #ccc; border-radius: 4px; }
    button { width: 100%; padding: 8px; background-color: #3498db; color: white; border: none; border-radius: 4px; cursor: pointer; margin-top: 8px; transition: background 0.3s; }
    button:hover { background-color: #2980b9; }
    h3 { margin-top: 10px; font-size: 18px; color: #333; }
  </style>
</head>
<body>
  <h2>ESP32 Measurement</h2>
  <div class="card">
    <div class="value"><span class="label">Temperature:</span><span class="data" id="temp">--</span><span>&deg;C</span></div>
    <div class="value"><span class="label">Humidity:</span><span class="data" id="hum">--</span><span>%</span></div>
    <div class="value"><span class="label">Pressure:</span><span class="data" id="pres">--</span><span>hPa</span></div>
    <div class="value"><span class="label">Brightness:</span><span class="data" id="lux">--</span><span>lux</span></div>
  </div>
  <div class="card">
    <h3>Set Sensor Intervals (ms)</h3>
    <div class="setting-block"><label for="bme">BME280 delay:</label><input type="number" id="bme"><button onclick="setBME()">Set</button></div>
    <div class="setting-block"><label for="photo">Photoresistor delay:</label><input type="number" id="photo"><button onclick="setPhoto()">Set</button></div>
    <div class="setting-block"><label for="mqtt_delay">MQTT send delay:</label><input type="number" id="mqtt_delay"><button onclick="setMQTT()">Set</button></div>
  </div>
  <script>
    function fetchData() {
      fetch('/data').then(res => res.json()).then(data => {
        document.getElementById('temp').textContent = data.temp;
        document.getElementById('hum').textContent = data.hum;
        document.getElementById('pres').textContent = data.pres;
        document.getElementById('lux').textContent = data.lux;
      });
    }
    function setBME() { fetch('/set_bme?val=' + document.getElementById('bme').value); }
    function setPhoto() { fetch('/set_photo?val=' + document.getElementById('photo').value); }
    function setMQTT() { fetch('/set_send_mqtt?val=' + document.getElementById('mqtt_delay').value); }
    document.addEventListener('DOMContentLoaded', () => {
      fetch('/config').then(res => res.json()).then(data => {
        document.getElementById('bme').value = data.bme_delay;
        document.getElementById('photo').value = data.photoresistor_delay;
        document.getElementById('mqtt_delay').value = data.send_mqtt_delay;
      });
      setInterval(fetchData, 200);
    });
  </script>
</body>
</html>
)rawliteral";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

void reconnect_mqtt()
{
  if (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client"))
    {
      Serial.println("connected");
    }
  }
}

void setup()
{
  Serial.begin(9600);
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS))
  {
    Serial.println("ERROR: OLED SSD1306 not found!");
    while (true)
      delay(1000);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OLED initialized");
  display.display();
  delay(500);
  display.clearDisplay();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, mqtt_port);
  if (!bme.begin(0x76))
  {
    Serial.println("Could not find BME280!");
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/html", index_html); });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String json = "{";
    json += "\"temp\":" + String(temperature, 1) + ",";
    json += "\"hum\":" + String(humidity, 1) + ",";
    json += "\"pres\":" + String(pressure, 1) + ",";
    json += "\"lux\":" + String(lux);
    json += "}";
    request->send(200, "application/json", json); });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String json = "{";
    json += "\"bme_delay\":" + String(BME_delay) + ",";
    json += "\"photoresistor_delay\":" + String(photoresistor_delay) + ",";
    json += "\"send_mqtt_delay\":" + String(send_mqtt_delay);
    json += "}";
    request->send(200, "application/json", json); });

  server.on("/set_bme", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("val")) {
      BME_delay = request->getParam("val")->value().toInt();
    }
    request->send(200, "text/plain", "OK"); });
  server.on("/set_photo", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("val")) {
      photoresistor_delay = request->getParam("val")->value().toInt();
    }
    request->send(200, "text/plain", "OK"); });
  server.on("/set_send_mqtt", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("val")) {
      send_mqtt_delay = request->getParam("val")->value().toInt();
    }
    request->send(200, "text/plain", "OK"); });

  server.begin();
  ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RES);
  ledcAttachPin(LAMP_PIN, LEDC_CHANNEL);
}

void loop()
{
  uint32_t now = millis();
  client.loop();

  // Publish MQTT data
  if (now - send_mqtt_tick >= send_mqtt_delay)
  {
    reconnect_mqtt();
    send_mqtt_tick = now;
    String json = "{";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"humidity\":" + String(humidity, 1) + ",";
    json += "\"pressure\":" + String(pressure, 1) + ",";
    json += "\"lux\":" + String(lux);
    json += "}";
    client.publish("esp/sensor/data", json.c_str());
    Serial.println("data published");
  }

  // Read BME280
  if (now - BME_tick >= BME_delay)
  {
    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure() / 100.0F;
    BME_tick = now;
  }

  // Read photoresistor
  if (now - photoresistor_tick >= photoresistor_delay)
  {
    adc_raw = analogRead(34);
    float vout = (adc_raw / 4095.0f) * 3.3f;
    if (vout <= 0.0f)
      vout = 0.001f;
    float r_photo = R_FIXED * (3.3f / vout - 1.0f);
    float ratio = R_LUX10 / r_photo;
    float temp_calc = (log10f(ratio) / GAMMA) + 1.0f;
    lux = (uint32_t)powf(10.0f, temp_calc);
    if (lux < 0)
      lux = 0;
    photoresistor_tick = now;
  }

  // Adjust lamp brightness
  int duty = map(lux, 0, 1000, 0, 255);
  ledcWrite(LEDC_CHANNEL, duty);

  // Display sensor data on OLED
  display.clearDisplay();
  display.setTextSize(1.5);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("T: %.1f C\n", temperature);
  display.printf("H: %.1f %%\n", humidity);
  display.printf("P: %.1f hPa\n", pressure);
  display.printf("L: %lu lux\n", lux);
  display.display();

  delay(50);
}
