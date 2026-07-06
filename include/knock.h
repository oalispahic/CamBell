#pragma once

void initKnock();
// Rising-edge, debounced. Returns true at most once per KNOCK_DEBOUNCE_MS.
bool knockDetected();
