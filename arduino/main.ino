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
#include <WiFiClientSecure.h>
#include "time.h"
#include "config.h"

// (all configuration constants are now defined in config.h)

// instances
TinyPICO tp = TinyPICO();
WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// variables
IPAddress mqttServerIP;
// Fixed-size buffers (no heap allocation): "YYYY-MM-DD HH:MM:SS" = 19 chars
// + null terminator + safety margin.
char startupDateTime[24] = "n/a";
char lastRelayDateTime[24] = "never";
bool isConfigured = false;
int mqttRetries = 0;
int wifiRetries = 0;
int bellPresses = 0;
int mqttDiscoverySent = 0;
bool isSilenced = false;
unsigned long lastRelayTime = 0;
unsigned long lastHttpTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
int relayActivations = 0;
// Mirror-mode relay state: while the physical bell button is held down, the
// relay is held HIGH. These are written by relaySetOn/relaySetOff (called
// from pollBellButton) and read by the safety force-off check.
bool relayOn = false;
unsigned long relayOnSince = 0;
// Deferred-HTTP flag: set on a press edge when the cooldown allows it, and
// consumed AFTER pollBellButton() so the release edge can be observed before
// the blocking HTTPS call is made. This keeps short presses from holding the
// relay on for the full HTTP timeout window.
bool httpSendPending = false;

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

    char timeStr[16];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    
    return String(timeStr);
}

String getDateString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Failed to obtain date";
    }

    char dateStr[16];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo); // Format: YYYY-MM-DD

    return String(dateStr);
}

String getFreeMemoryString() {
    int freeHeap = ESP.getFreeHeap();  // Get free heap memory in bytes
    return String(freeHeap);
}

// Block (up to timeoutMs) until the system clock has been synced via NTP.
// Returns true if time was obtained.
bool waitForTimeSync(uint32_t timeoutMs) {
    struct tm timeinfo;
    return getLocalTime(&timeinfo, timeoutMs);
}

// Fill a fixed-size buffer with the current local date+time as
// "YYYY-MM-DD HH:MM:SS". Falls back to "n/a" if the clock is not synced.
// Avoids any heap allocation, so it's safe to call indefinitely.
void fillDateTimeString(char* buf, size_t bufSize) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        snprintf(buf, bufSize, "n/a");
        return;
    }
    strftime(buf, bufSize, "%Y-%m-%d %H:%M:%S", &timeinfo);
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
  // wait for NTP to actually sync before stamping startupDateTime; otherwise
  // the stored value would be "Failed to obtain ..." on a cold boot.
  if (!waitForTimeSync(NTP_SYNC_TIMEOUT_MS)) {
    Serial.println("Warning: NTP sync timed out.");
  }
  // standard initial sleep...
  Serial.println("finishing initial setup()...");
  // print time
  fillDateTimeString(startupDateTime, sizeof(startupDateTime));
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
 
  // response
  String page = html_template_main;
  // variables 
  page.replace("{{firmware}}", FIRMWARE_VERSION);
  page.replace("{{startup}}", startupDateTime);
  page.replace("{{now}}", getDateString() + " "+ getTimeString());
  page.replace("{{heap}}", getFreeMemoryString());

  page.replace("{{ssid}}", WiFi.SSID());
  page.replace("{{ip}}", WiFi.localIP().toString());
  page.replace("{{rssi}}", String(WiFi.RSSI()) + " dBm");
  page.replace("{{mac}}", getMacAddress());

  page.replace("{{mqtt_server}}", "mqtt://" + String(mqtt_auth_user) + ":***@" + mqttServerIP.toString() + ":" + String(mqtt_port));
  page.replace("{{mqtt_connected}}", mqttClient.connected() ? "true" : "false");
  page.replace("{{mqtt_retries}}", String(mqttRetries));
  page.replace("{{mqtt_discovery}}", String(mqttDiscoverySent));

  page.replace("{{wifi_retries}}", String(wifiRetries));
  page.replace("{{bell_presses}}", String(bellPresses));
  page.replace("{{relay_activations}}", String(relayActivations));
  page.replace("{{relay_last}}", lastRelayDateTime);
  page.replace("{{silence_mode}}", isSilenced ? "true" : "false");
 
  // never cache this status page: values change constantly
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", page);
}

// silence mode URL handling
void handle_silence() {
  Serial.println("ESP32 Web Server: Toggling silence...");
  Serial.println("GET /silence");
  // send browser/client result
  server.send(200, "application/json", "{\"action\":\"silence\",\"result\":true}");
  // switch settings
  toggleSilence();
}

// relay URL handling
void handle_relay() {
  Serial.println("ESP32 Web Server: Activating relay...");
  Serial.println("GET /relay");
  // send browser/client result
  server.send(200, "application/json", "{\"action\":\"relay\",\"result\":true}");
  // relay trigger (fixed-duration pulse: no physical press to mirror)
  pulseRelay();
}
// button URL handling
void handle_button() {
  Serial.println("ESP32 Web Server: Activating button...");
  Serial.println("GET /button");
  // send browser/client result
  server.send(200, "application/json", "{\"action\":\"button\",\"result\":true}");
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
// Resolve the broker hostname via mDNS. Returns true if a valid (non-zero) IP
// was obtained. mqttServerIP is updated either way.
bool resolveMqttBroker() {
  if (strlen(mqtt_server_hostname_mdns) == 0 || mqtt_port <= 0) {
    return false;
  }
  mqttServerIP = MDNS.queryHost(mqtt_server_hostname_mdns);
  if (mqttServerIP == IPAddress(0, 0, 0, 0)) {
    Serial.print("mDNS lookup failed for ");
    Serial.println(mqtt_server_hostname_mdns);
    return false;
  }
  Serial.print("MQTT broker resolved: ");
  Serial.println(mqttServerIP);
  return true;
}

// Publish Home Assistant MQTT discovery payloads. Caller must ensure the
// client is connected.
void publishMqttDiscovery() {
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
}

// Non-blocking connect/reconnect attempt. Rate-limited via
// MQTT_RECONNECT_INTERVAL_MS. Returns true if connected after the call.
bool ensureMqttConnected() {
  if (!mqtt_enabled) {
    return false;
  }
  if (mqttClient.connected()) {
    return true;
  }
  unsigned long now = millis();
  if (lastMqttReconnectAttempt != 0 &&
      (now - lastMqttReconnectAttempt) < MQTT_RECONNECT_INTERVAL_MS) {
    return false;
  }
  lastMqttReconnectAttempt = now;

  // Re-resolve the broker every time we attempt to reconnect; the IP may
  // have changed and the previous lookup may have failed.
  if (!resolveMqttBroker()) {
    return false;
  }
  mqttClient.setServer(mqttServerIP, mqtt_port);
  mqttClient.setBufferSize(1024);

  mqttRetries++;
  Serial.print("MQTT connecting (attempt ");
  Serial.print(mqttRetries);
  Serial.println(")...");
  if (!mqttClient.connect(mqtt_device_id, mqtt_auth_user, mqtt_auth_pass,
                          mqtt_topic_availability, 0, true, "offline")) {
    Serial.print("MQTT connect failed, state=");
    Serial.println(mqttClient.state());
    return false;
  }
  mqttClient.publish(mqtt_topic_availability, "online", true);
  publishMqttDiscovery();
  Serial.println("MQTT connected and discovery sent!");
  return true;
}

void configureMQTT() {
  if (!mqtt_enabled) {
    Serial.println("MQTT service is not enabled.");
    return;
  }
  // First connection attempt; subsequent reconnects happen from loop().
  ensureMqttConnected();
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
  // setting pin as input. External pullup is present on the board; the
  // internal pullup is enabled too as a defensive backup (they parallel,
  // no conflict).
  pinMode(bellButtonPin, INPUT_PULLUP);
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
    delay(75);
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
    delay(75);
    // set yellow
    tp.DotStar_SetPixelColor(255, 255, 0);  //Yellow
    tmp--;
  }
}

void publishMqttState(const char* source, const char* state, bool sendAttributes) {

  if (!mqtt_enabled) {
    return;
  }
  if (!mqttClient.connected()) {
    Serial.println("MQTT not connected, skipping publish.");
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
  // HTTPS: use a secure client. setInsecure() skips certificate validation;
  // replace with setCACert(...) if you want to pin the backend's CA.
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(secureClient, serverRequest)) {
    Serial.println("HTTP begin() failed.");
    return;
  }
  int httpResponseCode = http.GET();
  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);
  http.end();
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
  Serial.print("isSilenced=");
  Serial.println(isSilenced ? "true" : "false");
}

// Non-blocking relay control used by the physical-button mirror path.
// Respects silence mode and the post-release cooldown. Safe to call
// repeatedly: it only acts on state transitions.
void relaySetOn() {
  if (relayOn) {
    return; // already on, nothing to do
  }
  if (isSilenced) {
    Serial.println("Relay ON suppressed: silenced.");
    return;
  }
  if ((millis() - lastRelayTime) < SLEEP_RELAY_AFTER_BELL_MS) {
    Serial.println("Relay ON suppressed: within cooldown.");
    return;
  }
  digitalWrite(bellRelayPin, HIGH);
  relayOn = true;
  relayOnSince = millis();
  relayActivations++;
  fillDateTimeString(lastRelayDateTime, sizeof(lastRelayDateTime));
  Serial.println("Relay ON (mirroring button).");
}

void relaySetOff() {
  if (!relayOn) {
    return;
  }
  digitalWrite(bellRelayPin, LOW);
  relayOn = false;
  // start the cooldown from the moment the relay is actually released,
  // so back-to-back presses are throttled but a single long hold is not
  // penalized any further than its own duration.
  lastRelayTime = millis();
  Serial.println("Relay OFF.");
}

// Blocking fixed-duration pulse, used by the HTTP-triggered paths
// (/relay and the simulated /button) where there is no physical press
// to mirror. Bails out if the relay is currently being held by the
// mirror path, to avoid fighting over the GPIO.
void pulseRelay() {
  if (relayOn) {
    Serial.println("pulseRelay() skipped: relay already held by physical button.");
    return;
  }
  if (isSilenced) {
    Serial.println("pulseRelay() skipped: silenced.");
    return;
  }
  if ((millis() - lastRelayTime) < SLEEP_RELAY_AFTER_BELL_MS) {
    Serial.println("pulseRelay() skipped: within cooldown.");
    return;
  }
  Serial.println("Relay pulse will be executed...");
  relayActivations++;
  fillDateTimeString(lastRelayDateTime, sizeof(lastRelayDateTime));
  digitalWrite(bellRelayPin, HIGH);
  delay(RELAY_DURATION_MS);
  digitalWrite(bellRelayPin, LOW);
  lastRelayTime = millis();
  Serial.println("Relay pulse completed.");
}

// HTTP-simulated button press (via /button). Does the full press
// sequence (MQTT ON, optional HTTP webhook, fixed relay pulse,
// MQTT OFF). The physical button does NOT go through this function;
// it is handled edge-by-edge in pollBellButton() so it can mirror
// the press duration.
void activateButton(const char* source) {
  Serial.println("Button Pressed (simulated)....");
  bellPresses++;
  blinkPurple(1);
  publishMqttState(source, "ON", true);
  pulseRelay();
  if ((millis() - lastHttpTime) >= SLEEP_HTTP_AFTER_BELL_MS) {
    sendHttpRequest();
    lastHttpTime = millis();
  } else {
    Serial.println("HTTP request within cooldown; not triggered.");
  }
  publishMqttState(source, "OFF", false);
  Serial.println("Actions were executed.");
}

// Poll the bell GPIO with software debouncing and MIRROR the relay to
// the debounced button state: relay is held HIGH for the duration of
// the press and released when the button is released. Side effects
// (counter, MQTT, HTTP webhook, LED blink) fire only on the press edge.
// A hard safety cap (RELAY_MAX_ON_MS) force-releases the relay if the
// button is stuck or held abusively long. Safe to call every loop().
void pollBellButton() {
  static int lastReading = HIGH;
  static int debouncedState = HIGH;
  static unsigned long lastDebounceChange = 0;

  // --- safety: force-off if held beyond the cap ---------------------------
  // Done first, every iteration, so it runs even when no edge happens.
  if (relayOn && (millis() - relayOnSince) >= RELAY_MAX_ON_MS) {
    Serial.println("Relay max-on reached, forcing OFF (safety).");
    relaySetOff();
  }

  // --- debounce -----------------------------------------------------------
  int reading = digitalRead(bellButtonPin);
  if (reading != lastReading) {
    lastDebounceChange = millis();
    lastReading = reading;
  }
  if ((millis() - lastDebounceChange) < DEBOUNCE_MS) {
    return; // not stable yet
  }
  if (reading == debouncedState) {
    return; // no edge
  }
  debouncedState = reading;

  // --- edge handling ------------------------------------------------------
  if (debouncedState == LOW) {
    // PRESS edge.
    //
    // Order matters: drive the relay FIRST so the chime fires with minimum
    // latency, BEFORE any network I/O or LED feedback. publishMqttState()
    // and (especially) sendHttpRequest() can block for seconds on a slow
    // network; if we did them first, a short press could end before the
    // relay was ever turned on.
    Serial.println("GPIO LOW (debounced) - press edge.");
    relaySetOn();
    bellPresses++;
    blinkPurple(1);
    publishMqttState("button", "ON", true);
    if ((millis() - lastHttpTime) >= SLEEP_HTTP_AFTER_BELL_MS) {
      // Defer the actual HTTPS call: do NOT block here, otherwise a slow
      // backend would prevent the release edge from being seen for the
      // whole HTTP_TIMEOUT_MS window. processPendingHttp() will fire it
      // from loop() after pollBellButton() returns.
      httpSendPending = true;
    } else {
      Serial.println("HTTP request within cooldown; not triggered.");
    }
  } else {
    // RELEASE edge.
    //
    // Same reasoning: release the relay FIRST so the chime stops promptly,
    // then announce the OFF state over MQTT.
    Serial.println("GPIO HIGH (debounced) - release edge.");
    relaySetOff();
    publishMqttState("button", "OFF", false);
  }
}

// Fire a deferred HTTPS webhook, if one is pending. Called from loop()
// AFTER pollBellButton(), so a release edge that arrived in the same
// iteration has already been processed (relay turned off) before we
// block on the network. At most one HTTP send per pending flag.
void processPendingHttp() {
  if (!httpSendPending) {
    return;
  }
  httpSendPending = false;
  sendHttpRequest();
  lastHttpTime = millis();
}

void loop() {
  // wait for setup()
  if (!isConfigured) {
    delay(1000);
    return;
  }
  // monitor the wifi
  watchWifi();
  // monitor the bell (debounced)
  pollBellButton();
  // fire any deferred HTTP webhook from the most recent press edge.
  // Done AFTER pollBellButton() so a same-iteration release edge has
  // already turned the relay off before we block on the network.
  processPendingHttp();
  // handle HTML requests
  server.handleClient();
  // handle OTA and reboot
  ElegantOTA.loop();
  // keep MQTT alive and reconnect if needed
  ensureMqttConnected();
  mqttClient.loop();
  // sleep a bit...
  delay(MONITORING_INTERVAL_MS);
}
