# Wireless 3D Printer for Cura

[![status](https://github.com/wil92/wireless-3d-printer/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/wil92/wireless-3d-printer/actions/workflows/ci.yml?query=branch%3Amaster)

Discover wireless-3d-printer devices on your local network, browse and delete
files on the printer, and send sliced G-code using Cura's save button.

Build the manual-install zip with `python3 ultimate-cura-plugin/build.py` from
the repository root. Close Cura, extract `dist/Wireless3DPrinter.zip` into the
Cura user plugins directory, and restart Cura. To update, replace the existing
`Wireless3DPrinter` directory with the new one; to uninstall, remove it.

**Linux only:** after building and closing Cura, run
`ultimate-cura-plugin/upload-plugin.sh` from the repository root to replace
the plugin in the latest Cura version under `~/.local/share/cura`.

See the [plugin guide](../docs/cura-plugin.md) for platform-specific installation,
usage, troubleshooting, and testing. Use only a trusted network.
