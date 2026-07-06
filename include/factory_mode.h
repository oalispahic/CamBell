#pragma once

bool shouldEnterFactoryMode();
void runFactoryMode();
// Called from the button handler: persists a "force setup on next boot"
// flag and immediately reboots. Never returns.
void triggerFactoryReset();
