#pragma once
#include "SeriesStorage.h"

// RAM-only storage: satisfies the interface, persists nothing across reboots.
// Used by unit tests and as the live fallback when no SD card is present.
class InMemoryStorage : public SeriesStorage {
public:
  bool begin() override { return true; }
  void appendSample(ts::NodeId, ts::Metric, bool daily, const Sample&) override;
  int  loadRing(ts::NodeId, ts::Metric, bool daily, Sample* out, int maxOut) override;
  void appendPumpEvent(const PumpEvent&) override;
  int  loadPumpEvents(PumpEvent* out, int maxOut) override;
  void saveClock(uint32_t epoch) override { clock_ = epoch; haveClock_ = true; }
  bool loadClock(uint32_t& epoch) override { if (haveClock_) epoch = clock_; return haveClock_; }

private:
  struct Buf { ts::NodeId n; ts::Metric m; bool daily; Sample s[ts::DAILY_CAP]; int count = 0; };
  static const int MAXB = 32;
  Buf  bufs_[MAXB]; int nbufs_ = 0;
  Buf* findBuf(ts::NodeId, ts::Metric, bool daily);  // non-allocating lookup; nullptr if absent
  Buf& bufFor(ts::NodeId, ts::Metric, bool daily);   // allocating; returns sentinel on overflow

  PumpEvent pumps_[256]; int npumps_ = 0;
  uint32_t clock_ = 0; bool haveClock_ = false;
};
