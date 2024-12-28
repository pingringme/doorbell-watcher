#include <HTTP_Method.h>
#include <Uri.h>
#include <NetworkClient.h>
#include <TinyPICO.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#define RELAY_DURATION_MS 500
#define SLEEP_AFTER_BELL_MS 30000
#define MONITORING_INTERVAL_MS 100

#define HTTP_SERVER_URL "https://***"
#define HTTP_SECURITY_CODE "***"
#define HTTP_BELL_UUID "***"

#define WIFI_SSID "***"
#define WIFI_PASSWORD "***"

// Wifi credentials
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
int bellState = 0;
bool configured = false;

void setup() {
  // booting up with blue as initial color
  tp.DotStar_SetPixelColor(0, 0, 255);  //Blue
  // serial setup
  Serial.begin(115200);
  Serial.println("booting up...");
  // pins setup
  configureIO();
  // wifi setup
  watchWifi();
  // web server
  configureWebServer();
  // standard sleep...
  sleep(1);
  // release
  configured = true;
}
// base URL handling
void handle_base() {
  Serial.println("ESP32 Web Server: New request received...");  // for debugging
  Serial.println("GET /");                                      // for debugging
  // board info
  String info = "Connected to: ";
  info += ssid;
  info += "<br/>";
  info += "IP Address: ";
  info += WiFi.localIP().toString();
  info += "<br/>";
  info += "RSSI: ";
  info += String(WiFi.RSSI());
  info += "<br/>";
  // actions
  String actions = "Actions: <br/>";
  actions += "Relay Only: <form action=\"/relay\" method=\"get\"><button type=\"submit\">Execute</button></form><br/>";
  actions += "Bell Button: <form action=\"/button\" method=\"get\"><button type=\"submit\">Execute</button></form><br/>";
  // body
  String body = "<html><header><title>PingRing.me</title></header><body><h1>PingRing.me</h1><br/>";
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
    Serial.println("Wifi is not connected....");
    WiFi.begin(ssid, password);
    // making sure wifi is connected
    while (WiFi.status() != WL_CONNECTED) {
      tp.DotStar_SetPixelColor(255, 255, 0);  //Yellow
      delay(500);
      Serial.print(".");
    }
    WiFi.setSleep(false);
    Serial.println("");
    printWifiInfo();
    tp.DotStar_SetPixelColor(0, 128, 0);  //Green
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
    sleep(1);
    return;
  }
  // monitor the wifi
  watchWifi();
  // monitor the bell
  bellState = digitalRead(bellButtonPin);
  // verifying if the bell button is pressed...
  if (bellState == LOW) {
    Serial.println("GPIO LOW detection.....");
    activateButton();
  }
  // handle HTML requests
  server.handleClient();
  // sleep a bit...
  delay(MONITORING_INTERVAL_MS);
}
