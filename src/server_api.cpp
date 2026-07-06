#include "server_api.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"   // FIRMWARE_VERSION
// secrets.h no longer needed here — credentials are passed in via initServerApi()

static String authToken = "";
static String backendUrl;
static String backendUsername;
static String backendPassword;

void initServerApi(const String& url, const String& username, const String& password) {
  backendUrl      = url;
  backendUsername = username;
  backendPassword = password;
}

String getAuthToken() {
  return authToken;
}

bool serverLogin() {
  WiFiClient client;
  HTTPClient http;

  String url = backendUrl + "/login";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  String body = String("{\"username\":\"") + backendUsername +
                "\",\"password\":\"" + backendPassword + "\"}";

  int code = http.POST(body);
  if (code == 200) {
    String resp = http.getString();
    int idx = resp.indexOf("\"token\":\"");
    if (idx >= 0) {
      int start = idx + 9;
      int end = resp.indexOf("\"", start);
      if (end > start) {
        authToken = resp.substring(start, end);
        http.end();
        return true;
      }
    }
  }
  Serial.printf("Login failed, code=%d\n", code);
  http.end();
  return false;
}

int sendPhotoToBackend(camera_fb_t* fb) {
  WiFiClient client;
  HTTPClient http;

  String url = backendUrl + "/telegram/photo";
  http.begin(client, url);
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Authorization", String("Bearer ") + authToken);

  Serial.printf("Posting %d bytes to %s\n", fb->len, url.c_str());
  int code = http.POST(fb->buf, fb->len);
  Serial.printf("Response code: %d\n", code);
  if (code > 0) {
    String resp = http.getString();
    Serial.println(resp);
  }
  http.end();
  return code;
}

int sendBackendMessage(const char* text) {
  WiFiClient client;
  HTTPClient http;

  String url = backendUrl + "/telegram";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + authToken);

  String body = String("{\"message\":\"") + text + "\"}";
  int code = http.POST(body);
  if (code > 0) {
    String resp = http.getString();
    Serial.println(resp);
  }
  http.end();
  return code;
}

bool pollCommand(String& outCmd) {
  if (backendUrl.length() == 0) return false;

  WiFiClient client;
  HTTPClient http;

  String url = backendUrl + "/device/command";
  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + authToken);
  http.setTimeout(3000);

  int code = http.GET();
  if (code != 200) {
    http.end();
    // Silent re-login on auth failure; next tick will retry.
    if (code == 401 || code == 403) serverLogin();
    return false;
  }

  String body = http.getString();
  http.end();

  // Look for {"cmd":"..."} — any {"cmd":null} or missing field returns false.
  int i = body.indexOf("\"cmd\":\"");
  if (i < 0) return false;
  i += 7;
  int end = body.indexOf("\"", i);
  if (end <= i) return false;
  outCmd = body.substring(i, end);
  outCmd.trim();
  return outCmd.length() > 0;
}

int sendDeviceInfo(const DeviceInfo& info) {
  if (backendUrl.length() == 0) return -1;

  WiFiClient client;
  HTTPClient http;

  String url = backendUrl + "/device/info";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + authToken);

  char body[320];
  snprintf(body, sizeof(body),
    "{\"armed\":%s,\"tempC\":%.1f,\"pressureHpa\":%.1f,\"voltageV\":%.2f,"
    "\"uptimeSeconds\":%lu,\"hadEvent\":%s,\"lastEventSecondsAgo\":%lu,"
    "\"firmware\":\"%s\"}",
    info.armed ? "true" : "false",
    info.tempC,
    info.pressureHpa,
    info.voltageV,
    info.uptimeSeconds,
    info.hadEvent ? "true" : "false",
    info.lastEventSecondsAgo,
    FIRMWARE_VERSION);

  int code = http.POST((uint8_t*)body, strlen(body));
  if (code > 0) {
    String resp = http.getString();
    Serial.println(resp);
  }
  http.end();
  return code;
}