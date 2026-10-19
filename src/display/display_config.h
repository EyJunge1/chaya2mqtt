#pragma once

/** Display delta shown before rolling baseline ("999+" overflow UI uses same cap). */
constexpr int kDisplayCounterMax = 999;

/**
 * Continuous Wi-Fi or MQTT outage before the heart view switches to Lucide heart-crack.
 * Recovery is immediate when both links are healthy again.
 */
constexpr unsigned long kDisplayOfflineGraceMs = 300000UL;
