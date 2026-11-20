# File management

The configured device page lists printable files on the printer's SD card and
uploads `.gcode` files through its USB connection. Keep the printer idle and
do not start a print or use another host during the entire transfer. The
printer cannot enforce this restriction; an M28 that races with a print may
abort the job. See [Marlin USB integration](marlin-usb.md) for limitations,
recovery and supported G-code.

Uploads map the name to an uppercase 8-character DOS base name plus `.GCO`:
letters, digits, `_` and `-` are retained, other characters become `_`, and
the base is truncated to eight characters. For example, `my print.gcode`
becomes `MY_PRINT.GCO`. The upload is rejected if the printer lists an
existing root file with that name; files are not intentionally overwritten.
The returned name is the one to select on the printer.

## HTTP API

- `GET /api/files` returns a JSON array such as
  `[{"name":"TEST.GCO","size":123}]`. Names are the printer's DOS paths and
  sizes are in bytes. At most 256 entries are accepted; an incomplete or
  larger listing fails instead of producing a partial result.
- `POST /upload?name=<url-encoded-filename>` accepts a nonempty raw body with
  `Content-Type: application/octet-stream` and `Content-Length` (up to
  100,000,000 bytes). Chunked transfer encoding and multipart are not
  supported. A successful response is `Saved as NAME.GCO`. Existing clients
  using the former multipart contract must be updated.
- DELETE is not supported. Use the printer's UI to remove unwanted or partial
  files while no print or transfer is running.

Operations return HTTP 404 in setup mode, HTTP 409 for upload preflight
failures (including a disconnected printer), and HTTP 503 for file-list or
in-progress upload failures. Upload failures can leave a partial file or the
printer in write mode. A successful HTTP response is sent only after the
printer confirms M29 completion. HTTP is unauthenticated and unencrypted; use a
trusted network.
