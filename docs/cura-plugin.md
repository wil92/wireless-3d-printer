# Cura wireless-3d-printer plugin

The `ultimate-cura-plugin` directory contains a Cura 5 output-device plugin.
It uploads G-code to the printer through the ESP32's USB host connection.
Rebuild and reinstall the plugin after updating the firmware: previous
versions use different discovery identifiers and cannot discover renamed
firmware. Remove the old `RemoteSD` plugin directory before installing the
new plugin so Cura does not show both versions.

First select a device in the **LOCAL PRINTER** tab. After slicing, select
**Save to wireless-3d-printer** from the arrow beside Cura's save button,
then click the save button. This option is available only while a printer is
selected. Confirm the selected device's name, ID, and address before uploading.
The plugin checks the device ID at that address again before transfer; if it
has changed or the device is unavailable, search for printers again.
Keep the printer idle and do not start a print during the transfer. Names
are shortened to DOS 8.3 and collisions are rejected; the resulting printer
filename is shown after success. See [file management](file-management.md).

HTTP and mDNS are unencrypted and unauthenticated, so use a trusted network.

## Local Printer tab

Choose **LOCAL PRINTER** to manage files without starting an upload. Select
**Search for printers**; a single discovered device is selected automatically.
If several are found, choose one and click **Connect**. The tab shows the
device name and the printer's file names and sizes. Use **Refresh files** to
reload the list; the bordered table has a visible vertical scrollbar for long
lists. The tab polls SD activity and nozzle/bed temperatures every 10 seconds.
On connection, a warning appears if the plugin and firmware major versions
differ or their compatibility cannot be verified. Operations remain available.
See [versioning](versioning.md) for the version policy and recovery.
Polling runs quietly in the background and keeps the last values visible until
they change. If a poll fails, the tab reports the error and marks status
unavailable until a later poll succeeds.
Printing and deletion are disabled while an SD print is active or paused, or status is
unavailable. The upload endpoint also rejects active SD prints. **Not SD
printing does not mean idle:** leveling, motion and jobs streamed by another
host are not detectable. The operator must ensure the printer stays idle
throughout file operations. See [status monitoring](marlin-usb.md#status-monitoring).

Files matching Cura's current job name, either by long name or the shortened
8.3 upload name, have a subtle row highlight and are labeled **current model
name**. This is a filename match, not proof that the contents are the same;
shortened names can collide.
Use **Print** and confirm to start a listed file. Make sure the printer is
idle, safe and ready before confirming. The tab immediately refreshes status
after the request; an acknowledged start does not prove the job started.
If the response is lost or an error says the print may have started, check the
printer before retrying. Updated firmware is required.
Use the light-red **Delete** button and confirm to remove a file by its printer
path. Select individual files using the first column, or use its header
checkbox to select or deselect all, then choose **Delete selected** and
confirm. Bulk deletion stops at the first error and may already have removed
some files. Refresh after an error or timeout to obtain an up-to-date list
before retrying. Refreshing, scanning again, or changing printers clears the
selection. Errors appear in the tab. Keep the printer idle and do not start a
print or another file operation during deletion. Bulk deletion requires updated
firmware.

Searching again clears the selected printer and hides the wireless save option
until a printer is selected again. An upload already confirmed remains bound to
that printer even if the selection later changes.

## Build and install

Requirements: Cura 5 (SDK 8), Python 3.7 or later with pip, and network access to
download the pinned dependencies. Build on the target operating system using a
Python interpreter compatible with the Python version bundled with that Cura
installation. Some platforms may build or download native dependency modules;
a zip built for one OS, architecture or Python version is not necessarily
portable to another.

The GitHub release ZIP is built for Linux x86_64 and Python 3.11. For another
platform or Cura Python version, build the ZIP locally as described below.

From the repository root:

```sh
python3 ultimate-cura-plugin/build.py
```

The output is `ultimate-cura-plugin/dist/Wireless3DPrinter.zip`. Close Cura, then extract
the `Wireless3DPrinter` directory from the zip into Cura's user plugin directory:

- Linux: `${XDG_DATA_HOME:-~/.local/share}/cura/<major.minor>/plugins`
- Windows: `%APPDATA%\cura\<major.minor>\plugins`
- macOS: `~/Library/Application Support/cura/<major.minor>/plugins`

For example, Cura 5.4 uses `5.4`. Create `plugins` if needed. The installed layout must be
`plugins/Wireless3DPrinter/plugin.json`, not an extra nested `Wireless3DPrinter` directory.
Restart Cura and enable the plugin in the plugin manager if it is disabled.
Remove `plugins/Wireless3DPrinter` and restart Cura to uninstall it.

On Linux only, after building and closing Cura, you can instead run:

```sh
ultimate-cura-plugin/upload-plugin.sh
```

The script selects the latest numeric version directory in
`~/.local/share/cura`, replaces its `plugins/Wireless3DPrinter` directory,
and extracts the built zip. It does not remove the legacy `RemoteSD` directory;
remove that separately if present. Restart Cura after installation.

The zip is a manually installed plugin directory, not a signed Marketplace
`.curapackage`. Building it uses pip and includes the dependencies and their
license notices in the archive. For Marketplace distribution, follow
[UltiMaker's package instructions](https://github.com/Ultimaker/Cura/wiki/Creating-Packages-Plugins-For-The-Marketplace)
and meet its licensing and distribution requirements.

## Troubleshooting

The device must already be configured and connected to the same network as
Cura; setup mode does not advertise the discovery service. Multicast DNS can be
blocked by firewalls, Wi-Fi client isolation or network segmentation. A device
that disappears during the scan or cannot answer `/api/device` is not shown.
Try again when it is available. There is currently no manual-address fallback.
If discovery reports a missing Python dependency, rebuild and reinstall the
complete zip rather than copying the source directory alone.

## Development

Run the host tests from the repository root:

```sh
python3 -m unittest discover -s ultimate-cura-plugin/tests -v
```

The transport tests use a local HTTP server and simulated discovery. The Cura
UI integration tests require Cura/Uranium and PyQt6 in the Python environment;
they are skipped if those imports are unavailable. A live Cura UI and a real
wireless-3d-printer device are needed to verify end-to-end operation. No test uploads
firmware.
