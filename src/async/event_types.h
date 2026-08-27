#pragma once

#include <cstdint>

/** Network-task command queue. */
enum class NetCmd : uint8_t {
    MqttSettingsChanged,
    MqttKillClient,
    WifiGotIp,
    WifiReconnect,
    ChayaSendRequested,
    FactoryResetRequested,
};

/** Message to dedicated display task (single SPI user). */
struct DisplayMsg {
    enum class Cmd : uint8_t {
        DrawHeart,
        DrawSplash,
        DrawPowerOff,
    };
    Cmd      cmd;
    uint32_t payload;
};

/** Playback request for the audio task (TX double-pulse / RX single tone). */
struct AudioMsg {
    enum class Kind : uint8_t { Tx, Rx };
    Kind kind;
};
