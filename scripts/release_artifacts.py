"""Prepare and validate the files published by the release workflow."""

import argparse
import json
import re
import shutil
from pathlib import Path
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\Z")
DEBUG_MARKER = b"Debug receiver: use"


def versions(root=ROOT):
    header = (root / "include/version.h").read_text()
    matches = re.findall(r'^\s*#define\s+FIRMWARE_VERSION\s+"([^"]+)"\s*$', header, re.M)
    if len(matches) != 1 or not VERSION.fullmatch(matches[0]):
        raise ValueError("Invalid or ambiguous firmware version")
    plugin = json.loads((root / "ultimate-cura-plugin/Wireless3DPrinter/plugin.json").read_text())
    plugin_version = plugin.get("version")
    if not isinstance(plugin_version, str) or not VERSION.fullmatch(plugin_version):
        raise ValueError("Invalid Cura plugin version")
    return matches[0], plugin_version


def asset_names(firmware, plugin):
    return (
        f"wireless-3d-printer_{firmware}.bin",
        f"wireless-3d-printer_debug-mode.{firmware}.bin",
        f"Wireless3DPrinter_{plugin}.zip",
    )


def release_tag(firmware, plugin):
    if not VERSION.fullmatch(firmware) or not VERSION.fullmatch(plugin):
        raise ValueError("Invalid component version")
    return f"v{firmware}-cura{plugin}"


def validate_tag(tag, firmware, plugin):
    expected = release_tag(firmware, plugin)
    if tag != expected:
        raise ValueError(f"Release tag must match the component versions: {expected}")


def verify(directory, firmware, plugin):
    directory = Path(directory)
    names = asset_names(firmware, plugin)
    if {path.name for path in directory.iterdir()} != set(names):
        raise ValueError("Release assets do not match the component versions")
    for name, debug in zip(names[:2], (False, True)):
        data = (directory / name).read_bytes()
        if len(data) < 0x10000 or data[0] != 0xE9 or firmware.encode() not in data:
            raise ValueError(f"{name} is not a valid versioned ESP32 factory image")
        if (DEBUG_MARKER in data) != debug:
            raise ValueError(f"{name} does not match the expected firmware variant")
    with ZipFile(directory / names[2]) as archive:
        if archive.testzip() is not None:
            raise ValueError("Cura archive failed its integrity check")
        metadata = json.loads(archive.read("Wireless3DPrinter/plugin.json"))
        if metadata.get("version") != plugin:
            raise ValueError("Cura archive version does not match plugin metadata")
        if not any(name.startswith("Wireless3DPrinter/vendor/zeroconf/") for name in archive.namelist()):
            raise ValueError("Cura archive is missing its bundled dependencies")


def package(directory, root=ROOT):
    firmware, plugin = versions(root)
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    names = asset_names(firmware, plugin)
    sources = (
        root / ".pio/build/esp32dev/firmware.factory.bin",
        root / ".pio/build/esp32dev-debug/firmware.factory.bin",
        root / "ultimate-cura-plugin/dist/Wireless3DPrinter.zip",
    )
    for source, name in zip(sources, names):
        shutil.copyfile(source, directory / name)
    verify(directory, firmware, plugin)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("package", "verify", "check-tag"))
    parser.add_argument("--directory", default="release-assets")
    parser.add_argument("--tag")
    args = parser.parse_args()
    if args.action == "check-tag":
        validate_tag(args.tag or "", *versions())
    elif args.action == "package":
        package(args.directory)
    else:
        verify(args.directory, *versions())


if __name__ == "__main__":
    main()
