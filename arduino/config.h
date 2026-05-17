#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Firmware
// ---------------------------------------------------------------------------
#define FIRMWARE_VERSION "20251126092014"

// ---------------------------------------------------------------------------
// Timings (milliseconds)
// ---------------------------------------------------------------------------
#define RELAY_DURATION_MS            750
#define SLEEP_AFTER_SETUP_MS         10000  // 10s
#define SLEEP_RELAY_AFTER_BELL_MS    45000  // 45s
#define SLEEP_HTTP_AFTER_BELL_MS     20000  // 20s
#define SLEEP_AFTER_WIFI_RETRY_MS    10000  // 10s
#define MONITORING_INTERVAL_MS       100
#define DEBOUNCE_MS                  50     // bell button debounce window
#define NTP_SYNC_TIMEOUT_MS          10000  // wait up to this long for NTP at boot
#define MQTT_RECONNECT_INTERVAL_MS   5000   // do not retry more often than this
#define HTTP_TIMEOUT_MS              5000   // outbound HTTPS request timeout

// ---------------------------------------------------------------------------
// HTTP backend
// ---------------------------------------------------------------------------
#define HTTP_SERVER_URL    "https://***"
#define HTTP_SECURITY_CODE "***"
#define HTTP_BELL_UUID     "***"

// ---------------------------------------------------------------------------
// WiFi credentials
// ---------------------------------------------------------------------------
#define WIFI_SSID     "***"
#define WIFI_PASSWORD "***"

// ---------------------------------------------------------------------------
// NTP
// ---------------------------------------------------------------------------
const char* ntpServer           = "pool.ntp.org";
const long  gmtOffset_sec       = 3600; // +1h Berlin
const int   daylightOffset_sec  = 3600; // +1h summer time

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
const char* hostname = "esp32-pingringme";
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
const bool  mqtt_enabled                = true;
const char* mqtt_server_hostname_mdns   = "homeassistant"; // homeassistant.local
const int   mqtt_port                   = 1883;
const char* mqtt_auth_user              = "admin";
const char* mqtt_auth_pass              = "***";
const char* mqtt_device_id              = "pingringme-esp32";
const char* mqtt_unique_id              = "pingringme_doorbell";
const char* mqtt_topic_state            = "pingringme/doorbell/state";
const char* mqtt_topic_count            = "pingringme/doorbell/count";
const char* mqtt_topic_attributes       = "pingringme/doorbell/attributes";
const char* mqtt_topic_availability     = "pingringme/status";
const char* mqtt_topic_discovery_state  = "homeassistant/binary_sensor/pingringme_doorbell/config";
const char* mqtt_topic_discovery_count  = "homeassistant/sensor/pingringme_doorbell_count/config";

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------
const int bellButtonPin = 23;  // button press pin
const int bellRelayPin  = 32;  // relay pin

// ---------------------------------------------------------------------------
// HTTP request (built from the values above)
// ---------------------------------------------------------------------------
const String serverPath    = HTTP_SERVER_URL;
const String action        = "?action=bell";
const String security      = "&sec=";
const String security_val  = HTTP_SECURITY_CODE;
const String uuid          = "&uuid=";
const String uuid_val      = HTTP_BELL_UUID;
const String serverRequest = serverPath + action + security + security_val + uuid + uuid_val;

// ---------------------------------------------------------------------------
// HTML template
// ---------------------------------------------------------------------------
const String html_template_main = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>PingRing.me</title>

    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.8/dist/css/bootstrap.min.css"
          rel="stylesheet"
          integrity="sha384-sRIl4kxILFvY47J16cr9ZwB07vP4J8+LH7qKQnuqkuIAvNWLzeN8tE5YBujZqJLB"
          crossorigin="anonymous">

    <style>
        body { background-color: #f7f9fc; }
        .card { border-radius: 12px; }
        .section-title { margin-top: 25px; }
    </style>
    <script>
        function executeAction(url) {
          fetch(url)
              .then(function (response) {
                  if (!response.ok) {
                      throw new Error('HTTP ' + response.status);
                  }
                  location.reload();
              })
              .catch(function (err) {
                  alert('Request failed: ' + err.message);
              });
        }
    </script>
</head>

<body>
<div class="container py-4">

    <h1 class="text-center mb-4">
        <a href="https://pingring.me" target="_blank" class="text-decoration-none">PingRing.me</a>
    </h1>

    <div class="row g-4">

        <div class="col-12 col-md-6">
            <div class="card shadow-sm h-100">
                <div class="card-body">
                    <h4 class="card-title">Device</h4>
                    <hr>
                    <p><strong>Firmware Version:</strong> {{firmware}}</p>
                    <p><strong>Startup Time:</strong> {{startup}}</p>
                    <p><strong>Current Time:</strong> {{now}}</p>
                    <p><strong>Free Heap Memory:</strong> {{heap}} bytes</p>
                </div>
            </div>
        </div>

        <div class="col-12 col-md-6">
            <div class="card shadow-sm h-100">
                <div class="card-body">
                    <h4 class="card-title">Network</h4>
                    <hr>
                    <p><strong>SSID:</strong> {{ssid}}</p>
                    <p><strong>IP Address:</strong> {{ip}}</p>
                    <p><strong>RSSI:</strong> {{rssi}}</p>
                    <p><strong>MAC Address:</strong> {{mac}}</p>
                </div>
            </div>
        </div>

        <div class="col-12 col-md-6">
            <div class="card shadow-sm h-100">
                <div class="card-body">
                    <h4 class="card-title">MQTT</h4>
                    <hr>
                    <p><strong>Server:</strong> {{mqtt_server}}</p>
                    <p><strong>Connected:</strong> {{mqtt_connected}} (retries: {{mqtt_retries}})</p>
                    <p><strong>Discovery Messages Sent:</strong> {{mqtt_discovery}}</p>
                </div>
            </div>
        </div>

        <div class="col-12 col-md-6">
            <div class="card shadow-sm h-100">
                <div class="card-body">
                    <h4 class="card-title">Status</h4>
                    <hr>
                    <p><strong>WiFi Connection Retries:</strong> {{wifi_retries}}</p>
                    <p><strong>Bell Presses:</strong> {{bell_presses}}</p>
                    <p><strong>Relay Activations:</strong> {{relay_activations}}</p>
                    <p><strong>Last Relay Activation:</strong> {{relay_last}}</p>
                    <p><strong>Silence Mode:</strong> {{silence_mode}}</p>
                </div>
            </div>
        </div>

    </div>

    <h2 class="section-title">Actions</h2>

    <div class="row g-3">

        <div class="col-12 col-md-6 col-lg-3">
            <button type="button" class="btn btn-primary w-100" onclick="executeAction('/silence')">Toggle Silence</button>
        </div>

        <div class="col-12 col-md-6 col-lg-3">
            <button type="button" class="btn btn-warning w-100" onclick="executeAction('/relay')">Relay Only</button>
        </div>

        <div class="col-12 col-md-6 col-lg-3">
            <button type="button" class="btn btn-success w-100" onclick="executeAction('/button')">Bell Button</button>
        </div>

        <div class="col-12 col-md-6 col-lg-3">
            <form action="/update" method="get">
                <button type="submit" class="btn btn-danger w-100">Update Firmware</button>
            </form>
        </div>

    </div>

</div>
</body>
</html>
)=====";

#endif // CONFIG_H
