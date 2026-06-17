#include "TimeSeriesStore.h"
#include <math.h>
#include <new>
using namespace ts;

void TimeSeriesStore::init() {
  int n; const SeriesCfg* cfg = registry(n);
  count_ = 0;
  for (int i = 0; i < n && count_ < MAX_SERIES; i++) {
    keys_[count_] = {cfg[i].node, cfg[i].metric};
    series_[count_] = new (pool_[count_]) Series(cfg[i].metric, cfg[i].hasDaily);
    count_++;
  }
}

Series* TimeSeriesStore::find(NodeId node, Metric metric) {
  for (int i = 0; i < count_; i++)
    if (keys_[i].node == node && keys_[i].metric == metric) return series_[i];
  return nullptr;
}
const Series* TimeSeriesStore::find(NodeId node, Metric metric) const {
  for (int i = 0; i < count_; i++)
    if (keys_[i].node == node && keys_[i].metric == metric) return series_[i];
  return nullptr;
}

void TimeSeriesStore::add(NodeId node, Metric metric, uint32_t ts, int16_t value) {
  if (Series* s = find(node, metric)) s->add(ts, value);
}

float TimeSeriesStore::latest(NodeId node, Metric metric) const {
  const Series* s = find(node, metric);
  if (!s || !s->hasData()) return NAN;
  return decode(metric, s->latest());
}

// --- Query implementations (Task 4.2) ---

static uint32_t windowSpan(Window w) {
  switch (w) { case W_12H: return 12 * 3600; case W_7D: return 7 * DAY_S;
               default: return 30 * DAY_S; }
}

int TimeSeriesStore::sampleWindow(NodeId node, Metric metric, Window w,
                                  uint32_t now, float* out, int nPoints) const {
  const Series* s = find(node, metric);
  if (!s || nPoints <= 0) return 0;
  const Ring& ring = (w == W_12H) ? s->raw() : s->daily();
  uint32_t span = windowSpan(w);
  // Align start to the natural bucket grid so whole intervals map cleanly to buckets.
  // W_12H aligns to 15-min (raw) intervals; W_7D/W_MONTH align to day boundaries.
  uint32_t align = (w == W_12H) ? RAW_INTERVAL_S : DAY_S;
  uint32_t raw_start = (now > span) ? now - span : 0;
  uint32_t start = (raw_start / align) * align;

  // Bucket the window into nPoints; each bucket = mean of samples that fall in it.
  // Empty buckets carry the previous bucket's value (flat hold) so sparklines stay continuous.
  for (int b = 0; b < nPoints; b++) {
    uint32_t b0 = start + (uint64_t)span * b / nPoints;
    uint32_t b1 = start + (uint64_t)span * (b + 1) / nPoints;
    double sum = 0; int cnt = 0;
    for (uint16_t i = 0; i < ring.size(); i++) {
      const Sample& smp = ring.at(i);
      if (smp.ts >= b0 && smp.ts < b1) { sum += decode(metric, smp.value); cnt++; }
    }
    if (cnt > 0)      out[b] = (float)(sum / cnt);
    else if (b > 0)   out[b] = out[b - 1];
    else {
      // first bucket empty: fall back to the newest sample at/under start, else 0
      out[b] = ring.size() ? decode(metric, ring.newest().value) : 0.0f;
    }
  }
  return nPoints;
}

int TimeSeriesStore::averageAcrossNodes(Metric metric, Window w, uint32_t now,
                                        float* out, int nPoints) const {
  const NodeId soilNodes[] = {NODE_BEET1, NODE_BEET2, NODE_BEET3, NODE_GEWAECHSHAUS};
  for (int b = 0; b < nPoints; b++) out[b] = 0.0f;
  int contributors = 0;
  float tmp[64];
  for (NodeId n : soilNodes) {
    if (sampleWindow(n, metric, w, now, tmp, nPoints) == nPoints) {
      for (int b = 0; b < nPoints; b++) out[b] += tmp[b];
      contributors++;
    }
  }
  if (contributors) for (int b = 0; b < nPoints; b++) out[b] /= contributors;
  return nPoints;
}

bool TimeSeriesStore::minMaxToday(NodeId node, Metric metric, uint32_t now,
                                  float& outMin, float& outMax) const {
  const Series* s = find(node, metric);
  if (!s) return false;
  uint32_t dayStart = (now / DAY_S) * DAY_S;
  const Ring& ring = s->raw();
  bool any = false;
  for (uint16_t i = 0; i < ring.size(); i++) {
    const Sample& smp = ring.at(i);
    if (smp.ts < dayStart) continue;
    float v = decode(metric, smp.value);
    if (!any) { outMin = outMax = v; any = true; }
    else { if (v < outMin) outMin = v; if (v > outMax) outMax = v; }
  }
  return any;
}

int TimeSeriesStore::trend(NodeId node, Metric metric, uint32_t now) const {
  const Series* s = find(node, metric);
  if (!s || s->raw().size() < 2) return 0;
  const Ring& ring = s->raw();
  float newest = decode(metric, ring.newest().value);
  // compare against the sample nearest (now - 3h)
  uint32_t target = (now > 3 * 3600) ? now - 3 * 3600 : 0;
  float ref = decode(metric, ring.at(0).value);
  for (uint16_t i = 0; i < ring.size(); i++)
    if (ring.at(i).ts <= target) ref = decode(metric, ring.at(i).value);
  float d = newest - ref;
  float deadband = (metric == M_PRESSURE) ? 1.0f : 0.5f;
  if (d > deadband) return 1;
  if (d < -deadband) return -1;
  return 0;
}
