#pragma once
#include <stdint.h>
#include <math.h>
#include "Sample.h"

namespace ts {

enum NodeId : uint8_t {
  NODE_BEET1 = 0, NODE_BEET2, NODE_BEET3, NODE_GEWAECHSHAUS,
  NODE_ENV, NODE_COUNT
};
enum Metric : uint8_t {
  M_SOIL = 0, M_SOIL_TEMP, M_AIR_TEMP, M_PRESSURE, M_HUMIDITY, M_LUX, M_COUNT
};
enum Window : uint8_t { W_12H = 0, W_7D, W_MONTH };

// constexpr (implicitly inline in C++17) → single definition across all TUs,
// so these stay odr-use-safe once Series.cpp / TimeSeriesStore.cpp etc. use them.
constexpr uint32_t RAW_INTERVAL_S  = 900;
constexpr uint32_t FINE_INTERVAL_S = 30;
constexpr uint32_t DAY_S           = 86400;
constexpr uint16_t RAW_CAP         = 192;
constexpr uint16_t DAILY_CAP       = 400;

// Fixed-point scale per metric: stored = round(real * mul / div).
struct Scale { int16_t mul; int16_t div; };
inline Scale scaleOf(Metric m) {
  switch (m) {
    case M_AIR_TEMP:
    case M_SOIL_TEMP: return {10, 1};
    case M_LUX:       return {1, 4};
    default:          return {1, 1}; // soil %, pressure hPa, humidity %
  }
}
inline int16_t encode(Metric m, float real) {
  Scale s = scaleOf(m);
  return (int16_t) lroundf(real * s.mul / s.div);
}
inline float decode(Metric m, int16_t stored) {
  Scale s = scaleOf(m);
  return (float) stored * s.div / s.mul;
}

// Series registry: which (node, metric) exist and whether they keep a daily tier.
struct SeriesCfg { NodeId node; Metric metric; bool hasDaily; };
inline const SeriesCfg* registry(int& outCount) {
  static const SeriesCfg cfg[] = {
    {NODE_BEET1,        M_SOIL,      true },  {NODE_BEET1,        M_SOIL_TEMP, false},
    {NODE_BEET2,        M_SOIL,      true },  {NODE_BEET2,        M_SOIL_TEMP, false},
    {NODE_BEET3,        M_SOIL,      true },  {NODE_BEET3,        M_SOIL_TEMP, false},
    {NODE_GEWAECHSHAUS, M_SOIL,      true },  {NODE_GEWAECHSHAUS, M_SOIL_TEMP, false},
    {NODE_ENV,          M_AIR_TEMP,  true },  {NODE_ENV,          M_PRESSURE,  false},
    {NODE_ENV,          M_HUMIDITY,  false},  {NODE_ENV,          M_LUX,       false},
  };
  outCount = sizeof(cfg) / sizeof(cfg[0]);
  return cfg;
}

} // namespace ts
