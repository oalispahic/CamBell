#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include "config.h"
#include "display.h"
#include "wifi_mgr.h"
#include "server_api.h"
#include "bmp280_helper.h"
#include "ota.h"
#include "factory_mode.h"
#include "credentials.h"
#include "state.h"

unsigned long lastDashboardUpdate = 0;
unsigned long lastCommandPoll     = 0;

static bool          buttonWasDown         = false;
static unsigned long buttonDownAt          = 0;
static bool          longPressFired        = false;
static unsigned long lastHoldDisplayUpdate = 0;
static unsigned long lastReleaseAction     = 0;
static bool          holdProgressShown     = false;

// Double-click detection: on a short release we don't fire immediately —
// we wait DOUBLE_CLICK_MS for a possible second click, and only then
// classify. Second click within the window = double-click (photo);
// timeout = single click (arm/disarm).
static bool          pendingSingleClick    = false;
static unsigned long pendingSingleClickAt  = 0;

void blinkLED(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(duration);
    digitalWrite(LED_PIN, LOW);
    delay(duration);
  }
}

static void sendStatsToBackend() {
  DeviceInfo info = {};
  info.tempC               = bmpAvailable() ? bmpReadTemperature() : 0.0f;
  info.pressureHpa         = bmpAvailable() ? bmpReadPressureHpa() : 0.0f;
  info.voltageV            = 0.0f;   // no voltage divider wired; backend shows n/a
  info.armed               = isArmed();
  info.uptimeSeconds       = millis() / 1000;
  info.hadEvent            = hadAnyEvent();
  info.lastEventSecondsAgo = hadAnyEvent() ? (millis() - lastEventMs()) / 1000 : 0;
  sendDeviceInfo(info);
}

static void handleTelegramCommand(const String& cmd) {
  Serial.printf("[TG CMD] %s\n", cmd.c_str());

  if (cmd == "pic") {
    markEvent();
    updateDashboard();          // flash "EVENT !" before we go blocking on HTTP
    captureAndSendPhoto();
  } else if (cmd == "arm") {
    toggleArmed();
    Serial.printf("[ARMED = %s]\n", isArmed() ? "yes" : "no");
    sendBackendMessage(isArmed() ? "Now ARMED." : "Now DISARMED.");
    if (displayMode == MODE_DASHBOARD) enterDashboard();   // refresh top-left label
  } else if (cmd == "stats") {
    sendStatsToBackend();
  } else {
    Serial.println("[TG CMD] unknown, ignoring");
  }
}

void setup() {
  delay(2000);
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Bring the OLED up before the factory-mode check so runFactoryMode()
  // can show the SSID + IP instead of leaving a stale screen behind.
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  initDisplay();

  if (shouldEnterFactoryMode()) {
    runFactoryMode();   // never returns — reboots on save
  }

  Credentials creds = loadCredentials();
  initServerApi(creds.backendApiUrl, creds.backendApiUsername, creds.backendApiPassword);

  bmpInit();
  initArmedState();

  logMsg("Setup starting");
  logMsgf("Firmware v%s", FIRMWARE_VERSION);
  logMsg(oled_ready     ? "OLED: OK"   : "OLED: FAIL");
  logMsg(bmpAvailable() ? "BMP280: OK" : "BMP280: FAIL");

  blinkLED(3, 200);

  logMsg("WiFi connecting");
  initWiFi(creds.wifiSSID.c_str(), creds.wifiPassword.c_str());

  logMsg("Init camera");
  initCamera();
  applyCameraSettings();

  if (isWiFiConnected()) {
    logMsg("Backend login");
    if (serverLogin()) {
      logMsg("Login OK");
      sendBackendMessage("ESP32-CAM online.");
    } else {
      logMsg("Login FAILED");
    }
  }

  logMsg("Setup complete");
  digitalWrite(LED_PIN, LOW);

  delay(1500);
  displayMode = MODE_DASHBOARD;
  enterDashboard();
  lastDashboardUpdate = millis();
}

static void runOTAFlow() {
  Serial.println("[BUTTON HELD 5s -> OTA]");
  blinkLED(1, 600);
  otaLogBegin();
  if (isWiFiConnected()) {
    checkAndPerformOTA();   // reboots on success
  } else {
    otaLogPrint("OTA: no WiFi");
  }
  delay(1500);
  displayMode = MODE_DASHBOARD;
  enterDashboard();
  lastDashboardUpdate = millis();
}

void loop() {
  bool buttonDown = (digitalRead(BUTTON_PIN) == LOW);

  // ------- rising edge -------
  if (buttonDown && !buttonWasDown) {
    buttonDownAt   = millis();
    longPressFired = false;
  }

  // ------- while held -------
  if (buttonDown && !longPressFired) {
    unsigned long elapsed = millis() - buttonDownAt;

    // Only start drawing the hold-progress screen after 3s, so brief clicks
    // don't visibly flash over the dashboard.
    if (elapsed >= BUTTON_HOLD_DISPLAY_MS &&
        millis() - lastHoldDisplayUpdate > 150) {
      lastHoldDisplayUpdate = millis();
      holdProgressShown = true;

      // Rescale 3s..10s -> 0..100% so the bar visibly moves.
      unsigned long span = BUTTON_FACTORY_MS - BUTTON_HOLD_DISPLAY_MS;
      int pct = (int)((elapsed - BUTTON_HOLD_DISPLAY_MS) * 100 / span);
      const char* label;
      if (elapsed < BUTTON_OTA_MS)          label = "Hold 5s: OTA";
      else if (elapsed < BUTTON_FACTORY_MS) label = "Release: OTA";
      else                                  label = "Release: RESET";
      showHoldProgress(pct, label);
    }

    // 10s reached — reboot into setup mode. Doesn't return.
    if (elapsed >= BUTTON_FACTORY_MS) {
      longPressFired = true;
      Serial.println("[BUTTON HELD 10s -> FACTORY]");
      triggerFactoryReset();   // reboots
    }
  }

  // ------- release -------
  if (!buttonDown && buttonWasDown) {
    unsigned long elapsed = millis() - buttonDownAt;
    bool wasFired = longPressFired;
    longPressFired = false;

    if (!wasFired) {
      if (elapsed >= BUTTON_OTA_MS && elapsed < BUTTON_FACTORY_MS) {
        // Long press wins over any pending click classification.
        pendingSingleClick = false;
        runOTAFlow();
      } else if (elapsed >= 40) {
        // Short release: either the second half of a double-click, or
        // the first half of a possible one. Rate-limit against bounce.
        if (millis() - lastReleaseAction > BUTTON_DEBOUNCE_MS) {
          lastReleaseAction = millis();
          if (pendingSingleClick &&
              millis() - pendingSingleClickAt < DOUBLE_CLICK_MS) {
            // Second click within the window — double click.
            pendingSingleClick = false;
            Serial.println("[DOUBLE CLICK -> PHOTO]");
            markEvent();
            updateDashboard();
            lastDashboardUpdate = millis();
            if (isWiFiConnected()) {
              captureAndSendPhoto();
            } else {
              Serial.println("Photo skipped (no WiFi)");
              blinkLED(5, 100);
            }
          } else {
            // First click — arm/disarm decision is deferred until the
            // double-click window elapses (see top of loop).
            pendingSingleClick   = true;
            pendingSingleClickAt = millis();
          }
        }
      }
    }

    // Only redraw the dashboard if we actually painted the hold-progress
    // screen over it. A tap under 3s never touched the OLED, so redrawing
    // would just cause a visible flash.
    if (holdProgressShown) {
      displayMode = MODE_DASHBOARD;
      enterDashboard();
      lastDashboardUpdate = millis();
    }
    holdProgressShown = false;
  }

  buttonWasDown = buttonDown;

  // ------- confirm pending single click after the double-click window -------
  if (pendingSingleClick && millis() - pendingSingleClickAt > DOUBLE_CLICK_MS) {
    pendingSingleClick = false;
    toggleArmed();
    Serial.printf("[ARMED = %s]\n", isArmed() ? "yes" : "no");
    blinkLED(1, 100);
  }

  // ------- Telegram command polling -------
  if (isWiFiConnected() && millis() - lastCommandPoll > COMMAND_POLL_INTERVAL_MS) {
    lastCommandPoll = millis();
    String cmd;
    if (pollCommand(cmd)) {
      handleTelegramCommand(cmd);
    }
  }

  // ------- display refresh -------
  if (displayMode == MODE_DASHBOARD &&
      millis() - lastDashboardUpdate > DASHBOARD_UPDATE_INTERVAL) {
    updateDashboard();
    lastDashboardUpdate = millis();
  }

  delay(LOOP_TICK_MS);
}
