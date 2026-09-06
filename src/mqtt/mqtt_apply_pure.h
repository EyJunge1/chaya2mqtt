#pragma once

/** Clear applyPending only when no newer pending snapshot remains (RC-WEB-01). */
inline auto mqttSettingsApplyShouldClearPending(bool hasUnappliedPending) -> bool { return !hasUnappliedPending; }
