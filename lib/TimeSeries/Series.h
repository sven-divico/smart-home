#pragma once
#include <stdint.h>
#include "Ring.h"
#include "metrics.h"

// One metric's history: a raw ring (15-min averages) + an optional daily ring.
// Buffers are members sized to the max caps (a few KB each) — no dynamic memory.
class Series {
public:
  Series(ts::Metric metric, bool hasDaily);

  void add(uint32_t ts, int16_t value);   // ingest one sample (fine or coarse)
  void pushRawDirect(const Sample& s);     // seeding: bypass accumulation
  void pushDailyDirect(const Sample& s);   // seeding: bypass accumulation

  int16_t latest() const { return haveLast_ ? last_.value : (raw_.empty() ? 0 : raw_.newest().value); }
  bool hasData() const { return haveLast_ || !raw_.empty(); }

  const Ring& raw() const { return raw_; }
  const Ring& daily() const { return daily_; }
  uint16_t rawSize() const { return raw_.size(); }
  uint16_t dailySize() const { return daily_.size(); }
  const Sample& rawNewest() const { return raw_.newest(); }
  const Sample& dailyNewest() const { return daily_.newest(); }
  bool hasDaily() const { return hasDaily_; }
  ts::Metric metric() const { return metric_; }

  Ring& rawMutable() { return raw_; }      // for storage load
  Ring& dailyMutable() { return daily_; }

private:
  void finalizeWindow();
  void finalizeDay();

  ts::Metric metric_;
  bool       hasDaily_;

  Sample rawBuf_[ts::RAW_CAP];
  Sample dailyBuf_[ts::DAILY_CAP];
  Ring   raw_;
  Ring   daily_;

  // pending 15-min window accumulator
  bool     haveWindow_ = false;
  uint32_t windowKey_  = 0;   // ts / RAW_INTERVAL_S
  int32_t  windowSum_  = 0;
  uint16_t windowCount_ = 0;

  // pending day accumulator (of finalized raw points)
  bool     haveDay_ = false;
  uint32_t dayKey_  = 0;      // windowStart / DAY_S
  int32_t  daySum_  = 0;
  uint16_t dayCount_ = 0;

  Sample last_{0, 0};        // freshest sample seen (for latest())
  bool   haveLast_ = false;
};
