#pragma once

#include <atomic>
#include <cstdint>

#include <freertos/portmacro.h>

extern portMUX_TYPE s_authMux;
extern uint8_t      s_csrfTokenRaw[16];

extern bool           s_sessionActive;
extern uint8_t        s_sessionRaw[16];
extern unsigned long  s_sessionCreatedMs;

extern std::atomic<uint32_t>      s_challengeCode;
extern std::atomic<unsigned long>   s_challengeStartedMs;
extern std::atomic<bool>            s_challengePending;

extern std::atomic<bool>           s_awaitingButtonConfirm;
extern std::atomic<unsigned long>  s_confirmStartedMs;

extern std::atomic<unsigned>       s_authFailStreak;
extern std::atomic<unsigned long>  s_authLockoutStartMs;

void rotateCsrfTokenLocked();
void hexEncode16(const uint8_t* in, char* outHex65);
bool hexDecode32Strict(const char* hex, uint8_t out16[16]);
bool secretsEqual16(const uint8_t* a, const uint8_t* b);

void challengeClearAtomic();
void awaitingClearAtomic();
void challengeBeginAtomic(uint32_t code, unsigned long startedMs);
bool tryConsumeAuthChallenge(uint32_t entered, unsigned long nowMs);

void scheduleMainTaskScreenAfterAuthFlow();
void maybeStartAwaitingButtonConfirm(bool resetFailStreakFromGet);

void authConfirmWindowExpired();
void authChallengeExpired();
