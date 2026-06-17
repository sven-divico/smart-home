#pragma once
#include <stdint.h>

// Soft wall-clock: an epoch baseline advanced by an injected millis source.
// Device passes Arduino millis(); tests pass a fake. Later the *source* of the
// baseline (gateway time-sync / NTP) changes — now() does not.
class Clock {
public:
  explicit Clock(uint32_t (*millisFn)());
  // Rebase: this epoch == "right now". Call at least every ~49 days — uint32_t
  // millis wraps at 2^32 ms (~49.7 d), after which an un-rebased now() drifts.
  void setEpoch(uint32_t epochSeconds);
  uint32_t now() const;                  // current epoch seconds
  uint32_t baseEpoch() const { return baseEpoch_; }

private:
  uint32_t (*millisFn_)();
  uint32_t baseEpoch_  = 0;
  uint32_t baseMillis_ = 0;
};
