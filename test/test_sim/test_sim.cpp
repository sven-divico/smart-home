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

static void test_tick_triggers_dry_pump() {
  TimeSeriesStore store; store.init(); PumpLog log; SimSource sim;
  uint32_t now = 40 * DAY_S;
  sim.seed(store, log, now);
  uint16_t before = log.size();
  uint32_t t2 = now + RAW_INTERVAL_S;
  sim.tick(store, log, t2);
  // greenhouse seeds dry (~31% < 35% DRY_THRESHOLD), so the first tick MUST start its pump
  TEST_ASSERT_TRUE(log.size() > before);                  // a pump event was actually generated
  TEST_ASSERT_TRUE(log.isRunning(NODE_GEWAECHSHAUS, t2)); // ...and the greenhouse pump is now running
  TEST_ASSERT_TRUE(store.find(NODE_GEWAECHSHAUS, M_SOIL)->hasData());  // tick advanced the series
}

static int countStarts(PumpLog& log, NodeId node) {
  int c = 0;
  for (uint16_t i = 0; i < log.size(); i++) {
    const PumpEvent& e = log.at(i);
    if (e.pumpId == (uint8_t)node && e.event == EV_START) c++;
  }
  return c;
}

// Reboot-while-watering: the PumpLog is restored from disk showing a pump running,
// but SimSource is fresh. tick() must read running-state from the log (not a private
// flag) so it does NOT append a duplicate START for an already-running pump.
static void test_tick_no_duplicate_start_when_log_says_running() {
  TimeSeriesStore store; store.init(); PumpLog log; SimSource sim;
  uint32_t now = 40 * DAY_S;
  sim.seed(store, log, now);
  log.append({now, (uint8_t)NODE_GEWAECHSHAUS, EV_START, 30, SimSource::DRY_THRESHOLD}); // dangling START
  TEST_ASSERT_TRUE(log.isRunning(NODE_GEWAECHSHAUS, now));
  int startsBefore = countStarts(log, NODE_GEWAECHSHAUS);
  sim.tick(store, log, now + RAW_INTERVAL_S);
  TEST_ASSERT_EQUAL_INT(startsBefore, countStarts(log, NODE_GEWAECHSHAUS)); // no duplicate START
  TEST_ASSERT_TRUE(log.isRunning(NODE_GEWAECHSHAUS, now + RAW_INTERVAL_S));  // still running
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_seed_fills_daily_history);
  RUN_TEST(test_seed_is_deterministic);
  RUN_TEST(test_tick_triggers_dry_pump);
  RUN_TEST(test_tick_no_duplicate_start_when_log_says_running);
  return UNITY_END();
}
