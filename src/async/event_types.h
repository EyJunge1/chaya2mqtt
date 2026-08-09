#pragma once

#include <cstdint>

/** Network-task command queue. */
enum class NetCmd : uint8_t {
    MqttSettingsChanged,
    MqttKillClient,
    WifiReconnect,
    ChayaSendRequested,
    FactoryResetRequested,
};

/** Message to dedicated display task (single SPI user). */
struct DisplayMsg {
    enum class Cmd : uint8_t {
        DrawHeart,
        DrawSplash,
    };
    Cmd      cmd;
    uint32_t payload;
};
