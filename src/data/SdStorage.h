#pragma once
#include "SeriesStorage.h"

// SeriesStorage over microSD (SPI). Each ring = one fixed-size binary file with
// a small header {magic,version,recordSize,capacity,head,count} + slots.
// Device-only: excluded from native builds. begin() returns false if no card.
class SdStorage : public SeriesStorage {
public:
  bool begin() override;
  void appendSample(ts::NodeId, ts::Metric, bool daily, const Sample&) override;
  int  loadRing(ts::NodeId, ts::Metric, bool daily, Sample* out, int maxOut) override;
  void appendPumpEvent(const PumpEvent&) override;
  int  loadPumpEvents(PumpEvent* out, int maxOut) override;
  void saveClock(uint32_t epoch) override;
  bool loadClock(uint32_t& epoch) override;
private:
  bool ok_ = false;
  void pathFor(ts::NodeId, ts::Metric, bool daily, char* out, int n) const;
};
