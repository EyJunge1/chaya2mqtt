#include <unity.h>

#include "audio/audio_pure.h"
#include "async/queue_coalesce_pure.h"
#include "display/display_refresh_pure.h"
#include "display/draw_pure.h"
#include "display/view_state.h"
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
    TEST_ASSERT_TRUE(displayBatteryIconYellow(10));
    TEST_ASSERT_FALSE(displayBatteryIconYellow(40));
}

void test_display_view_refresh_decision() {
    TEST_ASSERT_TRUE(displayViewNeedsRefresh(DisplayView::Unknown, DisplayView::ProductTitle));
    TEST_ASSERT_FALSE(
        displayViewNeedsRefresh(DisplayView::ProductTitle, DisplayView::ProductTitle));
    TEST_ASSERT_TRUE(displayViewNeedsRefresh(DisplayView::SetupQr, DisplayView::ProductTitle));
    TEST_ASSERT_TRUE(displayViewNeedsRefresh(DisplayView::ProductTitle, DisplayView::SetupQr));
    TEST_ASSERT_FALSE(displayViewNeedsRefresh(DisplayView::Heart, DisplayView::Heart));
    TEST_ASSERT_TRUE(displayRefreshRequired(DisplayView::Heart, DisplayView::Heart, false));
    TEST_ASSERT_FALSE(displayRefreshRequired(DisplayView::Heart, DisplayView::Heart, true));
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

void test_display_heart_redraw_leading_trailing() {
    constexpr unsigned long kMin = kHeartRedrawMinIntervalMs;
    TEST_ASSERT_EQUAL_UINT(20000UL, kMin);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::SkipUnchanged),
        static_cast<int>(displayHeartRedrawDecide(3, 1, 3, 1, 1000, 0, kMin, false)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(4, 1, 3, 1, 1000, 0, kMin, false)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::DeferPending),
        static_cast<int>(displayHeartRedrawDecide(5, 1, 3, 1, 10000, 1000, kMin, false)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(5, 1, 3, 1, 10000, 1000, kMin, true)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(5, 1, 3, 1, 32000, 1000, kMin, false)));
}

void test_display_heart_redraw_wait_and_follow_up() {
    constexpr unsigned long kMin = kHeartRedrawMinIntervalMs;

    TEST_ASSERT_EQUAL_UINT(ULONG_MAX, displayHeartRedrawWaitMs(5000, 1000, kMin, false));
    TEST_ASSERT_EQUAL_UINT(0UL, displayHeartRedrawWaitMs(5000, 0, kMin, true));
    TEST_ASSERT_EQUAL_UINT(0UL, displayHeartRedrawWaitMs(35000, 1000, kMin, true));
    TEST_ASSERT_EQUAL_UINT(16000UL, displayHeartRedrawWaitMs(5000, 1000, kMin, true));

    // Race: paint used old snapshot while atomics already advanced → follow-up required.
    TEST_ASSERT_TRUE(displayHeartNeedsFollowUpRedraw(6, 2, 7, 2, false));
    TEST_ASSERT_TRUE(displayHeartNeedsFollowUpRedraw(6, 2, 6, 2, true));
    TEST_ASSERT_FALSE(displayHeartNeedsFollowUpRedraw(6, 2, 6, 2, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_battery_pct_curve);
    RUN_TEST(test_audio_quiet_hours);
    RUN_TEST(test_audio_playback_gates);
    RUN_TEST(test_draw_yellow_flags);
    RUN_TEST(test_display_view_refresh_decision);
    RUN_TEST(test_queue_drop_coalescing);
    RUN_TEST(test_display_heart_redraw_leading_trailing);
    RUN_TEST(test_display_heart_redraw_wait_and_follow_up);
    return UNITY_END();
}
