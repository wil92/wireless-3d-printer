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

Set `DEBUG_ADDRESS` when building to enable network logging. The address can be
an IPv4 address or hostname, optionally with an `http://` prefix and port.
When the port is omitted it defaults to 8080, matching the receiver:

```sh
DEBUG_ADDRESS=192.168.1.10 pio run -e esp32dev-debug
```

The `esp32dev-debug` environment inherits the normal ESP32 configuration and
requires `DEBUG_ADDRESS`. The build sets `NETWORK_DEBUG_ADDRESS` to the full
HTTP URL. Use the receiver's reachable LAN address, not `127.0.0.1`, and
specify the actual port if it differs from 8080:

```sh
DEBUG_ADDRESS=192.168.1.10:9000 pio run -e esp32dev-debug
```

The regular environment also enables network logging when `DEBUG_ADDRESS` is
set, so `DEBUG_ADDRESS=192.168.1.10 pio run -e esp32dev` works as well.
Without that variable, `pio run -e esp32dev` builds without network logging.
Rebuild after changing or removing the address.

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
