#pragma once

/** Install embedded CA bundle once (shared by MQTT + OTA TLS). */
auto chayaTlsEnsureCaBundleInstalled() -> bool;

/** Create CA-bundle mutex during asyncInfraInit (before tasks start). */
void chayaTlsInfraInit();
