# ESP32-S3 and microSD wiring

The firmware targets an ESP32-S3 SuperMini and uses 4-bit SDMMC at up to
20 MHz. GPIO numbers below are ESP32-S3 GPIO numbers, not physical header pin
numbers. SuperMini designs vary; verify that your board exposes these GPIOs
and that they are not connected to conflicting onboard circuitry.

| Connection | ESP32-S3 GPIO |
| --- | --- |
| microSD CLK | 1 |
| microSD CMD | 2 |
| microSD D0 | 4 |
| microSD D1 | 5 |
| microSD D2 | 6 |
| microSD D3 | 7 |
| Configuration-reset button | 8 |
| External SD switching control | 9 |

Use a bare socket or breakout exposing CMD, CLK, and all four data lines.
Many SPI-only SD modules do not expose D1 and D2. Connect the card's VDD to
3.3 V and VSS to GND; these power connections are additional to the eight
GPIOs. The card and ESP32-S3 signals must be 3.3 V. A module with a regulator
may have different supply requirements: consult its documentation and verify
that its signal path supports bidirectional 3.3 V SDMMC, not just SPI.

Add separate 10 kΩ pull-ups from CMD and D0–D3 to 3.3 V unless the breakout
already provides suitable pull-ups. Do not pull up CLK. Keep connections short;
the actual maximum reliable clock depends on the wiring and switch hardware.
See [Espressif's SD pull-up requirements](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/sd_pullup_requirements.html).

Connect a normally open configuration-reset button between GPIO8 and GND.
The firmware enables the internal pull-up; pressing the button reads low.
See [configuration reset](configuration-reset.md).

## Sharing the card

GPIO9 is an **active-low control signal**, not an SD data line and not a
power output: LOW requests ESP32 access, HIGH releases access to the other
host. The firmware initializes it HIGH and waits 100 ms after requesting
access before mounting. It unmounts the card before releasing access. Use an
external switch or multiplexer designed for the SD signals and the actual
printer interface, with a safe default while the ESP32 is reset or unpowered.
The exact circuit depends on the switching hardware and printer; this project
does not specify or validate a particular circuit.

Do not connect two active hosts directly to the same SD bus. Ensure the
hardware isolates all necessary lines and prevents simultaneous access, and
that the printer handles the card being removed and reinserted. Never switch
the card during a print or another host's read/write operation. SDMMC support
alone does not establish safe hardware switching or filesystem coherence.

The firmware logs mount and file errors on serial; an upload or delete returns
HTTP 500 if mounting fails. It never formats the card on a mount failure. Hardware
operation and switching timing must be verified with the actual board, card,
switch and printer.
