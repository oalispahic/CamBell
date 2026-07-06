#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <string.h>
#include "config.h"
#include "factory_mode.h"
#include "display.h"
#include "secrets.h"

static WebServer server(80);

static const char* FORM_HTML = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<title>Device Setup</title>
<style>
body{font-family:sans-serif;max-width:420px;margin:30px auto;padding:0 16px;}
label{display:block;margin-top:14px;font-weight:600;}
input{width:100%;padding:8px;margin-top:4px;box-sizing:border-box;}
button{margin-top:20px;padding:10px 20px;width:100%;font-size:16px;}
fieldset{margin-top:20px;}
.err{color:#b00020;font-weight:600;}
</style></head><body>
<h2>%FIRMWARE%</h2>
%ERROR%
<p>Enter your Wi-Fi and connectivity details, then press Save. The device will restart.</p>
<form action="/save" method="POST">
  <fieldset>
    <legend>Wi-Fi</legend>
    <label>SSID *</label><input name="wifi_ssid" value="%WIFI_SSID%" required>
    <label>Password</label><input name="wifi_password" type="password" value="%WIFI_PASS%">
  </fieldset>
  <fieldset>
    <legend>Telegram (optional)</legend>
    <label>Bot API Key</label><input name="tg_key" value="%TG_KEY%">
    <label>Chat ID</label><input name="tg_chat" value="%TG_CHAT%">
  </fieldset>
  <fieldset>
    <legend>Backend API (optional)</legend>
    <label>URL</label><input name="api_url" value="%API_URL%">
    <label>Username</label><input name="api_user" value="%API_USER%">
    <label>Password</label><input name="api_pass" type="password" value="%API_PASS%">
  </fieldset>
  <button type="submit">Save &amp; Reboot</button>
</form>
</body></html>
)HTML";

// Values re-populated into the form after a failed submit, so the user
// doesn't have to retype everything just because SSID was blank.
struct PendingForm {
    String wifiSsid, wifiPass, tgKey, tgChat, apiUrl, apiUser, apiPass;
    String error;
};
static PendingForm pending;

// Basic HTML-attribute escaping — these values get echoed back inside
// value="..." attributes, so a stray " or < in a password field would
// otherwise break the form markup.
static String htmlEscape(const String& in) {
    String out;
    out.reserve(in.length());
    for (char c : in) {
        switch (c) {
            case '"':  out += "&quot;"; break;
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            default:   out += c;
        }
    }
    return out;
}

static String trim(const String& in) {
    String out = in;
    out.trim();
    return out;
}

static void handleRoot() {
    String page = FORM_HTML;
    page.replace("%FIRMWARE%", FIRMWARE_VERSION);
    page.replace("%ERROR%", pending.error.length()
        ? ("<p class=\"err\">" + pending.error + "</p>") : "");
    page.replace("%WIFI_SSID%", htmlEscape(pending.wifiSsid));
    page.replace("%WIFI_PASS%", htmlEscape(pending.wifiPass));
    page.replace("%TG_KEY%",    htmlEscape(pending.tgKey));
    page.replace("%TG_CHAT%",   htmlEscape(pending.tgChat));
    page.replace("%API_URL%",   htmlEscape(pending.apiUrl));
    page.replace("%API_USER%",  htmlEscape(pending.apiUser));
    page.replace("%API_PASS%",  htmlEscape(pending.apiPass));

    pending.error = ""; // one-shot — don't stick around after being shown
    server.send(200, "text/html", page);
}

static void handleSave() {
    pending.wifiSsid = trim(server.arg("wifi_ssid"));
    pending.wifiPass = server.arg("wifi_password"); // don't trim passwords — spaces may be intentional
    pending.tgKey    = trim(server.arg("tg_key"));
    pending.tgChat   = trim(server.arg("tg_chat"));
    pending.apiUrl   = trim(server.arg("api_url"));
    pending.apiUser  = trim(server.arg("api_user"));
    pending.apiPass  = server.arg("api_pass");

    // --- Validation ---
    // SSID is the only field that can hard-brick the boot (device would
    // come up with an empty WiFi.begin() and never connect, with no way
    // back into factory mode except the button-hold escape hatch).
    if (pending.wifiSsid.length() == 0) {
        pending.error = "SSID cannot be empty.";
        handleRoot();
        return;
    }
    if (pending.wifiSsid.length() > 32) {
        // 802.11 hard limit
        pending.error = "SSID is too long (max 32 characters).";
        handleRoot();
        return;
    }
    if (pending.wifiPass.length() > 0 && pending.wifiPass.length() < 8) {
        // WPA2 minimum — an open network is fine (empty), but a too-short
        // password will just fail to connect silently later.
        pending.error = "WiFi password must be at least 8 characters, or left blank for an open network.";
        handleRoot();
        return;
    }
    if (pending.apiUrl.length() > 0 &&
        !pending.apiUrl.startsWith("http://") &&
        !pending.apiUrl.startsWith("https://")) {
        pending.error = "Backend API URL must start with http:// or https://";
        handleRoot();
        return;
    }

    // --- Persist ---
    Preferences prefs;
    prefs.begin("cfg", false);

    prefs.putString("wifi_ssid", pending.wifiSsid);
    prefs.putString("wifi_pass", pending.wifiPass);
    prefs.putString("tg_key",    pending.tgKey);
    prefs.putString("tg_chat",   pending.tgChat);
    prefs.putString("api_url",   pending.apiUrl);
    prefs.putString("api_user",  pending.apiUser);
    prefs.putString("api_pass",  pending.apiPass);
    prefs.putBool("configured", true);

    prefs.end();

    server.send(200, "text/html", "<html><body><h3>Saved. Rebooting&hellip;</h3></body></html>");
    showFactorySaved();
    delay(1500);
    ESP.restart();
}

// Anything not / or /save just bounces back to the form (poor man's captive portal)
static void handleNotFound() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

bool shouldEnterFactoryMode() {
    Preferences prefs;
    prefs.begin("cfg", false);   // rw — we want to clear force_setup atomically
    bool forceSetup = prefs.getBool("force_setup", false);
    if (forceSetup) prefs.putBool("force_setup", false);
    bool configured = prefs.getBool("configured", false);
    prefs.end();

    if (forceSetup) return true;

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    if (digitalRead(BUTTON_PIN) == LOW) {
        unsigned long start = millis();
        while (digitalRead(BUTTON_PIN) == LOW) {
            if (millis() - start > FACTORY_RESET_HOLD_MS) return true; // forced re-setup
        }
    }

    if (configured) return false;

    // Device wasn't provisioned via the web UI, but if secrets.h has a real
    // SSID compiled in, the older firmware was already using it — keep
    // booting normally instead of stranding OTA-upgraded devices in the AP.
    const char* ssid = WIFI_SSID;
    if (ssid && ssid[0] != '\0' && strcmp(ssid, "SSID") != 0) {
        return false;
    }
    return true;
}

void triggerFactoryReset() {
    Preferences prefs;
    prefs.begin("cfg", false);
    prefs.putBool("force_setup", true);
    prefs.end();
    delay(200);
    ESP.restart();
}

void runFactoryMode() {
    // If we're transitioning here mid-run (10s button hold), STA may still
    // be up — drop it before flipping to AP so the WiFi stack isn't in a
    // hybrid state that confuses softAP().
    WiFi.disconnect(true, true);
    delay(200);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(FACTORY_MODE_AP_SSID, FACTORY_MODE_AP_PASSWORD);

    IPAddress ip = WiFi.softAPIP();   // typically 192.168.4.1
    Serial.print("[FACTORY] AP IP: ");
    Serial.println(ip);

    showFactorySetup(FACTORY_MODE_AP_SSID, ip.toString().c_str());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    blinkLED(3, 200); // visual "I'm in setup mode" signal

    while (true) {
        server.handleClient();
    }
}