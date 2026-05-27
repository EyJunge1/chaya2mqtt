#pragma once

/** Install embedded CA bundle once (shared by MQTT + OTA TLS). */
bool chayaTlsEnsureCaBundleInstalled();
