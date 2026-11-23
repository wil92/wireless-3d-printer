# ESP32-S3 hardware

The firmware targets an ESP32-S3 SuperMini using the PlatformIO
`esp32-s3-devkitc-1` board profile. Board variants differ; verify the flash
size, exposed GPIOs, and connections before use.

| Connection | ESP32-S3 GPIO |
| --- | --- |
| Configuration-reset button | 8 by default (configurable) |
| Native USB D− (printer host) | 19 |
| Native USB D+ (printer host) | 20 |

Connect a normally open button between the configured reset GPIO and GND. The
firmware enables the internal pull-up; pressing the button reads low. To change
the default GPIO 8, copy `include/reset_configuration_pin.example.h` to
`include/reset_configuration_pin.h` and set `RESET_CONFIG_PIN` to the desired GPIO
number, then rebuild and flash the firmware. The local header is ignored by Git.
Choose an exposed GPIO that supports input and an internal pull-up and is safe
for your board's wiring and boot configuration. GPIO 19 and 20 cannot be used
for reset. After adding or removing the local header, run
`pio run -e esp32dev -t clean` before rebuilding so PlatformIO picks up the
change. See
[configuration reset](configuration-reset.md).

The firmware no longer drives an SD card or an SD bus switch. With both
devices powered off, **disconnect the old shared-card switching hardware before
powering this firmware**, unless
its independent circuitry has been verified to maintain a safe state without
ESP32 control. Do not connect two active hosts to the same SD bus.

GPIO19 and GPIO20 are fixed by the ESP32-S3 internal USB PHY; do not swap
them or reassign them to other functions. The intended connection uses the
SuperMini's native USB-C connector and the printer's USB connector, not
GPIO UART wiring. See Espressif's
[USB PHY pin definitions](https://github.com/espressif/esp-idf/blob/ad20ded62a2d67a26e501fdc1905f43ba7ad6c81/components/soc/esp32s3/include/soc/usb_pins.h).

The firmware uses the native connector in USB host mode for a CH340G device
at 115200 baud. USB CDC logging on this connector is disabled. Firmware
flashing and diagnostics need a separate suitable UART connection or another
board-specific flashing method; check the SuperMini's actual wiring.

The printer's CH340G is a USB device; its connector must not be assumed to
supply 5 V to the ESP32. Verify the actual SuperMini and printer schematics,
USB-C host role, VBUS supply, current limits, common ground and back-power
protection before connecting. A separately supplied 5 V rail from the printer
board is only suitable after those checks; connecting VBUS directly between
two powered boards can be unsafe. Connector implementations vary by board.
The [Marlin integration notes](marlin-usb.md) describe the protocol and its
operational restrictions. This integration has not been validated on the
physical printer.
