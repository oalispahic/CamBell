#include "state.h"
#include <Preferences.h>

static bool          s_armed        = false;
static bool          s_hadEvent     = false;
static unsigned long s_lastEventMs  = 0;

void initArmedState() {
  Preferences prefs;
  prefs.begin("cfg", true);
  s_armed = prefs.getBool("armed", false);
  prefs.end();
}

bool isArmed() { return s_armed; }

void setArmed(bool armed) {
  if (armed == s_armed) return;
  s_armed = armed;
  Preferences prefs;
  prefs.begin("cfg", false);
  prefs.putBool("armed", armed);
  prefs.end();
}

void toggleArmed() { setArmed(!s_armed); }

void markEvent() {
  s_lastEventMs = millis();
  s_hadEvent    = true;
}

bool          hadAnyEvent() { return s_hadEvent; }
unsigned long lastEventMs() { return s_lastEventMs; }
