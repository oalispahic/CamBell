#pragma once
#include <Arduino.h>

enum DisplayMode { MODE_BOOT_LOG, MODE_TEMP, MODE_PIC };

extern DisplayMode displayMode;
extern bool oled_ready;

void initDisplay();
void logMsg(const char* msg);
void logMsgf(const char* fmt, ...);
void enterTempScreen();
void updateTempValue();
void showPicTaken();
void showHoldProgress(int percent);

// Dedicated OTA status renderer — does NOT reuse the boot-time u8log
// widget, since u8log tracks its own cursor separately from the OLED's
// actual pixel buffer, and gets desynced/garbled once the temp screen
// has been drawing over the same rows for hours in between.
void otaLogBegin();
void otaLogPrint(const char* msg);
void otaLogPrintf(const char* fmt, ...);