#pragma once
#include <stdint.h>
#include "Series.h"
#include "metrics.h"

// Registry of Series keyed by (node, metric) + the aggregate queries the
// repository needs. Holds Series by value in a fixed array (no dynamic memory).
class TimeSeriesStore {
public:
  void init();                          // register every series in the registry
  int  seriesCount() const { return count_; }
  Series* find(ts::NodeId node, ts::Metric metric);
  const Series* find(ts::NodeId node, ts::Metric metric) const;

  void add(ts::NodeId node, ts::Metric metric, uint32_t ts, int16_t value);
  float latest(ts::NodeId node, ts::Metric metric) const;  // decoded; NAN if none

  // Resample a window into nPoints decoded values (oldest..newest). Returns count written.
  int sampleWindow(ts::NodeId node, ts::Metric metric, ts::Window w,
                   uint32_t now, float* out, int nPoints) const;
  // Element-wise average of sampleWindow across the four soil nodes.
  int averageAcrossNodes(ts::Metric metric, ts::Window w,
                         uint32_t now, float* out, int nPoints) const;
  bool minMaxToday(ts::NodeId node, ts::Metric metric, uint32_t now,
                   float& outMin, float& outMax) const;
  int trend(ts::NodeId node, ts::Metric metric, uint32_t now) const; // -1/0/+1

private:
  static const int MAX_SERIES = 16;
  struct Key { ts::NodeId node; ts::Metric metric; };
  Key    keys_[MAX_SERIES];
  Series* series_[MAX_SERIES] = {nullptr};
  // Series are non-copyable in practice (big buffers); store in a fixed pool.
  alignas(Series) unsigned char pool_[MAX_SERIES][sizeof(Series)];
  int count_ = 0;
};
