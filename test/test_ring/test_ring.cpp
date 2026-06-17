#include <unity.h>

void setUp() {}
void tearDown() {}

static void test_harness_runs() {
  TEST_ASSERT_EQUAL_INT(2, 1 + 1);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_harness_runs);
  return UNITY_END();
}
