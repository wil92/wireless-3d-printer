# Firmware and Cura plugin versions

The firmware and Cura plugin have **independent** versions. Both begin at
`1.0` and use exactly two non-negative integer components, `major.minor`
(without leading zeroes). The firmware version is `FIRMWARE_VERSION` in
`include/version.h`; the plugin version is `version` in
`ultimate-cura-plugin/Wireless3DPrinter/plugin.json`, which is also its Cura
package metadata. Do not copy one component's version to the other just
because it changed.

For each change to either component, update that component's version once
as part of the change:

- Increment the **major** and reset the minor to zero for a change that
  breaks compatibility between the plugin and firmware, for example `1.3` to
  `2.0`.
- Increment the **minor** for a fix or behavior change that preserves
  compatibility, for example `1.3` to `1.4`.

If both components change, assess and update each independently. For a
coordinated breaking change, update the major of each component when its
change adopts the new incompatible protocol. Minor versions need not match;
equal majors indicate intended compatibility, not a guarantee of it. Review
protocol changes and tests as well as version numbers. Documentation-only
or test-only changes do not require a component version bump unless they
change a shipped component's behavior.

The initial versioning implementation establishes the `1.0` baseline for
both components. For later changes, rebuild and reinstall the plugin or
rebuild and upload the firmware as appropriate; changes to source version
values do not update already installed software.

## Device endpoint and compatibility warning

On a configured device, `GET /api/version` returns HTTP 200,
`Content-Type: application/json`, `Cache-Control: no-store`, and a JSON body:

```json
{"firmware_version":"1.0"}
```

The endpoint returns 404 during setup. It reports the installed firmware
version only: the installed Cura plugin has its own version and may be
updated independently. This is separate from `/api/device`'s integer
`api_version`, which describes the device-information response contract.

After selecting a printer, the plugin retrieves the firmware version and
compares its major to the installed plugin major. A mismatch displays a
warning containing both versions; it does not block printer operations.
An unavailable, malformed, or unsupported version response also produces a
warning that compatibility could not be verified. Older firmware may respond
with a web page instead of the version endpoint. Update the older component,
search again, and reconnect to recheck. HTTP is unauthenticated and
unencrypted; use a trusted network.
