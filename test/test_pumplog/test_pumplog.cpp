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
