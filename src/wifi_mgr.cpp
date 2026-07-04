#include "wifi_mgr.h"
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "display.h"
#include "secrets.h"

static bool wifi_connected = false;

void initWiFi() {
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  // Modem sleep left enabled (default) instead of forcing setSleep(false).
  // The radio still wakes immediately on any outgoing request (e.g. your
  // polling GET), it just powers down between DTIM beacon intervals when
  // idle instead of staying fully active 24/7. Biggest lever on baseline
  // current draw / heat for this board.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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