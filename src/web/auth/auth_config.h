#pragma once

constexpr unsigned long kChallengeTtlMs   = 300000UL;
constexpr unsigned long kConfirmWindowMs  = 10000UL;
constexpr unsigned long kAuthLockoutMs    = 3600000UL;
constexpr unsigned      kAuthFailsForLock = 3;
constexpr unsigned long kSessionCookieMaxAgeSec = 86400UL;
constexpr const char    kCookieName[]     = "chaya_sid";
