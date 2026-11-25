# File management

The configured device page lists printable files on the printer's SD card and
uploads `.gcode` files through its USB connection. The upload form appears
above the automatically loaded file list. Refresh updates the list, and Remove
deletes a selected file after confirmation. Keep the printer idle and
do not start a print or use another host during a transfer or deletion. The
printer cannot enforce this restriction; an M28 that races with a print may
abort the job. See [Marlin USB integration](marlin-usb.md) for limitations,
recovery and supported G-code.

Browser and standard API uploads map the name to an uppercase 8-character DOS base name plus `.GCO`:
letters, digits, `_` and `-` are retained, other characters become `_`, and
the base is truncated to eight characters. For example, `my print.gcode`
becomes `MY_PRINT.GCO`. The upload is rejected if the printer lists an
existing root file with that name; files are not intentionally overwritten.
The returned name is the one to select on the printer.

The Cura plugin instead requests a long name such as
`WRP-ESP32_S3_SUPERMINI_BOX.GCO`. This requires a printer that can write long
filenames with M28 and report them through M20 L. The ESP32 rejects an existing
matching long name, refuses a long upload when a root DOS alias has no long
name available for collision checking, and verifies the name in the listing
after M29. If verification fails, the file may nevertheless have been written;
refresh the list before retrying. The operator must prevent other hosts from
changing files during the operation.

To compare local copies of files, see the
[comparison script](compare-local-files.md).

## HTTP API

- `GET /api/status` returns e.g.
  `{"printer_name":"Workshop","nozzle_temperature":205.25,"bed_temperature":60.50,"printer_status":"not_sd_printing"}`.
  Temperatures are degrees Celsius; an unavailable bed temperature is `null`.
  The status is `not_sd_printing` or `sd_printing` (including paused SD jobs).
  It does **not** detect leveling or other non-SD activity. It returns 404
  during setup and 503 if status cannot be established. See
  [Marlin USB integration](marlin-usb.md#status-monitoring).
- `GET /api/files` returns a JSON array such as
  `[{"name":"My print.gcode","size":123,"path":"MYPRIN~1.GCO"}]`.
  `name` is the long display name when the printer supports `M20 L`, falling
  back to its DOS path otherwise. `path` is the DOS path used for operations;
  sizes are in bytes. At most 256 entries are accepted; an incomplete or
  larger listing fails instead of producing a partial result.
  Listing and status requests are read-only and can be requested while an SD
  print is active or paused, although long-running printer commands can delay
  or time out the response.
- `POST /upload?name=<url-encoded-filename>` accepts a nonempty raw body with
  `Content-Type: application/octet-stream` and `Content-Length` (up to
  100,000,000 bytes). Chunked transfer encoding and multipart are not
  supported. A successful response is `Saved as NAME.GCO`. Existing clients
  using the former multipart contract must be updated.
- Add `&long_name=1` to request a long Cura-style filename. The name must be
  `WRP-`, followed by uppercase ASCII letters, digits or underscores, and
  `.gcode`. The base, including `WRP-`, is limited to 85 characters to fit
  the M28 command. The stored extension is `.GCO`. Long-name uploads require
  printer support for creating and listing long names; an error after transfer
  can mean that the file was saved but its full name could not be verified.
- `DELETE /api/files?path=<url-encoded-DOS-path>` deletes a listed file using
  Marlin M30. The printer must report idle and confirm the exact deletion.
  Read-only media, unsupported commands and missing confirmations fail.
- `POST /api/files/print?path=<url-encoded-DOS-path>` selects a listed file
  with M23 and sends M24 to start it. The path follows the deletion rules and
  must fit the printer's serial command limit (at most 88 characters). The
  printer must report no SD print and confirm the selected filename and size.
  HTTP 200 means M24 was acknowledged, not that printing completed or even
  necessarily started. If the response is lost or an error occurs after M24,
  the print may have started; check the printer before retrying. Ensure the
  printer is safe and ready to print and avoid other hosts or starting jobs
  during this operation.
- `POST /api/files/delete` deletes 1–256 files in order. Send a raw
  `text/plain` body with `Content-Length` containing DOS paths separated by
  LF (no trailing LF), for example `ONE.GCO\nDIR/TWO.GCO`. The body is limited
  to 33,023 bytes; each path is at most 128 ASCII characters and must satisfy
  the same path rules as single-file deletion. Duplicate paths, ignoring case,
  are rejected before any deletion. Chunked encoding is not supported.
  Each deletion independently checks the printer's idle status and listing.
  The operation stops on the first failure with HTTP 409 and a message naming
  the failed path and the number confirmed deleted; deletions are not atomic.
  The failed file may also have been deleted if confirmation was lost.
  A connection loss or timeout can also leave an unknown partial result. Refresh
  the file list before retrying.

Operations return HTTP 404 in setup mode, HTTP 409 for upload preflight
failures (including a disconnected printer), print and delete failures, and HTTP 503 for file-list or
in-progress upload failures. Upload failures can leave a partial file or the
printer in write mode. A successful HTTP response is sent only after the
printer confirms M29 completion. HTTP is unauthenticated and unencrypted; use a
trusted network.
