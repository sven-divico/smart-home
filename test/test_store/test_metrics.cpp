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
