#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "display_wrapper.h"
#include "usage_model.h"

WebServer server(HTTP_PORT);
DNSServer dnsServer;
Preferences prefs;
const char *AUTH_HEADER_KEYS[] = {"X-Tokkani-Key"};

enum DeviceMode {
  MODE_BOOT,
  MODE_PORTAL,
  MODE_ONLINE
};

enum Animation {
  WAKEUP,
  RESET,
  BLINK_SHORT,
  HAPPY,
  SLEEP,
  SLEEPY_PEEK,
  MAX_ANIMATIONS
};

enum EyeMood {
  MOOD_CALM,
  MOOD_WARN,
  MOOD_CRITICAL
};

enum UiMode {
  UI_NORMAL,
  UI_MENU,
  UI_EDIT_VALUE,
  UI_RESET_CHOICE,
  UI_RESET_HOLD
};

enum MenuItem {
  MENU_EYE_COLOR,
  MENU_FACTORY_RESET,
  MENU_EXIT,
  MENU_ITEM_COUNT
};

struct EyeState {
  int height;
  int width;
  int x;
  int y;
};

struct UsageMetric {
  int value = -1;
  bool valueIsRemaining = false;
  String resetText;
  int periodMinutes = 0;
};

struct GaugeRow {
  String label;
  UsageMetric metric;
};

struct RenderEyeState {
  int height;
  int width;
  int x;
  int y;
};

struct DeviceSettings {
  String ssid;
  String wifiPassword;
  String deviceKey;
};

const IPAddress PORTAL_IP(192, 168, 4, 1);
const byte DNS_PORT = 53;
const int REF_EYE_HEIGHT = 58;
const int REF_EYE_WIDTH = 58;
const int REF_SPACE_BETWEEN_EYE = 72;
const int REF_CORNER_RADIUS = 14;
const int EYE_CENTER_X = 160;
const int EYE_CENTER_Y = 70;
const int FACE_DIRTY_X = 34;
const int FACE_DIRTY_Y = 16;
const int FACE_DIRTY_W = 252;
const int FACE_DIRTY_H = 96;
const int UI_DIRTY_X = 0;
const int UI_DIRTY_Y = 112;
const int UI_DIRTY_W = 320;
const int UI_DIRTY_H = 128;

DeviceMode deviceMode = MODE_BOOT;
DeviceSettings settings;
EyeState left_eye;
EyeState right_eye;
int corner_radius = REF_CORNER_RADIUS;
uint16_t currentEyeColor = EYE_COLOR;

UsageReading claudeUsage = {"claude"};
UsageReading codexUsage = {"codex"};
String apSsid;
String apPassword;
unsigned long lastIdleAnimationMillis = 0;
unsigned long lastDisplayRefreshMillis = 0;
unsigned long lastGaugeBlinkMillis = 0;
unsigned long lastBacklightOnMillis = 0;
unsigned long lastTouchToggleMillis = 0;
unsigned long touchPressedAt = 0;
unsigned long lastMenuBlinkMillis = 0;
unsigned long resetHoldPromptStartedAt = 0;
unsigned long resetHoldStartedAt = 0;
unsigned long lastResetHoldDrawMillis = 0;
bool displayDirty = true;
bool staticFrameDirty = true;
bool gaugeBlinkVisible = true;
bool backlightOn = true;
bool touchWasActive = false;
bool touchLongHandled = false;
bool menuBlinkVisible = true;
bool eyeColorWhite = true;
bool resetChoiceYes = false;
bool resetHoldArmed = false;
UiMode uiMode = UI_NORMAL;
int selectedMenuIndex = MENU_EYE_COLOR;

void drawStaticFrame();
void saveMenuSettings();
void factoryReset();

bool hasAnyUsage() {
  return hasUsage(claudeUsage) || hasUsage(codexUsage);
}

int remainingPercent(const UsageMetric &metric) {
  if (metric.value < 0) {
    return -1;
  }

  int clamped = constrain(metric.value, 0, 100);
  return metric.valueIsRemaining ? clamped : 100 - clamped;
}

void addGaugeRow(GaugeRow *rows, int &count, const String &label, int value, bool valueIsRemaining, const String &resetText, int periodMinutes) {
  if (value < 0 || count >= 4) {
    return;
  }

  rows[count++] = {label, {value, valueIsRemaining, resetText, periodMinutes}};
}

int buildGaugeRows(GaugeRow *rows) {
  int claudeSessionValue = claudeUsage.sessionUsedPct >= 0 ? claudeUsage.sessionUsedPct : claudeUsage.fiveHourUsedPct;

  int rowCount = 0;
  addGaugeRow(rows, rowCount, "C 5h", claudeSessionValue, false, claudeUsage.resetText, 300);
  addGaugeRow(rows, rowCount, "C 7d", claudeUsage.weeklyUsedPct, false, claudeUsage.resetText, 10080);
  addGaugeRow(rows, rowCount, "X 7d", codexUsage.weeklyUsedPct, true, codexUsage.resetText, 10080);
  return rowCount;
}

int lowestRemainingPercent() {
  GaugeRow rows[4];
  int rowCount = buildGaugeRows(rows);
  int lowest = 101;

  for (int i = 0; i < rowCount; i++) {
    int remaining = remainingPercent(rows[i].metric);
    if (remaining >= 0) {
      lowest = min(lowest, remaining);
    }
  }

  return lowest == 101 ? -1 : lowest;
}

int criticalRemainingPercent() {
  int lowest = lowestRemainingPercent();
  return (lowest >= 0 && lowest <= 20) ? lowest : -1;
}

unsigned long gaugeBlinkIntervalMillis() {
  int remaining = criticalRemainingPercent();
  if (remaining < 0) {
    return 0;
  }

  return map(constrain(remaining, 0, 20), 0, 20, 160, 900);
}

EyeMood currentEyeMood() {
  int lowest = lowestRemainingPercent();
  if (lowest < 0) {
    return MOOD_CALM;
  }
  if (lowest <= 20) {
    return MOOD_CRITICAL;
  }
  if (lowest <= 45) {
    return MOOD_WARN;
  }
  return MOOD_CALM;
}

uint16_t severityColor(int remaining) {
  if (remaining < 0) {
    return TEXT_COLOR;
  }
  if (remaining <= 20) {
    return HEALTH_BAD_COLOR;
  }
  if (remaining <= 45) {
    return HEALTH_WARN_COLOR;
  }
  return HEALTH_GOOD_COLOR;
}

int calculate_safe_radius(int r, int w, int h) {
  if (w < 2 * (r + 1)) {
    r = (w / 2) - 1;
  }
  if (h < 2 * (r + 1)) {
    r = (h / 2) - 1;
  }
  return (r < 0) ? 0 : r;
}

RenderEyeState adjustedEyeState(const EyeState &eye, bool isLeft) {
  RenderEyeState adjusted = {eye.height, eye.width, eye.x, eye.y};
  EyeMood mood = currentEyeMood();

  if (mood == MOOD_WARN) {
    adjusted.height = max(14, adjusted.height - 3);
    adjusted.width = max(42, adjusted.width - 1);
    adjusted.y += 2;
    adjusted.x += isLeft ? -1 : 1;
  } else if (mood == MOOD_CRITICAL) {
    adjusted.height = max(12, adjusted.height - 8);
    adjusted.width = max(40, adjusted.width - 2);
    adjusted.y += 5;
    adjusted.x += isLeft ? -2 : 2;
  }

  return adjusted;
}

void drawWorryLidDirect(const RenderEyeState &eye, bool isLeft, EyeMood mood) {
  if (mood == MOOD_CALM || eye.height < 18) {
    return;
  }

  int x = int(eye.x - eye.width / 2);
  int y = int(eye.y - eye.height / 2);
  int lidDepth = mood == MOOD_CRITICAL ? 18 : 10;

  if (isLeft) {
    g_draw_filled_triangle(x, y, x + eye.width, y, x, y + lidDepth, BACKGROUND_COLOR);
  } else {
    g_draw_filled_triangle(x, y, x + eye.width, y, x + eye.width, y + lidDepth, BACKGROUND_COLOR);
  }
}

void drawWorryLidCanvas(const RenderEyeState &eye, bool isLeft, EyeMood mood) {
  if (mood == MOOD_CALM || eye.height < 18) {
    return;
  }

  int x = int(eye.x - eye.width / 2) - FACE_DIRTY_X;
  int y = int(eye.y - eye.height / 2) - FACE_DIRTY_Y;
  int lidDepth = mood == MOOD_CRITICAL ? 18 : 10;

  if (isLeft) {
    g_draw_face_triangle(x, y, x + eye.width, y, x, y + lidDepth, BACKGROUND_COLOR);
  } else {
    g_draw_face_triangle(x, y, x + eye.width, y, x + eye.width, y + lidDepth, BACKGROUND_COLOR);
  }
}

String pctText(int value) {
  if (value < 0) {
    return "--";
  }
  return String(constrain(value, 0, 100));
}

int numberBeforeToken(const String &text, const String &token) {
  int tokenIndex = text.indexOf(token);
  if (tokenIndex < 0) {
    return 0;
  }

  int end = tokenIndex - 1;
  while (end >= 0 && text.charAt(end) == ' ') {
    end--;
  }

  int start = end;
  while (start >= 0 && isDigit(text.charAt(start))) {
    start--;
  }

  if (start == end) {
    return 0;
  }

  return text.substring(start + 1, end + 1).toInt();
}

int resetMinutesFromText(const String &resetText) {
  String text = resetText;
  text.toLowerCase();

  int days = numberBeforeToken(text, "d");
  int hours = numberBeforeToken(text, "hr") + numberBeforeToken(text, "hour") + numberBeforeToken(text, "hora");
  int minutes = numberBeforeToken(text, "min");
  int total = days * 1440 + hours * 60 + minutes;
  return total > 0 ? total : -1;
}

String countdownText(int minutes) {
  if (minutes < 0) {
    return "reset";
  }
  if (minutes < 60) {
    return String(minutes) + "m";
  }
  if (minutes < 1440) {
    int hours = minutes / 60;
    int mins = minutes % 60;
    return mins > 0 ? String(hours) + "h" + String(mins) : String(hours) + "h";
  }

  int days = minutes / 1440;
  int hours = (minutes % 1440) / 60;
  return hours > 0 ? String(days) + "d" + String(hours) + "h" : String(days) + "d";
}

int rechargeProgress(const UsageMetric &metric) {
  int minutes = resetMinutesFromText(metric.resetText);
  if (minutes < 0 || metric.periodMinutes <= 0) {
    return map((millis() / 220) % 10, 0, 9, 18, 82);
  }

  return constrain(map(constrain(metric.periodMinutes - minutes, 0, metric.periodMinutes), 0, metric.periodMinutes, 0, 100), 0, 100);
}

String ageText(const UsageReading &reading) {
  if (reading.receivedMillis == 0) {
    return "no data";
  }

  unsigned long ageSeconds = (millis() - reading.receivedMillis) / 1000;
  if (ageSeconds < 60) {
    return String(ageSeconds) + "s";
  }
  if (ageSeconds < 3600) {
    return String(ageSeconds / 60) + "m";
  }
  return String(ageSeconds / 3600) + "h";
}

String chipSuffix() {
  uint64_t mac = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06X", (uint32_t)(mac & 0xFFFFFF));
  return String(suffix);
}

String generatedPassword() {
  return "TG-" + chipSuffix() + "!";
}

String generatedDeviceKey() {
  uint64_t mac = ESP.getEfuseMac();
  char key[5];
  snprintf(key, sizeof(key), "%04u", (uint32_t)(mac % 10000));
  return String(key);
}

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type,X-Tokkani-Key");
}

void sendJson(int code, const String &payload) {
  addCorsHeaders();
  server.send(code, "application/json", payload);
}

void draw_eyes() {
  EyeMood mood = currentEyeMood();
  RenderEyeState left = adjustedEyeState(left_eye, true);
  RenderEyeState right = adjustedEyeState(right_eye, false);

  int r_left = calculate_safe_radius(corner_radius, left.width, left.height);
  int x_left = int(left.x - left.width / 2);
  int y_left = int(left.y - left.height / 2);
  g_draw_filled_round_rect(x_left, y_left, left.width, left.height, r_left, currentEyeColor);
  drawWorryLidDirect(left, true, mood);

  int r_right = calculate_safe_radius(corner_radius, right.width, right.height);
  int x_right = int(right.x - right.width / 2);
  int y_right = int(right.y - right.height / 2);
  g_draw_filled_round_rect(x_right, y_right, right.width, right.height, r_right, currentEyeColor);
  drawWorryLidDirect(right, false, mood);
}

void draw_eyes_to_canvas() {
  EyeMood mood = currentEyeMood();
  RenderEyeState left = adjustedEyeState(left_eye, true);
  RenderEyeState right = adjustedEyeState(right_eye, false);

  int r_left = calculate_safe_radius(corner_radius, left.width, left.height);
  int x_left = int(left.x - left.width / 2) - FACE_DIRTY_X;
  int y_left = int(left.y - left.height / 2) - FACE_DIRTY_Y;
  g_draw_face_round_rect(x_left, y_left, left.width, left.height, r_left, currentEyeColor);
  drawWorryLidCanvas(left, true, mood);

  int r_right = calculate_safe_radius(corner_radius, right.width, right.height);
  int x_right = int(right.x - right.width / 2) - FACE_DIRTY_X;
  int y_right = int(right.y - right.height / 2) - FACE_DIRTY_Y;
  g_draw_face_round_rect(x_right, y_right, right.width, right.height, r_right, currentEyeColor);
  drawWorryLidCanvas(right, false, mood);
}

void drawFaceOnly() {
  if (uiMode != UI_NORMAL) {
    return;
  }

  if (g_has_face_canvas()) {
    g_clear_face_canvas();
    draw_eyes_to_canvas();
    g_flush_face_canvas();
  } else {
    display->fillRect(FACE_DIRTY_X, FACE_DIRTY_Y, FACE_DIRTY_W, FACE_DIRTY_H, BACKGROUND_COLOR);
    draw_eyes();
    g_update_display();
  }
}

void reset_eyes(bool update = true) {
  left_eye.height = REF_EYE_HEIGHT;
  left_eye.width = REF_EYE_WIDTH;
  right_eye.height = REF_EYE_HEIGHT;
  right_eye.width = REF_EYE_WIDTH;

  left_eye.x = EYE_CENTER_X - REF_EYE_WIDTH / 2 - REF_SPACE_BETWEEN_EYE / 2;
  left_eye.y = EYE_CENTER_Y;
  right_eye.x = EYE_CENTER_X + REF_EYE_WIDTH / 2 + REF_SPACE_BETWEEN_EYE / 2;
  right_eye.y = EYE_CENTER_Y;

  corner_radius = REF_CORNER_RADIUS;

  if (update) {
    displayDirty = true;
  }
}

void sleep_eyes(bool update = true) {
  reset_eyes(false);
  left_eye.height = 8;
  right_eye.height = 8;
  corner_radius = 0;
  if (update) {
    displayDirty = true;
  }
}

void drawProgressArc(int cx, int cy, int radius, int value, uint16_t color) {
  display->drawCircle(cx, cy, radius, MUTED_TEXT_COLOR);
  display->drawCircle(cx, cy, radius - 1, MUTED_TEXT_COLOR);

  if (value < 0) {
    return;
  }

  int clamped = constrain(value, 0, 100);
  int endAngle = map(clamped, 0, 100, -140, 140);
  for (int angle = -140; angle <= endAngle; angle += 4) {
    float radians = angle * DEG_TO_RAD;
    int x = cx + cos(radians) * radius;
    int y = cy + sin(radians) * radius;
    display->fillCircle(x, y, 3, color);
  }
}

void drawGaugeBar(int x, int y, int w, int value, uint16_t color, uint16_t outlineColor = TEXT_COLOR) {
  display->drawRoundRect(x, y, w, 14, 7, outlineColor);
  if (value < 0) {
    return;
  }

  int fillWidth = map(constrain(value, 0, 100), 0, 100, 0, w - 4);
  display->fillRoundRect(x + 2, y + 2, fillWidth, 10, 5, color);
}

void drawRechargeGauge(int x, int y, const UsageMetric &metric, bool blinkVisible) {
  int resetMinutes = resetMinutesFromText(metric.resetText);
  uint16_t color = blinkVisible ? RECHARGE_COLOR : RECHARGE_DIM_COLOR;
  uint16_t textColor = TEXT_COLOR;

  g_draw_text(x + 76, y - 4, countdownText(resetMinutes), textColor, 2);
  drawGaugeBar(x + 188, y + 2, 110, rechargeProgress(metric), color, textColor);
}

void drawRechargeGaugeUi(int x, int y, const UsageMetric &metric, bool blinkVisible) {
  int resetMinutes = resetMinutesFromText(metric.resetText);
  uint16_t color = blinkVisible ? RECHARGE_COLOR : RECHARGE_DIM_COLOR;
  uint16_t textColor = TEXT_COLOR;

  g_draw_ui_text(x + 76, y - 4, countdownText(resetMinutes), textColor, 2);
  g_draw_ui_progress_bar(x + 188, y + 2, 110, 14, rechargeProgress(metric), color, textColor);
}

void drawCompactGauge(int x, int y, const String &label, const UsageMetric &metric, bool blinkVisible) {
  int remaining = remainingPercent(metric);
  if (remaining == 0) {
    g_draw_text(x, y - 2, label, TEXT_COLOR, 2);
    drawRechargeGauge(x, y, metric, blinkVisible);
    return;
  }

  bool active = remaining > 20 || blinkVisible;
  uint16_t color = active ? severityColor(remaining) : SURFACE_COLOR;
  uint16_t textColor = active ? TEXT_COLOR : SURFACE_COLOR;
  g_draw_text(x, y - 2, label, textColor, 2);
  g_draw_text(x + 76, y - 4, pctText(remaining) + "%", textColor, 2);
  drawGaugeBar(x + 132, y + 2, 166, remaining, color, textColor);
}

void drawCompactGaugeUi(int x, int y, const String &label, const UsageMetric &metric, bool blinkVisible) {
  int remaining = remainingPercent(metric);
  if (remaining == 0) {
    g_draw_ui_text(x, y - 2, label, TEXT_COLOR, 2);
    drawRechargeGaugeUi(x, y, metric, blinkVisible);
    return;
  }

  bool active = remaining > 20 || blinkVisible;
  uint16_t color = active ? severityColor(remaining) : SURFACE_COLOR;
  uint16_t textColor = active ? TEXT_COLOR : SURFACE_COLOR;
  g_draw_ui_text(x, y - 2, label, textColor, 2);
  g_draw_ui_text(x + 76, y - 4, pctText(remaining) + "%", textColor, 2);
  g_draw_ui_progress_bar(x + 132, y + 2, 166, 14, remaining, color, textColor);
}

void drawOnlineUiDirect() {
  display->fillRect(UI_DIRTY_X, UI_DIRTY_Y, UI_DIRTY_W, UI_DIRTY_H, BACKGROUND_COLOR);
  if (!hasAnyUsage()) {
    g_draw_text(26, 152, "Waiting data", TEXT_COLOR, 2);
    g_draw_text(26, 180, WiFi.localIP().toString(), TEXT_COLOR, 3);
    g_draw_text(26, 214, "Key " + settings.deviceKey, TEXT_COLOR, 2);
    return;
  }

  GaugeRow rows[4];
  int rowCount = buildGaugeRows(rows);

  const int rowHeight = 25;
  const int gaugeTop = 122;
  const int gaugeHeight = 108;
  int startY = gaugeTop + max(0, (gaugeHeight - rowCount * rowHeight) / 2);
  for (int i = 0; i < rowCount; i++) {
    drawCompactGauge(14, startY + i * rowHeight, rows[i].label, rows[i].metric, gaugeBlinkVisible);
  }
}

void drawOnlineUiCanvas() {
  g_clear_ui_canvas();
  if (!hasAnyUsage()) {
    g_draw_ui_text(26, 152 - UI_DIRTY_Y, "Waiting data", TEXT_COLOR, 2);
    g_draw_ui_text(26, 180 - UI_DIRTY_Y, WiFi.localIP().toString(), TEXT_COLOR, 3);
    g_draw_ui_text(26, 214 - UI_DIRTY_Y, "Key " + settings.deviceKey, TEXT_COLOR, 2);
    g_flush_ui_canvas();
    return;
  }

  GaugeRow rows[4];
  int rowCount = buildGaugeRows(rows);

  const int rowHeight = 25;
  const int gaugeTop = 122;
  const int gaugeHeight = 108;
  int startY = gaugeTop + max(0, (gaugeHeight - rowCount * rowHeight) / 2);
  for (int i = 0; i < rowCount; i++) {
    drawCompactGaugeUi(14, startY + i * rowHeight - UI_DIRTY_Y, rows[i].label, rows[i].metric, gaugeBlinkVisible);
  }
  g_flush_ui_canvas();
}

void drawOnlineUiOnly() {
  if (uiMode != UI_NORMAL) {
    return;
  }

  if (g_has_ui_canvas()) {
    drawOnlineUiCanvas();
  } else {
    drawOnlineUiDirect();
    g_update_display();
  }
}

void drawOnlineStatic() {
  drawFaceOnly();
  drawOnlineUiOnly();
  g_update_display();
}

void setEyeColorWhite(bool enabled) {
  eyeColorWhite = enabled;
  currentEyeColor = enabled ? EYE_COLOR : 0x0000;
}

void saveMenuSettings() {
  prefs.begin("tokkani", false);
  prefs.putBool("eyeWhite", eyeColorWhite);
  prefs.end();
}

void factoryReset() {
  g_clear_display();
  reset_eyes(false);
  draw_eyes();
  g_draw_text(34, 150, "Factory reset", TEXT_COLOR, 2);
  g_draw_text(34, 184, "Restarting...", TEXT_COLOR, 2);
  g_update_display();

  prefs.begin("tokkani", false);
  prefs.clear();
  prefs.end();

  WiFi.disconnect(true, true);
  delay(1200);
  ESP.restart();
}

void setBacklight(bool enabled) {
  backlightOn = enabled;
  if (TFT_BL >= 0) {
    digitalWrite(TFT_BL, enabled ? HIGH : LOW);
  }

  if (enabled) {
    lastBacklightOnMillis = millis();
    displayDirty = true;
    staticFrameDirty = true;
  }
}

void drawMenuRow(int rowIndex, int y, const String &label, const String &value = "") {
  bool selected = selectedMenuIndex == rowIndex;
  bool editing = uiMode == UI_EDIT_VALUE && selected;
  uint16_t rowColor = editing && !menuBlinkVisible ? SURFACE_COLOR : TEXT_COLOR;
  const int menuWidth = 248;
  const int menuX = (SCREEN_WIDTH - menuWidth) / 2;
  const int arrowX = menuX + 8;
  const int labelX = menuX + 36;
  const int valueX = menuX + 166;

  if (selected) {
    g_draw_filled_triangle(arrowX, y + 3, arrowX, y + 21, arrowX + 18, y + 12, TEXT_COLOR);
  }

  g_draw_text(labelX, y, label, rowColor, 2);
  if (value.length() > 0) {
    g_draw_text(valueX, y, value, rowColor, 2);
  }
}

void drawMenu() {
  g_clear_display();
  const int menuWidth = 248;
  const int menuX = (SCREEN_WIDTH - menuWidth) / 2;
  const int titleY = 38;
  const int firstRowY = 78;
  const int rowHeight = 38;

  g_draw_text(menuX + 54, titleY, "SETTINGS", TEXT_COLOR, 2);
  drawMenuRow(MENU_EYE_COLOR, firstRowY, "Eye color", eyeColorWhite ? "White" : "Black");
  drawMenuRow(MENU_FACTORY_RESET, firstRowY + rowHeight, "Reset all", uiMode == UI_EDIT_VALUE && selectedMenuIndex == MENU_FACTORY_RESET ? "Hold" : "");
  drawMenuRow(MENU_EXIT, firstRowY + rowHeight * 2, "Exit");
  g_update_display();
}

void drawResetChoice() {
  g_clear_display();
  const int menuWidth = 248;
  const int menuX = (SCREEN_WIDTH - menuWidth) / 2;
  g_draw_text(menuX + 18, 42, "RESET DEVICE?", TEXT_COLOR, 2);
  g_draw_text(menuX + 18, 76, "Erase WiFi, key", TEXT_COLOR, 2);
  g_draw_text(menuX + 18, 102, "and menu prefs.", TEXT_COLOR, 2);

  int arrowX = menuX + 28;
  int noY = 146;
  int yesY = 184;
  int selectedY = resetChoiceYes ? yesY : noY;
  g_draw_filled_triangle(arrowX, selectedY + 3, arrowX, selectedY + 21, arrowX + 18, selectedY + 12, TEXT_COLOR);
  g_draw_text(menuX + 58, noY, "No", TEXT_COLOR, 2);
  g_draw_text(menuX + 58, yesY, "Yes", TEXT_COLOR, 2);
  g_update_display();
}

void drawResetHold() {
  unsigned long elapsed = resetHoldStartedAt > 0 ? millis() - resetHoldStartedAt : 0;
  int progress = resetHoldStartedAt > 0 ? constrain(map(elapsed, 0, FACTORY_RESET_CONFIRM_MS, 0, 100), 0, 100) : 0;

  g_clear_display();
  g_draw_text(34, 48, "Hold 5 seconds", TEXT_COLOR, 2);
  g_draw_text(34, 82, "to factory reset", TEXT_COLOR, 2);
  drawGaugeBar(34, 130, 252, progress, HEALTH_BAD_COLOR, TEXT_COLOR);
  g_draw_text(34, 170, resetHoldArmed ? "Release cancels" : "Release, then hold", TEXT_COLOR, 2);
  g_update_display();
}

void showResetCancelled() {
  g_clear_display();
  reset_eyes(false);
  draw_eyes();
  g_draw_text(38, 164, "Reset cancelled", TEXT_COLOR, 2);
  g_update_display();
  delay(2000);
  uiMode = UI_MENU;
  selectedMenuIndex = MENU_FACTORY_RESET;
  menuBlinkVisible = true;
  drawMenu();
}

void enterResetChoice() {
  uiMode = UI_RESET_CHOICE;
  resetChoiceYes = false;
  menuBlinkVisible = true;
  drawResetChoice();
}

void enterResetHold() {
  uiMode = UI_RESET_HOLD;
  resetHoldArmed = false;
  resetHoldStartedAt = 0;
  resetHoldPromptStartedAt = millis();
  lastResetHoldDrawMillis = 0;
  drawResetHold();
}

void enterMenu() {
  if (!backlightOn) {
    setBacklight(true);
  } else {
    lastBacklightOnMillis = millis();
  }

  uiMode = UI_MENU;
  selectedMenuIndex = MENU_EYE_COLOR;
  menuBlinkVisible = true;
  lastMenuBlinkMillis = millis();
  drawMenu();
}

void exitMenu() {
  uiMode = UI_NORMAL;
  menuBlinkVisible = true;
  lastBacklightOnMillis = millis();
  g_clear_display();
  displayDirty = true;
  staticFrameDirty = true;
  drawStaticFrame();
  displayDirty = false;
  staticFrameDirty = false;
  lastDisplayRefreshMillis = millis();
}

void handleShortTap() {
  if (uiMode == UI_NORMAL) {
    setBacklight(!backlightOn);
    return;
  }

  if (uiMode == UI_MENU) {
    selectedMenuIndex = (selectedMenuIndex + 1) % MENU_ITEM_COUNT;
    drawMenu();
    return;
  }

  if (uiMode == UI_RESET_CHOICE) {
    resetChoiceYes = !resetChoiceYes;
    drawResetChoice();
    return;
  }

  if (uiMode == UI_EDIT_VALUE && selectedMenuIndex == MENU_EYE_COLOR) {
    setEyeColorWhite(!eyeColorWhite);
    saveMenuSettings();
    menuBlinkVisible = true;
    lastMenuBlinkMillis = millis();
    drawMenu();
    return;
  }

  if (uiMode == UI_EDIT_VALUE && selectedMenuIndex == MENU_FACTORY_RESET) {
    uiMode = UI_MENU;
    menuBlinkVisible = true;
    drawMenu();
  }
}

void handleLongPress() {
  if (uiMode == UI_NORMAL) {
    enterMenu();
    return;
  }

  if (uiMode == UI_MENU) {
    if (selectedMenuIndex == MENU_EXIT) {
      exitMenu();
      return;
    }

    if (selectedMenuIndex == MENU_FACTORY_RESET) {
      enterResetChoice();
      return;
    }

    uiMode = UI_EDIT_VALUE;
    menuBlinkVisible = true;
    lastMenuBlinkMillis = millis();
    drawMenu();
    return;
  }

  if (uiMode == UI_RESET_CHOICE) {
    if (resetChoiceYes) {
      enterResetHold();
    } else {
      showResetCancelled();
    }
    return;
  }

  if (uiMode == UI_EDIT_VALUE) {
    uiMode = UI_MENU;
    menuBlinkVisible = true;
    drawMenu();
  }
}

void tickMenuBlink() {
  if (uiMode != UI_EDIT_VALUE) {
    return;
  }

  if (millis() - lastMenuBlinkMillis > MENU_BLINK_INTERVAL_MS) {
    menuBlinkVisible = !menuBlinkVisible;
    lastMenuBlinkMillis = millis();
    drawMenu();
  }
}

void initBacklightControls() {
  if (TFT_BL >= 0) {
    pinMode(TFT_BL, OUTPUT);
  }
  pinMode(TOUCH_SENSOR_PIN, INPUT_PULLDOWN);
  setBacklight(true);
  touchWasActive = digitalRead(TOUCH_SENSOR_PIN) == TOUCH_ACTIVE_LEVEL;
  lastTouchToggleMillis = millis();
}

void tickResetHoldTouch(bool touchActive, unsigned long now) {
  if (!resetHoldArmed) {
    if (!touchActive) {
      resetHoldArmed = true;
      resetHoldPromptStartedAt = now;
      drawResetHold();
    } else if (now - lastResetHoldDrawMillis > 120) {
      drawResetHold();
      lastResetHoldDrawMillis = now;
    }
    touchWasActive = touchActive;
    return;
  }

  if (!touchActive) {
    if (resetHoldStartedAt > 0 || now - resetHoldPromptStartedAt >= FACTORY_RESET_IDLE_TIMEOUT_MS) {
      showResetCancelled();
    }
    touchWasActive = false;
    return;
  }

  if (resetHoldStartedAt == 0) {
    resetHoldStartedAt = now;
    lastResetHoldDrawMillis = 0;
  }

  if (now - resetHoldStartedAt >= FACTORY_RESET_CONFIRM_MS) {
    factoryReset();
    return;
  }

  if (now - lastResetHoldDrawMillis > 80) {
    drawResetHold();
    lastResetHoldDrawMillis = now;
  }

  touchWasActive = true;
}

void tickTouchAndBacklight() {
  bool touchActive = digitalRead(TOUCH_SENSOR_PIN) == TOUCH_ACTIVE_LEVEL;
  unsigned long now = millis();

  if (uiMode == UI_RESET_HOLD) {
    tickResetHoldTouch(touchActive, now);
    return;
  }

  if (touchActive && !touchWasActive && now - lastTouchToggleMillis > TOUCH_DEBOUNCE_MS) {
    touchPressedAt = now;
    touchLongHandled = false;
    touchWasActive = true;
    lastTouchToggleMillis = now;
  }

  if (touchActive && touchWasActive && !touchLongHandled) {
    unsigned long longPressMs = uiMode == UI_NORMAL ? MENU_OPEN_PRESS_MS : MENU_SELECT_PRESS_MS;
    if (now - touchPressedAt >= longPressMs) {
      handleLongPress();
      touchLongHandled = true;
    }
  }

  if (!touchActive && touchWasActive && now - lastTouchToggleMillis > TOUCH_DEBOUNCE_MS) {
    touchWasActive = false;
    lastTouchToggleMillis = now;
    if (!touchLongHandled) {
      handleShortTap();
    }
  }

  tickMenuBlink();

  if (uiMode == UI_NORMAL && backlightOn && now - lastBacklightOnMillis > BACKLIGHT_AUTO_OFF_MS) {
    setBacklight(false);
  }
}

void tickGaugeBlink() {
  if (uiMode != UI_NORMAL || deviceMode != MODE_ONLINE || !hasAnyUsage()) {
    return;
  }

  unsigned long blinkInterval = gaugeBlinkIntervalMillis();
  if (blinkInterval > 0) {
    if (millis() - lastGaugeBlinkMillis > blinkInterval) {
      gaugeBlinkVisible = !gaugeBlinkVisible;
      drawOnlineUiOnly();
      lastGaugeBlinkMillis = millis();
    }
  } else if (!gaugeBlinkVisible) {
    gaugeBlinkVisible = true;
    drawOnlineUiOnly();
  }
}

void delayWithGaugeBlink(unsigned long durationMillis) {
  unsigned long startedAt = millis();

  while (millis() - startedAt < durationMillis) {
    tickTouchAndBacklight();
    tickGaugeBlink();
    unsigned long elapsed = millis() - startedAt;
    unsigned long remaining = elapsed >= durationMillis ? 0 : durationMillis - elapsed;
    delay(min(remaining, 10UL));
  }
}

void drawPortalStatic() {
  g_clear_display();
  sleep_eyes(false);
  draw_eyes();
  g_draw_text(20, 146, "Setup WiFi", TEXT_COLOR, 2);
  g_draw_text(20, 174, "AP " + apSsid, TEXT_COLOR, 1);
  g_draw_text(20, 194, "Pass " + apPassword, TEXT_COLOR, 1);
  g_draw_text(20, 218, "Open 192.168.4.1", TEXT_COLOR, 1);
  g_update_display();
}

void drawIpSplash() {
  g_clear_display();
  reset_eyes(false);
  draw_eyes();
  g_draw_text(26, 150, "Connected", TEXT_COLOR, 2);
  g_draw_text(26, 180, WiFi.localIP().toString(), TEXT_COLOR, 3);
  g_draw_text(26, 214, "Key " + settings.deviceKey, TEXT_COLOR, 2);
  g_update_display();
}

void drawConnectingFrame(const String &ssid) {
  g_clear_display();
  reset_eyes(false);
  draw_eyes();
  g_draw_text(26, 154, "WiFi...", TEXT_COLOR, 2);
  g_draw_text(26, 186, ssid, TEXT_COLOR, 2);
  g_update_display();
}

void drawDisplaySelfTest() {
  display->fillRect(0, 0, 80, SCREEN_HEIGHT, 0xF800);
  display->fillRect(80, 0, 80, SCREEN_HEIGHT, 0x07E0);
  display->fillRect(160, 0, 80, SCREEN_HEIGHT, 0x001F);
  display->fillRect(240, 0, 80, SCREEN_HEIGHT, BACKGROUND_COLOR);
  g_draw_text(184, 104, "Tokkani", TEXT_COLOR, 2);
  delay(1200);
}

void drawStaticFrame() {
  if (uiMode != UI_NORMAL) {
    if (uiMode == UI_RESET_CHOICE) {
      drawResetChoice();
    } else if (uiMode == UI_RESET_HOLD) {
      drawResetHold();
    } else {
      drawMenu();
    }
    return;
  }

  if (deviceMode == MODE_PORTAL) {
    drawPortalStatic();
  } else {
    drawOnlineStatic();
  }
}

void draw_frame() {
  drawStaticFrame();
}

void blink(int speed = 16) {
  reset_eyes(false);
  for (int i = 0; i < 3; i++) {
    left_eye.height -= speed;
    right_eye.height -= speed;
    corner_radius = max(1, left_eye.height / 2);
    left_eye.width += 3;
    right_eye.width += 3;
    drawFaceOnly();
    delayWithGaugeBlink(18);
  }
  for (int i = 0; i < 3; i++) {
    left_eye.height += speed;
    right_eye.height += speed;
    corner_radius = min(REF_CORNER_RADIUS, left_eye.height / 2);
    left_eye.width -= 3;
    right_eye.width -= 3;
    drawFaceOnly();
    delayWithGaugeBlink(18);
  }
  reset_eyes(false);
  drawFaceOnly();
}

void glance(int direction_x, int direction_y) {
  reset_eyes(false);
  for (int i = 0; i < 4; i++) {
    left_eye.x += direction_x * 5;
    right_eye.x += direction_x * 5;
    left_eye.y += direction_y * 3;
    right_eye.y += direction_y * 3;
    drawFaceOnly();
    delayWithGaugeBlink(35);
  }
  delayWithGaugeBlink(180);
  for (int i = 0; i < 4; i++) {
    left_eye.x -= direction_x * 5;
    right_eye.x -= direction_x * 5;
    left_eye.y -= direction_y * 3;
    right_eye.y -= direction_y * 3;
    drawFaceOnly();
    delayWithGaugeBlink(35);
  }
  reset_eyes(false);
  drawFaceOnly();
}

void wakeup() {
  reset_eyes(false);
  for (int h = 5; h <= REF_EYE_HEIGHT; h += 5) {
    left_eye.height = h;
    right_eye.height = h;
    corner_radius = min(REF_CORNER_RADIUS, h / 2);
    drawFaceOnly();
    delayWithGaugeBlink(22);
  }
}

void happy_eye() {
  reset_eyes(false);
  for (int i = 0; i < 6; i++) {
    left_eye.height = REF_EYE_HEIGHT - 18;
    right_eye.height = REF_EYE_HEIGHT - 18;
    left_eye.y = EYE_CENTER_Y - 4;
    right_eye.y = EYE_CENTER_Y - 4;
    corner_radius = 18;
    drawFaceOnly();
    delayWithGaugeBlink(45);

    left_eye.y = EYE_CENTER_Y + 2;
    right_eye.y = EYE_CENTER_Y + 2;
    drawFaceOnly();
    delayWithGaugeBlink(45);
  }
  reset_eyes(false);
  drawFaceOnly();
}

void sleepyPeek() {
  sleep_eyes(false);
  for (int h = 8; h <= 34; h += 5) {
    left_eye.height = h;
    right_eye.height = h;
    corner_radius = min(10, h / 2);
    drawFaceOnly();
    delayWithGaugeBlink(45);
  }
  delayWithGaugeBlink(350);
  sleep_eyes(false);
  drawFaceOnly();
}

void launch_animation_with_index(int animation_index) {
  switch (animation_index) {
    case WAKEUP:
      wakeup();
      break;
    case RESET:
      reset_eyes(true);
      break;
    case BLINK_SHORT:
      blink(16);
      break;
    case HAPPY:
      happy_eye();
      break;
    case SLEEP:
      sleep_eyes(true);
      break;
    case SLEEPY_PEEK:
      sleepyPeek();
      break;
    default:
      break;
  }
}

UsageReading *readingForProvider(const String &provider) {
  String normalized = provider;
  normalized.toLowerCase();

  if (normalized == "claude") {
    return &claudeUsage;
  }
  if (normalized == "codex") {
    return &codexUsage;
  }
  return nullptr;
}

DeviceSettings loadSettings() {
  prefs.begin("tokkani", true);
  DeviceSettings loaded;
  loaded.ssid = prefs.getString("ssid", "");
  loaded.wifiPassword = prefs.getString("wifiPass", "");
  loaded.deviceKey = prefs.getString("deviceKey", "");
  bool loadedEyeWhite = prefs.getBool("eyeWhite", true);
  prefs.end();
  setEyeColorWhite(loadedEyeWhite);
  if (loaded.deviceKey.length() != 4) {
    loaded.deviceKey = generatedDeviceKey();
  }
  return loaded;
}

void saveSettings(const DeviceSettings &nextSettings) {
  String normalizedKey = nextSettings.deviceKey;
  normalizedKey.trim();
  if (normalizedKey.length() != 4) {
    normalizedKey = generatedDeviceKey();
  }

  prefs.begin("tokkani", false);
  prefs.putString("ssid", nextSettings.ssid);
  prefs.putString("wifiPass", nextSettings.wifiPassword);
  prefs.putString("deviceKey", normalizedKey);
  prefs.end();
}

bool connectWiFiFromSettings() {
  if (settings.ssid.length() == 0) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.ssid.c_str(), settings.wifiPassword.c_str());
  drawConnectingFrame(settings.ssid);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 18000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

String configPage() {
  String html;
  html.reserve(3500);
  html += F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>");
  html += F("<title>Tokkani Setup</title><style>");
  html += F("body{margin:0;font-family:system-ui;background:#e95f1d;color:#fff}main{display:grid;gap:16px;padding:24px;max-width:460px;margin:auto}h1{margin:0;font-size:28px}label{display:grid;gap:6px;color:#ffd9bd}input{font:inherit;padding:12px;border-radius:8px;border:0}button{font:inherit;font-weight:700;border:0;border-radius:8px;padding:12px;background:#fff;color:#5b2100}.note{color:#ffe6d2;font-size:14px}</style></head><body><main>");
  html += F("<h1>Tokkani</h1><p class=note>Configure Wi-Fi and a local device key for the extension. Do not use Claude or ChatGPT cookies or passwords.</p>");
  html += F("<form method=post action=/save><label>WiFi SSID<input name=ssid required value='");
  html += settings.ssid;
  html += F("'></label><label>WiFi password<input name=wifiPass type=password></label><label>Device key<input name=deviceKey required inputmode=numeric pattern='[0-9]{4}' maxlength=4 value='");
  html += settings.deviceKey;
  html += F("'></label><button>Save and restart</button></form>");
  html += F("<p class=note>AP: ");
  html += apSsid;
  html += F("<br>IP: 192.168.4.1</p></main></body></html>");
  return html;
}

void redirectToPortal() {
  server.sendHeader("Location", String("http://") + PORTAL_IP.toString() + "/", true);
  server.send(302, "text/plain", "");
}

void handlePortalRoot() {
  server.send(200, "text/html", configPage());
}

void handlePortalSave() {
  DeviceSettings nextSettings;
  nextSettings.ssid = server.arg("ssid");
  nextSettings.wifiPassword = server.arg("wifiPass");
  nextSettings.deviceKey = server.arg("deviceKey");
  saveSettings(nextSettings);

  server.send(200, "text/html", "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#e95f1d;color:white;padding:24px'><h1>Saved</h1><p>Tokkani will restart and try to connect to your Wi-Fi.</p></body>");
  delay(1200);
  ESP.restart();
}

void writeUsageJson() {
  JsonDocument doc;
  doc["mode"] = deviceMode == MODE_ONLINE ? "online" : "portal";
  doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : PORTAL_IP.toString();

  JsonObject claude = doc["claude"].to<JsonObject>();
  claude["sessionUsedPct"] = claudeUsage.sessionUsedPct;
  claude["fiveHourUsedPct"] = claudeUsage.fiveHourUsedPct;
  claude["weeklyUsedPct"] = claudeUsage.weeklyUsedPct;
  claude["resetText"] = claudeUsage.resetText;
  claude["age"] = ageText(claudeUsage);

  JsonObject codex = doc["codex"].to<JsonObject>();
  codex["weeklyUsedPct"] = codexUsage.weeklyUsedPct;
  codex["resetText"] = codexUsage.resetText;
  codex["age"] = ageText(codexUsage);

  String response;
  serializeJson(doc, response);
  sendJson(200, response);
}

void handleStatus() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["mode"] = deviceMode == MODE_ONLINE ? "online" : "portal";
  doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : PORTAL_IP.toString();
  doc["hasClaude"] = hasUsage(claudeUsage);
  doc["hasCodex"] = hasUsage(codexUsage);

  String response;
  serializeJson(doc, response);
  sendJson(200, response);
}

bool incomingUsageValueChanged(const JsonDocument &doc, const char *key, int currentValue) {
  JsonVariantConst value = doc[key];
  if (value.isNull() || !value.is<int>()) {
    return false;
  }

  return value.as<int>() != currentValue;
}

bool incomingUsageChanged(const UsageReading &reading, const JsonDocument &doc) {
  return incomingUsageValueChanged(doc, "fiveHourUsedPct", reading.fiveHourUsedPct) ||
         incomingUsageValueChanged(doc, "sessionUsedPct", reading.sessionUsedPct) ||
         incomingUsageValueChanged(doc, "weeklyUsedPct", reading.weeklyUsedPct);
}

void handleUsagePost() {
  String providedKey = server.header("X-Tokkani-Key");
  if (providedKey != settings.deviceKey) {
    sendJson(401, "{\"error\":\"bad key\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }

  String provider = doc["provider"] | "";
  UsageReading *reading = readingForProvider(provider);
  if (reading == nullptr) {
    sendJson(400, "{\"error\":\"unknown provider\"}");
    return;
  }

  bool usageChanged = provider == "codex"
    ? incomingUsageValueChanged(doc, "weeklyUsedPct", reading->weeklyUsedPct)
    : incomingUsageChanged(*reading, doc);
  bool forceWakeUp = doc["forceWakeUp"] | false;

  if (provider == "codex") {
    reading->fiveHourUsedPct = -1;
    reading->sessionUsedPct = -1;
  } else {
    reading->fiveHourUsedPct = doc["fiveHourUsedPct"] | reading->fiveHourUsedPct;
    reading->sessionUsedPct = doc["sessionUsedPct"] | reading->sessionUsedPct;
  }
  reading->weeklyUsedPct = doc["weeklyUsedPct"] | reading->weeklyUsedPct;
  reading->resetText = String((const char *)(doc["resetText"] | ""));
  reading->capturedAt = String((const char *)(doc["capturedAt"] | ""));
  reading->receivedMillis = millis();

  if (forceWakeUp || (usageChanged && !backlightOn)) {
    setBacklight(true);
  }

  if (random(0, 10) == 0) {
    wakeup();
    happy_eye();
  }
  gaugeBlinkVisible = true;
  lastGaugeBlinkMillis = millis();
  displayDirty = true;
  staticFrameDirty = true;
  addCorsHeaders();
  server.send(204);
}

void setupOnlineRoutes() {
  server.collectHeaders(AUTH_HEADER_KEYS, 1);
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain", "Tokkani online. Use GET /status, GET /usage, or POST /usage.");
  });
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/usage", HTTP_GET, writeUsageJson);
  server.on("/usage", HTTP_OPTIONS, []() {
    addCorsHeaders();
    server.send(204);
  });
  server.on("/usage", HTTP_POST, handleUsagePost);
  server.onNotFound([]() {
    addCorsHeaders();
    server.send(404, "application/json", "{\"error\":\"not found\"}");
  });
  server.begin();
}

void startPortal() {
  deviceMode = MODE_PORTAL;
  apSsid = "Tokkani-" + chipSuffix();
  apPassword = generatedPassword();

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(PORTAL_IP, PORTAL_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid.c_str(), apPassword.c_str());
  dnsServer.start(DNS_PORT, "*", PORTAL_IP);

  server.on("/", HTTP_GET, handlePortalRoot);
  server.on("/save", HTTP_POST, handlePortalSave);
  server.on("/generate_204", HTTP_GET, redirectToPortal);
  server.on("/gen_204", HTTP_GET, redirectToPortal);
  server.on("/hotspot-detect.html", HTTP_GET, handlePortalRoot);
  server.on("/ncsi.txt", HTTP_GET, []() {
    server.send(200, "text/plain", "Microsoft NCSI");
  });
  server.onNotFound(redirectToPortal);
  server.begin();

  drawPortalStatic();
}

void startOnline() {
  deviceMode = MODE_ONLINE;
  setupOnlineRoutes();
  drawIpSplash();
  delay(3000);
  sleep_eyes(false);
  staticFrameDirty = true;
  displayDirty = true;
}

void setup() {
  Serial.begin(BAUD_RATE);
  randomSeed(analogRead(0));
  g_init_display();
  initBacklightControls();
  g_init_face_canvas(FACE_DIRTY_W, FACE_DIRTY_H, FACE_DIRTY_X, FACE_DIRTY_Y);
  g_init_ui_canvas(UI_DIRTY_W, UI_DIRTY_H, UI_DIRTY_X, UI_DIRTY_Y);
  drawDisplaySelfTest();

  settings = loadSettings();
  if (connectWiFiFromSettings()) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    startOnline();
  } else {
    Serial.println("Starting captive portal");
    startPortal();
  }

  Serial.println("READY");
}

void loop() {
  if (deviceMode == MODE_PORTAL) {
    dnsServer.processNextRequest();
  }

  server.handleClient();
  tickTouchAndBacklight();

  if (displayDirty || staticFrameDirty || millis() - lastDisplayRefreshMillis > 30000) {
    drawStaticFrame();
    displayDirty = false;
    staticFrameDirty = false;
    lastDisplayRefreshMillis = millis();
  }

  tickGaugeBlink();

  if (uiMode == UI_NORMAL && deviceMode == MODE_ONLINE && millis() - lastIdleAnimationMillis > 4500) {
    if (hasAnyUsage()) {
      if (random(0, 3) == 0) {
        blink(18);
      } else {
        glance(random(0, 2) == 0 ? -1 : 1, random(-1, 2));
      }
    } else {
      launch_animation_with_index(SLEEPY_PEEK);
    }
    lastIdleAnimationMillis = millis();
  }
}
