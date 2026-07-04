#include <Arduino.h>
#include "esp_camera.h"
#include "config.h"
#include "display.h"
#include "server_api.h"

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
    if (serverLogin()) {
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