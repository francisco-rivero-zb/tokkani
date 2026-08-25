#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "config.h"

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC,
    TFT_CS,
    TFT_SCK,
    TFT_MOSI,
    TFT_MISO);

Arduino_GFX *display = new Arduino_ST7789(
    bus,
    TFT_RST,
    TFT_ROTATION,
    true,
    TFT_NATIVE_WIDTH,
    TFT_NATIVE_HEIGHT);

Arduino_Canvas *faceCanvas = nullptr;
Arduino_Canvas *uiCanvas = nullptr;

inline void g_init_display() {
  if (TFT_BL >= 0) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  }

  display->begin();
  display->fillScreen(BACKGROUND_COLOR);
  display->setTextWrap(false);
}

inline void g_clear_display() {
  display->fillScreen(BACKGROUND_COLOR);
}

inline void g_update_display() {
  // Direct drawing backend; no buffered flush is required.
}

inline void g_draw_filled_round_rect(int x, int y, int w, int h, int r, int color) {
  display->fillRoundRect(x, y, w, h, r, color);
}

inline void g_draw_filled_triangle(int x0, int y0, int x1, int y1, int x2, int y2, int color) {
  display->fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

inline void g_draw_text(int x, int y, const String &text, uint16_t color, uint8_t size = 2) {
  display->setCursor(x, y);
  display->setTextColor(color, BACKGROUND_COLOR);
  display->setTextSize(size);
  display->print(text);
}

inline void g_draw_progress_bar(int x, int y, int w, int h, int value, uint16_t color, uint16_t outlineColor = MUTED_TEXT_COLOR) {
  display->drawRoundRect(x, y, w, h, 4, outlineColor);

  if (value < 0) {
    return;
  }

  int clamped = constrain(value, 0, 100);
  int fillWidth = map(clamped, 0, 100, 0, w - 4);
  display->fillRoundRect(x + 2, y + 2, fillWidth, h - 4, 3, color);
}

inline bool g_init_face_canvas(int width, int height, int x, int y) {
  if (faceCanvas != nullptr) {
    return true;
  }

  faceCanvas = new Arduino_Canvas(width, height, display, x, y);
  if (faceCanvas == nullptr) {
    return false;
  }

  if (!faceCanvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
    delete faceCanvas;
    faceCanvas = nullptr;
    return false;
  }

  faceCanvas->fillScreen(BACKGROUND_COLOR);
  return true;
}

inline bool g_has_face_canvas() {
  return faceCanvas != nullptr;
}

inline void g_clear_face_canvas() {
  faceCanvas->fillScreen(BACKGROUND_COLOR);
}

inline void g_draw_face_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
  faceCanvas->fillRoundRect(x, y, w, h, r, color);
}

inline void g_draw_face_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
  faceCanvas->fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

inline void g_flush_face_canvas() {
  faceCanvas->flush();
}

inline bool g_init_ui_canvas(int width, int height, int x, int y) {
  if (uiCanvas != nullptr) {
    return true;
  }

  uiCanvas = new Arduino_Canvas(width, height, display, x, y);
  if (uiCanvas == nullptr) {
    return false;
  }

  if (!uiCanvas->begin(GFX_SKIP_OUTPUT_BEGIN)) {
    delete uiCanvas;
    uiCanvas = nullptr;
    return false;
  }

  uiCanvas->fillScreen(BACKGROUND_COLOR);
  return true;
}

inline bool g_has_ui_canvas() {
  return uiCanvas != nullptr;
}

inline void g_clear_ui_canvas() {
  uiCanvas->fillScreen(BACKGROUND_COLOR);
}

inline void g_flush_ui_canvas() {
  uiCanvas->flush();
}

inline void g_draw_ui_text(int x, int y, const String &text, uint16_t color, uint8_t size = 2) {
  uiCanvas->setCursor(x, y);
  uiCanvas->setTextColor(color, BACKGROUND_COLOR);
  uiCanvas->setTextSize(size);
  uiCanvas->print(text);
}

inline void g_draw_ui_progress_bar(int x, int y, int w, int h, int value, uint16_t color, uint16_t outlineColor = TEXT_COLOR) {
  uiCanvas->drawRoundRect(x, y, w, h, 4, outlineColor);

  if (value < 0) {
    return;
  }

  int clamped = constrain(value, 0, 100);
  int fillWidth = map(clamped, 0, 100, 0, w - 4);
  uiCanvas->fillRoundRect(x + 2, y + 2, fillWidth, h - 4, 3, color);
}
