#pragma once

void otaTaskStart();

/** Wake the OTA task from idle wait (manual check, daily trigger, etc.). */
void otaTaskWake();
