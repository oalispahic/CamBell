#include "display.h"
#include "config.h"
#include <Wire.h>
#include <U8x8lib.h>
#include "bmp280_helper.h"

static U8X8_SH1106_128X64_NONAME_HW_I2C oled(U8X8_PIN_NONE);
static uint8_t u8log_buf[U8LOG_WIDTH * U8LOG_HEIGHT];
static U8X8LOG u8log;

bool oled_ready = false;
DisplayMode displayMode = MODE_BOOT_LOG;

// Manually clear all 132 SH1106 RAM columns. U8g2's NONAME variant
// writes with a +2 column offset and only touches cols 2-129, so any
// power-on garbage in cols 0-1 / 130-131 stays visible if the panel
// maps the full 132 columns.
static void sh1106ClearAllColumns() {
  for (uint8_t page = 0; page < 8; page++) {
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(0x00);            // command stream
    Wire.write(0xB0 | page);     // set page
    Wire.write(0x00);            // col low nibble = 0
    Wire.write(0x10);            // col high nibble = 0
    Wire.endTransmission();

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

void initDisplay() {
  oled.begin();
  oled.setFlipMode(0);
  sh1106ClearAllColumns();
  oled.setFont(u8x8_font_chroma48medium8_r);
  u8log.begin(oled, U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buf);
  u8log.setRedrawMode(0);
  oled_ready = true;
}

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

void showHoldProgress(int percent) {
  if (!oled_ready) return;
  if (percent > 100) percent = 100;
  oled.clear();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 0, "Hold for update");

  // Simple text bar, 16 chars wide to match U8LOG_WIDTH.
  char bar[17];
  int filled = (percent * 16) / 100;
  for (int i = 0; i < 16; i++) bar[i] = (i < filled) ? '#' : '-';
  bar[16] = '\0';
  oled.drawString(0, 2, bar);

  char pct[8];
  snprintf(pct, sizeof(pct), "%3d%%", percent);
  oled.setFont(u8x8_font_courB18_2x3_f);
  oled.drawString(0, 4, pct);
}

// ===== OTA status log =====
// Self-contained: owns its own line buffer and redraws every row from
// scratch on every call. Doesn't touch u8log or its internal cursor,
// so it can't get desynced by whatever the temp/pic screens drew
// before it, and can't desync anything after it either.
#define OTA_LOG_LINES    6
#define OTA_LOG_LINE_LEN 17  // 16 chars wide (128px / 8px font) + null

static char otaLogBuf[OTA_LOG_LINES][OTA_LOG_LINE_LEN];
static int  otaLogCount = 0;

static void otaLogRedraw() {
  if (!oled_ready) return;
  for (int i = 0; i < OTA_LOG_LINES; i++) {
    oled.drawString(0, 2 + i, "                ");  // blank the row first
    if (i < otaLogCount) {
      oled.drawString(0, 2 + i, otaLogBuf[i]);
    }
  }
}

void otaLogBegin() {
  otaLogCount = 0;
  for (int i = 0; i < OTA_LOG_LINES; i++) otaLogBuf[i][0] = '\0';
  if (!oled_ready) return;
  oled.clear();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 0, "== OTA UPDATE ==");
  otaLogRedraw();
}

void otaLogPrint(const char* msg) {
  Serial.println(msg);
  if (otaLogCount < OTA_LOG_LINES) {
    strncpy(otaLogBuf[otaLogCount], msg, OTA_LOG_LINE_LEN - 1);
    otaLogBuf[otaLogCount][OTA_LOG_LINE_LEN - 1] = '\0';
    otaLogCount++;
  } else {
    // Full — scroll everything up one line, drop the oldest.
    for (int i = 1; i < OTA_LOG_LINES; i++) {
      strncpy(otaLogBuf[i - 1], otaLogBuf[i], OTA_LOG_LINE_LEN);
    }
    strncpy(otaLogBuf[OTA_LOG_LINES - 1], msg, OTA_LOG_LINE_LEN - 1);
    otaLogBuf[OTA_LOG_LINES - 1][OTA_LOG_LINE_LEN - 1] = '\0';
  }
  otaLogRedraw();
}

void otaLogPrintf(const char* fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  otaLogPrint(buf);
}