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
  // Each Buf is sized to DAILY_CAP (~2.4 KB) regardless of tier — simple but
  // RAM-heavy: MAXB*2.4 KB lives in .bss even when SD is present and this fallback
  // is unused. MAXB must stay >= the number of distinct (node,metric,daily) keys
  // (currently 17: 12 raw + 5 daily). To reclaim internal SRAM later, lower MAXB
  // toward ~20 and/or size non-daily Bufs to RAW_CAP.
  struct Buf { ts::NodeId n; ts::Metric m; bool daily; Sample s[ts::DAILY_CAP]; int count = 0; };
  static const int MAXB = 32;
  Buf  bufs_[MAXB]; int nbufs_ = 0;
  Buf* findBuf(ts::NodeId, ts::Metric, bool daily);  // non-allocating lookup; nullptr if absent
  Buf& bufFor(ts::NodeId, ts::Metric, bool daily);   // allocating; returns sentinel on overflow

  PumpEvent pumps_[256]; int npumps_ = 0;
  uint32_t clock_ = 0; bool haveClock_ = false;
};
