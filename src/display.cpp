#include "display.h"
#include "config.h"
#include <Wire.h>
#include <U8x8lib.h>
#include <string.h>
#include "bmp280_helper.h"
#include "state.h"

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

void showFactorySetup(const char* apSsid, const char* ip) {
  if (!oled_ready) return;
  oled.clear();
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 0, "= SETUP  MODE =");
  oled.drawString(0, 2, "Connect WiFi to:");
  oled.drawString(0, 3, apSsid);
  oled.drawString(0, 5, "Then open:");
  oled.drawString(0, 6, ip);
}

void showFactorySaved() {
  if (!oled_ready) return;
  oled.clear();
  oled.setFont(u8x8_font_courB18_2x3_f);
  oled.drawString(0, 1, "   OK   ");
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 5, "Rebooting...   ");
}

void logMsgf(const char* fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  logMsg(buf);
}

// Format elapsed millis into a short "Xs / Xm / Xh / Xd ago" string.
// Padded to 8 chars so it fully overwrites the previous large-font
// value without needing an oled.clear().
static void formatEventAgo(unsigned long ms, char* out, size_t n) {
  unsigned long secs = ms / 1000;
  char raw[16];
  if      (secs < 60)    snprintf(raw, sizeof(raw), "%lus ago", secs);
  else if (secs < 3600)  snprintf(raw, sizeof(raw), "%lum ago", secs / 60);
  else if (secs < 86400) snprintf(raw, sizeof(raw), "%luh ago", secs / 3600);
  else                   snprintf(raw, sizeof(raw), "%lud ago", secs / 86400);
  snprintf(out, n, "%-8s", raw);   // pad/truncate to 8
}

void enterDashboard() {
  if (!oled_ready) return;
  oled.clear();
  updateDashboard();
}

void updateDashboard() {
  if (!oled_ready) return;

  // Row 0: state left, temperature right, padded to full 16 chars so
  // shrinking values (ARMED after DISARMED) get cleanly overwritten.
  const char* stateLabel = isArmed() ? "ARMED" : "DISARMED";
  char tempStr[8];
  if (bmpAvailable()) snprintf(tempStr, sizeof(tempStr), "%.1fC", bmpReadTemperature());
  else                snprintf(tempStr, sizeof(tempStr), "--.-C");

  char row0[17];
  memset(row0, ' ', 16);
  row0[16] = '\0';
  size_t sLen = strlen(stateLabel);
  memcpy(row0, stateLabel, sLen);
  size_t tLen = strlen(tempStr);
  if (tLen <= 16) memcpy(row0 + 16 - tLen, tempStr, tLen);
  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 0, row0);

  oled.drawString(0, 2, "Last event:    ");

  char center[16];
  if (hadAnyEvent() && (millis() - lastEventMs()) < PIC_MESSAGE_DURATION_MS) {
    // Recent knock/photo — flash "EVENT !" instead of a "0s ago" that
    // would immediately roll over anyway.
    snprintf(center, sizeof(center), "%-8s", "EVENT !");
  } else if (hadAnyEvent()) {
    formatEventAgo(millis() - lastEventMs(), center, sizeof(center));
  } else {
    snprintf(center, sizeof(center), "%-8s", "--");
  }
  oled.setFont(u8x8_font_courB18_2x3_f);
  oled.drawString(0, 4, center);
}

void showHoldProgress(int percent, const char* label) {
  if (!oled_ready) return;
  if (percent > 100) percent = 100;
  oled.clear();
  oled.setFont(u8x8_font_chroma48medium8_r);

  char lbuf[17];
  snprintf(lbuf, sizeof(lbuf), "%-16s", label ? label : "");
  oled.drawString(0, 0, lbuf);

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
#define OTA_LOG_LINES    5   // rows 2..6 — row 7 is the progress bar
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
  otaLogProgress(0);          // seed the bar so the user sees something immediately
}

void otaLogProgress(int pct) {
  if (!oled_ready) return;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  // Row 7 layout: "XX% ###########-" — 4 chars of "XX% ", 12 chars of bar.
  char row[17];
  snprintf(row, sizeof(row), "%3d%% ", pct);
  const int barChars = 12;
  const int filled   = (pct * barChars) / 100;
  for (int i = 0; i < barChars; i++) row[4 + i] = (i < filled) ? '#' : '-';
  row[16] = '\0';

  oled.setFont(u8x8_font_chroma48medium8_r);
  oled.drawString(0, 7, row);
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