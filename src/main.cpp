#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "display.h"
#include "wifi_mgr.h"
#include "server_api.h"
#include "bmp280_helper.h"
#include "ota.h"

#define OTA_HOLD_MS 4000

unsigned long lastButtonPress = 0;
unsigned long lastTempUpdate  = 0;
unsigned long picMessageStart = 0;

static bool buttonWasDown  = false;
static unsigned long buttonDownAt = 0;
static bool longPressFired = false;
static unsigned long lastHoldDisplayUpdate = 0;

void blinkLED(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(duration);
    digitalWrite(LED_PIN, LOW);
    delay(duration);
  }
}

void setup() {
  delay(2000);
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  initDisplay();
  bmpInit();
  logMsg("Newer test !!!!!!!!!!!!!!!");
  logMsg("Newer test !!!!!!!!!!!!!!!");
  logMsg("Newer test !!!!!!!!!!!!!!!");
  logMsg(oled_ready     ? "OLED: OK"   : "OLED: FAIL");
  logMsg(bmpAvailable() ? "BMP280: OK" : "BMP280: FAIL");
  

  blinkLED(3, 200);

  logMsg("WiFi connecting");
  initWiFi();

  logMsg("Init camera");
  initCamera();
  applyCameraSettings();

  if (isWiFiConnected()) {
    logMsg("Backend login");
    if (serverLogin()) {
      logMsg("Login OK");
      int code = sendBackendMessage("ESP32-CAM connected and ready.");
      logMsgf("Msg code=%d", code);
    } else {
      logMsg("Login FAILED");
    }
  }

  logMsg("Setup complete");
  digitalWrite(LED_PIN, LOW);

  delay(1500);
  displayMode = MODE_TEMP;
  enterTempScreen();
  lastTempUpdate = millis();
}

void loop() {
  bool buttonDown = (digitalRead(BUTTON_PIN) == LOW);

  if (buttonDown && !buttonWasDown) {
    // Rising edge of a press
    buttonDownAt = millis();
    longPressFired = false;
  }

  if (buttonDown && !longPressFired) {
    // Update the on-screen progress bar a few times a second while held,
    // rather than every 50ms loop tick (avoids hammering the I2C bus).
    if (millis() - lastHoldDisplayUpdate > 150) {
      lastHoldDisplayUpdate = millis();
      int elapsed = (int)(millis() - buttonDownAt);
      int percent = (elapsed * 100) / OTA_HOLD_MS;
      showHoldProgress(percent);
    }
  }

  if (buttonDown && !longPressFired && millis() - buttonDownAt >= OTA_HOLD_MS) {
    // Held past the threshold — fire OTA immediately, don't wait for release
    longPressFired = true;
    Serial.println("\n[BUTTON HELD 4s -> OTA]");
    blinkLED(1, 600);  // long single blink = distinct from short-press feedback

    // Dedicated OTA screen — doesn't touch displayMode or u8log at all,
    // so there's nothing for it to desync with the temp/pic screens.
    otaLogBegin();

    if (isWiFiConnected()) {
      checkAndPerformOTA();  // reboots on success; returns here on failure/no-update
    } else {
      logMsg("OTA: no WiFi");
    }

    // Only reached if checkAndPerformOTA() returned without rebooting
    // (no update available, or a failure) — restore the normal screen.
    delay(1500);
    displayMode = MODE_TEMP;
    enterTempScreen();
    lastTempUpdate = millis();
  }

  if (!buttonDown && buttonWasDown && !longPressFired) {
    // Released before the OTA threshold -> treat as a normal short press
    if (millis() - lastButtonPress > BUTTON_DEBOUNCE_MS) {
      lastButtonPress = millis();
      Serial.println("\n[BUTTON PRESSED]");

      if (isWiFiConnected()) {
        captureAndSendPhoto();
      } else {
        Serial.println("WiFi not connected!");
        blinkLED(5, 100);
      }

      showPicTaken();
      displayMode = MODE_PIC;
      picMessageStart = millis();
    }
  }

  buttonWasDown = buttonDown;

  if (displayMode == MODE_PIC) {
    if (millis() - picMessageStart > PIC_MESSAGE_DURATION_MS) {
      displayMode = MODE_TEMP;
      enterTempScreen();
      lastTempUpdate = millis();
    }
  } else if (displayMode == MODE_TEMP) {
    if (millis() - lastTempUpdate > TEMP_UPDATE_INTERVAL_MS) {
      updateTempValue();
      lastTempUpdate = millis();
    }
  }

  delay(50);
}