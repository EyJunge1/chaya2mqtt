#include "web/deferred_reboot.h"

#include "admin_globals.h"

void deferredRebootAfterWifiSave() { g_webAdminWifiReconnectRequested.store(true, std::memory_order_release); }
