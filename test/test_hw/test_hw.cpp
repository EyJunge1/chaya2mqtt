#include <unity.h>

#include "audio/audio_pure.h"
#include "async/queue_coalesce_pure.h"
#include "button/button_debounce_pure.h"
#include "button/button_soft_off_pure.h"
#include "display/display_config.h"
#include "display/display_link_pure.h"
#include "display/display_refresh_pure.h"
#include "display/draw_pure.h"
#include "display/view_state.h"
#include "battery/battery_pure.h"
#include "led/led_pattern_pure.h"

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
    TEST_ASSERT_FALSE(audioPlaybackAllowed(false, 70, false, 12, 23, 8));
    TEST_ASSERT_FALSE(audioPlaybackAllowed(true, 0, false, 12, 23, 8));
    TEST_ASSERT_TRUE(audioPlaybackAllowed(true, 70, false, 2, 23, 8));
    TEST_ASSERT_FALSE(audioPlaybackAllowed(true, 70, true, 2, 23, 8));
    TEST_ASSERT_TRUE(audioPlaybackAllowed(true, 70, true, 12, 23, 8));
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
        static_cast<int>(displayHeartRedrawDecide(3, 1, 3, 1, false, false, 1000, 0, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(4, 1, 3, 1, false, false, 1000, 0, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(3, 1, 3, 1, true, false, 1000, 0, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(3, 1, 3, 1, false, true, 1000, 0, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::DeferPending),
        static_cast<int>(displayHeartRedrawDecide(5, 1, 3, 1, false, false, 10000, 1000, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::DeferPending),
        static_cast<int>(displayHeartRedrawDecide(3, 1, 3, 1, true, false, 10000, 1000, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::DeferPending),
        static_cast<int>(displayHeartRedrawDecide(3, 1, 3, 1, false, true, 10000, 1000, kMin)));

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(DisplayHeartRedrawDecision::QueueNow),
        static_cast<int>(displayHeartRedrawDecide(5, 1, 3, 1, false, false, 32000, 1000, kMin)));
}

void test_display_heart_redraw_wait_and_follow_up() {
    constexpr unsigned long kMin = kHeartRedrawMinIntervalMs;

    TEST_ASSERT_EQUAL_UINT(ULONG_MAX, displayHeartRedrawWaitMs(5000, 1000, kMin, false));
    TEST_ASSERT_EQUAL_UINT(0UL, displayHeartRedrawWaitMs(5000, 0, kMin, true));
    TEST_ASSERT_EQUAL_UINT(0UL, displayHeartRedrawWaitMs(35000, 1000, kMin, true));
    TEST_ASSERT_EQUAL_UINT(16000UL, displayHeartRedrawWaitMs(5000, 1000, kMin, true));

    // Race: paint used old snapshot while atomics already advanced → follow-up required.
    TEST_ASSERT_TRUE(displayHeartNeedsFollowUpRedraw(6, 2, 7, 2, false, false, false));
    TEST_ASSERT_TRUE(displayHeartNeedsFollowUpRedraw(6, 2, 6, 2, false, false, true));
    TEST_ASSERT_TRUE(displayHeartNeedsFollowUpRedraw(6, 2, 6, 2, true, false, false));
    TEST_ASSERT_TRUE(displayHeartNeedsFollowUpRedraw(6, 2, 6, 2, false, true, false));
    TEST_ASSERT_FALSE(displayHeartNeedsFollowUpRedraw(6, 2, 6, 2, false, false, false));
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

void test_led_pattern_three_blinks() {
    LedPatternRuntime rt{};
    TEST_ASSERT_TRUE(ledPatternBegin(rt, 3, 200, 200));
    TEST_ASSERT_TRUE(rt.onPhase);
    TEST_ASSERT_EQUAL_UINT8(3, rt.onPulsesLeft);

    LedPatternAdvanceResult r = ledPatternAdvance(rt);
    TEST_ASSERT_FALSE(r.done);
    TEST_ASSERT_FALSE(r.ledOn);
    TEST_ASSERT_EQUAL_UINT16(200, r.durationMs);

    r = ledPatternAdvance(rt);
    TEST_ASSERT_FALSE(r.done);
    TEST_ASSERT_TRUE(r.ledOn);
    TEST_ASSERT_EQUAL_UINT16(200, r.durationMs);

    r = ledPatternAdvance(rt);
    TEST_ASSERT_FALSE(r.done);
    TEST_ASSERT_FALSE(r.ledOn);

    r = ledPatternAdvance(rt);
    TEST_ASSERT_FALSE(r.done);
    TEST_ASSERT_TRUE(r.ledOn);

    r = ledPatternAdvance(rt);
    TEST_ASSERT_FALSE(r.done);
    TEST_ASSERT_FALSE(r.ledOn);
    TEST_ASSERT_EQUAL_UINT16(200, r.durationMs);

    r = ledPatternAdvance(rt);
    TEST_ASSERT_TRUE(r.done);
    TEST_ASSERT_FALSE(r.ledOn);
}

void test_led_pattern_single_pulse_no_off() {
    LedPatternRuntime rt{};
    TEST_ASSERT_TRUE(ledPatternBegin(rt, 1, 150, 0));
    const LedPatternAdvanceResult r = ledPatternAdvance(rt);
    TEST_ASSERT_TRUE(r.done);
    TEST_ASSERT_FALSE(r.ledOn);
    TEST_ASSERT_EQUAL_UINT16(0, r.durationMs);
}

void test_led_pattern_normalize_zero_count() {
    uint8_t  count = 0;
    uint16_t onMs  = 0;
    uint16_t offMs = 50;
    ledPatternNormalize(count, onMs, offMs);
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_EQUAL_UINT16(1, onMs);
    TEST_ASSERT_EQUAL_UINT16(50, offMs);
}

void test_debounce_commits_after_stable_ms() {
    DebouncedGpioState st{};
    st.lastRawReading       = 1;
    st.debouncedLevel       = 1;
    st.lastDebounceChangeMs = 1000;

    debounceUpdate(st, 0, 1000, 20);
    TEST_ASSERT_EQUAL_INT(0, st.lastRawReading);
    TEST_ASSERT_EQUAL_INT(1, st.debouncedLevel);

    debounceUpdate(st, 0, 1019, 20);
    TEST_ASSERT_EQUAL_INT(1, st.debouncedLevel);

    debounceUpdate(st, 0, 1020, 20);
    TEST_ASSERT_EQUAL_INT(0, st.debouncedLevel);
}

void test_soft_off_may_arm_ext1_wake() {
    TEST_ASSERT_FALSE(softOffMayArmExt1Wake(0));
    TEST_ASSERT_TRUE(softOffMayArmExt1Wake(1));
}

void test_soft_off_release_settle() {
    SoftOffReleaseSettle st{};
    TEST_ASSERT_FALSE(softOffReleaseSettled(st, 0, 1000, 300));
    TEST_ASSERT_EQUAL_UINT(0, st.highSinceMs);

    TEST_ASSERT_FALSE(softOffReleaseSettled(st, 1, 2000, 300));
    TEST_ASSERT_EQUAL_UINT(2000, st.highSinceMs);
    TEST_ASSERT_FALSE(softOffReleaseSettled(st, 1, 2299, 300));
    TEST_ASSERT_TRUE(softOffReleaseSettled(st, 1, 2300, 300));

    TEST_ASSERT_FALSE(softOffReleaseSettled(st, 0, 2400, 300));
    TEST_ASSERT_EQUAL_UINT(0, st.highSinceMs);
    TEST_ASSERT_FALSE(softOffReleaseSettled(st, 1, 2500, 300));
    TEST_ASSERT_FALSE(softOffReleaseSettled(st, 1, 2799, 300));
    TEST_ASSERT_TRUE(softOffReleaseSettled(st, 1, 2800, 300));
}

void test_debounce_resets_timer_on_bounce() {
    DebouncedGpioState st{};
    st.lastRawReading       = 1;
    st.debouncedLevel       = 1;
    st.lastDebounceChangeMs = 0;

    debounceUpdate(st, 0, 100, 20);
    debounceUpdate(st, 1, 110, 20);
    debounceUpdate(st, 0, 120, 20);
    TEST_ASSERT_EQUAL_INT(1, st.debouncedLevel);

    debounceUpdate(st, 0, 140, 20);
    TEST_ASSERT_EQUAL_INT(0, st.debouncedLevel);
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
    RUN_TEST(test_led_pattern_three_blinks);
    RUN_TEST(test_led_pattern_single_pulse_no_off);
    RUN_TEST(test_led_pattern_normalize_zero_count);
    RUN_TEST(test_debounce_commits_after_stable_ms);
    RUN_TEST(test_soft_off_may_arm_ext1_wake);
    RUN_TEST(test_soft_off_release_settle);
    RUN_TEST(test_debounce_resets_timer_on_bounce);
    return UNITY_END();
}
