#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <U8x8lib.h>
#include "bmp280_helper.h"
#include "../include/secrets.h"

// ========== WiFi Configuration ==========
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// ========== Backend Configuration ==========
const char* backend_base = BACKEND_API_URL;
const char* backend_username = BACKEND_API_USERNAME;
const char* backend_password = BACKEND_API_PASSWORD;

String authToken = "";

// ========== Pin Configuration ==========
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define BUTTON_PIN        14
#define LED_PIN            4

// I2C shared by OLED + BMP280. Note: pin 1 is also UART0 TX,
// so USB serial output will be disabled once Wire takes the pin.
#define I2C_SDA            3
#define I2C_SCL            1

// ========== I2C Devices ==========
U8X8_SH1106_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);
#define OLED_I2C_ADDR 0x3C

#define U8LOG_WIDTH  16
#define U8LOG_HEIGHT 8
uint8_t u8log_buf[U8LOG_WIDTH * U8LOG_HEIGHT];
U8X8LOG u8log;

bool oled_ready = false;

enum DisplayMode { MODE_BOOT_LOG, MODE_TEMP, MODE_PIC };
DisplayMode displayMode = MODE_BOOT_LOG;

// ========== State ==========
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 300;
bool wifi_connected = false;

unsigned long lastTempUpdate = 0;
const unsigned long tempUpdateInterval = 2000;
unsigned long picMessageStart = 0;
const unsigned long picMessageDuration = 1500;

// ========== Prototypes ==========
void initI2CDevices();
void initWiFi();
void initCamera();
bool login();
void captureAndSendPhoto();
int  sendPhotoToBackend(camera_fb_t* fb);
int  sendBackendMessage(const char* text);
void blinkLED(int times, int duration);
void applyCameraSettings();
void logMsg(const char* msg);
void logMsgf(const char* fmt, ...);
void enterTempScreen();
void updateTempValue();
void showPicTaken();
void sh1106ClearAllColumns();

// ========== Setup ==========
void setup() {
  delay(2000);
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  initI2CDevices();

  logMsg("=== ESP32-CAM ===");
  logMsg(oled_ready       ? "OLED: OK"   : "OLED: FAIL");
  logMsg(bmpAvailable()   ? "BMP280: OK" : "BMP280: FAIL");

  blinkLED(3, 200);

  logMsg("WiFi connecting");
  initWiFi();

  logMsg("Init camera");
  initCamera();
  applyCameraSettings();

  if (wifi_connected) {
    logMsg("Backend login");
    if (login()) {
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

// ========== Loop ==========
void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis();
      Serial.println("\n[BUTTON PRESSED]");

      if (wifi_connected) {
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

  if (displayMode == MODE_PIC) {
    if (millis() - picMessageStart > picMessageDuration) {
      displayMode = MODE_TEMP;
      enterTempScreen();
      lastTempUpdate = millis();
    }
  } else if (displayMode == MODE_TEMP) {
    if (millis() - lastTempUpdate > tempUpdateInterval) {
      updateTempValue();
      lastTempUpdate = millis();
    }
  }

  delay(50);
}

// ========== I2C Devices Init ==========
void initI2CDevices() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  oled.begin();
  oled.setFlipMode(0);
  sh1106ClearAllColumns();  // wipe RAM cols 0-1 and 130-131 that NONAME never writes
  oled.setFont(u8x8_font_chroma48medium8_r);
  u8log.begin(oled, U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buf);
  u8log.setRedrawMode(0);
  oled_ready = true;

  bmpInit();
}

// ========== Logging Helpers ==========
void logMsg(const char* msg) {
  Serial.println(msg);
  if (oled_ready && displayMode == MODE_BOOT_LOG) {
    u8log.print(msg);
    u8log.print("\n");
  }
}

void logMsgf(const char* fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  logMsg(buf);
}

// ========== Display Screens ==========
// Manually clear all 132 SH1106 RAM columns. U8g2's NONAME variant
// writes with a +2 column offset and only touches cols 2-129, so any
// power-on garbage in cols 0-1 / 130-131 stays visible if the panel
// maps the full 132 columns.
void sh1106ClearAllColumns() {
  for (uint8_t page = 0; page < 8; page++) {
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(0x00);            // command stream
    Wire.write(0xB0 | page);     // set page
    Wire.write(0x00);            // col low nibble = 0
    Wire.write(0x10);            // col high nibble = 0
    Wire.endTransmission();

    // Send 132 zero bytes as data, chunked to fit the Wire buffer.
    uint8_t remaining = 132;
    while (remaining) {
      uint8_t chunk = remaining > 16 ? 16 : remaining;
      Wire.beginTransmission(OLED_I2C_ADDR);
      Wire.write(0x40);          // data stream
      for (uint8_t i = 0; i < chunk; i++) Wire.write((uint8_t)0x00);
      Wire.endTransmission();
      remaining -= chunk;
    }
  }
}

void enterTempScreen() {
  if (!oled_ready) return;
  oled.clear();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 0, "Temperature:");
  updateTempValue();
}

void updateTempValue() {
  if (!oled_ready) return;
  // Pad to 8 chars so a shorter new reading (e.g. "9.5 C")
  // fully overwrites the prior one without re-clearing the screen.
  char buf[16];
  if (bmpAvailable()) {
    snprintf(buf, sizeof(buf), "%5.1f C ", bmpReadTemperature());
  } else {
    snprintf(buf, sizeof(buf), "BMP err ");
  }
  oled.setFont(u8x8_font_courB18_2x3_f);
  oled.drawString(0, 3, buf);
}

void showPicTaken() {
  if (!oled_ready) return;
  oled.clear();
  oled.setFont(u8x8_font_courB18_2x3_f);
  oled.drawString(0, 1, "Picture");
  oled.drawString(0, 4, "taken!");
}

void applyCameraSettings() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) {
    logMsg("Sensor get fail");
    return;
  }

  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);

  s->set_brightness(s, -1);
  s->set_contrast(s, 0);
  s->set_saturation(s, 0);
  s->set_whitebal(s, 1);
  s->set_exposure_ctrl(s, 1);

  logMsg("Cam settings OK");
}

// ========== WiFi Initialization ==========
void initWiFi() {
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(ssid, password);

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

// ========== Camera Initialization ==========
void initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 14;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 14;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    logMsg("Cam init FAIL");
    while (1) blinkLED(10, 100);
  }

  logMsg("Cam init OK");
}

// ========== Login (JWT) ==========
bool login() {
  WiFiClient client;
  HTTPClient http;

  String url = String(backend_base) + "/login";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  String body = String("{\"username\":\"") + backend_username +
                "\",\"password\":\"" + backend_password + "\"}";

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

// ========== Capture and Send Photo ==========
void captureAndSendPhoto() {
  Serial.println("Capturing photo...");

  for (int i = 0; i < 2; i++) {
    camera_fb_t* stale = esp_camera_fb_get();
    if (stale) esp_camera_fb_return(stale);
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed!");
    blinkLED(5, 100);
    return;
  }

  Serial.printf("Photo captured. Size: %d bytes\n", fb->len);

  int code = sendPhotoToBackend(fb);
  if (code == 401 || code == 403) {
    Serial.println("Auth expired, re-logging in...");
    if (login()) {
      code = sendPhotoToBackend(fb);
    }
  }

  if (code == 200) {
    Serial.println("Photo sent successfully!");
    blinkLED(3, 300);
  } else {
    Serial.printf("Upload failed, code=%d\n", code);
    blinkLED(5, 100);
  }

  esp_camera_fb_return(fb);
}

// ========== Send Photo to Backend ==========
int sendPhotoToBackend(camera_fb_t* fb) {
  WiFiClient client;
  HTTPClient http;

  String url = String(backend_base) + "/telegram/photo";
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

// ========== Send Text Message via Backend ==========
int sendBackendMessage(const char* text) {
  WiFiClient client;
  HTTPClient http;

  String url = String(backend_base) + "/telegram";
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

// ========== LED Blink Utility ==========
void blinkLED(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(duration);
    digitalWrite(LED_PIN, LOW);
    delay(duration);
  }
}
