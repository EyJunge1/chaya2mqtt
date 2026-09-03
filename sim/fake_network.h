#pragma once

/** Controllable network / NTP status for simulator scenarios. */
struct FakeNetwork {
    bool wifiConnected = false;
    bool wifiStable = false;
    bool ntpSynced = false;

    void setOffline() {
        wifiConnected = false;
        wifiStable = false;
        ntpSynced = false;
    }

    void setWifiUpUnstable() {
        wifiConnected = true;
        wifiStable = false;
        ntpSynced = false;
    }

    void setReadyForMqtt() {
        wifiConnected = true;
        wifiStable = true;
        ntpSynced = true;
    }
};
