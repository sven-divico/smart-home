#include "Series.h"
using namespace ts;

Series::Series(Metric metric, bool hasDaily)
  : metric_(metric), hasDaily_(hasDaily),
    raw_(rawBuf_, RAW_CAP), daily_(dailyBuf_, DAILY_CAP) {}

void Series::add(uint32_t ts, int16_t value) {
  uint32_t wk = ts / RAW_INTERVAL_S;
  if (haveWindow_ && wk != windowKey_) finalizeWindow();
  if (!haveWindow_) { haveWindow_ = true; windowKey_ = wk; windowSum_ = 0; windowCount_ = 0; }
  windowSum_ += value;
  windowCount_++;
  last_ = {ts, value};
  haveLast_ = true;
}

void Series::finalizeWindow() {
  // Integer division truncates toward zero. Bias ≤ 0.5 LSB (e.g. 0.05°C for
  // temp); below sensor noise, so rounding isn't worth the cost. Same in finalizeDay.
  int16_t avg = (int16_t)(windowSum_ / windowCount_);
  uint32_t windowStart = windowKey_ * RAW_INTERVAL_S;
  raw_.push({windowStart, avg});
  haveWindow_ = false;

  if (hasDaily_) {
    uint32_t dk = windowStart / DAY_S;
    if (haveDay_ && dk != dayKey_) finalizeDay();
    if (!haveDay_) { haveDay_ = true; dayKey_ = dk; daySum_ = 0; dayCount_ = 0; }
    daySum_ += avg;
    dayCount_++;
  }
}

void Series::finalizeDay() {
  int16_t avg = (int16_t)(daySum_ / dayCount_);
  daily_.push({dayKey_ * DAY_S, avg});
  haveDay_ = false;
}

void Series::pushRawDirect(const Sample& s)   { raw_.push(s); }
void Series::pushDailyDirect(const Sample& s) { if (hasDaily_) daily_.push(s); }
