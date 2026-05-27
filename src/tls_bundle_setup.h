#pragma once

/** Install embedded CA bundle once (shared by MQTT + OTA TLS). */
bool chayaTlsEnsureCaBundleInstalled();

/** Create CA-bundle mutex during asyncInfraInit (before tasks start). */
void chayaTlsInfraInit();
