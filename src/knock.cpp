#include "knock.h"
#include <Arduino.h>
#include "config.h"

// Rising-edge interrupt latches into a flag. The ISR fires regardless
// of what else the main loop is blocked on (HTTP polls, photo upload),
// so short knocks can't be missed.
static volatile bool          s_flag        = false;
static volatile unsigned long s_lastKnockMs = 0;

static void IRAM_ATTR knockISR() {
  unsigned long now = millis();
  if (now - s_lastKnockMs > KNOCK_DEBOUNCE_MS) {
    s_lastKnockMs = now;
    s_flag = true;
  }
}

void initKnock() {
  // PULLDOWN so the pin sits LOW if the sensor's D0 tri-states or the
  // wire wiggles loose — a floating pin picks up mains hum and phantom-
  // triggers the ISR.
  pinMode(KNOCK_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(KNOCK_PIN), knockISR, RISING);
}

bool knockDetected() {
  if (s_flag) {
    s_flag = false;
    return true;
  }
  return false;
}
