#include "motion.h"
#include <Arduino.h>
#include <string.h>
#include "esp_camera.h"
#include "config.h"

// QQVGA grayscale: 160 * 120 = 19,200 bytes of luma.
#define MOTION_W  160
#define MOTION_H  120
#define MOTION_SZ (MOTION_W * MOTION_H)

// Reference lives in PSRAM to keep DRAM free for the WiFi/HTTP stack.
static uint8_t* s_ref     = nullptr;
static bool     s_ready   = false;

// Drain a few frames after a camera mode change — the OV2640 needs a
// couple of frame times for its AGC/AWB to settle into the new format.
static void drainFrames(int n) {
  for (int i = 0; i < n; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }
}

void motionCameraForDetect() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  s->set_pixformat(s, PIXFORMAT_GRAYSCALE);
  s->set_framesize(s, FRAMESIZE_QQVGA);
  drainFrames(3);
}

void motionCameraForPhoto() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  s->set_pixformat(s, PIXFORMAT_JPEG);
  s->set_framesize(s, FRAMESIZE_UXGA);
  drainFrames(2);
}

static bool grabReference() {
  if (!s_ref) return false;
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;
  if (fb->format != PIXFORMAT_GRAYSCALE || fb->len < MOTION_SZ) {
    Serial.printf("[MOTION] wrong fb: fmt=%d len=%u\n", (int)fb->format, fb->len);
    esp_camera_fb_return(fb);
    return false;
  }
  memcpy(s_ref, fb->buf, MOTION_SZ);
  esp_camera_fb_return(fb);
  s_ready = true;
  return true;
}

bool initMotion() {
  if (!s_ref) {
    s_ref = (uint8_t*)ps_malloc(MOTION_SZ);
    if (!s_ref) s_ref = (uint8_t*)malloc(MOTION_SZ);
    if (!s_ref) {
      Serial.println("[MOTION] alloc failed");
      return false;
    }
  }
  return grabReference();
}

void motionResetReference() {
  grabReference();
}

bool motionDetected() {
  static unsigned long lastPollMs   = 0;
  static unsigned long lastMotionMs = 0;
  unsigned long now = millis();

  if (!s_ready) return false;

  // Cooldown — swallow polls entirely, don't even grab a frame.
  if (now - lastMotionMs < MOTION_COOLDOWN_MS) return false;

  if (now - lastPollMs < MOTION_POLL_INTERVAL_MS) return false;
  lastPollMs = now;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;

  if (fb->format != PIXFORMAT_GRAYSCALE || fb->len < MOTION_SZ) {
    // Someone else changed the format (photo capture in flight, boot
    // race, etc.) — bail without touching state.
    esp_camera_fb_return(fb);
    return false;
  }

  const uint8_t* cur = fb->buf;
  unsigned int changed = 0;
  for (int i = 0; i < MOTION_SZ; i++) {
    int diff = (int)cur[i] - (int)s_ref[i];
    if (diff < 0) diff = -diff;
    if (diff > MOTION_PIXEL_DELTA) changed++;
  }

  unsigned int pct = (changed * 100u) / MOTION_SZ;
  bool motion = pct >= MOTION_THRESHOLD_PCT;

  if (motion) {
    Serial.printf("[MOTION] %u%% pixels changed (%u/%u)\n",
                  pct, changed, MOTION_SZ);
    lastMotionMs = now;
    // Deliberately don't fold this frame into the reference — otherwise
    // sustained motion would train the detector to ignore itself.
  } else {
    // Slow EWMA drift so gradual lighting shifts don't accumulate into
    // a permanent false-positive baseline. α = 0.05.
    for (int i = 0; i < MOTION_SZ; i++) {
      s_ref[i] = (uint8_t)(((int)s_ref[i] * 95 + (int)cur[i] * 5) / 100);
    }
  }

  esp_camera_fb_return(fb);
  return motion;
}
