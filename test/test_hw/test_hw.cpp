#include <unity.h>

#include "audio/audio_pure.h"
#include "async/queue_coalesce_pure.h"
#include "display/draw_pure.h"
#include "hw/battery_pure.h"

void test_battery_pct_curve() {
    TEST_ASSERT_EQUAL_INT(0, batteryPctFromMilliVolts(3200));
    TEST_ASSERT_EQUAL_INT(0, batteryPctFromMilliVolts(3300));
    TEST_ASSERT_EQUAL_INT(20, batteryPctFromMilliVolts(3700));
    TEST_ASSERT_EQUAL_INT(55, batteryPctFromMilliVolts(3900));
    TEST_ASSERT_EQUAL_INT(100, batteryPctFromMilliVolts(4200));
    TEST_ASSERT_EQUAL_INT(100, batteryPctFromMilliVolts(4300));
    TEST_ASSERT_TRUE(batteryWarnLow(19));
    TEST_ASSERT_FALSE(batteryWarnLow(20));
}

void test_audio_quiet_hours() {
    TEST_ASSERT_FALSE(audioQuietHoursActive(12, 23, 23));
    TEST_ASSERT_TRUE(audioQuietHoursActive(23, 23, 8));
    TEST_ASSERT_TRUE(audioQuietHoursActive(2, 23, 8));
    TEST_ASSERT_FALSE(audioQuietHoursActive(12, 23, 8));
    TEST_ASSERT_TRUE(audioQuietHoursActive(14, 13, 17));
    TEST_ASSERT_FALSE(audioQuietHoursActive(17, 13, 17));
}

void test_audio_playback_gates() {
    TEST_ASSERT_FALSE(audioPlaybackAllowed(true, 70, false, 12, 23, 8));
    TEST_ASSERT_FALSE(audioPlaybackAllowed(false, 0, false, 12, 23, 8));
    TEST_ASSERT_TRUE(audioPlaybackAllowed(false, 70, false, 2, 23, 8));
    TEST_ASSERT_FALSE(audioPlaybackAllowed(false, 70, true, 2, 23, 8));
    TEST_ASSERT_TRUE(audioPlaybackAllowed(false, 70, true, 12, 23, 8));
}

void test_draw_yellow_flags() {
    TEST_ASSERT_TRUE(displayFreshRxDots(true));
    TEST_ASSERT_FALSE(displayFreshRxDots(false));
    TEST_ASSERT_TRUE(displayBatteryIconYellow(10));
    TEST_ASSERT_FALSE(displayBatteryIconYellow(40));
}

void test_queue_drop_coalescing() {
    bool pending = false;
    pending = queueCoalescePendingAfterPost(pending, true);
    TEST_ASSERT_FALSE(pending);
    pending = queueCoalescePendingAfterPost(pending, false);
    pending = queueCoalescePendingAfterPost(pending, false);
    TEST_ASSERT_TRUE(pending);
    TEST_ASSERT_TRUE(queueCoalesceConsume(&pending));
    TEST_ASSERT_FALSE(pending);
    TEST_ASSERT_FALSE(queueCoalesceConsume(&pending));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_battery_pct_curve);
    RUN_TEST(test_audio_quiet_hours);
    RUN_TEST(test_audio_playback_gates);
    RUN_TEST(test_draw_yellow_flags);
    RUN_TEST(test_queue_drop_coalescing);
    return UNITY_END();
}
