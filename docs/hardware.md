# ESP32-S3 hardware

The firmware targets an ESP32-S3 SuperMini using the PlatformIO
`esp32-s3-devkitc-1` board profile. Board variants differ; verify the flash
size, exposed GPIOs, and connections before use.

| Connection | ESP32-S3 GPIO |
| --- | --- |
| Configuration-reset button | 8 |

Connect a normally open button between GPIO8 and GND. The firmware enables the
internal pull-up; pressing the button reads low. See
[configuration reset](configuration-reset.md).

The firmware no longer drives an SD card or an SD bus switch. With both
devices powered off, **disconnect the old shared-card switching hardware before
powering this firmware**, unless
its independent circuitry has been verified to maintain a safe state without
ESP32 control. Do not connect two active hosts to the same SD bus.

Printer USB host support is not implemented. Do not use the USB port to connect
the printer until the host driver, power arrangements, and wiring are defined
and verified for the actual hardware.
