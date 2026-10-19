#pragma once

#include "async/event_types.hpp"

void audioInit();
void audioStartTask();

/** Non-blocking; safe from MQTT callbacks. Drops if the queue is full. */
void audioRequest(AudioMsg::Kind kind);
