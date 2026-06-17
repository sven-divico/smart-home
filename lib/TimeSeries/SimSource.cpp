#include "SimSource.h"
#include <math.h>
using namespace ts;

float SimSource::frand(float lo, float hi) {
  rng_ = rng_ * 1664525u + 1013904223u;             // Numerical Recipes LCG
  float u = (rng_ >> 8) / (float)(1u << 24);        // [0,1)
  return lo + u * (hi - lo);
}
float SimSource::soilBaseFor(NodeId n) const {
  switch (n) { case NODE_BEET1: return 42; case NODE_BEET2: return 38;
               case NODE_BEET3: return 51; case NODE_GEWAECHSHAUS: return 31; default: return 40; }
}
float SimSource::diurnalTemp(uint32_t ts) const {
  float hour = (ts % DAY_S) / 3600.0f;
  return 19.0f + 6.0f * sinf((hour - 9.0f) / 24.0f * 2.0f * (float)M_PI); // peak ~15:00
}

void SimSource::seed(TimeSeriesStore& store, PumpLog& log, uint32_t now) {
  const NodeId soil[] = {NODE_BEET1, NODE_BEET2, NODE_BEET3, NODE_GEWAECHSHAUS};

  // 30 daily points per soil node + env air temp.
  // Precondition: now >= 30*DAY_S (real epoch timestamps always are). With a tiny
  // synthetic `now`, (now/DAY_S - d) would underflow uint32_t — don't seed with one.
  for (int d = 30; d >= 1; d--) {
    uint32_t dayTs = ((now / DAY_S) - d) * DAY_S;
    for (NodeId n : soil) {
      float v = soilBaseFor(n) + frand(-4, 4) - (d % 7); // weekly sawtooth variation + noise
      if (v < 12) v += 25;                               // a watering bump
      Series* s = store.find(n, M_SOIL); if (s) s->pushDailyDirect({dayTs, encode(M_SOIL, v)});
    }
    Series* et = store.find(NODE_ENV, M_AIR_TEMP);
    if (et) et->pushDailyDirect({dayTs, encode(M_AIR_TEMP, diurnalTemp(dayTs + 12*3600) + frand(-2,2))});
  }

  // 48 h of raw points (every 15 min) for all series, ending at `now`
  for (uint32_t t = (now > 48*3600 ? now - 48*3600 : 0); t < now; t += RAW_INTERVAL_S) {
    for (NodeId n : soil) {
      float v = soilBaseFor(n) + frand(-3, 3);
      Series* s = store.find(n, M_SOIL); if (s) s->pushRawDirect({t, encode(M_SOIL, v)});
      Series* st = store.find(n, M_SOIL_TEMP); if (st) st->pushRawDirect({t, encode(M_SOIL_TEMP, 17.0f + frand(-1,1))});
    }
    store.find(NODE_ENV, M_AIR_TEMP)->pushRawDirect({t, encode(M_AIR_TEMP, diurnalTemp(t) + frand(-1,1))});
    store.find(NODE_ENV, M_PRESSURE)->pushRawDirect({t, encode(M_PRESSURE, 1011 + frand(0,4))});
    store.find(NODE_ENV, M_HUMIDITY)->pushRawDirect({t, encode(M_HUMIDITY, 55 + frand(-6,6))});
    store.find(NODE_ENV, M_LUX)->pushRawDirect({t, encode(M_LUX, (diurnalTemp(t) > 19 ? 12000 : 200) + frand(0,2000))});
  }

  // A couple of past greenhouse waterings. Uses append() (NOT appendDirect) ON PURPOSE:
  // when a storage sink is attached (device first boot), these seeded events should be
  // persisted to pumps.log — the pump-log analog of persistAll() for the series rings —
  // so the watering history survives a reboot and isn't lost when the store is non-empty.
  uint32_t y = ((now / DAY_S) - 1) * DAY_S + 6 * 3600;
  log.append({y, NODE_GEWAECHSHAUS, EV_START, 30, DRY_THRESHOLD});
  log.append({y + 180, NODE_GEWAECHSHAUS, EV_STOP, 46, WET_TARGET});
}

void SimSource::tick(TimeSeriesStore& store, PumpLog& log, uint32_t now) {
  const NodeId soil[] = {NODE_BEET1, NODE_BEET2, NODE_BEET3, NODE_GEWAECHSHAUS};
  for (NodeId n : soil) {
    float cur = store.latest(n, M_SOIL);
    if (isnan(cur)) cur = soilBaseFor(n);
    float next = pumpRunning_[n] ? cur + frand(1.5f, 3.0f)   // watering raises soil
                                 : cur - frand(0.2f, 0.8f);  // drying lowers it
    if (next < 5) next = 5; if (next > 95) next = 95;
    store.add(n, M_SOIL, now, encode(M_SOIL, next));
    store.add(n, M_SOIL_TEMP, now, encode(M_SOIL_TEMP, 17.0f + frand(-1, 1)));

    int16_t soilPct = (int16_t)lroundf(next);
    if (!pumpRunning_[n] && soilPct < DRY_THRESHOLD) {
      pumpRunning_[n] = true;
      log.append({now, (uint8_t)n, EV_START, soilPct, DRY_THRESHOLD});
    } else if (pumpRunning_[n] && soilPct >= WET_TARGET) {
      pumpRunning_[n] = false;
      log.append({now, (uint8_t)n, EV_STOP, soilPct, WET_TARGET});
    }
  }
  store.add(NODE_ENV, M_AIR_TEMP, now, encode(M_AIR_TEMP, diurnalTemp(now) + frand(-1, 1)));
  store.add(NODE_ENV, M_PRESSURE, now, encode(M_PRESSURE, 1011 + frand(0, 4)));
  store.add(NODE_ENV, M_HUMIDITY, now, encode(M_HUMIDITY, 55 + frand(-6, 6)));
  store.add(NODE_ENV, M_LUX, now, encode(M_LUX, (diurnalTemp(now) > 19 ? 12000 : 200) + frand(0, 2000)));
}
