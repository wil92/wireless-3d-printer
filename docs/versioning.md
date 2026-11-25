# Firmware and Cura plugin versions

The firmware and Cura plugin have **independent** versions. Both begin at
`1.0` and use exactly two non-negative integer components, `major.minor`
(without leading zeroes). The firmware version is `FIRMWARE_VERSION` in
`include/version.h`; the plugin version is `version` in
`ultimate-cura-plugin/Wireless3DPrinter/plugin.json`, which is also its Cura
package metadata. Do not copy one component's version to the other just
because it changed.

Before changing an affected component's version, check the release tags
locally and on the remote. A component version is released if it appears in
any `v<firmware-version>-cura<plugin-version>` tag, even when the other
component's version differs. Fetch or inspect remote tags as well as local
tags; if the remote cannot be checked, do not assume the version is unreleased.
If the current component version has no release tag, retain it for subsequent
changes. If it has been tagged, increment it once for the accumulated changes:

- Increment the **major** and reset the minor to zero for a change that
  breaks compatibility between the plugin and firmware, for example `1.3` to
  `2.0`.
- Increment the **minor** for a fix or behavior change that preserves
  compatibility, for example `1.3` to `1.4`.

If both components change, assess and update each independently. Once a
component has been bumped to an untagged version, retain that version for
further unreleased changes. If those changes introduce a compatibility break,
choose the appropriate major version before release. For a
coordinated breaking change, update the major of each component when its
change adopts the new incompatible protocol. Minor versions need not match;
equal majors indicate intended compatibility, not a guarantee of it. Review
protocol changes and tests as well as version numbers. Documentation-only
or test-only changes do not require a component version bump unless they
change a shipped component's behavior.

Both components start at the `1.0` baseline. Rebuild and reinstall the plugin
or rebuild and upload the firmware as appropriate; changes to source version
values do not update already installed software.

## Creating a release

The release tag identifies both independently versioned components as
`v<firmware-version>-cura<plugin-version>`, for example `v1.1-cura1.0`.
Each component-version pair can be released only once. The tag must match
both version files in the tagged commit.

To prepare a human-readable GitHub release description, use the
[`release-changelog` skill](../.github/skills/release-changelog/SKILL.md).
It compares release history and produces Markdown for copying into the
description; it does not publish or modify a release.

1. Update affected component versions according to the policy above, commit
   and push all changes to `master`, and confirm the CI run passes.
2. Switch to `master` and run the release script:

   ```sh
   python3 scripts/create_release_tag.py
   ```

   Configure `origin` to push to the GitHub repository. The script requires a
   clean working tree, a single push URL for `origin`,
   and a local `master` commit identical to the remote `master`. It reads both
   versions, rejects existing local or remote tags, creates an annotated tag,
   and displays the tag, versions, and commit before asking for confirmation.
   Enter or `y` pushes the tag to `origin`; `n` deletes the new local tag
   without pushing it. End-of-input or an interrupt also cancels it. If the
   push fails, the local tag is retained because the remote may have accepted
   it before the connection failed. Check whether the tag exists on the
   remote and points to the intended commit. If it is absent, delete the local
   tag with `git tag -d <tag>` before rerunning the script. If the remote
   cannot be checked, retain the local tag until its state is known.
3. Check the tag's Actions run and the resulting GitHub release. Only a tag
   push can publish a release; its commit must be reachable from `master`.
   Both build and test jobs must succeed first. A failed release job does not
   automatically change a published release. Investigate the failure before
   rerunning the job; if a draft was left behind, inspect and remove it before
   retrying. Do not move or reuse published tags.

Publishing marks the new release as GitHub's latest stable release. A new
release with no change to either component version would reuse the same tag
and is rejected; do not bump a component solely for documentation or tests.

The release contains factory images
`wireless-3d-printer_<firmware-version>.bin` and
`wireless-3d-printer_debug-mode.<firmware-version>.bin`, and
`Wireless3DPrinter_<plugin-version>.zip`. The factory images include the
bootloader, partition table, and application and are written at flash offset
`0x0` on the supported 4 MB ESP32-S3 board. Verify the board and use a
compatible ESP32-S3 flashing tool and UART connection; flashing can overwrite
existing firmware and settings. The debug image's receiver is configured over
UART as described in [network logging](network-logging.md).

The published Cura ZIP is built on Linux x86_64 using Python 3.11 and can
contain native modules. Use it only with a compatible Cura Python runtime on
that platform. Build locally for other systems or Cura Python versions as
described in [Cura plugin installation](cura-plugin.md#build-and-install).
Automated CI does not replace live Cura or printer/device validation.

## Device endpoint and compatibility warning

On a configured device, `GET /api/version` returns HTTP 200,
`Content-Type: application/json`, `Cache-Control: no-store`, and a JSON body:

```json
{"firmware_version":"1.1"}
```

The endpoint returns 404 during setup. It reports the installed firmware
version only: the installed Cura plugin has its own version and may be
updated independently. This is separate from `/api/device`'s integer
`api_version`, which describes the device-information response contract.

After selecting a printer, the plugin retrieves the firmware version and
compares its major to the installed plugin major. A mismatch displays a
warning containing both versions; it does not block printer operations.
An unavailable, malformed, or unsupported version response also produces a
warning that compatibility could not be verified. File management remains
available, but uploads require a matching major version and a valid response,
because older firmware cannot honor the long-name upload request. Older firmware may respond
with a web page instead of the version endpoint. Update the older component,
search again, and reconnect to recheck. HTTP is unauthenticated and
unencrypted; use a trusted network.
