#pragma once

// Waveshare 2 inch ST7789V 240x320.
#define TFT_NATIVE_WIDTH 240
#define TFT_NATIVE_HEIGHT 320

// Physical display is mounted 90 degrees counter-clockwise, so render 90 degrees clockwise.
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define TFT_ROTATION 1

// ESP32 Super Mini C3 SPI wiring. Adjust to match your board wiring.
// Keep pins in one place because C3 mini boards vary by vendor.
#define TFT_SCK 4
#define TFT_MOSI 6
#define TFT_MISO -1
#define TFT_CS 7
#define TFT_DC 10
#define TFT_RST 3
#define TFT_BL 2
#define TOUCH_SENSOR_PIN 1
#define TOUCH_ACTIVE_LEVEL HIGH
#define BACKLIGHT_AUTO_OFF_MS 60000UL
#define TOUCH_DEBOUNCE_MS 250UL
#define MENU_OPEN_PRESS_MS 3000UL
#define MENU_SELECT_PRESS_MS 1000UL
#define MENU_BLINK_INTERVAL_MS 350UL
#define FACTORY_RESET_CONFIRM_MS 5000UL
#define FACTORY_RESET_IDLE_TIMEOUT_MS 5000UL

#define BACKGROUND_COLOR 0xEAC0
#define SURFACE_COLOR 0xCC80
#define EYE_COLOR 0xFFFF
#define TEXT_COLOR 0xFFFF
#define DARK_TEXT_COLOR 0x3200
#define MUTED_TEXT_COLOR 0xFDD0
#define CLAUDE_COLOR 0xFFFF
#define CODEX_COLOR 0xFFE0
#define HEALTH_GOOD_COLOR 0x07E0
#define HEALTH_WARN_COLOR 0xFFE0
#define HEALTH_BAD_COLOR 0xF800
#define RECHARGE_COLOR 0x001F
#define RECHARGE_DIM_COLOR 0x0010

#define HTTP_PORT 80
#define BAUD_RATE 115200
