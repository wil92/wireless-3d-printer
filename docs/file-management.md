# File management

File management is currently unavailable. The firmware no longer accesses an
SD card, and printer USB integration has not yet been implemented. The
configured device page displays this status instead of upload controls.

`POST /upload`, `GET /api/files`, and `DELETE /?file=...` return HTTP 503
when configured and HTTP 404 in setup mode. They do not read or write files.
The existing Cura plugin can discover the device, but uploads will fail until
a replacement file-transfer backend is implemented.
