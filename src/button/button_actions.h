#pragma once

/**
 * Policy hooks for the button task (QUAL-07). Input code only emits actions;
 * Network/App/MQTT/OTA decide availability.
 */
using ButtonRequestSendFn = void (*)();
using ButtonSoftOffAllowedFn = bool (*)();
using ButtonPerformSoftOffFn = void (*)();

struct ButtonActionHooks {
    ButtonRequestSendFn requestSend       = nullptr;
    ButtonSoftOffAllowedFn softOffAllowed = nullptr;
    ButtonPerformSoftOffFn performSoftOff = nullptr;
};

void buttonSetActionHooks(const ButtonActionHooks& hooks);
const ButtonActionHooks& buttonActionHooks();
