# Mission 2 — Local Time-Series Storage Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the mock `UiModel` on the display node with a real, persistent local time-series store (multi-resolution ring buffers on SD), fed by a built-in simulator, behind a Repository seam that can later be swapped for a cloud-backed cache.

**Architecture:** All testable data-layer logic is pure C++ in `lib/TimeSeries/` (wraps caller-provided buffers, zero Arduino deps — same pattern as `Canvas1`), unit-tested natively with PlatformIO's Unity runner. SD persistence and the UI-building repository live in `src/data/` behind interfaces; the device-only SD code is excluded from native builds. The existing `host_sim` preview is extended to render the real pages from a seeded, simulated store so layouts and data are validated on the Mac without flashing.

**Tech Stack:** C++17, PlatformIO (`espressif32`/Arduino for device, `native` for host & tests), Unity test framework, ESP32-S3 (8 MB flash / 8 MB PSRAM), microSD over SPI.

**Reference spec:** [docs/specs/2026-06-16-mission2-timeseries-storage-design.md](../specs/2026-06-16-mission2-timeseries-storage-design.md)

---

## File Structure (decomposition)

**`lib/TimeSeries/` — pure, host-testable data layer (no Arduino, no SD):**
- `metrics.h` — `NodeId`/`Metric`/`Window` enums, fixed-point `encode`/`decode`, the series registry table, time constants.
- `Sample.h` — packed on-disk record types (`Sample`, `PumpEvent`).
- `Ring.h` / `Ring.cpp` — fixed-capacity ring over a caller-provided buffer; round-robin + ordered read + state load.
- `Series.h` / `Series.cpp` — one metric: raw ring (+ optional daily ring); window/day accumulation, downsample-on-write, `latest`.
- `TimeSeriesStore.h` / `TimeSeriesStore.cpp` — registry of `Series` keyed by `(node, metric)`; aggregate queries.
- `Clock.h` / `Clock.cpp` — soft clock: epoch baseline + injected `millis`.
- `PumpLog.h` / `PumpLog.cpp` — pump event ring; run pairing, minutes-today, avg/day, dangling-START handling.
- `SeriesStorage.h` — abstract persistence interface (load/save a ring; append/read pump events).
- `InMemoryStorage.h` / `.cpp` — pure no-op/RAM storage impl (tests + the "no SD card" fallback).
- `SimSource.h` / `SimSource.cpp` — seeding (~30 d daily + ~48 h raw) and the live tick model.

**`src/data/` — app glue (some Arduino, excluded from native where noted):**
- `GardenRepository.h` / `.cpp` — `IGardenRepository` + `LocalRepository`: builds a `UiModel` from store + pump log + clock; computes all derived values. Pure C++ (compiles native).
- `SdStorage.h` / `SdStorage.cpp` — `SeriesStorage` impl over the Arduino `SD` lib. **Device-only**, excluded from `host_sim` and `native_test`.

**Modified:**
- `platformio.ini` — add `env:native_test`; extend `env:host_sim` to compile `src/data/` (minus `SdStorage`).
- `src/host_preview.cpp` — build a seeded store + repository, render the 3 pages from real data.
- `src/apps/display_controller_app.cpp` — wire SD storage + store + seed + repository; replace `mockModel()`; route the Page-3 toggle through the repository.

**Tests (`test/`, one folder per suite — PlatformIO convention):**
`test/test_ring/`, `test/test_series/`, `test/test_store/`, `test/test_clock/`, `test/test_pumplog/`, `test/test_sim/`.

### Cross-cutting constants (defined once in `metrics.h`)
```cpp
static const uint32_t RAW_INTERVAL_S = 900;    // 15 min
static const uint32_t FINE_INTERVAL_S = 30;    // while pumping
static const uint32_t DAY_S          = 86400;
static const uint16_t RAW_CAP        = 192;    // 48 h of 15-min points
static const uint16_t DAILY_CAP      = 400;    // ~13 months of daily points
```
Endianness note: ESP32-S3 and the dev Mac are both little-endian, so packed records are byte-compatible across device/host. Documented here so nobody adds a big-endian target without revisiting it.

---

## Chunk 1: Test harness + metrics foundation

### Task 1.1: Add a native Unity test environment

**Files:**
- Modify: `platformio.ini` (append a new env)

- [ ] **Step 1: Add the test env**

Append to `platformio.ini`:
```ini
; --------------------------------------------------------------------------
; Native unit tests — pure data layer in lib/TimeSeries (Unity runner).
; Compiles NO src/ app code; only lib/ pulled in by LDF + the test files.
; --------------------------------------------------------------------------
[env:native_test]
platform = native
test_framework = unity
build_src_filter = -<*>
lib_ignore = CrowPanelEPD
build_flags =
  -std=gnu++17
  -D HOST_TEST
```

- [ ] **Step 2: Add a smoke test proving the runner works**

Create `test/test_ring/test_ring.cpp`:
```cpp
#include <unity.h>

void setUp() {}
void tearDown() {}

static void test_harness_runs() {
  TEST_ASSERT_EQUAL_INT(2, 1 + 1);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_harness_runs);
  return UNITY_END();
}
```

- [ ] **Step 3: Run the test, expect PASS**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_ring`
Expected: `test_harness_runs ... PASS`, `1 Tests 0 Failures`.

- [ ] **Step 4: Commit**
```bash
git add platformio.ini test/test_ring/test_ring.cpp
git commit -m "Mission 2: native Unity test env + smoke test"
```

### Task 1.2: Metrics, samples, encode/decode

**Files:**
- Create: `lib/TimeSeries/metrics.h`, `lib/TimeSeries/Sample.h`
- Test: `test/test_store/test_metrics.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_store/test_metrics.cpp`:
```cpp
#include <unity.h>
#include "metrics.h"

void setUp() {} void tearDown() {}

static void test_temp_roundtrip() {
  int16_t enc = ts::encode(ts::M_AIR_TEMP, 22.4f);   // ×10
  TEST_ASSERT_EQUAL_INT16(224, enc);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 22.4f, ts::decode(ts::M_AIR_TEMP, enc));
}
static void test_lux_scale_no_overflow() {
  int16_t enc = ts::encode(ts::M_LUX, 100000.0f);    // ÷4 = 25000 < 32767
  TEST_ASSERT_EQUAL_INT16(25000, enc);
  TEST_ASSERT_FLOAT_WITHIN(4.0f, 100000.0f, ts::decode(ts::M_LUX, enc));
}
static void test_soil_identity() {
  TEST_ASSERT_EQUAL_INT16(41, ts::encode(ts::M_SOIL, 41.0f));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_temp_roundtrip);
  RUN_TEST(test_lux_scale_no_overflow);
  RUN_TEST(test_soil_identity);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, expect FAIL** (compile error: `metrics.h` not found)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 3: Implement `Sample.h`**

Create `lib/TimeSeries/Sample.h`:
```cpp
#pragma once
#include <stdint.h>

#pragma pack(push, 1)
// One time-series point. 6 bytes on disk and in RAM.
struct Sample { uint32_t ts; int16_t value; };

// One pump event. Self-contained audit: soil + threshold at the moment.
struct PumpEvent {
  uint32_t ts;
  uint8_t  pumpId;
  uint8_t  event;        // EV_START / EV_STOP
  int16_t  soilPct;      // freshest soil reading at the event
  int16_t  thresholdPct; // dry-threshold on START, target on STOP
};
#pragma pack(pop)

enum PumpEventType : uint8_t { EV_START = 0, EV_STOP = 1 };
```

- [ ] **Step 4: Implement `metrics.h`**

Create `lib/TimeSeries/metrics.h`:
```cpp
#pragma once
#include <stdint.h>
#include <math.h>
#include "Sample.h"

namespace ts {

enum NodeId : uint8_t {
  NODE_BEET1 = 0, NODE_BEET2, NODE_BEET3, NODE_GEWAECHSHAUS,
  NODE_ENV, NODE_COUNT
};
enum Metric : uint8_t {
  M_SOIL = 0, M_SOIL_TEMP, M_AIR_TEMP, M_PRESSURE, M_HUMIDITY, M_LUX, M_COUNT
};
enum Window : uint8_t { W_12H = 0, W_7D, W_MONTH };

static const uint32_t RAW_INTERVAL_S  = 900;
static const uint32_t FINE_INTERVAL_S = 30;
static const uint32_t DAY_S           = 86400;
static const uint16_t RAW_CAP         = 192;
static const uint16_t DAILY_CAP       = 400;

// Fixed-point scale per metric: stored = round(real * mul / div).
struct Scale { int16_t mul; int16_t div; };
inline Scale scaleOf(Metric m) {
  switch (m) {
    case M_AIR_TEMP:
    case M_SOIL_TEMP: return {10, 1};
    case M_LUX:       return {1, 4};
    default:          return {1, 1}; // soil %, pressure hPa, humidity %
  }
}
inline int16_t encode(Metric m, float real) {
  Scale s = scaleOf(m);
  return (int16_t) lroundf(real * s.mul / s.div);
}
inline float decode(Metric m, int16_t stored) {
  Scale s = scaleOf(m);
  return (float) stored * s.div / s.mul;
}

// Series registry: which (node, metric) exist and whether they keep a daily tier.
struct SeriesCfg { NodeId node; Metric metric; bool hasDaily; };
inline const SeriesCfg* registry(int& outCount) {
  static const SeriesCfg cfg[] = {
    {NODE_BEET1,        M_SOIL,      true },  {NODE_BEET1,        M_SOIL_TEMP, false},
    {NODE_BEET2,        M_SOIL,      true },  {NODE_BEET2,        M_SOIL_TEMP, false},
    {NODE_BEET3,        M_SOIL,      true },  {NODE_BEET3,        M_SOIL_TEMP, false},
    {NODE_GEWAECHSHAUS, M_SOIL,      true },  {NODE_GEWAECHSHAUS, M_SOIL_TEMP, false},
    {NODE_ENV,          M_AIR_TEMP,  true },  {NODE_ENV,          M_PRESSURE,  false},
    {NODE_ENV,          M_HUMIDITY,  false},  {NODE_ENV,          M_LUX,       false},
  };
  outCount = sizeof(cfg) / sizeof(cfg[0]);
  return cfg;
}

} // namespace ts
```

- [ ] **Step 5: Run, expect PASS** (3 tests)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 6: Commit**
```bash
git add lib/TimeSeries/metrics.h lib/TimeSeries/Sample.h test/test_store/test_metrics.cpp
git commit -m "Mission 2: metric enums, packed records, fixed-point encode/decode"
```

---

## Chunk 2: Ring buffer

### Task 2.1: Round-robin ring over a caller-provided buffer

**Files:**
- Create: `lib/TimeSeries/Ring.h`, `lib/TimeSeries/Ring.cpp`
- Test: `test/test_ring/test_ring.cpp` (replace the smoke test)

- [ ] **Step 1: Write the failing tests** — replace `test/test_ring/test_ring.cpp`:
```cpp
#include <unity.h>
#include "Ring.h"

void setUp() {} void tearDown() {}

static Sample slots[4];

static void test_fill_then_read_in_order() {
  Ring r(slots, 4);
  for (uint16_t i = 0; i < 3; i++) r.push({(uint32_t)(100 + i), (int16_t)i});
  TEST_ASSERT_EQUAL_UINT16(3, r.size());
  TEST_ASSERT_EQUAL_UINT32(100, r.at(0).ts);   // oldest
  TEST_ASSERT_EQUAL_UINT32(102, r.at(2).ts);   // newest
  TEST_ASSERT_EQUAL_INT16(2, r.newest().value);
}

static void test_wrap_overwrites_oldest() {
  Ring r(slots, 4);
  for (uint16_t i = 0; i < 6; i++) r.push({(uint32_t)i, (int16_t)i}); // 0..5 into cap 4
  TEST_ASSERT_EQUAL_UINT16(4, r.size());        // saturated
  TEST_ASSERT_EQUAL_UINT32(2, r.at(0).ts);      // 0 and 1 overwritten
  TEST_ASSERT_EQUAL_UINT32(5, r.at(3).ts);
}

static void test_load_restores_state() {
  Ring r(slots, 4);
  slots[0] = {10, 1}; slots[1] = {20, 2};
  r.load(/*head=*/2, /*count=*/2);
  TEST_ASSERT_EQUAL_UINT16(2, r.size());
  TEST_ASSERT_EQUAL_UINT32(10, r.at(0).ts);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fill_then_read_in_order);
  RUN_TEST(test_wrap_overwrites_oldest);
  RUN_TEST(test_load_restores_state);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, expect FAIL** (`Ring.h` not found)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_ring`

- [ ] **Step 3: Implement `Ring.h`**
```cpp
#pragma once
#include <stdint.h>
#include "Sample.h"

// Fixed-capacity round-robin ring over a buffer the caller owns (Canvas1 pattern).
// at(0) is the oldest live sample, at(size()-1) the newest.
class Ring {
public:
  Ring(Sample* buf, uint16_t capacity);
  void push(const Sample& s);
  uint16_t size() const;            // min(count, capacity)
  const Sample& at(uint16_t i) const;
  const Sample& newest() const;
  bool empty() const { return count_ == 0; }

  // Persistence helpers: head/count are the only mutable state beyond the buffer.
  uint16_t head() const { return head_; }
  uint16_t rawCount() const { return count_; }
  uint16_t capacity() const { return cap_; }
  void load(uint16_t head, uint16_t count);

private:
  Sample*  buf_;
  uint16_t cap_;
  uint16_t head_  = 0;  // next write index
  uint16_t count_ = 0;  // total pushes, saturates at cap_
};
```

- [ ] **Step 4: Implement `Ring.cpp`**
```cpp
#include "Ring.h"

Ring::Ring(Sample* buf, uint16_t capacity) : buf_(buf), cap_(capacity) {}

void Ring::push(const Sample& s) {
  buf_[head_] = s;
  head_ = (uint16_t)((head_ + 1) % cap_);
  if (count_ < cap_) count_++;
}

uint16_t Ring::size() const { return count_ < cap_ ? count_ : cap_; }

const Sample& Ring::at(uint16_t i) const {
  // oldest lives at (head_ - size + i) modulo cap_
  uint16_t start = (uint16_t)((head_ + cap_ - size()) % cap_);
  return buf_[(uint16_t)((start + i) % cap_)];
}

const Sample& Ring::newest() const { return at((uint16_t)(size() - 1)); }

void Ring::load(uint16_t head, uint16_t count) {
  head_ = head % cap_;
  count_ = count > cap_ ? cap_ : count;
}
```

- [ ] **Step 5: Run, expect PASS** (3 tests)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_ring`

- [ ] **Step 6: Commit**
```bash
git add lib/TimeSeries/Ring.h lib/TimeSeries/Ring.cpp test/test_ring/test_ring.cpp
git commit -m "Mission 2: Ring buffer (round-robin, ordered read, load)"
```

---

## Chunk 3: Series (downsample-on-write + fine-tier rollup)

### Task 3.1: Series with raw ring, daily rollup, and interval averaging

**Files:**
- Create: `lib/TimeSeries/Series.h`, `lib/TimeSeries/Series.cpp`
- Test: `test/test_series/test_series.cpp`

Behaviour (from spec §3.3 and §6):
- `add(ts, value)` ingests a sample (coarse 15-min OR fine 30-s — Series doesn't care).
- A raw point is the **average of every sample whose ts falls in the same 15-min window**. The window finalizes (one raw point pushed) when a sample arrives in a later window.
- When a finalized raw point belongs to a new day, the previous day's average of raw points is pushed to the daily ring.
- `latest()` returns the freshest sample value (including the not-yet-finalized window), so the pump controller sees fresh data.

- [ ] **Step 1: Write the failing tests** — `test/test_series/test_series.cpp`:
```cpp
#include <unity.h>
#include "Series.h"
#include "metrics.h"
using namespace ts;

void setUp() {} void tearDown() {}

static void test_window_average_one_raw_point() {
  Series s(M_SOIL, /*hasDaily=*/false);
  // three fine samples inside the same 15-min window -> one raw point = avg
  s.add(0,  encode(M_SOIL, 40));
  s.add(30, encode(M_SOIL, 44));
  s.add(60, encode(M_SOIL, 42));
  // not finalized yet -> raw empty, but latest reflects freshest
  TEST_ASSERT_EQUAL_UINT16(0, s.rawSize());
  TEST_ASSERT_EQUAL_INT16(42, s.latest());
  // a sample in the NEXT window finalizes the previous one
  s.add(RAW_INTERVAL_S + 1, encode(M_SOIL, 50));
  TEST_ASSERT_EQUAL_UINT16(1, s.rawSize());
  TEST_ASSERT_EQUAL_INT16(42, s.rawNewest().value);  // (40+44+42)/3
  TEST_ASSERT_EQUAL_UINT32(0, s.rawNewest().ts);     // aligned to window start
}

static void test_day_rollup() {
  Series s(M_AIR_TEMP, /*hasDaily=*/true);
  // A day only closes when a *finalized raw point* lands in a later day. So we
  // need day-0 raw points AND a day-1 raw point to actually finalize day 0.
  s.add(0,                     encode(M_AIR_TEMP, 10.0f)); // opens window @0 (day0)
  s.add(RAW_INTERVAL_S,        encode(M_AIR_TEMP, 20.0f)); // finalizes raw @0 (=10), opens day0 acc
  s.add(DAY_S,                 encode(M_AIR_TEMP, 30.0f)); // finalizes raw @900 (=20, still day0)
  s.add(DAY_S + RAW_INTERVAL_S,encode(M_AIR_TEMP, 40.0f)); // finalizes raw @DAY_S (day1) -> closes day0
  // day 0 daily avg = avg of its raw points (10, 20) = 15
  TEST_ASSERT_EQUAL_UINT16(1, s.dailySize());
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 15.0f, decode(M_AIR_TEMP, s.dailyNewest().value));
  TEST_ASSERT_EQUAL_UINT32(0, s.dailyNewest().ts);   // aligned to day start
}

static void test_no_daily_when_disabled() {
  Series s(M_SOIL, /*hasDaily=*/false);
  s.add(0, encode(M_SOIL, 40));
  s.add(DAY_S, encode(M_SOIL, 50));
  TEST_ASSERT_EQUAL_UINT16(0, s.dailySize());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_window_average_one_raw_point);
  RUN_TEST(test_day_rollup);
  RUN_TEST(test_no_daily_when_disabled);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, expect FAIL**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_series`

- [ ] **Step 3: Implement `Series.h`**
```cpp
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
```

- [ ] **Step 4: Implement `Series.cpp`**
```cpp
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
```

- [ ] **Step 5: Run, expect PASS** (3 tests)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_series`

- [ ] **Step 6: Commit**
```bash
git add lib/TimeSeries/Series.h lib/TimeSeries/Series.cpp test/test_series/test_series.cpp
git commit -m "Mission 2: Series with window averaging + daily downsample-on-write"
```

---

## Chunk 4: TimeSeriesStore (registry + queries)

### Task 4.1: Store registry and `latest`

**Files:**
- Create: `lib/TimeSeries/TimeSeriesStore.h`, `lib/TimeSeries/TimeSeriesStore.cpp`
- Test: `test/test_store/test_store.cpp`

- [ ] **Step 1: Write the failing tests** — `test/test_store/test_store.cpp`:
```cpp
#include <unity.h>
#include "TimeSeriesStore.h"
#include "metrics.h"
using namespace ts;

void setUp() {} void tearDown() {}

static void test_registers_all_series() {
  TimeSeriesStore store; store.init();
  int n; registry(n);
  TEST_ASSERT_EQUAL_INT(n, store.seriesCount());
  TEST_ASSERT_NOT_NULL(store.find(NODE_BEET1, M_SOIL));
  TEST_ASSERT_NULL(store.find(NODE_ENV, M_SOIL));   // env has no soil
}

static void test_add_and_latest() {
  TimeSeriesStore store; store.init();
  store.add(NODE_BEET1, M_SOIL, 100, encode(M_SOIL, 41));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 41.0f, store.latest(NODE_BEET1, M_SOIL));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_registers_all_series);
  RUN_TEST(test_add_and_latest);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, expect FAIL**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 3: Implement `TimeSeriesStore.h`**
```cpp
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
```

- [ ] **Step 4: Implement `TimeSeriesStore.cpp`** (registry + `latest`; queries land in Task 4.2)
```cpp
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
```

- [ ] **Step 5: Run, expect PASS** (2 tests)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 6: Commit**
```bash
git add lib/TimeSeries/TimeSeriesStore.h lib/TimeSeries/TimeSeriesStore.cpp test/test_store/test_store.cpp
git commit -m "Mission 2: TimeSeriesStore registry + latest()"
```

### Task 4.2: Window resampling, cross-node average, today min/max, trend

> Note: spec §4 lists `trend(metric)`; this plan uses the fuller `trend(node, metric, now)` (the env node + a clock are needed to actually compute it). Intentional refinement, not scope creep.

**Files:**
- Modify: `lib/TimeSeries/TimeSeriesStore.cpp` (add query bodies)
- Modify: `test/test_store/test_store.cpp` (add tests + RUN_TEST lines)

- [ ] **Step 1: Add the failing tests** to `test/test_store/test_store.cpp` (and add their `RUN_TEST` lines in `main`):
```cpp
static void test_sample_window_buckets_to_n_points() {
  TimeSeriesStore store; store.init();
  // 7 daily points (values 10..16) ending "today"; ask for 7 points over 7 days
  uint32_t now = 7 * DAY_S + 100;
  Series* s = store.find(NODE_BEET1, M_SOIL);
  for (int d = 0; d < 7; d++) s->pushDailyDirect({(uint32_t)(d * DAY_S), (int16_t)(10 + d)});
  float out[7];
  int got = store.sampleWindow(NODE_BEET1, M_SOIL, W_7D, now, out, 7);
  TEST_ASSERT_EQUAL_INT(7, got);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 10.0f, out[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 16.0f, out[6]);
}

static void test_average_across_nodes() {
  TimeSeriesStore store; store.init();
  uint32_t now = 1 * DAY_S + 100;
  store.find(NODE_BEET1, M_SOIL)->pushDailyDirect({0, 40});
  store.find(NODE_BEET2, M_SOIL)->pushDailyDirect({0, 50});
  store.find(NODE_BEET3, M_SOIL)->pushDailyDirect({0, 30});
  store.find(NODE_GEWAECHSHAUS, M_SOIL)->pushDailyDirect({0, 20});
  float out[1];
  store.averageAcrossNodes(M_SOIL, W_7D, now, out, 1);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 35.0f, out[0]);  // (40+50+30+20)/4
}

static void test_min_max_today() {
  TimeSeriesStore store; store.init();
  uint32_t now = DAY_S + 12 * 3600;       // midday of day 1
  Series* s = store.find(NODE_ENV, M_AIR_TEMP);
  s->pushRawDirect({DAY_S + 1 * 3600, encode(M_AIR_TEMP, 14.0f)});
  s->pushRawDirect({DAY_S + 6 * 3600, encode(M_AIR_TEMP, 25.0f)});
  s->pushRawDirect({1 * 3600,         encode(M_AIR_TEMP, 99.0f)});  // yesterday — ignored
  float lo, hi;
  TEST_ASSERT_TRUE(store.minMaxToday(NODE_ENV, M_AIR_TEMP, now, lo, hi));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 14.0f, lo);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, hi);
}

static void test_trend_rising() {
  TimeSeriesStore store; store.init();
  uint32_t now = 6 * 3600;
  Series* s = store.find(NODE_ENV, M_PRESSURE);
  s->pushRawDirect({0,            encode(M_PRESSURE, 1008)});
  s->pushRawDirect({5 * 3600,     encode(M_PRESSURE, 1014)});
  TEST_ASSERT_EQUAL_INT(1, store.trend(NODE_ENV, M_PRESSURE, now));
}
```

- [ ] **Step 2: Run, expect FAIL** (linker/compile: queries unimplemented)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 3: Implement the query bodies** — append to `TimeSeriesStore.cpp`:
```cpp
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
  uint32_t start = (now > span) ? now - span : 0;

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
```

- [ ] **Step 4: Run, expect PASS** (6 tests total in this suite)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 5: Commit**
```bash
git add lib/TimeSeries/TimeSeriesStore.cpp test/test_store/test_store.cpp
git commit -m "Mission 2: store queries — window resample, cross-node avg, today min/max, trend"
```

---

## Chunk 5: Clock + PumpLog

### Task 5.1: Soft clock with injected millis

**Files:**
- Create: `lib/TimeSeries/Clock.h`, `lib/TimeSeries/Clock.cpp`
- Test: `test/test_clock/test_clock.cpp`

- [ ] **Step 1: Write the failing test** — `test/test_clock/test_clock.cpp`:
```cpp
#include <unity.h>
#include "Clock.h"

void setUp() {} void tearDown() {}

static uint32_t g_fakeMillis = 0;
static uint32_t fakeMillis() { return g_fakeMillis; }

static void test_now_advances_with_millis() {
  g_fakeMillis = 5000;
  Clock clk(fakeMillis);
  clk.setEpoch(1000);            // epoch 1000 at millis 5000
  g_fakeMillis = 5000 + 90000;   // +90 s
  TEST_ASSERT_EQUAL_UINT32(1090, clk.now());
}

static void test_set_epoch_rebases() {
  g_fakeMillis = 0;
  Clock clk(fakeMillis);
  clk.setEpoch(2000);
  g_fakeMillis = 10000;          // +10 s
  TEST_ASSERT_EQUAL_UINT32(2010, clk.now());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_now_advances_with_millis);
  RUN_TEST(test_set_epoch_rebases);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, expect FAIL**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_clock`

- [ ] **Step 3: Implement `Clock.h`**
```cpp
#pragma once
#include <stdint.h>

// Soft wall-clock: an epoch baseline advanced by an injected millis source.
// Device passes Arduino millis(); tests pass a fake. Later the *source* of the
// baseline (gateway time-sync / NTP) changes — now() does not.
class Clock {
public:
  explicit Clock(uint32_t (*millisFn)());
  void setEpoch(uint32_t epochSeconds);  // rebase: this epoch == "right now"
  uint32_t now() const;                  // current epoch seconds
  uint32_t baseEpoch() const { return baseEpoch_; }

private:
  uint32_t (*millisFn_)();
  uint32_t baseEpoch_  = 0;
  uint32_t baseMillis_ = 0;
};
```

- [ ] **Step 4: Implement `Clock.cpp`**
```cpp
#include "Clock.h"

Clock::Clock(uint32_t (*millisFn)()) : millisFn_(millisFn) {}

void Clock::setEpoch(uint32_t epochSeconds) {
  baseEpoch_  = epochSeconds;
  baseMillis_ = millisFn_();
}

uint32_t Clock::now() const {
  return baseEpoch_ + (millisFn_() - baseMillis_) / 1000;
}
```

- [ ] **Step 5: Run, expect PASS** (2 tests)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_clock`

- [ ] **Step 6: Commit**
```bash
git add lib/TimeSeries/Clock.h lib/TimeSeries/Clock.cpp test/test_clock/test_clock.cpp
git commit -m "Mission 2: soft Clock with injected millis source"
```

### Task 5.2: PumpLog — events, run pairing, dangling START

**Files:**
- Create: `lib/TimeSeries/PumpLog.h`, `lib/TimeSeries/PumpLog.cpp`
- Test: `test/test_pumplog/test_pumplog.cpp`

- [ ] **Step 1: Write the failing tests** — `test/test_pumplog/test_pumplog.cpp`:
```cpp
#include <unity.h>
#include "PumpLog.h"
#include "metrics.h"

void setUp() {} void tearDown() {}

static void test_minutes_today_paired_run() {
  PumpLog log;
  // pump 0 ran 06:00 -> 06:04 today (day 1)
  uint32_t day = ts::DAY_S;
  log.append({day + 6 * 3600,        0, EV_START, 30, 35});
  log.append({day + 6 * 3600 + 240,  0, EV_STOP,  36, 35});
  uint32_t now = day + 12 * 3600;
  TEST_ASSERT_EQUAL_INT(4, log.minutesToday(0, now));
}

static void test_dangling_start_counts_until_now() {
  PumpLog log;
  uint32_t day = ts::DAY_S;
  log.append({day + 6 * 3600, 0, EV_START, 30, 35});  // no STOP yet
  uint32_t now = day + 6 * 3600 + 120;                // 2 min later
  TEST_ASSERT_EQUAL_INT(2, log.minutesToday(0, now));
  TEST_ASSERT_TRUE(log.isRunning(0, now));
}

static void test_other_pump_isolated() {
  PumpLog log;
  uint32_t day = ts::DAY_S;
  log.append({day + 1 * 3600, 1, EV_START, 20, 35});
  log.append({day + 1 * 3600 + 60, 1, EV_STOP, 36, 35});
  TEST_ASSERT_EQUAL_INT(0, log.minutesToday(0, day + 12 * 3600));
  TEST_ASSERT_FALSE(log.isRunning(0, day + 12 * 3600));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_minutes_today_paired_run);
  RUN_TEST(test_dangling_start_counts_until_now);
  RUN_TEST(test_other_pump_isolated);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, expect FAIL**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_pumplog`

- [ ] **Step 3: Implement `PumpLog.h`**
```cpp
#pragma once
#include <stdint.h>
#include "Sample.h"
#include "metrics.h"

// Append-only ring of pump events. Durations are derived by pairing START->STOP;
// a dangling START (still running, or STOP lost to reboot) counts until `now`.
class PumpLog {
public:
  static const uint16_t CAP = 256;

  void append(const PumpEvent& e);
  uint16_t size() const { return ring_.size(); }
  const PumpEvent& at(uint16_t i) const { return ring_.at(i); }

  int  minutesToday(uint8_t pumpId, uint32_t now) const;
  int  avgPerDay(uint8_t pumpId, uint32_t now, int days) const;
  bool isRunning(uint8_t pumpId, uint32_t now) const;

private:
  // Sum run-seconds for pumpId within [from, now]; dangling START runs to now.
  uint32_t runSeconds(uint8_t pumpId, uint32_t from, uint32_t now) const;

  PumpEvent buf_[CAP];
  // Reuse Ring's index math via a tiny inline ring of PumpEvent.
  struct EvtRing {
    PumpEvent* b; uint16_t cap, head = 0, count = 0;
    void push(const PumpEvent& e){ b[head]=e; head=(head+1)%cap; if(count<cap)count++; }
    uint16_t size() const { return count<cap?count:cap; }
    const PumpEvent& at(uint16_t i) const {
      uint16_t s=(head+cap-size())%cap; return b[(s+i)%cap];
    }
  } ring_{buf_, CAP};
};
```

- [ ] **Step 2 note:** `EvtRing` mirrors `Ring` but for `PumpEvent`; kept inline to avoid templating `Ring` this mission (YAGNI — revisit if a third record type appears).

- [ ] **Step 4: Implement `PumpLog.cpp`**
```cpp
#include "PumpLog.h"
using namespace ts;

void PumpLog::append(const PumpEvent& e) { ring_.push(e); }

uint32_t PumpLog::runSeconds(uint8_t pumpId, uint32_t from, uint32_t now) const {
  uint32_t total = 0;
  bool open = false; uint32_t startTs = 0;
  for (uint16_t i = 0; i < ring_.size(); i++) {
    const PumpEvent& e = ring_.at(i);
    if (e.pumpId != pumpId) continue;
    if (e.event == EV_START) { open = true; startTs = e.ts; }
    else if (e.event == EV_STOP && open) {
      uint32_t a = startTs < from ? from : startTs;
      if (e.ts > a) total += e.ts - a;
      open = false;
    }
  }
  if (open) {  // dangling START -> runs until now
    uint32_t a = startTs < from ? from : startTs;
    if (now > a) total += now - a;
  }
  return total;
}

int PumpLog::minutesToday(uint8_t pumpId, uint32_t now) const {
  uint32_t dayStart = (now / DAY_S) * DAY_S;
  return (int)(runSeconds(pumpId, dayStart, now) / 60);
}

int PumpLog::avgPerDay(uint8_t pumpId, uint32_t now, int days) const {
  if (days <= 0) return 0;
  uint32_t from = (now > (uint32_t)days * DAY_S) ? now - days * DAY_S : 0;
  return (int)((runSeconds(pumpId, from, now) / 60) / days);
}

bool PumpLog::isRunning(uint8_t pumpId, uint32_t now) const {
  bool open = false;
  for (uint16_t i = 0; i < ring_.size(); i++) {
    const PumpEvent& e = ring_.at(i);
    if (e.pumpId != pumpId) continue;
    open = (e.event == EV_START);
  }
  (void)now;
  return open;
}
```

- [ ] **Step 5: Run, expect PASS** (3 tests)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_pumplog`

- [ ] **Step 6: Commit**
```bash
git add lib/TimeSeries/PumpLog.h lib/TimeSeries/PumpLog.cpp test/test_pumplog/test_pumplog.cpp
git commit -m "Mission 2: PumpLog — run pairing, minutes-today, dangling START"
```

---

## Chunk 6: Persistence interface + storage impls

### Task 6.1: `SeriesStorage` interface + `InMemoryStorage`

**Files:**
- Create: `lib/TimeSeries/SeriesStorage.h`, `lib/TimeSeries/InMemoryStorage.h`, `lib/TimeSeries/InMemoryStorage.cpp`
- Test: `test/test_store/test_storage.cpp`

The interface is what the device `SdStorage` and the test/fallback `InMemoryStorage` both implement. The store calls it write-through on each sample and on boot to reload. `InMemoryStorage` keeps everything in RAM (it IS the "no SD card" fallback — the system runs, just doesn't persist).

- [ ] **Step 1: Write the failing test** — `test/test_store/test_storage.cpp`:
```cpp
#include <unity.h>
#include "InMemoryStorage.h"

void setUp() {} void tearDown() {}

static void test_inmemory_roundtrips_samples() {
  InMemoryStorage st;
  TEST_ASSERT_TRUE(st.begin());
  st.appendSample(ts::NODE_BEET1, ts::M_SOIL, /*daily=*/false, {100, 41});
  st.appendSample(ts::NODE_BEET1, ts::M_SOIL, /*daily=*/false, {200, 42});
  Sample out[8];
  int n = st.loadRing(ts::NODE_BEET1, ts::M_SOIL, false, out, 8);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_UINT32(41, out[0].value);
}

static void test_inmemory_pump_events() {
  InMemoryStorage st; st.begin();
  st.appendPumpEvent({100, 0, EV_START, 30, 35});
  PumpEvent out[8];
  int n = st.loadPumpEvents(out, 8);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_UINT8(EV_START, out[0].event);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_inmemory_roundtrips_samples);
  RUN_TEST(test_inmemory_pump_events);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, expect FAIL**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 3: Implement `SeriesStorage.h`**
```cpp
#pragma once
#include <stdint.h>
#include "Sample.h"
#include "metrics.h"

// Persistence boundary. Device impl = SD files; test/fallback = in-memory.
// The store writes through on each sample and reads back on boot.
class SeriesStorage {
public:
  virtual ~SeriesStorage() {}
  virtual bool begin() = 0;                 // mount; false => caller runs RAM-only

  // Append one finalized point (raw or daily) for a series.
  virtual void appendSample(ts::NodeId, ts::Metric, bool daily, const Sample&) = 0;
  // Load up to maxOut samples, oldest..newest. The store replays them via
  // pushRawDirect/pushDailyDirect to rebuild the ring, so no head/count is needed.
  virtual int  loadRing(ts::NodeId, ts::Metric, bool daily, Sample* out, int maxOut) = 0;

  virtual void appendPumpEvent(const PumpEvent&) = 0;
  virtual int  loadPumpEvents(PumpEvent* out, int maxOut) = 0;

  virtual void saveClock(uint32_t epoch) = 0;
  virtual bool loadClock(uint32_t& epoch) = 0;
};
```

- [ ] **Step 4: Implement `InMemoryStorage.h` / `.cpp`**

`InMemoryStorage.h`:
```cpp
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
  struct Buf { ts::NodeId n; ts::Metric m; bool daily; Sample s[ts::DAILY_CAP]; int count = 0; };
  static const int MAXB = 32;
  Buf  bufs_[MAXB]; int nbufs_ = 0;
  Buf& bufFor(ts::NodeId, ts::Metric, bool daily);

  PumpEvent pumps_[PumpLogCapHint]; int npumps_ = 0;  // see note below
  uint32_t clock_ = 0; bool haveClock_ = false;
};
```
Note: replace `PumpLogCapHint` with a literal `256` (mirrors `PumpLog::CAP`); kept as a plain constant to avoid coupling headers.

`InMemoryStorage.cpp`:
```cpp
#include "InMemoryStorage.h"
#include <string.h>
using namespace ts;

InMemoryStorage::Buf& InMemoryStorage::bufFor(NodeId n, Metric m, bool daily) {
  for (int i = 0; i < nbufs_; i++)
    if (bufs_[i].n == n && bufs_[i].m == m && bufs_[i].daily == daily) return bufs_[i];
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
  Buf& b = bufFor(n, m, daily);
  int k = b.count < maxOut ? b.count : maxOut;
  for (int i = 0; i < k; i++) out[i] = b.s[i];
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
```
Also change the header array declaration to `PumpEvent pumps_[256]; int npumps_ = 0;`.

- [ ] **Step 5: Run, expect PASS** (2 tests)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 6: Commit**
```bash
git add lib/TimeSeries/SeriesStorage.h lib/TimeSeries/InMemoryStorage.h lib/TimeSeries/InMemoryStorage.cpp test/test_store/test_storage.cpp
git commit -m "Mission 2: SeriesStorage interface + InMemoryStorage (tests + RAM fallback)"
```

### Task 6.2: Wire write-through + reload into the store

**Files:**
- Modify: `lib/TimeSeries/TimeSeriesStore.h` / `.cpp` (accept a `SeriesStorage*`, write-through on finalize, `reload()`)
- Modify: `lib/TimeSeries/Series.h` / `.cpp` (notify a callback when a raw/daily point finalizes)
- Modify: `test/test_store/test_store.cpp` (add a write-through test)

Approach: `Series` gets an optional sink callback (a function pointer + `void* ctx`, no Arduino/storage type — keeps `Series` pure) invoked from `finalizeWindow`/`finalizeDay`. The store sets the sink to a trampoline that forwards to `SeriesStorage::appendSample`. Only **finalized** points fire the sink, so only they persist; the in-progress 15-min window (and the open day accumulator) are intentionally **not** persisted — they're cheap to rebuild from live data and `latest()` already reflects the freshest sample. `pushRawDirect`/`pushDailyDirect` deliberately do **not** fire the sink (used by `reload()` to avoid re-persisting, and by seeding which persists once via `persistAll()`).

- [ ] **Step 1: Add the failing test** to `test/test_store/test_store.cpp` (+ `RUN_TEST`):
```cpp
#include "InMemoryStorage.h"
static void test_writethrough_persists_finalized_points() {
  InMemoryStorage st; st.begin();
  TimeSeriesStore store; store.init(&st);
  store.add(NODE_BEET1, M_SOIL, 0, encode(M_SOIL, 40));
  store.add(NODE_BEET1, M_SOIL, RAW_INTERVAL_S + 1, encode(M_SOIL, 50)); // finalizes window @0
  Sample out[8];
  int n = st.loadRing(NODE_BEET1, M_SOIL, false, out, 8);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT16(40, out[0].value);
}
```

- [ ] **Step 2: Run, expect FAIL** (`init` takes no arg yet)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 3: Implement** — in `Series.h` add:
```cpp
  using Sink = void(*)(void* ctx, ts::NodeId, ts::Metric, bool daily, const Sample&);
  void setSink(Sink fn, void* ctx, ts::NodeId node) { sink_ = fn; sinkCtx_ = ctx; node_ = node; }
```
and members `Sink sink_ = nullptr; void* sinkCtx_ = nullptr; ts::NodeId node_ = ts::NODE_COUNT;`
In `Series.cpp`, at the end of `finalizeWindow` (after `raw_.push`) call `if (sink_) sink_(sinkCtx_, node_, metric_, false, {windowStart, avg});` and in `finalizeDay` after `daily_.push` call `if (sink_) sink_(sinkCtx_, node_, metric_, true, {dayKey_*DAY_S, avg});`.

In `TimeSeriesStore.h`: add `#include "SeriesStorage.h"` (forward-declaring is enough, but the include keeps it simple); change `void init();` → `void init(SeriesStorage* storage = nullptr);`; add `void reload();` and `void persistAll();`; add a member `SeriesStorage* storage_ = nullptr;` and declare `static void sinkTrampoline(void* ctx, ts::NodeId, ts::Metric, bool daily, const Sample&);`.
In `TimeSeriesStore.cpp` `init`: store `storage_`, and after constructing each Series call `series_[count_]->setSink(&TimeSeriesStore::sinkTrampoline, this, cfg[i].node);`. Add:
```cpp
void TimeSeriesStore::sinkTrampoline(void* ctx, NodeId n, Metric m, bool daily, const Sample& s) {
  auto* self = static_cast<TimeSeriesStore*>(ctx);
  if (self->storage_) self->storage_->appendSample(n, m, daily, s);
}
void TimeSeriesStore::reload() {
  if (!storage_) return;
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
```
(declare `static void sinkTrampoline(...)` in the header.)

- [ ] **Step 4: Run, expect PASS** (all test_store suites green)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_store`

- [ ] **Step 5: Commit**
```bash
git add lib/TimeSeries/Series.h lib/TimeSeries/Series.cpp lib/TimeSeries/TimeSeriesStore.h lib/TimeSeries/TimeSeriesStore.cpp test/test_store/test_store.cpp
git commit -m "Mission 2: write-through persistence + reload via Series sink"
```

### Task 6.3: `SdStorage` (device-only) — defined, not unit-tested

**Files:**
- Create: `src/data/SdStorage.h`, `src/data/SdStorage.cpp`

`SdStorage` implements `SeriesStorage` over the Arduino `SD` library. It is **never compiled natively** (excluded from `host_sim`/`native_test`), so it has no unit test; it is exercised on hardware in Chunk 9. Files live under `/garden/` per spec §8.

- [ ] **Step 1: Implement `SdStorage.h`**
```cpp
#pragma once
#include "SeriesStorage.h"

// SeriesStorage over microSD (SPI). Each ring = one fixed-size binary file with
// a small header {magic,version,recordSize,capacity,head,count} + slots.
// Device-only: excluded from native builds. begin() returns false if no card.
class SdStorage : public SeriesStorage {
public:
  bool begin() override;
  void appendSample(ts::NodeId, ts::Metric, bool daily, const Sample&) override;
  int  loadRing(ts::NodeId, ts::Metric, bool daily, Sample* out, int maxOut) override;
  void appendPumpEvent(const PumpEvent&) override;
  int  loadPumpEvents(PumpEvent* out, int maxOut) override;
  void saveClock(uint32_t epoch) override;
  bool loadClock(uint32_t& epoch) override;
private:
  bool ok_ = false;
  void pathFor(ts::NodeId, ts::Metric, bool daily, char* out, int n) const;
};
```

- [ ] **Step 2: Implement `SdStorage.cpp`**

Use `#include <SD.h>` / `#include <SPI.h>`. The SD shares the SPI bus; reuse the bus already brought up for the EPD (SCK 12, MOSI 11 per the hardware notes) with the SD card's own CS. Confirm the CrowPanel SD CS pin from the Elecrow pinout during Chunk 9 bring-up and set `SD_CS` accordingly (it is not one of the EPD pins 12/11/47/46/45/48).
```cpp
#include "SdStorage.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
using namespace ts;

static const int SD_CS = 10;   // TODO(Chunk 9): confirm against Elecrow CrowPanel pinout
static const uint16_t MAGIC = 0xC1A0;

#pragma pack(push,1)
struct RingHeader { uint16_t magic; uint8_t version; uint8_t recordSize;
                    uint16_t capacity; uint16_t head; uint16_t count; };
#pragma pack(pop)

bool SdStorage::begin() {
  if (!SD.begin(SD_CS)) { ok_ = false; return false; }
  SD.mkdir("/garden");
  ok_ = true;
  return true;
}

void SdStorage::pathFor(NodeId n, Metric m, bool daily, char* out, int len) const {
  snprintf(out, len, "/garden/%u_%u_%s.bin", (unsigned)n, (unsigned)m, daily ? "day" : "raw");
}

void SdStorage::appendSample(NodeId n, Metric m, bool daily, const Sample& s) {
  if (!ok_) return;
  char path[48]; pathFor(n, m, daily, path, sizeof(path));
  uint16_t cap = daily ? DAILY_CAP : RAW_CAP;
  File f = SD.open(path, FILE_WRITE);          // opens R/W, created if absent
  if (!f) return;
  RingHeader h;
  if (f.size() >= (int)sizeof(h)) { f.seek(0); f.read((uint8_t*)&h, sizeof(h)); }
  else { h = {MAGIC, 1, (uint8_t)sizeof(Sample), cap, 0, 0}; }
  uint32_t slotOff = sizeof(h) + (uint32_t)h.head * sizeof(Sample);
  f.seek(slotOff); f.write((const uint8_t*)&s, sizeof(Sample));
  h.head = (h.head + 1) % cap;
  if (h.count < cap) h.count++;
  f.seek(0); f.write((const uint8_t*)&h, sizeof(h));
  f.close();
}

int SdStorage::loadRing(NodeId n, Metric m, bool daily, Sample* out, int maxOut) {
  if (!ok_) return 0;
  char path[48]; pathFor(n, m, daily, path, sizeof(path));
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;
  RingHeader h;
  if (f.size() < (int)sizeof(h)) { f.close(); return 0; }
  f.read((uint8_t*)&h, sizeof(h));
  if (h.magic != MAGIC) { f.close(); return 0; }
  uint16_t size = h.count < h.capacity ? h.count : h.capacity;
  uint16_t start = (h.head + h.capacity - size) % h.capacity;
  int k = 0;
  for (uint16_t i = 0; i < size && k < maxOut; i++, k++) {
    uint16_t slot = (start + i) % h.capacity;
    f.seek(sizeof(h) + (uint32_t)slot * sizeof(Sample));
    f.read((uint8_t*)&out[k], sizeof(Sample));
  }
  f.close();
  return k;
}

void SdStorage::appendPumpEvent(const PumpEvent& e) {
  if (!ok_) return;
  File f = SD.open("/garden/pumps.log", FILE_APPEND);
  if (!f) return;
  f.write((const uint8_t*)&e, sizeof(e)); f.close();
}
int SdStorage::loadPumpEvents(PumpEvent* out, int maxOut) {
  if (!ok_) return 0;
  File f = SD.open("/garden/pumps.log", FILE_READ);
  if (!f) return 0;
  int k = 0;
  while (k < maxOut && f.available() >= (int)sizeof(PumpEvent)) {
    f.read((uint8_t*)&out[k], sizeof(PumpEvent)); k++;
  }
  f.close();
  return k;
}
void SdStorage::saveClock(uint32_t epoch) {
  if (!ok_) return;
  File f = SD.open("/garden/clock.dat", FILE_WRITE);
  if (!f) return;
  f.seek(0); f.write((const uint8_t*)&epoch, sizeof(epoch)); f.close();
}
bool SdStorage::loadClock(uint32_t& epoch) {
  if (!ok_) return false;
  File f = SD.open("/garden/clock.dat", FILE_READ);
  if (!f || f.size() < (int)sizeof(epoch)) { if (f) f.close(); return false; }
  f.read((uint8_t*)&epoch, sizeof(epoch)); f.close();
  return true;
}
```

- [ ] **Step 3: Verify it compiles for the device** (no unit test; just the device build)

Run: `~/.platformio/penv/bin/pio run -e display_controller`
Expected: build succeeds (the file is included in the device env). It is not yet referenced by app code — that happens in Chunk 9. If LDF complains it's unused, that's fine; the wiring lands in Chunk 9.

- [ ] **Step 4: Commit**
```bash
git add src/data/SdStorage.h src/data/SdStorage.cpp
git commit -m "Mission 2: SdStorage — binary ring files over microSD (device-only)"
```

### Task 6.4: PumpLog write-through (sink + direct-append for reload)

**Files:**
- Modify: `lib/TimeSeries/PumpLog.h`, `lib/TimeSeries/PumpLog.cpp`
- Modify: `test/test_pumplog/test_pumplog.cpp` (add a sink test)

Mirror the Series persistence pattern so pump events persist: `append()` fires an optional sink (wired by the device app to `SeriesStorage::appendPumpEvent`); `appendDirect()` is RAM-only for `reload`. PumpLog stays pure (function-pointer sink, no storage type).

- [ ] **Step 1: Add the failing test** to `test/test_pumplog/test_pumplog.cpp` (+ its `RUN_TEST`):
```cpp
static int g_sinkCount = 0;
static void countingSink(void*, const PumpEvent&) { g_sinkCount++; }

static void test_sink_fires_on_append_only() {
  PumpLog log; g_sinkCount = 0;
  log.setSink(countingSink, nullptr);
  log.append({100, 0, EV_START, 30, 35});   // fires sink
  log.appendDirect({200, 0, EV_STOP, 46, 45}); // RAM only, no sink
  TEST_ASSERT_EQUAL_INT(1, g_sinkCount);
  TEST_ASSERT_EQUAL_UINT16(2, log.size());
}
```

- [ ] **Step 2: Run, expect FAIL**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_pumplog`

- [ ] **Step 3: Implement** — in `PumpLog.h` add to the public section:
```cpp
  using Sink = void(*)(void* ctx, const PumpEvent&);
  void setSink(Sink fn, void* ctx) { sink_ = fn; sinkCtx_ = ctx; }
  void appendDirect(const PumpEvent& e) { ring_.push(e); }  // RAM only (reload)
```
and to the private section:
```cpp
  Sink  sink_ = nullptr;
  void* sinkCtx_ = nullptr;
```
In `PumpLog.cpp` replace `append`:
```cpp
void PumpLog::append(const PumpEvent& e) {
  ring_.push(e);
  if (sink_) sink_(sinkCtx_, e);
}
```

- [ ] **Step 4: Run, expect PASS** (4 tests in this suite)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_pumplog`

- [ ] **Step 5: Commit**
```bash
git add lib/TimeSeries/PumpLog.h lib/TimeSeries/PumpLog.cpp test/test_pumplog/test_pumplog.cpp
git commit -m "Mission 2: PumpLog write-through sink + appendDirect for reload"
```

---

## Chunk 7: Simulator (seeding + live tick)

### Task 7.1: SimSource — deterministic seeding and a live tick

**Files:**
- Create: `lib/TimeSeries/SimSource.h`, `lib/TimeSeries/SimSource.cpp`
- Test: `test/test_sim/test_sim.cpp`

`SimSource` is pure: it takes a `TimeSeriesStore&` and a `PumpLog&` and `now`. Seeding writes history directly into the rings (via `pushRawDirect`/`pushDailyDirect`) so it bypasses write-through (we don't want to re-persist seeded data — it's regenerated only when there's nothing on disk). The live `tick(now)` calls `store.add(...)` (which DOES write through). Randomness uses a deterministic LCG seeded by a fixed constant so tests are reproducible and the "varies by index" rule is satisfied without `Math.random`.

- [ ] **Step 1: Write the failing tests** — `test/test_sim/test_sim.cpp`:
```cpp
#include <unity.h>
#include "SimSource.h"
#include "TimeSeriesStore.h"
#include "PumpLog.h"
#include "metrics.h"
using namespace ts;

void setUp() {} void tearDown() {}

static void test_seed_fills_daily_history() {
  TimeSeriesStore store; store.init();
  PumpLog log;
  SimSource sim;
  uint32_t now = 40 * DAY_S;
  sim.seed(store, log, now);
  // ~30 daily points for a soil series
  float out[30];
  int n = store.sampleWindow(NODE_BEET1, M_SOIL, W_MONTH, now, out, 30);
  TEST_ASSERT_EQUAL_INT(30, n);
  // values are plausible soil percentages
  TEST_ASSERT_TRUE(out[0] > 0.0f && out[0] < 100.0f);
}

static void test_seed_is_deterministic() {
  TimeSeriesStore a; a.init(); PumpLog la; SimSource s1;
  TimeSeriesStore b; b.init(); PumpLog lb; SimSource s2;
  uint32_t now = 40 * DAY_S;
  s1.seed(a, la, now); s2.seed(b, lb, now);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, a.latest(NODE_BEET1, M_SOIL), b.latest(NODE_BEET1, M_SOIL));
}

static void test_tick_appends_and_can_trigger_pump() {
  TimeSeriesStore store; store.init(); PumpLog log; SimSource sim;
  uint32_t now = 40 * DAY_S;
  sim.seed(store, log, now);
  bool before = store.find(NODE_GEWAECHSHAUS, M_SOIL)->hasData();
  sim.tick(store, log, now + RAW_INTERVAL_S);
  TEST_ASSERT_TRUE(before);  // sanity: data present after seed
  TEST_ASSERT_TRUE(store.find(NODE_GEWAECHSHAUS, M_SOIL)->hasData());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_seed_fills_daily_history);
  RUN_TEST(test_seed_is_deterministic);
  RUN_TEST(test_tick_appends_and_can_trigger_pump);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, expect FAIL**

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_sim`

- [ ] **Step 3: Implement `SimSource.h`**
```cpp
#pragma once
#include <stdint.h>
#include "TimeSeriesStore.h"
#include "PumpLog.h"
#include "metrics.h"

// Deterministic data source for Mission 2 (stands in for real sensors).
// seed(): ~30 d daily + ~48 h raw history written directly into the rings.
// tick(now): generate the next sample per series; drive pump start/stop on soil.
class SimSource {
public:
  void seed(TimeSeriesStore& store, PumpLog& log, uint32_t now);
  void tick(TimeSeriesStore& store, PumpLog& log, uint32_t now);

  // thresholds (also written into pump-event audit fields)
  static const int16_t DRY_THRESHOLD = 35;   // start watering below this
  static const int16_t WET_TARGET    = 45;   // stop watering at/above this

private:
  uint32_t rng_ = 0x1234567u;
  float frand(float lo, float hi);           // deterministic LCG in [lo,hi]
  float soilBaseFor(NodeId n) const;
  float diurnalTemp(uint32_t ts) const;
  bool  pumpRunning_[NODE_COUNT] = {false};
};
```

- [ ] **Step 4: Implement `SimSource.cpp`**
```cpp
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

  // 30 daily points per soil node + env air temp
  for (int d = 30; d >= 1; d--) {
    uint32_t dayTs = ((now / DAY_S) - d) * DAY_S;
    for (NodeId n : soil) {
      float v = soilBaseFor(n) + frand(-4, 4) - (d % 7); // slow decline + noise
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
    store.find(NODE_ENV, M_LUX)->pushRawDirect({t, encode(M_LUX, fmaxf(0, diurnalTemp(t) > 19 ? 12000 : 200) + frand(0,2000))});
  }

  // a couple of past greenhouse waterings in the pump log
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
```

- [ ] **Step 5: Run, expect PASS** (3 tests)

Run: `~/.platformio/penv/bin/pio test -e native_test -f test_sim`

- [ ] **Step 6: Run the full native suite to confirm nothing regressed**

Run: `~/.platformio/penv/bin/pio test -e native_test`
Expected: all suites (`test_ring`, `test_series`, `test_store`, `test_clock`, `test_pumplog`, `test_sim`) report PASS.

- [ ] **Step 7: Commit**
```bash
git add lib/TimeSeries/SimSource.h lib/TimeSeries/SimSource.cpp test/test_sim/test_sim.cpp
git commit -m "Mission 2: SimSource — deterministic seeding + live tick with pump triggers"
```

---

## Chunk 8: Repository + host preview integration

### Task 8.1: `GardenRepository` builds the `UiModel`

**Files:**
- Create: `src/data/GardenRepository.h`, `src/data/GardenRepository.cpp`
- (No Unity test — the repository is validated visually via the preview in Task 8.2, matching how Mission 1 validated rendering.)

`IGardenRepository` is the cloud-ready seam (spec §4). `LocalRepository` builds a `UiModel` from the store + pump log + clock, computing every derived value (spec §5). It depends only on pure headers (`TimeSeriesStore`, `PumpLog`, `Clock`, `ui/ui_model.h`) so it compiles in both `host_sim` and the device env.

- [ ] **Step 1: Implement `GardenRepository.h`**
```cpp
#pragma once
#include "../ui/ui_model.h"
#include "TimeSeriesStore.h"
#include "PumpLog.h"
#include "Clock.h"

// The seam the UI sees. Today: LocalRepository over the on-device store.
// Later: a CachedCloudRepository implements the same interface, UI unchanged.
class IGardenRepository {
public:
  virtual ~IGardenRepository() {}
  virtual UiModel buildModel() = 0;
  virtual void togglePump(int index) = 0;   // Page-3 CONF action
};

class LocalRepository : public IGardenRepository {
public:
  LocalRepository(TimeSeriesStore& store, PumpLog& log, Clock& clock)
    : store_(store), log_(log), clock_(clock) {}
  UiModel buildModel() override;
  void togglePump(int index) override;
private:
  void fillDateTime(UiModel&, uint32_t now);
  void fillEnv(UiModel&, uint32_t now);
  void fillChart(UiModel&, uint32_t now);
  void fillNodes(UiModel&, uint32_t now);
  void fillPumps(UiModel&, uint32_t now);
  bool lastStart(ts::NodeId, uint32_t& ts) const;  // newest START ts for a node
  bool lastStop(ts::NodeId, uint32_t& ts) const;   // newest STOP ts for a node

  TimeSeriesStore& store_;
  PumpLog& log_;
  Clock& clock_;
};
```

- [ ] **Step 2: Implement `GardenRepository.cpp`**

`UiModel` holds `const char*` fields, so string storage must outlive the render call. The device renders one model at a time, so file-static buffers are safe. Every `UiModel` field is populated (spec §5); `hasAlert`/`alertText` are constants this mission.
```cpp
#include "GardenRepository.h"
#include "SimSource.h"     // DRY_THRESHOLD / WET_TARGET
#include <time.h>
#include <math.h>
#include <stdio.h>
using namespace ts;

static const char*  NODE_NAMES[4] = {"Beet 1", "Beet 2", "Beet 3", "Gewächshaus Hochbeet"};
static const NodeId SOIL_NODES[4] = {NODE_BEET1, NODE_BEET2, NODE_BEET3, NODE_GEWAECHSHAUS};

// string storage for the const char* fields of one UiModel
static char s_dateLine[48], s_clockLong[16], s_clock[8];
static char s_nodeId[4][12], s_statusLine[4][24];

static int    iround(float v) { return (int)lroundf(v); }
static int8_t i8(float v) { if (v < -128) v = -128; if (v > 127) v = 127; return (int8_t)lroundf(v); }

void LocalRepository::fillDateTime(UiModel& m, uint32_t now) {
  time_t t = (time_t)now; struct tm tv; gmtime_r(&t, &tv);
  static const char* WD[7] = {"Sonntag","Montag","Dienstag","Mittwoch","Donnerstag","Freitag","Samstag"};
  static const char* MO[12] = {"Januar","Februar","März","April","Mai","Juni",
                               "Juli","August","September","Oktober","November","Dezember"};
  snprintf(s_dateLine, sizeof(s_dateLine), "%s, %d. %s %d",
           WD[tv.tm_wday], tv.tm_mday, MO[tv.tm_mon], tv.tm_year + 1900);
  snprintf(s_clockLong, sizeof(s_clockLong), "%02d:%02d Uhr", tv.tm_hour, tv.tm_min);
  snprintf(s_clock, sizeof(s_clock), "%02d:%02d", tv.tm_hour, tv.tm_min);
  m.dateLine = s_dateLine; m.clockLong = s_clockLong; m.clock = s_clock;
}

void LocalRepository::fillEnv(UiModel& m, uint32_t now) {
  EnvStation& e = m.env;
  float temp = store_.latest(NODE_ENV, M_AIR_TEMP); if (isnan(temp)) temp = 20;
  float hum  = store_.latest(NODE_ENV, M_HUMIDITY); if (isnan(hum))  hum  = 55;
  float lux  = store_.latest(NODE_ENV, M_LUX);      if (isnan(lux))  lux  = 0;
  float pres = store_.latest(NODE_ENV, M_PRESSURE); if (isnan(pres)) pres = 1013;
  e.tempC = temp; e.humidityPct = iround(hum); e.lightLux = iround(lux); e.pressureHpa = iround(pres);

  if      (lux < 500)  { e.glyph = WX_MOON;  e.condition = "Klar"; }
  else if (hum > 80)   { e.glyph = WX_RAIN;  e.condition = "Regen"; }
  else if (lux > 8000) { e.glyph = WX_SUN;   e.condition = "Sonnig"; }
  else                 { e.glyph = WX_CLOUD; e.condition = "Bewölkt"; }

  e.feelsLikeC = iround(temp + (hum > 65 ? 2.0f : 0.0f) - (hum < 30 ? 1.0f : 0.0f));

  int tr = store_.trend(NODE_ENV, M_PRESSURE, now);
  e.pressureTrend = (tr > 0 ? TREND_UP : (tr < 0 ? TREND_DOWN : TREND_STEADY));
  e.pressureWord  = (tr > 0 ? "Luftdruck steigt · stabil"
                            : (tr < 0 ? "Luftdruck fällt" : "Luftdruck stabil"));

  float lo, hi;
  if (store_.minMaxToday(NODE_ENV, M_AIR_TEMP, now, lo, hi)) { e.dayLoC = iround(lo); e.dayHiC = iround(hi); }
  else { e.dayLoC = iround(temp); e.dayHiC = iround(temp); }
}

void LocalRepository::fillChart(UiModel& m, uint32_t now) {
  float temp7[7]; store_.sampleWindow(NODE_ENV, M_AIR_TEMP, W_7D, now, temp7, 7);
  float soil7[7]; store_.averageAcrossNodes(M_SOIL, W_7D, now, soil7, 7);
  for (int i = 0; i < 7; i++) { m.chart.tempC[i] = temp7[i]; m.chart.soilPct[i] = iround(soil7[i]); }
}

void LocalRepository::fillNodes(UiModel& m, uint32_t now) {
  for (int i = 0; i < 4; i++) {
    Node& nd = m.nodes[i]; NodeId node = SOIL_NODES[i];
    float soil = store_.latest(node, M_SOIL);      if (isnan(soil)) soil = 40;
    float stmp = store_.latest(node, M_SOIL_TEMP); if (isnan(stmp)) stmp = 17;
    nd.name = NODE_NAMES[i];
    nd.soilPct = iround(soil); nd.soilTempC = stmp;
    nd.dry = nd.soilPct < SimSource::DRY_THRESHOLD;
    nd.pumpOn = log_.isRunning((uint8_t)node, now);
    float s12[7]; store_.sampleWindow(node, M_SOIL, W_12H, now, s12, 7);
    float s7[7];  store_.sampleWindow(node, M_SOIL, W_7D,  now, s7,  7);
    for (int k = 0; k < 7; k++) { nd.spark12h[k] = i8(s12[k]); nd.spark7d[k] = i8(s7[k]); }
  }
}

bool LocalRepository::lastStart(NodeId node, uint32_t& ts) const {
  bool found = false;
  for (uint16_t i = 0; i < log_.size(); i++) {
    const PumpEvent& e = log_.at(i);
    if (e.pumpId == (uint8_t)node && e.event == EV_START) { ts = e.ts; found = true; }
  }
  return found;
}
bool LocalRepository::lastStop(NodeId node, uint32_t& ts) const {
  bool found = false;
  for (uint16_t i = 0; i < log_.size(); i++) {
    const PumpEvent& e = log_.at(i);
    if (e.pumpId == (uint8_t)node && e.event == EV_STOP) { ts = e.ts; found = true; }
  }
  return found;
}

void LocalRepository::fillPumps(UiModel& m, uint32_t now) {
  int active = 0;
  for (int i = 0; i < 4; i++) {
    Pump& p = m.pumps[i]; NodeId node = SOIL_NODES[i];
    bool running = log_.isRunning((uint8_t)node, now);
    float soil = store_.latest(node, M_SOIL); if (isnan(soil)) soil = 40;
    p.zone = NODE_NAMES[i];
    snprintf(s_nodeId[i], sizeof(s_nodeId[i]), "PUMP-20%d", i + 1);
    p.nodeId = s_nodeId[i];
    p.on = running; p.active = running;
    p.currentPct = iround(soil); p.targetPct = SimSource::WET_TARGET; p.mode = "AUTO";
    p.minutesToday = log_.minutesToday((uint8_t)node, now);
    p.avgPerDay    = log_.avgPerDay((uint8_t)node, now, 7);
    if (running) {
      uint32_t since, dur = 0;
      if (lastStart(node, since)) dur = (now > since) ? now - since : 0;
      snprintf(s_statusLine[i], sizeof(s_statusLine[i]), "läuft · %02u:%02u",
               (unsigned)(dur / 60), (unsigned)(dur % 60));
      p.reason = "< Schwelle";
    } else {
      uint32_t stopTs;
      if (lastStop(node, stopTs)) {
        time_t t = (time_t)stopTs; struct tm tv; gmtime_r(&t, &tv);
        snprintf(s_statusLine[i], sizeof(s_statusLine[i]), "zuletzt %02d:%02d", tv.tm_hour, tv.tm_min);
      } else snprintf(s_statusLine[i], sizeof(s_statusLine[i]), "–");
      p.reason = nullptr;
    }
    p.statusLine = s_statusLine[i];
    if (running) active++;
  }
  m.activePumps = active;
}

UiModel LocalRepository::buildModel() {
  uint32_t now = clock_.now();
  UiModel m{};                       // zero-init; const char* fields set below
  fillDateTime(m, now);
  fillEnv(m, now);
  fillChart(m, now);
  fillNodes(m, now);
  fillPumps(m, now);
  m.hasAlert = false;
  m.alertText = "STURMWARNUNG bis 20:00 · Böen 75 km/h";
  return m;
}

void LocalRepository::togglePump(int index) {
  if (index < 0 || index >= 4) return;
  NodeId node = SOIL_NODES[index];
  uint32_t now = clock_.now();
  float soil = store_.latest(node, M_SOIL); if (isnan(soil)) soil = 40;
  int16_t soilPct = (int16_t)lroundf(soil);
  if (log_.isRunning((uint8_t)node, now))
    log_.append({now, (uint8_t)node, EV_STOP,  soilPct, SimSource::WET_TARGET});
  else
    log_.append({now, (uint8_t)node, EV_START, soilPct, SimSource::DRY_THRESHOLD});
}
```
Field-mapping note: `spark12h`/`spark7d` are `int8_t` and `chart.soilPct` is `int`, so `sampleWindow`'s `float` output is rounded/clamped (`i8`/`iround`). `gmtime_r` is available on both ESP32 newlib and host. `togglePump` records freshest soil + the active threshold into the event audit fields and write-through persists it (via the PumpLog sink wired in Chunk 9).

- [ ] **Step 3: Verify it compiles for the device**

Run: `~/.platformio/penv/bin/pio run -e display_controller`
Expected: builds (still using `mockModel()` in the app; repository compiled but not yet wired).

- [ ] **Step 4: Commit**
```bash
git add src/data/GardenRepository.h src/data/GardenRepository.cpp
git commit -m "Mission 2: GardenRepository builds UiModel from the store (cloud-ready seam)"
```

### Task 8.2: Render real data through the host preview

**Files:**
- Modify: `platformio.ini` (`env:host_sim` — compile `src/data/` except `SdStorage`, and the lib)
- Modify: `src/host_preview.cpp` (seed a store, build the model via the repository, render)

- [ ] **Step 1: Extend `env:host_sim`** in `platformio.ini`:
```ini
[env:host_sim]
platform = native
build_src_filter = -<*> +<ui/> +<data/> -<data/SdStorage.cpp> +<host_preview.cpp>
lib_ignore = CrowPanelEPD
build_flags =
  -std=gnu++17
  -D HOST_PREVIEW
```

- [ ] **Step 2: Rewrite `src/host_preview.cpp`** to use real data:
```cpp
#include <cstdio>
#include <cstdint>
#include "ui/canvas1.h"
#include "ui/ui_model.h"
#include "ui/pages.h"
#include "TimeSeriesStore.h"
#include "PumpLog.h"
#include "Clock.h"
#include "SimSource.h"
#include "data/GardenRepository.h"

static uint32_t g_simMillis = 0;
static uint32_t simMillis() { return g_simMillis; }

static bool writePGM(Canvas1 &c, const char *path) { /* unchanged from current file */ }

int main(int argc, char **argv) {
  const char *outDir = (argc > 1) ? argv[1] : "build/preview";

  TimeSeriesStore store; store.init();           // no storage -> pure RAM
  PumpLog log;
  Clock clock(simMillis);
  clock.setEpoch(1750000000);                    // a fixed plausible "now"
  uint32_t now = clock.now();

  SimSource sim; sim.seed(store, log, now);
  LocalRepository repo(store, log, clock);
  UiModel m = repo.buildModel();

  uint8_t frame[Canvas1::FRAME_BYTES];
  Canvas1 canvas(frame);
  char path[512];

  renderMain(canvas, m);
  snprintf(path, sizeof(path), "%s/main.pgm", outDir); writePGM(canvas, path);
  renderDetail(canvas, m);
  snprintf(path, sizeof(path), "%s/detail.pgm", outDir); writePGM(canvas, path);
  renderActuators(canvas, m, 1);
  snprintf(path, sizeof(path), "%s/actuators.pgm", outDir); writePGM(canvas, path);
  return 0;
}
```
(Keep the existing `writePGM` body.)

- [ ] **Step 3: Build + render the preview**

Run: `sim/preview.sh`
Expected: `build/preview/main.png`, `detail.png`, `actuators.png` regenerate with no errors.

- [ ] **Step 4: Eyeball the output** — open `build/preview/main@2x.png`, `detail@2x.png`, `actuators@2x.png`.
Expected: the three pages render with simulated-but-plausible values — chart has 7 bars + a temp line, each node tile shows soil%/temp + two sparklines, the greenhouse reads dry, pump page lists the four zones. Layout must match the Mission 1 baseline (no regressions).

- [ ] **Step 5: Commit**
```bash
git add platformio.ini src/host_preview.cpp
git commit -m "Mission 2: host preview renders real pages from the simulated store"
```

---

## Chunk 9: Device integration

### Task 9.1: Wire the data layer into the display controller

**Files:**
- Modify: `src/apps/display_controller_app.cpp`

Replace the mock model with the live data layer: build the store with `SdStorage` (RAM-only fallback if no card), seed if empty, reload if present, run the simulator on a timer, render via the repository, and route the Page-3 CONF toggle through `repo.togglePump`.

- [ ] **Step 1: Add includes + globals** (after the existing UI includes):
```cpp
#include "TimeSeriesStore.h"
#include "PumpLog.h"
#include "Clock.h"
#include "SimSource.h"
#include "InMemoryStorage.h"
#include "data/SdStorage.h"
#include "data/GardenRepository.h"

// Arduino millis() returns `unsigned long`, which is a DISTINCT type from
// uint32_t on the ESP32 toolchain — &millis won't bind to uint32_t(*)().
// Wrap it so the Clock's function-pointer type matches.
static uint32_t nowMillis() { return (uint32_t)millis(); }

static SdStorage       s_sd;
static InMemoryStorage s_ram;              // fallback when no card
static SeriesStorage*  s_storage = nullptr;  // the active backend (sd or ram)
static TimeSeriesStore s_store;
static PumpLog         s_log;
static Clock           s_clock(nowMillis);
static SimSource       s_sim;
static IGardenRepository* s_repo = nullptr;
static uint32_t s_lastTickMs = 0;

// Forward the PumpLog sink to whatever storage is active (mirrors the Series sink).
static void pumpPersist(void* ctx, const PumpEvent& e) {
  static_cast<SeriesStorage*>(ctx)->appendPumpEvent(e);
}

#ifndef TICK_MS
#define TICK_MS 900000   // 15 min; build with -D TICK_MS=5000 for a fast bench demo
#endif
```

- [ ] **Step 2: Initialise in `display_controller_setup()`** (replace `g_model = mockModel();`):
```cpp
  s_storage = s_sd.begin() ? (SeriesStorage*)&s_sd : (SeriesStorage*)&s_ram;
  if (s_storage == (SeriesStorage*)&s_ram)
    Serial.println(F("SD: no card — running RAM-only (history will not persist)"));
  s_store.init(s_storage);
  s_log.setSink(pumpPersist, s_storage);       // pump events write through too

  uint32_t epoch;
  if (s_storage->loadClock(epoch)) s_clock.setEpoch(epoch);
  else s_clock.setEpoch(1750000000);           // first-boot baseline

  s_store.reload();                            // load any persisted rings
  PumpEvent evbuf[256];
  int ne = s_storage->loadPumpEvents(evbuf, 256);
  for (int i = 0; i < ne; i++) s_log.appendDirect(evbuf[i]);   // RAM only, no re-persist

  bool empty = !s_store.find(ts::NODE_BEET1, ts::M_SOIL)->hasData();
  if (empty) {
    Serial.println(F("Store empty — seeding ~30 d history"));
    s_sim.seed(s_store, s_log, s_clock.now());  // pump events persist via the sink
    s_store.persistAll();                       // persist the seeded ring contents once
  }

  static LocalRepository repo(s_store, s_log, s_clock);
  s_repo = &repo;
  g_model = s_repo->buildModel();
```

- [ ] **Step 3: Run the simulator + clock persistence in `display_controller_loop()`** — insert this block immediately **after** the existing `bool pageChanged = false; bool inPageChanged = false;` declarations (so `pageChanged` is in scope), before the Menu/Exit handling:
```cpp
  uint32_t ms = millis();
  if (ms - s_lastTickMs >= TICK_MS) {
    s_lastTickMs = ms;
    s_sim.tick(s_store, s_log, s_clock.now());
    s_storage->saveClock(s_clock.now());       // active backend (no-op when RAM-only)
    g_model = s_repo->buildModel();
    pageChanged = true;                        // refresh with fresh data
  }
```

- [ ] **Step 4: Route the CONF toggle through the repository** — replace the `if (eConf) { ... }` body:
```cpp
    if (eConf) {
      s_repo->togglePump(g_focus);
      g_model = s_repo->buildModel();
      inPageChanged = true;
    }
```
Remove the now-dead `countActivePumps()` helper and the direct `g_model.pumps[...]` mutation (the repository owns pump state now).

- [ ] **Step 5: Build for the device**

Run: `~/.platformio/penv/bin/pio run -e display_controller`
Expected: clean build.

- [ ] **Step 6: Commit**
```bash
git add src/apps/display_controller_app.cpp
git commit -m "Mission 2: wire store + simulator + repository into the display controller"
```

### Task 9.2: Flash and verify on hardware

**Files:** none (hardware validation)

- [ ] **Step 1: Confirm the SD CS pin.** Check the Elecrow CrowPanel 5.79" pinout (wiki/manual DIS08792E) for the microSD chip-select GPIO. Update `SD_CS` in `src/data/SdStorage.cpp` if it differs from the placeholder, rebuild. Insert the FAT32-formatted 64 GB card.

- [ ] **Step 2: Flash.** Check the port with `ls /dev/cu.*` first (CH340 suffix drifts).
Run: `~/.platformio/penv/bin/pio run -e display_controller -t upload --upload-port /dev/cu.usbserial-110`

- [ ] **Step 3: Watch serial** with `~/.platformio/penv/bin/python -m serial.tools.miniterm /dev/cu.usbserial-110 115200`.
Expected on first boot: `SD: ...`, `Store empty — seeding ~30 d history`; on subsequent boots no re-seed (data reloaded). If no card: `running RAM-only`.

- [ ] **Step 4: Verify the panel** shows the three pages with simulated data; Menu/Exit navigation and the Page-3 wheel + CONF toggle still work (CONF now logs a pump event — confirm the row's running state and minutes update).

- [ ] **Step 5: Verify persistence** — power-cycle the panel; confirm it does NOT re-seed (serial shows reload, not seed) and the pages render the previously stored history. Pop the card and reboot to confirm the RAM-only fallback path runs without crashing.

- [ ] **Step 6 (optional bench check):** temporarily build with `-D TICK_MS=5000` (the `#ifndef` guard lets the flag win) so samples flow every few seconds, and watch a soil value drift + a pump trigger live. Rebuild without the flag before the final flash.

- [ ] **Step 7: Update the project log.** Append a dated entry to `docs/memory.md` summarising Mission 2 (what shipped, the SD layout, the SD CS pin, any bring-up gotchas) and tick the Mission 2 checkbox. Commit.
```bash
git add docs/memory.md
git commit -m "Mission 2: log completion + bring-up notes"
```

---

## Done criteria
- `~/.platformio/penv/bin/pio test -e native_test` — all suites PASS.
- `sim/preview.sh` renders the three pages from simulated data (no mock model).
- Device boots, seeds on first run, reloads on later runs, survives a missing card, runs the simulator, and navigation + pump toggle work on the panel.
- Pump-event persistence round-trips: toggle a pump on the panel, power-cycle, and confirm the running state + minutes-today survive (events reloaded from `/garden/pumps.log`).
- `mockModel()` is no longer referenced by `host_preview.cpp` or `display_controller_app.cpp` (it may remain in the tree as reference data; note it in the log if kept).
