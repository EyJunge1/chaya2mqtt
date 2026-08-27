#pragma once

/** Create app task (web admin loop, heart counters). */
void appTaskStart();

/** Wake app task early (e.g. deferred admin work pending). */
void appTaskNotify();
