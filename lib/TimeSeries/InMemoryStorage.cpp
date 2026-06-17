#include "InMemoryStorage.h"
#include <string.h>
using namespace ts;

InMemoryStorage::Buf* InMemoryStorage::findBuf(NodeId n, Metric m, bool daily) {
  for (int i = 0; i < nbufs_; i++)
    if (bufs_[i].n == n && bufs_[i].m == m && bufs_[i].daily == daily) return &bufs_[i];
  return nullptr;
}

InMemoryStorage::Buf& InMemoryStorage::bufFor(NodeId n, Metric m, bool daily) {
  if (Buf* b = findBuf(n, m, daily)) return *b;
  // Overflow (>MAXB unique keys) drops data rather than corrupting memory.
  // With 12 series this never triggers in practice.
  if (nbufs_ >= MAXB) { static Buf sentinel; sentinel.count = 0; return sentinel; }
  Buf& b = bufs_[nbufs_++];
  b.n = n; b.m = m; b.daily = daily; b.count = 0;
  return b;
}

void InMemoryStorage::appendSample(NodeId n, Metric m, bool daily, const Sample& s) {
  Buf& b = bufFor(n, m, daily);
  int cap = daily ? DAILY_CAP : RAW_CAP;
  if (b.count < cap) b.s[b.count++] = s;
  else { memmove(b.s, b.s + 1, (cap - 1) * sizeof(Sample)); b.s[cap - 1] = s; }
}

int InMemoryStorage::loadRing(NodeId n, Metric m, bool daily, Sample* out, int maxOut) {
  Buf* b = findBuf(n, m, daily);
  if (!b) return 0;   // key never written — don't allocate a slot
  int k = b->count < maxOut ? b->count : maxOut;
  for (int i = 0; i < k; i++) out[i] = b->s[i];
  return k;
}

void InMemoryStorage::appendPumpEvent(const PumpEvent& e) {
  if (npumps_ < 256) pumps_[npumps_++] = e;
}
int InMemoryStorage::loadPumpEvents(PumpEvent* out, int maxOut) {
  int k = npumps_ < maxOut ? npumps_ : maxOut;
  for (int i = 0; i < k; i++) out[i] = pumps_[i];
  return k;
}
