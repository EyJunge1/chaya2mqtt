#pragma once

/** Clear applyPending only when no newer pending snapshot remains (RC-WEB-01). */
inline bool mqttSettingsApplyShouldClearPending(bool hasUnappliedPending) { return !hasUnappliedPending; }
