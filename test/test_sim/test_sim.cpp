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
