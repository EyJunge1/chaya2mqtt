#include "web/deferred_reboot.hpp"

#include "admin_globals.hpp"

void deferredRebootAfterWifiSave() { g_webAdminWifiReconnectRequested.store(true, std::memory_order_release); }
