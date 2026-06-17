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
  TEST_ASSERT_EQUAL_UINT32(20, r.at(1).ts);   // exercise the (start+i) path, not just start
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fill_then_read_in_order);
  RUN_TEST(test_wrap_overwrites_oldest);
  RUN_TEST(test_load_restores_state);
  return UNITY_END();
}
