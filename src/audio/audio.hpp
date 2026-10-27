#pragma once

#include "async/event_types.hpp"

void audioInit();
void audioStartTask();

/** ES8311 answered I2C probe at boot. */
auto audioCodecPresent() -> bool;
/** Codec present and speaker marked installed in config. */
auto audioPresent() -> bool;

/** Non-blocking; safe from MQTT callbacks. Drops if the queue is full. */
void audioRequest(AudioMsg::Kind kind);
