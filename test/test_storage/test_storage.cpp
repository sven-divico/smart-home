#include <unity.h>
#include "InMemoryStorage.h"
#include "TimeSeriesStore.h"
#include "metrics.h"
using namespace ts;

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

// Verify that reload() restores finalized raw samples that were persisted by a
// prior TimeSeriesStore instance (boot-critical path).
static void test_reload_roundtrip() {
  InMemoryStorage st; st.begin();

  // Store A: feed two samples in different 15-min windows so the first window
  // finalizes and the sink persists it to st.
  TimeSeriesStore a; a.init(&st);
  a.add(NODE_BEET1, M_SOIL, 0, encode(M_SOIL, 55));
  a.add(NODE_BEET1, M_SOIL, RAW_INTERVAL_S + 1, encode(M_SOIL, 60)); // finalizes window @0

  // Confirm storage holds exactly one persisted raw point.
  Sample check[8];
  int n = st.loadRing(NODE_BEET1, M_SOIL, false, check, 8);
  TEST_ASSERT_EQUAL_INT(1, n);

  // Store B: fresh instance over the same storage — reload must restore that point.
  TimeSeriesStore b; b.init(&st); b.reload();
  const Series* s = b.find(NODE_BEET1, M_SOIL);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_UINT16(1, s->rawSize());
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 55.0f, decode(M_SOIL, s->rawNewest().value));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_inmemory_roundtrips_samples);
  RUN_TEST(test_inmemory_pump_events);
  RUN_TEST(test_reload_roundtrip);
  return UNITY_END();
}
