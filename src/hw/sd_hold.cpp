#include "hw/sd_hold.h"

#include <Arduino.h>

#include "hw/pins.h"

void sdHoldOff() {
    pinMode(pins::kSdClk, OUTPUT);
    digitalWrite(pins::kSdClk, LOW);
    pinMode(pins::kSdMiso, OUTPUT);
    digitalWrite(pins::kSdMiso, LOW);
    pinMode(pins::kSdMosi, OUTPUT);
    digitalWrite(pins::kSdMosi, LOW);
}
