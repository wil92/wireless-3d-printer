#pragma once

#if __has_include("reset_configuration_pin.h")
#include "reset_configuration_pin.h"
#else
#include "reset_configuration_pin.example.h"
#endif

namespace HardwarePins {
constexpr int resetConfig = RESET_CONFIG_PIN;
// Fixed by the ESP32-S3 internal USB PHY.
constexpr int usbDataMinus = 19;
constexpr int usbDataPlus = 20;

static_assert(resetConfig != usbDataMinus && resetConfig != usbDataPlus,
              "Configuration reset pin must not use the native USB pins");
}
