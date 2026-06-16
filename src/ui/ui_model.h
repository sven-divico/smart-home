#pragma once
// Mock data model for the UI milestone (spec §6). Fixed, representative values,
// shared by all three pages so numbers stay consistent across them. No sensors,
// transport or storage yet — those arrive in Mission 2.
#include <stdint.h>

enum WeatherGlyph { WX_SUN, WX_CLOUD, WX_RAIN, WX_MOON };
enum Trend { TREND_DOWN = -1, TREND_STEADY = 0, TREND_UP = 1 };

struct EnvStation {
  float       tempC;        // 22.4
  const char *condition;    // "Sonnig"
  int         feelsLikeC;   // 24
  WeatherGlyph glyph;       // WX_SUN
  int         pressureHpa;  // 1013
  Trend       pressureTrend;// TREND_UP
  const char *pressureWord; // "Luftdruck steigt · stabil"
  int         humidityPct;  // 58
  int         lightLux;     // 12000  (shown as "12k")
  int         dayLoC;       // 14
  int         dayHiC;       // 25
};

// 7-day main chart: a temperature line over soil-moisture-average bars.
struct ChartSeries {
  float tempC[7];    // °C, left axis  (10..30)
  int   soilPct[7];  // %,  right axis (0..60)
};

struct Node {
  const char *name;       // "Beet 1" … "Gewächshaus Hochbeet" (may overflow -> truncates)
  int         soilPct;    // 41
  float       soilTempC;  // 18.2
  bool        dry;        // true -> show "trocken" instead of soil temp
  bool        pumpOn;     // badge EIN/AUS
  int8_t      spark12h[7];// 12 h sparkline samples (soil %)
  int8_t      spark7d[7]; // 7 day sparkline samples
};

struct Pump {
  const char *zone;        // "Beet 1"
  const char *nodeId;      // "PUMP-201"
  bool        on;          // toggle state
  bool        active;      // currently running (row accented)
  int         currentPct;  // 41
  int         targetPct;   // 35
  const char *mode;        // "AUTO"
  const char *statusLine;  // "zuletzt 06:30" / "läuft · 00:02"
  const char *reason;      // "< Schwelle" or nullptr
  int         minutesToday;// 4
  int         avgPerDay;   // 5
};

struct UiModel {
  const char *dateLine;    // "Dienstag, 16. Juni 2026"
  const char *clockLong;   // "14:23 Uhr"
  const char *clock;       // "14:23"
  EnvStation  env;
  ChartSeries chart;
  Node        nodes[4];
  Pump        pumps[4];
  bool        hasAlert;    // false -> calm message band
  const char *alertText;   // shown inverted when hasAlert
  int         activePumps; // count for Page 3 header
};

const UiModel &mockModel();
