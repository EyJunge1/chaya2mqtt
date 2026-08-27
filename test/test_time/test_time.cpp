#include <climits>
#include <unity.h>

#include "constants.h"
#include "heart/counter_pure.h"
#include "util/time_helpers.h"

void test_calendar_day_since_epoch() {
    TEST_ASSERT_EQUAL_UINT32(0U, calendarDaySinceEpochUtc(0));
    TEST_ASSERT_EQUAL_UINT32(1U, calendarDaySinceEpochUtc(86400));
}

void test_deadline_helpers() {
    TEST_ASSERT_FALSE(deadlineReached(1000U, 500U, 1200U));
    TEST_ASSERT_TRUE(deadlineReached(1000U, 500U, 1500U));
    TEST_ASSERT_EQUAL_UINT32(300U, remainingMs(1000U, 500U, 1200U));
    TEST_ASSERT_EQUAL_UINT32(0U, remainingMs(1000U, 500U, 2000U));
}

void test_deadline_wraparound() {
    const uint32_t start = 0xFFFFFFF0U;
    TEST_ASSERT_FALSE(deadlineReached(start, 32U, start + 10U));
    TEST_ASSERT_TRUE(deadlineReached(start, 32U, start + 32U));
    TEST_ASSERT_EQUAL_UINT32(22U, remainingMs(start, 32U, start + 10U));
    TEST_ASSERT_TRUE(ntpTimeLooksSynced(static_cast<time_t>(1700000001)));
    TEST_ASSERT_FALSE(ntpTimeLooksSynced(static_cast<time_t>(1000)));
}

void test_counter_delta_and_cap() {
    TEST_ASSERT_EQUAL_INT(0, heartCounterDeltaPure(5, 10));
    TEST_ASSERT_EQUAL_INT(7, heartCounterDeltaPure(17, 10));
    TEST_ASSERT_EQUAL_INT(999, heartCounterShownDeltaPure(1009, 10));
    TEST_ASSERT_TRUE(heartCounterShouldShowPlusPure(1010, 10));
    TEST_ASSERT_FALSE(heartCounterShouldShowPlusPure(1009, 10));
    TEST_ASSERT_EQUAL_INT(INT_MAX, heartSentCounterNextPure(INT_MAX));
    TEST_ASSERT_EQUAL_INT(1, heartSentCounterNextPure(0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_calendar_day_since_epoch);
    RUN_TEST(test_deadline_helpers);
    RUN_TEST(test_deadline_wraparound);
    RUN_TEST(test_counter_delta_and_cap);
    return UNITY_END();
}
