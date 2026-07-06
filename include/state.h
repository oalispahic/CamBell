#pragma once
#include <Arduino.h>

void initArmedState();
bool isArmed();
void setArmed(bool armed);
void toggleArmed();

void          markEvent();
bool          hadAnyEvent();
unsigned long lastEventMs();
