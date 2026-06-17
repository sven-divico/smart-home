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

static void test_latest_seed_and_empty_paths() {
  Series s(M_SOIL, /*hasDaily=*/false);
  TEST_ASSERT_EQUAL_INT16(0, s.latest());          // fresh series: branch 3 (no data)
  s.pushRawDirect({100, encode(M_SOIL, 37)});      // seeding bypass leaves haveLast_ false
  TEST_ASSERT_EQUAL_INT16(37, s.latest());         // branch 2: newest raw point
  s.pushDailyDirect({0, encode(M_SOIL, 50)});      // no-op when hasDaily is false
  TEST_ASSERT_EQUAL_UINT16(0, s.dailySize());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_window_average_one_raw_point);
  RUN_TEST(test_day_rollup);
  RUN_TEST(test_no_daily_when_disabled);
  RUN_TEST(test_latest_seed_and_empty_paths);
  return UNITY_END();
}
