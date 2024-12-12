#include <TinyPICO.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define RELAY_DURATION_MS 250
#define SLEEP_AFTER_BELL_MS 30000
#define MONITORING_INTERVAL_MS 150

// Wifi credentials
const char* ssid = "Duarte-IoT";
const char* password = "***";

// button press pin
const int bellButtonPin = 23;

// relay pin
const int bellRelayPin = 4;

// HTTP GET url
const String serverPath = "https://url";
const String security = "***";
const String action = "&action=bell";
const String uuid = "***";
const String serverRequest = serverPath + security + action + uuid;

// TinyPICO helper
TinyPICO tp = TinyPICO();
int bellState = 0;
bool configured = false;

void setup() {
  // booting up with blue as initial color
  tp.DotStar_SetPixelColor(0, 0, 255);  //Blue
  // serial setup
  Serial.begin(115200);
  sleep(1);
  Serial.println("booting up...");
  // wifi setup
  watchWifi();
  // pins setup
  configureIO();
  // release
  configured = true;
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

void buttonPressed() {
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
  if (!configured) {
    sleep(1);
    return;
  }
  // monitor the wifi
  watchWifi();
  // monitor the bell
  bellState = digitalRead(bellButtonPin);
  // verifying if the button is pressed...
  if (bellState == LOW) {
    buttonPressed();
  }
  // sleep a bit...
  delay(MONITORING_INTERVAL_MS);
}
