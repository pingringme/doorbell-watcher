// Copy this file to secrets.h (git-ignored) and fill in real values.
#ifndef SECRETS_H
#define SECRETS_H

// WiFi
#define WIFI_SSID          "***"   // 2.4 GHz network name (ESP32 does not support 5 GHz)
#define WIFI_PASSWORD      "***"   // WPA2/WPA3 passphrase

// AWS Lambda
#define HTTP_SERVER_URL    "***"   // full HTTPS endpoint of the Lambda function URL / API Gateway (no query string)
#define HTTP_SECURITY_CODE "***"   // shared secret validated server-side; rejects requests from anyone who finds the URL
#define HTTP_BELL_UUID     "***"   // identifies which physical doorbell is ringing so the Lambda can route to the right recipients

// Telegram
#define TELEGRAM_BOT_TOKEN "***"   // bot token issued by @BotFather
#define TELEGRAM_CHAT_ID   "***"   // destination chat: numeric user/group id, or @channelname for public channels

// MQTT
#define MQTT_AUTH_PASS     "***"   // broker password

#endif // SECRETS_H
