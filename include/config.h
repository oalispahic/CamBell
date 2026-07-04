#pragma once

// ========== Camera Pin Configuration (AI-Thinker ESP32-CAM) ==========
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ========== Peripheral Pins ==========
#define BUTTON_PIN        14
#define LED_PIN            4
#define I2C_SDA            3   // NOTE: also UART0 RX
#define I2C_SCL            1   // NOTE: also UART0 TX — Serial dies once Wire takes it

// ========== OLED ==========
#define OLED_I2C_ADDR    0x3C
#define U8LOG_WIDTH        16
#define U8LOG_HEIGHT        8

// ========== Timing ==========
#define BUTTON_DEBOUNCE_MS      300
#define TEMP_UPDATE_INTERVAL_MS 2000
#define PIC_MESSAGE_DURATION_MS 1500

// ========== Shared utility (defined in main.cpp) ==========
void blinkLED(int times, int duration);

// ========== camera.cpp prototypes ==========
// No dedicated camera.h in the current project layout, so these live
// here since config.h is already included wherever camera.cpp's
// functions are called from.
void initCamera();
void applyCameraSettings();
void captureAndSendPhoto();