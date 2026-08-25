#pragma once

#include <Arduino.h>

struct UsageReading {
  String provider;
  int sessionUsedPct = -1;
  int fiveHourUsedPct = -1;
  int weeklyUsedPct = -1;
  String resetText;
  String capturedAt;
  unsigned long receivedMillis = 0;
};

inline bool hasUsage(const UsageReading &reading) {
  return reading.sessionUsedPct >= 0 || reading.fiveHourUsedPct >= 0 || reading.weeklyUsedPct >= 0;
}
