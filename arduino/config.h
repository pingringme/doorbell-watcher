#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing arduino/secrets.h: copy secrets.example.h to secrets.h and fill in your values."
#endif

// ---------------------------------------------------------------------------
// Firmware
// ---------------------------------------------------------------------------
#define FIRMWARE_VERSION "20261003222246"

// ---------------------------------------------------------------------------
// Timings (milliseconds)
// ---------------------------------------------------------------------------
#define RELAY_DURATION_MS            750    // how long the relay should be active when triggered via HTTP (/relay or /button), i.e. without a physical press to mirror
#define RELAY_MAX_ON_MS              3000   // hard safety cap: never hold the relay on for longer than this, even if the physical button is stuck or held forever
#define SLEEP_AFTER_SETUP_MS         2000   // wait this long after setup() before allowing button presses, to avoid issues during boot
#define SLEEP_RELAY_AFTER_BELL_MS    15000  // after a bell press, do not allow another relay activation for this long, even if the button is pressed again (debounce + cooldown)
#define SLEEP_HTTP_AFTER_BELL_MS     15000  // after a bell press, do not allow another HTTP request for this long
#define SLEEP_AFTER_WIFI_RETRY_MS    5000   // after a failed WiFi connection attempt, wait this long before retrying, to avoid spamming the network
#define MONITORING_INTERVAL_MS       25     // interval between loop() iterations. Kept well below DEBOUNCE_MS (50) so the debounce filter gets at least 2 samples per window. Also bounds release-edge detection latency.
#define DEBOUNCE_MS                  50     // bell button debounce window
#define NTP_SYNC_TIMEOUT_MS          10000  // wait up to this long for NTP at boot
#define MQTT_RECONNECT_INTERVAL_MS   5000   // do not retry more often than this
#define HTTP_TIMEOUT_MS              3000   // outbound HTTPS request timeout. Kept tight: while the request is in flight the main loop cannot poll the button, so this bounds the worst-case release-detection delay. Sized to fit the AWS Lambda fan-out (SMS + WhatsApp + email) typical latency.

// ---------------------------------------------------------------------------
// Notification backends
// ---------------------------------------------------------------------------
// Toggle each backend independently. Both can be enabled at once; they will
// be fired sequentially from processPendingHttp() / activateButton().
// Runtime flags (matches mqtt_enabled style). Both backends are always
// compiled into the binary; disabling one only skips the HTTPS call.
const bool notify_aws_enabled      = false;
const bool notify_telegram_enabled = false;

// Credentials and endpoints (WiFi, AWS Lambda, Telegram, MQTT password) live in secrets.h.

// ---------------------------------------------------------------------------
// NTP
// ---------------------------------------------------------------------------
const char* ntpServer           = "pool.ntp.org"; // public NTP pool; override with a LAN NTP server if available
const long  gmtOffset_sec       = 3600;           // base UTC offset in seconds (+1h Berlin / CET)
const int   daylightOffset_sec  = 3600;           // additional DST offset in seconds (+1h during summer time)

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
const char* hostname = "esp32-pingringme"; // advertised hostname + mDNS name (resolves as esp32-pingringme.local)
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* dns_override_primary   = "8.8.8.8";   // empty = use the router's DNS (DHCP)
const char* dns_override_secondary = "1.1.1.1";   // only used if primary is set

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
const bool  mqtt_enabled                = true;                                                            // master switch for all MQTT activity (publish + Home Assistant discovery)
const char* mqtt_server_hostname_mdns   = "homeassistant";                                                 // broker hostname resolved via mDNS (homeassistant.local); change to a static IP if mDNS is unreliable
const int   mqtt_port                   = 1883;                                                            // plain MQTT (not TLS); keep on a trusted LAN only
const char* mqtt_auth_user              = "admin";                                                         // broker username
const char* mqtt_auth_pass              = MQTT_AUTH_PASS;                                                           // broker password
const char* mqtt_device_id              = "pingringme-esp32";                                              // MQTT client id, also used as the Home Assistant device identifier
const char* mqtt_unique_id              = "pingringme_doorbell";                                           // stable unique id for the HA entities; do not change after install or HA will create duplicates
const char* mqtt_topic_state            = "pingringme/doorbell/state";                                     // binary_sensor payload: "ON" while button held, "OFF" on release
const char* mqtt_topic_count            = "pingringme/doorbell/count";                                     // monotonically increasing press counter (sensor)
const char* mqtt_topic_attributes       = "pingringme/doorbell/attributes";                               // JSON attributes (timestamp, source, etc.) attached to the binary_sensor
const char* mqtt_topic_availability     = "pingringme/status";                                             // LWT topic: "online" / "offline" so HA shows the device as unavailable when it crashes
const char* mqtt_topic_discovery_state  = "homeassistant/binary_sensor/pingringme_doorbell/config";        // HA MQTT auto-discovery config for the press state
const char* mqtt_topic_discovery_count  = "homeassistant/sensor/pingringme_doorbell_count/config";         // HA MQTT auto-discovery config for the press counter

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------
const int bellButtonPin = 23;  // input: doorbell button sense line (active level handled in main.ino)
const int bellRelayPin  = 32;  // output: drives the relay that rings the chime

// ---------------------------------------------------------------------------
// HTTP request (built from the values above)
// ---------------------------------------------------------------------------
// Assembled once at startup so the hot path only does a single concatenation
// of the final URL. Lambda contract: GET <HTTP_SERVER_URL>?action=bell&sec=<code>&uuid=<id>
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
        function showResponse(url, status, body) {
          var out = document.getElementById('response');
          out.value = new Date().toLocaleTimeString() + '  GET ' + url + '  -> ' + status + '\n' + body + '\n\n' + out.value;
          // survives the location.reload() done after state-changing actions
          sessionStorage.setItem('response', out.value);
        }
        function executeAction(url, reload) {
          fetch(url)
              .then(function (response) {
                  return response.text().then(function (body) {
                      showResponse(url, response.status, body);
                      if (response.ok && reload) {
                          location.reload();
                      }
                  });
              })
              .catch(function (err) {
                  showResponse(url, 'error', err.message);
              });
        }
        function clearResponse() {
          document.getElementById('response').value = '';
          sessionStorage.removeItem('response');
        }
        window.addEventListener('DOMContentLoaded', function () {
          document.getElementById('response').value = sessionStorage.getItem('response') || '';
        });
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
                    <h2 class="card-title h4">Device</h2>
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
                    <h2 class="card-title h4">Network</h2>
                    <hr>
                    <p><strong>SSID:</strong> {{ssid}}</p>
                    <p><strong>IP Address:</strong> {{ip}}</p>
                    <p><strong>RSSI:</strong> {{rssi}}</p>
                    <p><strong>MAC Address:</strong> {{mac}}</p>
                    <p><strong>DNS:</strong> {{dns}}</p>
                </div>
            </div>
        </div>

        <div class="col-12 col-md-6">
            <div class="card shadow-sm h-100">
                <div class="card-body">
                    <h2 class="card-title h4">MQTT</h2>
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
                    <h2 class="card-title h4">Status</h2>
                    <hr>
                    <p><strong>WiFi Connection Retries:</strong> {{wifi_retries}}</p>
                    <p><strong>Bell Presses:</strong> {{bell_presses}}</p>
                    <p><strong>Relay Activations:</strong> {{relay_activations}}</p>
                    <p><strong>Last Relay Activation:</strong> {{relay_last}}</p>
                    <p><strong>Silence Mode:</strong> {{silence_mode}}</p>
                </div>
            </div>
        </div>

        <div class="col-12">
            <div class="card shadow-sm h-100">
                <div class="card-body">
                    <h2 class="card-title h4">Configuration</h2>
                    <hr>
                    <p><strong>MQTT:</strong> {{cfg_mqtt}}</p>
                    <p><strong>AWS Lambda Notifications:</strong> {{cfg_aws}}</p>
                    <p><strong>Telegram Notifications:</strong> {{cfg_telegram}}</p>
                    <p><strong>Telegram Bot Token:</strong> {{cfg_telegram_token}}</p>
                    <p><strong>Telegram Chat ID:</strong> {{cfg_telegram_chat}}</p>
                    <button type="button" class="btn btn-info" onclick="if (confirm('This triggers the real Lambda fan-out (SMS/WhatsApp/email). Continue?')) executeAction('/notify?backend=aws', false)">Test AWS</button>
                    <button type="button" class="btn btn-info" onclick="executeAction('/notify?backend=telegram', false)">Test Telegram</button>
                </div>
            </div>
        </div>

    </div>

    <h2 class="section-title">Actions</h2>

    <div class="row row-cols-1 row-cols-md-2 row-cols-lg-5 g-3">

        <div class="col">
            <button type="button" class="btn btn-primary w-100" onclick="executeAction('/silence', true)">Toggle Silence</button>
        </div>

        <div class="col">
            <button type="button" class="btn btn-warning w-100" onclick="executeAction('/relay', true)">Relay Only</button>
        </div>

        <div class="col">
            <button type="button" class="btn btn-success w-100" onclick="executeAction('/button', true)">Bell Button</button>
        </div>

        <div class="col">
            <form action="/update" method="get">
                <button type="submit" class="btn btn-danger w-100">Update Firmware</button>
            </form>
        </div>

        <div class="col">
            <button type="button" class="btn btn-dark w-100" onclick="if (confirm('Restart the device?')) executeAction('/restart', false)">Restart Device</button>
        </div>

    </div>

    <div class="d-flex justify-content-between align-items-center section-title mb-2">
        <h2 class="h4 mb-0">Response</h2>
        <button type="button" class="btn btn-sm btn-outline-secondary" onclick="clearResponse()">Clear</button>
    </div>
    <textarea id="response" class="form-control font-monospace" rows="8" readonly></textarea>

</div>
</body>
</html>
)=====";

#endif // CONFIG_H
