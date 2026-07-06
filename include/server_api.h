#pragma once
#include "esp_camera.h"
#include <Arduino.h>

struct DeviceInfo {
  float         tempC;
  float         pressureHpa;
  float         voltageV;             // 0.0 if no voltage sense wired
  bool          armed;
  bool          hadEvent;
  unsigned long uptimeSeconds;
  unsigned long lastEventSecondsAgo;
};

void   initServerApi(const String& url, const String& username, const String& password);
bool   serverLogin();
int    sendPhotoToBackend(camera_fb_t* fb);
int    sendBackendMessage(const char* text);
String getAuthToken();

// GETs the next pending command for this device from the backend.
// Backend must return either {"cmd":"photo"} / {"cmd":"info"} / {"cmd":null}.
// Returns true iff a non-null command was returned; command name is written
// to outCmd.
bool   pollCommand(String& outCmd);

// POSTs current device state to the backend so it can be relayed to
// Telegram in response to an /info request.
int    sendDeviceInfo(const DeviceInfo& info);