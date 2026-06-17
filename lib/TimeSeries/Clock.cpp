#include "Clock.h"

Clock::Clock(uint32_t (*millisFn)()) : millisFn_(millisFn) {}

void Clock::setEpoch(uint32_t epochSeconds) {
  baseEpoch_  = epochSeconds;
  baseMillis_ = millisFn_();
}

uint32_t Clock::now() const {
  return baseEpoch_ + (millisFn_() - baseMillis_) / 1000;
}
