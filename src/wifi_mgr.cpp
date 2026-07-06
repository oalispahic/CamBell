#include "wifi_mgr.h"
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "display.h"
// secrets.h no longer needed here — credentials are passed in

static bool wifi_connected = false;

void initWiFi(const char* ssid, const char* password) {
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    logMsg("WiFi OK");
    logMsgf("IP %s", WiFi.localIP().toString().c_str());
    wifi_connected = true;
    blinkLED(2, 300);
  } else {
    logMsg("WiFi FAILED");
    wifi_connected = false;
  }
}

bool isWiFiConnected() {
  return wifi_connected;
}