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
