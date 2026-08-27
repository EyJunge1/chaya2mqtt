#include <unity.h>

#include "audio/audio_pure.h"
#include "async/queue_coalesce_pure.h"
#include "display/display_config.h"
#include "display/display_link_pure.h"
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

void test_display_battery_colors() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryColor::Red),
                          static_cast<int>(displayBatteryColor(0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryColor::Red),
                          static_cast<int>(displayBatteryColor(14)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryColor::Yellow),
                          static_cast<int>(displayBatteryColor(15)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryColor::Yellow),
                          static_cast<int>(displayBatteryColor(39)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryColor::Black),
                          static_cast<int>(displayBatteryColor(40)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryColor::Black),
                          static_cast<int>(displayBatteryColor(100)));
}

void test_display_battery_icons() {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryIcon::Empty),
                          static_cast<int>(displayBatteryIcon(0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryIcon::Empty),
                          static_cast<int>(displayBatteryIcon(14)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryIcon::Low),
                          static_cast<int>(displayBatteryIcon(15)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryIcon::Low),
                          static_cast<int>(displayBatteryIcon(39)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryIcon::Medium),
                          static_cast<int>(displayBatteryIcon(40)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryIcon::Medium),
                          static_cast<int>(displayBatteryIcon(79)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryIcon::Full),
                          static_cast<int>(displayBatteryIcon(80)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayBatteryIcon::Full),
                          static_cast<int>(displayBatteryIcon(100)));
}

void test_display_view_refresh_decision() {
    TEST_ASSERT_TRUE(displayViewNeedsRefresh(DisplayView::Unknown, DisplayView::ProductTitle));
    TEST_ASSERT_FALSE(
        displayViewNeedsRefresh(DisplayView::ProductTitle, DisplayView::ProductTitle));
    TEST_ASSERT_TRUE(displayViewNeedsRefresh(DisplayView::SetupQr, DisplayView::ProductTitle));
    TEST_ASSERT_TRUE(displayViewNeedsRefresh(DisplayView::ProductTitle, DisplayView::SetupQr));
    TEST_ASSERT_FALSE(displayViewNeedsRefresh(DisplayView::Heart, DisplayView::Heart));
    TEST_ASSERT_TRUE(displayViewNeedsRefresh(DisplayView::Heart, DisplayView::HeartCrack));
    TEST_ASSERT_TRUE(displayViewNeedsRefresh(DisplayView::HeartCrack, DisplayView::Heart));
    TEST_ASSERT_TRUE(displayViewNeedsRefresh(DisplayView::ProductTitle, DisplayView::PowerOff));
    TEST_ASSERT_FALSE(displayViewNeedsRefresh(DisplayView::PowerOff, DisplayView::PowerOff));
    TEST_ASSERT_TRUE(displayViewIsValid(DisplayView::HeartCrack));
    TEST_ASSERT_TRUE(displayViewIsValid(DisplayView::PowerOff));
    TEST_ASSERT_TRUE(displayViewIsHeartFamily(DisplayView::Heart));
    TEST_ASSERT_TRUE(displayViewIsHeartFamily(DisplayView::HeartCrack));
    TEST_ASSERT_FALSE(displayViewIsHeartFamily(DisplayView::PowerOff));
    TEST_ASSERT_TRUE(displayRefreshRequired(DisplayView::Heart, DisplayView::Heart, false));
    TEST_ASSERT_FALSE(displayRefreshRequired(DisplayView::Heart, DisplayView::Heart, true));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayView::Heart),
                          static_cast<int>(displayViewForHeartIcon(DisplayHeartIcon::Filled)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DisplayView::HeartCrack),
                          static_cast<int>(displayViewForHeartIcon(DisplayHeartIcon::Crack)));
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
        static_cast<int>(displayHeartRedrawDecide(3, 1, 3, 1, false, 1000, 0, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(4, 1, 3, 1, false, 1000, 0, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(3, 1, 3, 1, true, 1000, 0, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::DeferPending),
        static_cast<int>(displayHeartRedrawDecide(5, 1, 3, 1, false, 10000, 1000, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::DeferPending),
        static_cast<int>(displayHeartRedrawDecide(3, 1, 3, 1, true, 10000, 1000, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(5, 1, 3, 1, false, 32000, 1000, kMin)));
}

void test_display_heart_redraw_wait_and_follow_up() {
    constexpr unsigned long kMin = kHeartRedrawMinIntervalMs;

    TEST_ASSERT_EQUAL_UINT(ULONG_MAX, displayHeartRedrawWaitMs(5000, 1000, kMin, false));
    TEST_ASSERT_EQUAL_UINT(0UL, displayHeartRedrawWaitMs(5000, 0, kMin, true));
    TEST_ASSERT_EQUAL_UINT(0UL, displayHeartRedrawWaitMs(35000, 1000, kMin, true));
    TEST_ASSERT_EQUAL_UINT(16000UL, displayHeartRedrawWaitMs(5000, 1000, kMin, true));

    // Race: paint used old snapshot while atomics already advanced → follow-up required.
    TEST_ASSERT_TRUE(displayHeartNeedsFollowUpRedraw(6, 2, 7, 2, false, false));
    TEST_ASSERT_TRUE(displayHeartNeedsFollowUpRedraw(6, 2, 6, 2, false, true));
    TEST_ASSERT_TRUE(displayHeartNeedsFollowUpRedraw(6, 2, 6, 2, true, false));
    TEST_ASSERT_FALSE(displayHeartNeedsFollowUpRedraw(6, 2, 6, 2, false, false));
}

void test_display_link_offline_grace() {
    TEST_ASSERT_EQUAL_UINT(300000UL, kDisplayOfflineGraceMs);

    DisplayLinkState st{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Filled),
        static_cast<int>(displayHeartIconDecide(false, true, true, 1000, kDisplayOfflineGraceMs,
                                                st)));
    TEST_ASSERT_EQUAL_UINT(0UL, st.outageSinceMs);

    // First unhealthy sample starts the timer but keeps the filled heart.
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Filled),
        static_cast<int>(displayHeartIconDecide(false, false, true, 2000, kDisplayOfflineGraceMs,
                                                st)));
    TEST_ASSERT_EQUAL_UINT(2000UL, st.outageSinceMs);

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Filled),
        static_cast<int>(displayHeartIconDecide(false, true, false, 100000, kDisplayOfflineGraceMs,
                                                st)));

    // Still within 5 minutes.
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Filled),
        static_cast<int>(
            displayHeartIconDecide(false, false, false, 2000 + kDisplayOfflineGraceMs - 1,
                                   kDisplayOfflineGraceMs, st)));

    // At/after grace → crack.
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Crack),
        static_cast<int>(
            displayHeartIconDecide(false, false, false, 2000 + kDisplayOfflineGraceMs,
                                   kDisplayOfflineGraceMs, st)));

    // Immediate recovery when Wi-Fi + MQTT are healthy again.
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Filled),
        static_cast<int>(displayHeartIconDecide(false, true, true, 2000 + kDisplayOfflineGraceMs
                                                                       + 5000,
                                                kDisplayOfflineGraceMs, st)));
    TEST_ASSERT_EQUAL_UINT(0UL, st.outageSinceMs);

    // AP mode never shows crack.
    DisplayLinkState apSt{};
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Filled),
        static_cast<int>(
            displayHeartIconDecide(true, false, false, 999999, kDisplayOfflineGraceMs, apSt)));
    TEST_ASSERT_EQUAL_UINT(0UL, apSt.outageSinceMs);
}

void test_display_link_millis_wrap() {
    DisplayLinkState st{};
    const unsigned long nearWrap = ULONG_MAX - 1000UL;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Filled),
        static_cast<int>(
            displayHeartIconDecide(false, false, false, nearWrap, kDisplayOfflineGraceMs, st)));
    TEST_ASSERT_EQUAL_UINT(nearWrap, st.outageSinceMs);

    // Unsigned elapsed across wrap: (500 - nearWrap) == 1501.
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Filled),
        static_cast<int>(
            displayHeartIconDecide(false, false, false, 500UL, kDisplayOfflineGraceMs, st)));

    // Force crack by setting an outage far enough in the past across the wrap.
    st.outageSinceMs = ULONG_MAX - (kDisplayOfflineGraceMs - 10UL);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartIcon::Crack),
        static_cast<int>(
            displayHeartIconDecide(false, false, false, 20UL, kDisplayOfflineGraceMs, st)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_battery_pct_curve);
    RUN_TEST(test_audio_quiet_hours);
    RUN_TEST(test_audio_playback_gates);
    RUN_TEST(test_display_battery_colors);
    RUN_TEST(test_display_battery_icons);
    RUN_TEST(test_display_view_refresh_decision);
    RUN_TEST(test_queue_drop_coalescing);
    RUN_TEST(test_display_heart_redraw_leading_trailing);
    RUN_TEST(test_display_heart_redraw_wait_and_follow_up);
    RUN_TEST(test_display_link_offline_grace);
    RUN_TEST(test_display_link_millis_wrap);
    return UNITY_END();
}
