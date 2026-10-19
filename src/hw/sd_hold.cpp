#include "hw/sd_hold.hpp"

#include <Arduino.h>

#include "hw/pins.hpp"

void sdHoldOff() {
    pinMode(pins::kSdClk, OUTPUT);
    digitalWrite(pins::kSdClk, LOW);
    pinMode(pins::kSdMiso, OUTPUT);
    digitalWrite(pins::kSdMiso, LOW);
    pinMode(pins::kSdMosi, OUTPUT);
    digitalWrite(pins::kSdMosi, LOW);
}
