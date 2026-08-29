#pragma once

#include "async/event_types.h"
#include "display_link_pure.h"

#include <cstdint>

#include <freertos/FreeRTOS.h>

/** Payload bit: skip EPD refresh when NVS view already matches the target. */
inline constexpr uint32_t kDrawOnlyIfViewChanged = 1U;

bool displayPostMsg(DisplayMsg::Cmd cmd, uint32_t payload, TickType_t waitTicks);
bool displayPostHeartRedraw(TickType_t waitTicks);

void displayTaskSetDesiredHeartIcon(DisplayHeartIcon icon);
DisplayHeartIcon displayTaskDesiredHeartIcon();

void displayTaskDrainDrawIdleSem();
void displayTaskSetSplashDrawPending(bool pending);
bool displayTaskWaitDrawIdle(uint32_t timeoutMs);
bool displayTaskDrawPowerOffAndWait(uint32_t timeoutMs);

/** Create sync primitives and pin the display FreeRTOS task (aborts on failure). */
void displayTaskStart();
