#include "TimeSeriesStore.h"
#include <math.h>
#include <new>
using namespace ts;

// Call once (the store is a program-lifetime singleton). Re-calling would
// placement-new over already-constructed Series — don't.
void TimeSeriesStore::init(SeriesStorage* storage) {
  storage_ = storage;
  int n; const SeriesCfg* cfg = registry(n);
  count_ = 0;
  for (int i = 0; i < n && count_ < MAX_SERIES; i++) {
    keys_[count_] = {cfg[i].node, cfg[i].metric};
    series_[count_] = new (pool_[count_]) Series(cfg[i].metric, cfg[i].hasDaily);
    series_[count_]->setSink(&TimeSeriesStore::sinkTrampoline, this, cfg[i].node);
    count_++;
  }
}

void TimeSeriesStore::sinkTrampoline(void* ctx, NodeId n, Metric m, bool daily, const Sample& s) {
  auto* self = static_cast<TimeSeriesStore*>(ctx);
  if (self->storage_) self->storage_->appendSample(n, m, daily, s);
}

void TimeSeriesStore::reload() {
  if (!storage_) return;
  // Sample buf[DAILY_CAP] is ~2.4 KB of stack (fine on the app_main task; don't call from a tiny stack).
  Sample buf[DAILY_CAP];
  for (int i = 0; i < count_; i++) {
    NodeId n = keys_[i].node; Metric m = keys_[i].metric;
    int nr = storage_->loadRing(n, m, false, buf, RAW_CAP);
    for (int k = 0; k < nr; k++) series_[i]->pushRawDirect(buf[k]);
    if (series_[i]->hasDaily()) {
      int nd = storage_->loadRing(n, m, true, buf, DAILY_CAP);
      for (int k = 0; k < nd; k++) series_[i]->pushDailyDirect(buf[k]);
    }
  }
}

// Persist the entire current contents of every ring to storage (one-time, after
// seeding). Live points persist automatically via the Series sink; seeded points
// are inserted with pushRawDirect (no sink), so they need this explicit flush.
// Note: with the per-slot SdStorage this re-opens each file per sample, so first-
// boot seeding takes a few seconds — acceptable as a one-time cost; a bulk
// saveRing() could be added later if it becomes annoying.
void TimeSeriesStore::persistAll() {
  if (!storage_) return;
  for (int i = 0; i < count_; i++) {
    NodeId n = keys_[i].node; Metric m = keys_[i].metric;
    const Ring& r = series_[i]->raw();
    for (uint16_t k = 0; k < r.size(); k++) storage_->appendSample(n, m, false, r.at(k));
    if (series_[i]->hasDaily()) {
      const Ring& d = series_[i]->daily();
      for (uint16_t k = 0; k < d.size(); k++) storage_->appendSample(n, m, true, d.at(k));
    }
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
  // tmp is a fixed per-node scratch buffer; clamp nPoints so a large request can
  // never write past it (stack corruption on-device). Callers use ≤ 30 in practice.
  static const int TMP_CAP = 64;
  if (nPoints > TMP_CAP) nPoints = TMP_CAP;
  const NodeId soilNodes[] = {NODE_BEET1, NODE_BEET2, NODE_BEET3, NODE_GEWAECHSHAUS};
  for (int b = 0; b < nPoints; b++) out[b] = 0.0f;
  int contributors = 0;
  float tmp[TMP_CAP];
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
