#include "ota.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "config.h"
#include "display.h"
#include "server_api.h"
#include "secrets.h"



static bool fetchMeta(String& outVersion, size_t& outSize, String& outMd5) {
  WiFiClient client;
  HTTPClient http;

  String url = String(BACKEND_API_URL) + "/firmware/meta";
  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + getAuthToken());

  int code = http.GET();
  if (code != 200) {
    otaLogPrintf("OTA meta HTTP %d", code);
    http.end();
    return false;
  }

  // Minimal hand-rolled JSON pull — avoids pulling in ArduinoJson for
  // three fields. Expects: {"version":"...","size":123,"md5":"..."}
  String body = http.getString();
  http.end();

  auto extractString = [&](const char* key) -> String {
    String pat = String("\"") + key + "\":\"";
    int i = body.indexOf(pat);
    if (i < 0) return "";
    i += pat.length();
    int end = body.indexOf("\"", i);
    if (end < 0) return "";
    return body.substring(i, end);
  };
  auto extractInt = [&](const char* key) -> long {
    String pat = String("\"") + key + "\":";
    int i = body.indexOf(pat);
    if (i < 0) return -1;
    i += pat.length();
    int end = body.indexOf(",", i);
    int end2 = body.indexOf("}", i);
    if (end < 0 || (end2 >= 0 && end2 < end)) end = end2;
    return body.substring(i, end).toInt();
  };

  outVersion = extractString("version");
  outMd5     = extractString("md5");
  long sz    = extractInt("size");

  if (outVersion.length() == 0 || outMd5.length() == 0 || sz <= 0) {
    otaLogPrint("OTA meta parse fail");
    return false;
  }
  outSize = (size_t)sz;
  return true;
}

static bool downloadAndFlash(size_t expectedSize, const String& expectedMd5) {
  WiFiClient client;
  HTTPClient http;

  String url = String(BACKEND_API_URL) + "/firmware/latest.bin";
  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + getAuthToken());

  int code = http.GET();
  if (code != 200) {
    otaLogPrintf("OTA bin HTTP %d", code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0 || (size_t)contentLength != expectedSize) {
    otaLogPrint("OTA size mismatch");
    http.end();
    return false;
  }

  if (!Update.begin(contentLength)) {
    otaLogPrintf("OTA no space: %s", Update.errorString());
    http.end();
    return false;
  }

  Update.setMD5(expectedMd5.c_str());

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t written = 0;
  unsigned long lastProgressLog = 0;

  while (http.connected() && written < (size_t)contentLength) {
    size_t avail = stream->available();
    if (avail) {
      size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
      int c = stream->readBytes(buf, toRead);
      if (c > 0) {
        size_t w = Update.write(buf, c);
        if (w != (size_t)c) {
          otaLogPrintf("OTA write err: %s", Update.errorString());
          Update.abort();
          http.end();
          return false;
        }
        written += w;

        if (millis() - lastProgressLog > 150) {
          lastProgressLog = millis();
          otaLogProgress((int)(100 * written / expectedSize));
        }
      }
    } else {
      delay(1);
    }
  }
  http.end();

  if (written != expectedSize) {
    otaLogPrint("OTA incomplete transfer");
    Update.abort();
    return false;
  }

  if (!Update.end(true)) {
    otaLogPrintf("OTA end err: %s", Update.errorString());
    return false;
  }

  if (!Update.isFinished()) {
    otaLogPrint("OTA not finished");
    return false;
  }

  return true;
}

void checkAndPerformOTA() {
  otaLogPrint("OTA: checking...");

  String serverVersion, serverMd5;
  size_t serverSize = 0;

  if (!fetchMeta(serverVersion, serverSize, serverMd5)) {
    otaLogPrint("OTA: meta fetch failed");
    return;
  }

  otaLogPrintf("OTA: server %s", serverVersion.c_str());
  otaLogPrintf("OTA: local  %s", FIRMWARE_VERSION);

  if (serverVersion == FIRMWARE_VERSION) {
    otaLogPrint("OTA: already current");
    return;
  }

  otaLogPrint("OTA: flashing...");
  if (!downloadAndFlash(serverSize, serverMd5)) {
    otaLogPrint("OTA: FAILED, staying on old fw");
    return;
  }

  otaLogPrint("OTA: OK, rebooting");
  delay(500);
  ESP.restart();
}