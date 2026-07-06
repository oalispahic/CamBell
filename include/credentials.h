#pragma once
#include <Preferences.h>
#include "secrets.h"

struct Credentials {
    String wifiSSID, wifiPassword;
    String telegramApiKey, telegramChatId;
    String backendApiUrl, backendApiUsername, backendApiPassword;
};

inline Credentials loadCredentials() {
    Preferences prefs;
    prefs.begin("cfg", true);

    Credentials c;
    c.wifiSSID          = prefs.getString("wifi_ssid", WIFI_SSID);
    c.wifiPassword       = prefs.getString("wifi_pass", WIFI_PASSWORD);
    c.telegramApiKey     = prefs.getString("tg_key",   TELEGRAM_API_KEY);
    c.telegramChatId     = prefs.getString("tg_chat",  TELEGRAM_CHAT_ID);
    c.backendApiUrl      = prefs.getString("api_url",  BACKEND_API_URL);
    c.backendApiUsername = prefs.getString("api_user", BACKEND_API_USERNAME);
    c.backendApiPassword = prefs.getString("api_pass", BACKEND_API_PASSWORD);

    prefs.end();
    return c;
}