#pragma once

#include "async/event_types.h"

/** Overflow pending of the same kind as a just-played queue item must not play again. */
inline bool audioShouldPlayOverflowPending(bool hadQueuePlay, AudioMsg::Kind queueKind, AudioMsg::Kind pendingKind) {
    return !hadQueuePlay || queueKind != pendingKind;
}
