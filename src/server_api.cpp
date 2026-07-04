#include "server_api.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"

static String authToken = "";

String getAuthToken() {
  return authToken;
}

bool serverLogin() {
  WiFiClient client;
  HTTPClient http;

  String url = String(BACKEND_API_URL) + "/login";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  String body = String("{\"username\":\"") + BACKEND_API_USERNAME +
                "\",\"password\":\"" + BACKEND_API_PASSWORD + "\"}";

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

  String url = String(BACKEND_API_URL) + "/telegram/photo";
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

  String url = String(BACKEND_API_URL) + "/telegram";
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