#pragma once

#include "async/event_types.hpp"

void audioInit();
void audioStartTask();

/** ES8311 answered I2C probe (only attempted when speaker is installed). */
auto audioCodecPresent() -> bool;
/** Speaker installed and codec probed OK. May probe once when speaker is enabled. */
auto audioPresent() -> bool;
/** Probe codec if speaker installed and not yet probed; keeps PA off after. */
auto audioEnsureCodecProbed() -> bool;

/** Non-blocking; safe from MQTT callbacks. Drops if the queue is full. */
void audioRequest(AudioMsg::Kind kind);
