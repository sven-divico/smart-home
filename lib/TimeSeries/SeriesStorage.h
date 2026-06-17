#pragma once
#include <stdint.h>
#include "Sample.h"
#include "metrics.h"

// Persistence boundary. Device impl = SD files; test/fallback = in-memory.
// The store writes through on each sample and reads back on boot.
class SeriesStorage {
public:
  virtual ~SeriesStorage() {}
  virtual bool begin() = 0;                 // mount; false => caller runs RAM-only

  // Append one finalized point (raw or daily) for a series.
  virtual void appendSample(ts::NodeId, ts::Metric, bool daily, const Sample&) = 0;
  // Load up to maxOut samples, oldest..newest. The store replays them via
  // pushRawDirect/pushDailyDirect to rebuild the ring, so no head/count is needed.
  virtual int  loadRing(ts::NodeId, ts::Metric, bool daily, Sample* out, int maxOut) = 0;

  virtual void appendPumpEvent(const PumpEvent&) = 0;
  virtual int  loadPumpEvents(PumpEvent* out, int maxOut) = 0;

  virtual void saveClock(uint32_t epoch) = 0;
  virtual bool loadClock(uint32_t& epoch) = 0;
};
