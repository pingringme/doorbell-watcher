#include <HTTP_Method.h>
#include <Uri.h>
#include <NetworkClient.h>
#include <TinyPICO.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ElegantOTA.h>
#include "time.h"

#define FIRMWARE_VERSION "20250111145837"

#define RELAY_DURATION_MS 500
#define SLEEP_AFTER_SETUP_MS 10000 // 10s
#define SLEEP_AFTER_BELL_MS 45000 // 45s
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

// TinyPICO helper
TinyPICO tp = TinyPICO();
WebServer server(80);
String startupDateTime;
int bellState = 0;
bool configured = false;
int wifiRetries = 0;
int bellPresses = 0;

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
  configured = true;
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
  info += "Connection retries: ";
  info += String(wifiRetries);
  info += "<br>";
  info += "Bell presses: ";
  info += String(bellPresses);
  info += "<br>";
  // actions
  String actions = "<h2>Actions</h2>";
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
  activateButton();
}
// not found
void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}
void configureWebServer() {
  // registering two routes...
  Serial.println("Creating web routes...");
  // default
  server.on("/", handle_base);
  // registering /relay for relay trigger
  server.on("/relay", handle_relay);
  // registering /button for button trigger
  server.on("/button", handle_button);
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

void activateRelay() {
  Serial.println("Relay will be executed...");
  // activating relay
  digitalWrite(bellRelayPin, HIGH);
  delay(RELAY_DURATION_MS);
  digitalWrite(bellRelayPin, LOW);
  delay(RELAY_DURATION_MS);
  Serial.println("Relay was switched on/off.");
}

void activateButton() {
  Serial.println("Button Pressed....");
  // actions when button is pressed
  blinkPurple(2);
  activateRelay();
  sendHttpRequest();
  // debug
  Serial.println("Actions were executed.");
  // sleep
  delay(SLEEP_AFTER_BELL_MS);
}

void loop() {
  // wait for setup()
  if (!configured) {
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
    bellPresses++;
    activateButton();
  }
  // handle HTML requests
  server.handleClient();
  // handle OTA and reboot
  ElegantOTA.loop();
  // sleep a bit...
  delay(MONITORING_INTERVAL_MS);
}
