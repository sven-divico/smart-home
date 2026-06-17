#include <unity.h>
#include "metrics.h"
#include "TimeSeriesStore.h"
using namespace ts;

void setUp() {} void tearDown() {}

// --- Metrics / encode-decode tests (Task 1.2) ---

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

// --- TimeSeriesStore: registry + latest (Task 4.1) ---

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

// --- TimeSeriesStore: window queries (Task 4.2) ---

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
  s->pushRawDirect({1 * 3600,         encode(M_AIR_TEMP, 99.0f)});  // yesterday -- ignored
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

int main(int, char **) {
  UNITY_BEGIN();
  // Task 1.2: encode/decode
  RUN_TEST(test_temp_roundtrip);
  RUN_TEST(test_lux_scale_no_overflow);
  RUN_TEST(test_soil_identity);
  // Task 4.1: registry + latest
  RUN_TEST(test_registers_all_series);
  RUN_TEST(test_add_and_latest);
  // Task 4.2: window queries
  RUN_TEST(test_sample_window_buckets_to_n_points);
  RUN_TEST(test_average_across_nodes);
  RUN_TEST(test_min_max_today);
  RUN_TEST(test_trend_rising);
  return UNITY_END();
}
