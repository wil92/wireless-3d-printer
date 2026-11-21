# Build configuration

The firmware uses the example headers in `../include` by default. To customize a
group, copy its example to the corresponding local header and edit that copy:

```sh
cp include/network_config.example.h include/network_config.h
cp include/access_point_config.example.h include/access_point_config.h
cp include/usb_config.example.h include/usb_config.h
```

Local headers under `../include` are ignored by Git. Keep private credentials in
local headers, not in example files or other tracked files.

## Network

`network_config.h` defines `NETWORK_SSID` and `NETWORK_PASSWORD` as C string
literals. Escape quotes and backslashes as needed. Both default to empty
strings. These values initialize the in-memory credentials; they do not
automatically mark the device as configured or bypass the setup access point.
Saved EEPROM credentials replace them when a configured device starts.

The device connects directly in Wi-Fi station mode using the saved credentials
and enables automatic reconnection. It sends `remote-sd` as its DHCP hostname;
routers may cache an older name or display clients differently. Multiple
devices use the same DHCP hostname. This is separate from the setup display
name and the unique mDNS hostname described in [network discovery](discovery.md).
During initial connection it logs status
changes, then prints its local IP address when connected. Serial logs use 9600
baud and do not print the saved credentials. If connection setup fails, the
web server does not start; the reset button remains available.
Optional local HTTP log forwarding is described in
[network debug logging](network-logging.md).

The setup page also accepts a device name, initially `Remote SD`, which is
saved with the credentials and returned by the discovery API. The name must
be 1-64 UTF-8 bytes and cannot contain ASCII control characters. Existing
configurations without a saved name continue to connect and use `Remote SD`;
reset and configure the device again to select a different name. The new
flash format is not recognized by older firmware versions.

The former `NETWORK_SSID` and `NETWORK_PASSWORD` environment variables are no
longer read by the build. The former `HOST`, `PORT`, `URL`, and `WSS` build
flags were unused by this application and have been removed.

## Setup access point

`access_point_config.h` defines `AP_SSID` and `AP_PASSWORD` as C string
literals for the setup access point. The example preserves the existing
`RemoteSDDevice` and `remotesd` defaults. Set a private password in a local
header before deploying the device; the example password is public.

Connect to the setup access point and open `http://192.168.1.1/`. The page
scans nearby Wi-Fi networks automatically; use **Refresh** to scan again, or
enter an SSID manually for a hidden network. Scanning may briefly interrupt
the setup connection because the radio changes channels. SSIDs must be 1-32
UTF-8 bytes. The page shows whether saving succeeded or failed. After a
successful save, the device sends the response, closes the client connection,
and automatically restarts; it does not restart if saving fails.

During setup, `POST /scan` starts an asynchronous scan (HTTP 202); a concurrent
request reuses the running scan. `GET /refresh-ssid` returns HTTP 202 while it
is running, or HTTP 200 and a JSON array of up to 40 visible, UTF-8 SSIDs
from the first 256 scan entries when complete. Hidden and non-UTF-8 SSIDs are
omitted. Failures return HTTP 500 with a text explanation. Both endpoints
return HTTP 404 outside setup mode. Responses are not cached; a new scan
replaces the previous results.

## USB

`usb_config.h` defines `ARDUINO_USB_MODE` and `ARDUINO_USB_CDC_ON_BOOT`; they
default to `1` and `0`, respectively, in `../src/main.h`. These are
Arduino framework settings, so PlatformIO force-includes `src/main.h` in
every firmware compilation, including the framework and libraries. USB CDC
on boot must remain disabled because the native USB PHY is used by the printer
host. Diagnostic `Serial` output uses the UART, not the native USB connector.

After adding, changing, or removing `usb_config.h`, or changing the defaults
in `../src/main.h`, run `pio run -e esp32dev -t clean` before building.
PlatformIO may otherwise reuse objects compiled with the previous settings.

## Building and testing

The `esp32dev` environment uses the pinned pioarduino 55.03.39 platform
(Arduino 3.3.9 / ESP-IDF 5.5.4) and ESP32-S3 DevKitC board profile for the
SuperMini, with a 4 MB flash size and partition layout and no PSRAM enabled.
Confirm the actual board's flash capacity and mode before flashing; board
variants may require different PlatformIO settings. The upload port is
auto-detected unless specified with `--upload-port`. See
[hardware wiring](hardware.md) for the pin assignments.

Build the configured board with `pio run -e esp32dev`. Run host tests
from the project root with `pio test -e tests`. Run the configuration-header tests with
`python3 -m unittest discover -s test -p 'test_configuration.py'`. Host tests
do not upload firmware.
Run `python3 -m unittest discover -s test -p 'test_debug_build_flags.py'` for
debug-address build configuration tests.
Run `python3 scripts/build_views.py --check` and `node test/host/frontend_test.js`
to check generated pages and frontend interactions.
If the pioarduino-managed Python environment cannot import `minify_html`,
install the package in that environment as well as in the Python environment
used to run `scripts/build_views.py` directly.
