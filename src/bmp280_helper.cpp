#include "bmp280_helper.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>

static Adafruit_BMP280 bmp;
static bool bmp_ready = false;

bool bmpInit() {
  if (bmp.begin(0x76) || bmp.begin(0x77)) {
    // FORCED mode + minimal oversampling: the chip sleeps between
    // reads, so self-heating stays low and the reading tracks ambient.
    // Pressure sampling now enabled so bmpReadPressureHpa() has real data.
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                    Adafruit_BMP280::SAMPLING_X1,
                    Adafruit_BMP280::SAMPLING_X1,
                    Adafruit_BMP280::FILTER_OFF,
                    Adafruit_BMP280::STANDBY_MS_1);
    bmp_ready = true;
  }
  return bmp_ready;
}

bool bmpAvailable() {
  return bmp_ready;
}

float bmpReadTemperature() {
  if (!bmp_ready) return 0.0f;
  bmp.takeForcedMeasurement();
  return bmp.readTemperature();
}

float bmpReadPressureHpa() {
  if (!bmp_ready) return 0.0f;
  bmp.takeForcedMeasurement();
  return bmp.readPressure() / 100.0f;   // Pa -> hPa
}
