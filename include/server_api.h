#pragma once
#include "esp_camera.h"
#include <Arduino.h>

bool   serverLogin();
int    sendPhotoToBackend(camera_fb_t* fb);
int    sendBackendMessage(const char* text);
String getAuthToken();