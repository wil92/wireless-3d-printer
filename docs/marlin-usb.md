# Marlin USB integration

The target is a Voxelab Aquila X2 with an N32G455 controller, CH340G
USB-to-serial bridge, and
[MRiscoCProUI](https://github.com/classicrocker883/MRiscoCProUI) firmware.
The ESP32-S3 acts as USB host using `EspUsbHost`, identifies CH340 devices
with USB VID:PID `1a86:7523`, and configures 115200 baud, 8 data bits, no
parity and one stop bit. It uses a bounded receive queue and fails operations
on overflow or a changed USB connection. Hardware and power must be verified
as described in [hardware](hardware.md). **This integration has not been
tested on the physical printer.**

## Protocol and restrictions

The implementation synchronizes with `M115` (the printer build must enable
`CAPABILITIES_REPORT`), then checks
[M27](https://marlinfw.org/docs/gcode/M027.html) before an upload and again
after collision checking. The referenced firmware reports active and paused
SD jobs as `SD printing byte ...`; only a complete `Not SD printing` response
and acknowledgement permits an upload. M27 cannot detect a job streamed from
another host or prevent a job from starting at the printer. **The operator
must keep the printer idle and must not start a job or use another host
throughout a transfer.** In this firmware M28 opens the file by calling
`abortFilePrintNow()`, so a race can interrupt a print.

[M20 L](https://marlinfw.org/docs/gcode/M020.html) supplies long display names
when `LONG_FILENAME_HOST_SUPPORT` is enabled, as well as DOS paths for file
operations. Builds without it return DOS names only. M20 supplies the file list and
the preflight collision check. A complete list and acknowledgement are
required. The firmware limits listings to 256 entries, filenames to 128
bytes and response lines to 1024 bytes. It does not mount media automatically;
the printer must have an SD card mounted and writable.

Before [M28](https://marlinfw.org/docs/gcode/M028.html), the host resets the
serial line number with `M110`. Each nonempty G-code line is sent with a
sequential `N` number and XOR checksum, waiting for `ok` before sending the
next. Errors and resend requests stop the transfer rather than retrying.
Semicolon comments and blank lines are discarded. Lines containing control
characters, non-ASCII command text, quotes, parentheses, backslashes,
checksums, existing line numbers, `M29` (except commands such as `M290`),
or the substring `M110` are
rejected. The entire numbered and checksummed line must fit in 94 bytes;
some printer builds may use a smaller configured limit. The stored file is
not byte-identical to the input. [M29](https://marlinfw.org/docs/gcode/M029.html)
must return both the saved message and acknowledgement before success is
reported.

[M30](https://marlinfw.org/docs/gcode/M030.html) deletes a listed DOS path
after two idle checks and requires Marlin's matching `File deleted:` message
and acknowledgement. The operator must keep the printer idle and avoid other
hosts throughout deletion: M30 closes the current file, and the idle check
cannot prevent a race with a new print.

Timeouts, malformed responses, unsupported lines, disconnected USB, SD write
errors and interrupted HTTP uploads fail explicitly. When possible, an
aborted upload is closed with M29; it can leave a partial file. If M28 or M29
completion is uncertain, further operations are blocked until the ESP32 is
restarted. Before retrying, verify the printer is idle, check whether it is
still in SD write mode, close that mode using a trusted host if necessary,
and remove any partial file from the printer UI. Do not start a print until
the incomplete file is removed or verified.

Details were checked against Marlin's documentation and MRiscoCProUI at
revision `a9fb83ac8b85868fd2978f96841c325ec3ad5a01`:
[command queue](https://github.com/classicrocker883/MRiscoCProUI/blob/a9fb83ac8b85868fd2978f96841c325ec3ad5a01/Marlin/src/gcode/queue.cpp),
[card reader](https://github.com/classicrocker883/MRiscoCProUI/blob/a9fb83ac8b85868fd2978f96841c325ec3ad5a01/Marlin/src/sd/cardreader.cpp)
and [M27](https://github.com/classicrocker883/MRiscoCProUI/blob/a9fb83ac8b85868fd2978f96841c325ec3ad5a01/Marlin/src/gcode/sd/M27.cpp).
Actual behavior depends on the firmware build and configuration installed on
the printer.
