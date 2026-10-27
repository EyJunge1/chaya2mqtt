#include "hw/touch_hold.hpp"

#include <driver/gpio.h>

#include "hw/pins.hpp"

namespace {
void forceGpioLowHeld(gpio_num_t gpio) {
    (void)gpio_reset_pin(gpio);
    (void)gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    (void)gpio_set_pull_mode(gpio, GPIO_FLOATING);
    (void)gpio_set_level(gpio, 0);
    // Keep level across sleep / stray reconfigs — touch is never enabled on this SKU.
    (void)gpio_hold_en(gpio);
}
} // namespace

void touchHoldOff() {
    forceGpioLowHeld(static_cast<gpio_num_t>(pins::kEpdTpRst));
    forceGpioLowHeld(static_cast<gpio_num_t>(pins::kEpdTpInt));

    // RTC INT (open-drain): defined idle, not a touch line.
    const auto rtcInt = static_cast<gpio_num_t>(pins::kRtcInt);
    (void)gpio_reset_pin(rtcInt);
    (void)gpio_set_direction(rtcInt, GPIO_MODE_INPUT);
    (void)gpio_set_pull_mode(rtcInt, GPIO_PULLDOWN_ONLY);
}
