# Network debug logging

The optional log receiver uses only Node.js built-in modules. On a trusted
computer on the same network as the ESP32-S3, run:

```sh
node log-server/server.js
```

It listens on all interfaces on port 8080. Set `PORT` to change the port and
`LOG_DIR` to change the output directory. Each server start creates a new,
timestamped `.log` file in `log-server/logs/` by default. It accepts plain
text with `POST /`, appends a newline after each message, and returns HTTP 204
only after writing it. Limit access using a firewall: the server and firmware
use unauthenticated, unencrypted HTTP, so anyone with access can submit logs
and read traffic. The log directory is ignored by Git.

## Released debug firmware

The debug factory image enables network logging without embedding a receiver
address. It initially logs to Serial only. Connect to the diagnostic UART at
9600 baud, using a terminal that sends a newline at the end of each command.
Enter a hostname or IPv4 address and optional port, then restart the device:

```text
debug-address 192.168.1.10:8080
```

The default port is 8080. The receiver must be reachable on the device's LAN;
do not use `127.0.0.1`. The command saves the address in the ESP32's nonvolatile
preferences and reports success or an error over Serial. To stop network
logging, send `debug-address off` and restart. Changes take effect only after
restart. Commands are accepted while the device is waiting for Wi-Fi as well
as during normal operation. The address persists across configuration resets, but flashing a
factory image may erase it along with other saved settings. Use the debug
firmware's `off` command to remove it. The regular release image does not send
network logs, regardless of the stored address.

## Local builds

Build the configurable variant with `pio run -e esp32dev-debug`. For a
custom regular build, `DEBUG_ADDRESS` enables network logging with a fixed
address. The address can be an IPv4 address or hostname, optionally with an
`http://` prefix and port. When the port is omitted it defaults to 8080:

```sh
DEBUG_ADDRESS=192.168.1.10 pio run -e esp32dev
```

The build sets `NETWORK_DEBUG_ADDRESS` to the full HTTP URL. Use the
receiver's reachable LAN address and specify the actual port if it differs
from 8080:

```sh
DEBUG_ADDRESS=192.168.1.10:9000 pio run -e esp32dev
```

Without that variable, `pio run -e esp32dev` builds without network logging.
Rebuild after changing or removing the address. `esp32dev-debug` ignores
`DEBUG_ADDRESS` and uses its persistent UART configuration instead.

Without the flag, diagnostics go
only to Serial at 9600 baud, as before. With it, diagnostics still go to
Serial and are also queued for network delivery on a low-priority task.
Messages generated before Wi-Fi connects are buffered, subject to a 32-message
limit; full queues drop new messages and oversized messages are truncated.
The UART reports queue, HTTP and startup errors. Failed HTTP deliveries are
not retried, so network logs are best-effort and are not available when the
device never reaches the configured network. Do not put credentials in logs.

Printer upload diagnostics include the target DOS filename, HTTP byte and line
counts, elapsed time, the current printer command and numbered line, response
or timeout details, USB session changes, and whether an abort closed the file.
Print-start diagnostics identify each idle check (`before_listing` and
`before_selection`), bounded M27 and M23 responses,
file-selection confirmation, the M24 send/acknowledgement, and the phase and
error on failure. Paths and printer-provided replies are sanitized and limited
in length; avoid sharing logs if their filenames are sensitive. These logs
describe the existing checks and do not change the print sequence.
Status timeout recovery logs the pending command, synchronization marker,
wait failures and successful recovery. The marker must be echoed and
acknowledged before other operations resume.
Progress is logged at most once every 30 seconds; ordinary G-code lines are not
logged. Unexpected printer responses are bounded and sanitized, but may still
contain printer-provided information, including G-code echoed by the printer.
A numeric ESP boot reset reason helps
distinguish ESP resets from printer resets. The receiver has no timestamps of
its own: printer diagnostics include ESP uptime in milliseconds. Delivery
and timestamps can be incomplete because logging is best-effort.

After a failure, keep the printer idle and verify that it has exited SD write
mode before retrying; see [Marlin USB integration](marlin-usb.md). Preserve the
receiver's log file across a retry, including the startup and failure messages.

Run the receiver's host test with `node --test log-server/server.test.js`.
