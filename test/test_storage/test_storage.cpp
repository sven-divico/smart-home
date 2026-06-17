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
