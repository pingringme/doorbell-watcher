#include <HTTP_Method.h>
#include <Uri.h>
#include <NetworkClient.h>
#include <TinyPICO.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ElegantOTA.h>
#include <PubSubClient.h>
#include "time.h"

#define FIRMWARE_VERSION "20251125201714"

#define RELAY_DURATION_MS 500
#define SLEEP_AFTER_SETUP_MS 10000 // 10s
#define SLEEP_RELAY_AFTER_BELL_MS 45000 // 45s
#define SLEEP_HTTP_AFTER_BELL_MS 20000 // 20s
#define SLEEP_AFTER_WIFI_RETRY_MS 10000 // 10s
#define MONITORING_INTERVAL_MS 100

#define HTTP_SERVER_URL "https://***"
#define HTTP_SECURITY_CODE "***"
#define HTTP_BELL_UUID "***"

#define WIFI_SSID "***"
#define WIFI_PASSWORD "***"

// NTP config
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600; // +1h Berlin
const int   daylightOffset_sec = 3600; // +1h summer time

// Wifi credentials
const char* hostname = "esp32-pingringme";
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// MQTT config
const bool  mqtt_enabled            = true;
const char* mqtt_server_hostname_mdns = "homeassistant"; // homeassistant.local
const int   mqtt_port = 1883;
const char* mqtt_auth_user                  = "admin";
const char* mqtt_auth_pass                  = "***";
const char* mqtt_device_id                  = "pingringme-esp32";
const char* mqtt_unique_id                  = "pingringme_doorbell";
const char* mqtt_topic_state                = "pingringme/doorbell/state";
const char* mqtt_topic_count                = "pingringme/doorbell/count";
const char* mqtt_topic_attributes           = "pingringme/doorbell/attributes";
const char* mqtt_topic_availability         = "pingringme/status";
const char* mqtt_topic_discovery_state      = "homeassistant/binary_sensor/pingringme_doorbell/config";
const char* mqtt_topic_discovery_count      = "homeassistant/sensor/pingringme_doorbell_count/config";

// button press pin
const int bellButtonPin = 23;

// relay pin
const int bellRelayPin = 32;

// HTTP GET url
const String serverPath = HTTP_SERVER_URL;
const String action = "?action=bell";
const String security = "&sec=";
const String security_val = HTTP_SECURITY_CODE;
const String uuid = "&uuid=";
const String uuid_val = HTTP_BELL_UUID;
// concatenate
const String serverRequest = serverPath + action + security + security_val + uuid + uuid_val;

// instances
TinyPICO tp = TinyPICO();
WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// variables
IPAddress mqttServerIP;
String startupDateTime;
int bellState = 0;
bool isConfigured = false;
int mqttRetries = 0;
int wifiRetries = 0;
int bellPresses = 0;
int mqttDiscoverySent = 0;
bool isSilenced = false;
unsigned long lastBellTime = 0;

String getMacAddress() {
    uint8_t mac[6];
    WiFi.macAddress(mac); // Get MAC address as an array
    
    // Convert MAC array to string format
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return String(macStr);
}

String getTimeString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Failed to obtain time";
    }

    char timeStr[10];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    
    return String(timeStr);
}

String getDateString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Failed to obtain date";
    }

    char dateStr[11];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo); // Format: YYYY-MM-DD

    return String(dateStr);
}

String getFreeMemoryString() {
    int freeHeap = ESP.getFreeHeap();  // Get free heap memory in bytes
    return String(freeHeap) + " bytes";
}

void setup() {
  // booting up with blue as initial color
  tp.DotStar_SetPixelColor(0, 0, 255);  //Blue
  // serial setup
  Serial.begin(115200);
  Serial.println("*************");
  Serial.println("Booting up...");
  // pins setup
  configureIO();
  // wifi setup
  watchWifi();
  // mDNS
  configureMDNS();
  // mqtt
  configureMQTT();
  // web server
  configureWebServer();
  // configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // standard initial sleep...
  Serial.println("finishing initial setup()...");
  // print time
  startupDateTime = getDateString() + " " + getTimeString();
  Serial.println(startupDateTime);
  // delay after setup
  delay(SLEEP_AFTER_SETUP_MS);
  // release
  isConfigured = true;
}
// base URL handling
void handle_base() {
  Serial.println("ESP32 Web Server: New request received...");  // for debugging
  Serial.println("GET /");                                      // for debugging
  // header
  String header = "<header><title>PingRing.me</title></header>";
  // board info
  String info = "";
  info += "Firmware Version: ";
  info += FIRMWARE_VERSION;
  info += "<br>";
  info += "Startup date & time: ";
  info += startupDateTime;
  info += "<br>";
  info += "Current date & time: ";
  info += getDateString();
  info += " ";
  info += getTimeString();
  info += "<br>";
  info += "Free Heap Memory: ";
  info += getFreeMemoryString();
  info += "<br>";
  info += "<br>";
  info += "Connected to SSID: ";
  info += ssid;
  info += "<br>";
  info += "IP Address: ";
  info += WiFi.localIP().toString();
  info += "<br>";
  info += "RSSI: ";
  info += String(WiFi.RSSI());
  info += "<br>";
  info += "Mac Address: ";
  info += getMacAddress();
  info += "<br>";
  info += "<br>";
  info += "MQTT Connected: ";
  info += String(mqttClient.connected());
  info += " (" + mqttServerIP.toString() + ":" + String(mqtt_port) + " and user: " + mqtt_auth_user + ")";
  info += "<br>";
  info += "MQTT Conenction Retries: ";
  info += String(mqttRetries);
  info += "<br>";
  info += "MQTT Discovery Message(s) sent: ";
  info += String(mqttDiscoverySent);
  info += "<br>";
  info += "<br>";
  info += "WiFi Connection retries: ";
  info += String(wifiRetries);
  info += "<br>";
  info += "Bell presses: ";
  info += String(bellPresses);
  info += "<br>";
  info += "Silence Mode: ";
  info += String(isSilenced);
  info += "<br>";
  // actions
  String actions = "<h2>Actions</h2>";
  actions += "Toggle Silence: <br><form action=\"/silence\" method=\"get\"><button type=\"submit\">Execute</button></form>";
  actions += "Relay Only: <br><form action=\"/relay\" method=\"get\"><button type=\"submit\">Execute</button></form>";
  actions += "Bell Button: <br><form action=\"/button\" method=\"get\"><button type=\"submit\">Execute</button></form>";
  actions += "Update Firmware: <br><form action=\"/update\" method=\"get\"><button type=\"submit\">Execute</button></form>";
  // body
  String body = "";
  body += "<html>";
  body += header;
  body += "<body>";
  body += "<h1><a href=\"https://pingring.me\" target=\"_blank\">PingRing.me</a></h1><br/>";
  body += info;
  body += actions;
  body += "</body></html>";
  server.send(200, "text/html", body);
}

// silence mode URL handling
void handle_silence() {
  Serial.println("ESP32 Web Server: Toggling silence...");
  Serial.println("GET /silence");
  // send browser/client result
  server.send(200, "application/json", "{action: 'silence', result: true}");
  // switch settings
  toggleSilence();
}

// relay URL handling
void handle_relay() {
  Serial.println("ESP32 Web Server: Activating relay...");
  Serial.println("GET /relay");
  // send browser/client result
  server.send(200, "application/json", "{action: 'relay', result: true}");
  // relay trigger
  activateRelay();
}
// button URL handling
void handle_button() {
  Serial.println("ESP32 Web Server: Activating button...");
  Serial.println("GET /button");
  // send browser/client result
  server.send(200, "application/json", "{action: 'button', result: true}");
  // button trigger
  activateButton("http");
}
// not found
void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}
void handle_restart() {
  Serial.println("arduino/esp32 - restart requested...");
  ESP.restart();
}
void configureMQTT() {

  if (!mqtt_enabled) {
    Serial.println("MQTT service is not enabled.");
    return;
  }

  int retries = 0;
  if (strlen(mqtt_server_hostname_mdns) > 0 && mqtt_port > 0) {
    mqttServerIP = MDNS.queryHost(mqtt_server_hostname_mdns);
    mqttClient.setServer(mqttServerIP, mqtt_port);
    mqttClient.setBufferSize(1024);
    while (!mqttClient.connected() && retries < 3) {
      mqttRetries++;
      mqttClient.connect(mqtt_device_id, mqtt_auth_user, mqtt_auth_pass);
      if (!mqttClient.connected()) {
        retries++;
        delay(1000);
      } else {
        // Publish availability
        mqttClient.publish(mqtt_topic_availability, "online", true);
        // Publish MQTT Discovery config (retain = true)
        String discovery_payload;
        // discovery for STATE
        discovery_payload = "{";
        discovery_payload += "\"name\": \"Doorbell\",";
        discovery_payload += "\"unique_id\": \"" + String(mqtt_unique_id) + "\",";
        discovery_payload += "\"state_topic\": \"" + String(mqtt_topic_state) + "\",";
        discovery_payload += "\"json_attributes_topic\": \"" + String(mqtt_topic_attributes) + "\",";
        discovery_payload += "\"device_class\": \"occupancy\",";
        discovery_payload += "\"payload_on\": \"ON\",";
        discovery_payload += "\"payload_off\": \"OFF\",";
        discovery_payload += "\"availability_topic\": \"" + String(mqtt_topic_availability) + "\",";
        discovery_payload += "\"payload_available\": \"online\",";
        discovery_payload += "\"payload_not_available\": \"offline\",";
        discovery_payload += "\"device\": {";
        discovery_payload +=   "\"identifiers\": [\"" + String(mqtt_device_id) + "\"],";
        discovery_payload +=   "\"manufacturer\": \"PingRing.me\",";
        discovery_payload +=   "\"model\": \"PingRing Board\",";
        discovery_payload +=   "\"name\": \"PingRing\"";
        discovery_payload += "}";
        discovery_payload += "}";
        mqttClient.publish(mqtt_topic_discovery_state, discovery_payload.c_str(), true);
        mqttDiscoverySent++;
        // discovery for COUNT
        discovery_payload = "{";
        discovery_payload += "\"name\": \"Presses\",";
        discovery_payload += "\"unique_id\": \"" + String(mqtt_unique_id) + "_count\",";
        discovery_payload += "\"state_topic\": \"" + String(mqtt_topic_count) + "\",";
        discovery_payload += "\"unit_of_measurement\": \"presses\",";
        discovery_payload += "\"state_class\": \"total_increasing\",";
        discovery_payload += "\"device\": {";
        discovery_payload +=   "\"identifiers\": [\"" + String(mqtt_device_id) + "\"],";
        discovery_payload +=   "\"manufacturer\": \"PingRing.me\",";
        discovery_payload +=   "\"model\": \"PingRing Board\",";
        discovery_payload +=   "\"name\": \"PingRing\"";
        discovery_payload += "}";
        discovery_payload += "}";
        mqttClient.publish(mqtt_topic_discovery_count, discovery_payload.c_str(), true);
        mqttDiscoverySent++;
        Serial.println("MQTT connected and discovery sent!");
      }
    }
  }
}
void configureWebServer() {
  // registering two routes...
  Serial.println("Creating web routes...");
  // default
  server.on("/", handle_base);
  // registering /silence for toggling silence mode
  server.on("/silence", handle_silence);
  // registering /relay for relay trigger
  server.on("/relay", handle_relay);
  // registering /button for button trigger
  server.on("/button", handle_button);
  // registering /restart for arduino&esp32 restart
  server.on("/restart", handle_restart);
  // not found
  server.onNotFound(handle_NotFound);
  // installing OTA feature
  ElegantOTA.begin(&server);
  // start web server
  server.begin();
  Serial.println("HTTP server started");
}
void configureIO() {
  // setting pin as input
  pinMode(bellButtonPin, INPUT);
  pinMode(bellRelayPin, OUTPUT);
  digitalWrite(bellRelayPin, LOW);
}
void configureMDNS() {
    // starting mDNS
    if (!MDNS.begin(hostname)) {
      Serial.println("Error setting up mDNS responder!");
    } else {
      Serial.println("mDNS responder started.");
    }
}
void watchWifi() {
  // (re-)cconnecting to the wifi, if needed.
  if (WiFi.status() != WL_CONNECTED) {
    // initial config
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(hostname);
    // logging....
    Serial.println("Wifi is not connected....");
    // making sure wifi is connected
    while (WiFi.status() != WL_CONNECTED) {
      // blink a few times (white/yellow)
      blinkYellow(3);
      // increase counter
      wifiRetries++;
      // logging
      Serial.print("Attempting to connect to SSID: ");
      Serial.print(ssid);
      Serial.println(" (" + String(wifiRetries) + ").");
      // Connect to WPA/WPA2 network. Change this line if using open or WEP network:    
      WiFi.begin(ssid, password);
      // wait X seconds after wifi connection:
      delay(SLEEP_AFTER_WIFI_RETRY_MS);
    }
    // set green LED
    tp.DotStar_SetPixelColor(0, 128, 0);  //Green
    // logging...
    printWifiInfo();

  }
}
void printWifiInfo() {
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Wifi Status:");
  Serial.println(WiFi.status());
}

void blinkPurple(int count) {
  int tmp = count;
  while (tmp > 0) {
    // set purple
    tp.DotStar_SetPixelColor(128, 0, 128);  //purple
    // sleep
    delay(100);
    // set green
    tp.DotStar_SetPixelColor(0, 128, 0);  //Green
    tmp--;
  }
}

void blinkYellow(int count) {
  int tmp = count;
  while (tmp > 0) {
    // set white
    tp.DotStar_SetPixelColor(255, 255, 255);  //White
    // sleep
    delay(100);
    // set yellow
    tp.DotStar_SetPixelColor(255, 255, 0);  //Yellow
    tmp--;
  }
}

void publishMqttState(const char* source, const char* state, bool sendAttributes) {

  if (!mqtt_enabled) {
    return;
  }

  // Publish bell state
  mqttClient.publish(mqtt_topic_state, state);
  mqttClient.publish(mqtt_topic_count, String(bellPresses).c_str(), true); // retain - to be able to see it later :)

  // should we send attributes?
  if (sendAttributes) {
    // Build JSON attributes
    String payload = "{";
    payload += "\"source\":\"" + String(source) + "\",";
    payload += "\"event\":\"pressed\",";
    payload += "\"is_silenced\":" + String(isSilenced ? "true" : "false") + ",";
    payload += "\"bell_counter\":" + String(bellPresses);
    payload += "}";
    // publishing attributes
    mqttClient.publish(mqtt_topic_attributes, payload.c_str(), false);
  }
}

void sendHttpRequest() {
  Serial.println("HTTP Request...");
  // variables
  HTTPClient http;
  // http request
  http.begin(serverRequest);
  int httpResponseCode = http.GET();
  // debug
  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);
}

void toggleSilence() {
  Serial.println("Silence setting will be toggled...");
  if (isSilenced == true) {
    Serial.println("Bell is not silenced anymore, relay will be triggered...");
    isSilenced = false;
  } else {
    Serial.println("Bell will be silenced, relay will NOT be triggered...");
    isSilenced = true;
  }
  Serial.println("isSilenced=" + String(isSilenced));
}

void activateRelay() {
  Serial.println("Relay will be executed...");
  // activating relay
  digitalWrite(bellRelayPin, HIGH);
  delay(RELAY_DURATION_MS);
  digitalWrite(bellRelayPin, LOW);
  delay(RELAY_DURATION_MS);
  Serial.println("Relay was switched on/off.");
}

void activateButton(const char* source) {
  Serial.println("Button Pressed....");
  // counter
  bellPresses++;
  // actions when button is pressed
  blinkPurple(2);
  // triggers mqtt , no matter what, "ON" state  :)
  publishMqttState(source, "ON", true);
  // only rings the bell if not silenced and if there is enough time since last bell has been pressed
  if (!isSilenced && (millis() - lastBellTime) >= SLEEP_RELAY_AFTER_BELL_MS) {
    activateRelay();
  } else {
    Serial.println("Bell is currently in silent mode and relay was not triggered...");
  }
  // only send http request after some safe period
  if ((millis() - lastBellTime) >= SLEEP_HTTP_AFTER_BELL_MS) {
    sendHttpRequest();
  } else {
    Serial.println("Bell is currently in silent mode and HTTP request was not triggered...");
  }
  // triggers mqtt , no matter what, "OFF" state, but without attributes  :)
  publishMqttState(source, "OFF", false);
  // debug
  Serial.println("Actions were executed.");
  // keep track of the last bell...
  lastBellTime = millis();
}

void loop() {
  // wait for setup()
  if (!isConfigured) {
    delay(1000);
    return;
  }
  // monitor the wifi
  watchWifi();
  // monitor the bell
  bellState = digitalRead(bellButtonPin);
  // verifying if the bell button is pressed...
  if (bellState == LOW) {
    Serial.println("GPIO LOW detection.....");
    activateButton("button");
  }
  // handle HTML requests
  server.handleClient();
  // handle OTA and reboot
  ElegantOTA.loop();
  // handle MQTT
  mqttClient.loop();
  // sleep a bit...
  delay(MONITORING_INTERVAL_MS);
}
