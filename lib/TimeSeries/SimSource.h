#pragma once
#include <stdint.h>
#include "TimeSeriesStore.h"
#include "PumpLog.h"
#include "metrics.h"

// Deterministic data source for Mission 2 (stands in for real sensors).
// seed(): ~30 d daily + ~48 h raw history written directly into the rings.
// tick(now): generate the next sample per series; drive pump start/stop on soil.
class SimSource {
public:
  void seed(TimeSeriesStore& store, PumpLog& log, uint32_t now);
  void tick(TimeSeriesStore& store, PumpLog& log, uint32_t now);

  // thresholds (also written into pump-event audit fields)
  static const int16_t DRY_THRESHOLD = 35;   // start watering below this
  static const int16_t WET_TARGET    = 45;   // stop watering at/above this

private:
  uint32_t rng_ = 0x1234567u;
  float frand(float lo, float hi);           // deterministic LCG in [lo,hi]
  float soilBaseFor(ts::NodeId n) const;
  float diurnalTemp(uint32_t ts) const;
  bool  pumpRunning_[ts::NODE_COUNT] = {false};
};
