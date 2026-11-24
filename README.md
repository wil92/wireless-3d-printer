# wireless-3d-printer

[![status](https://github.com/wil92/wireless-3d-printer/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/wil92/wireless-3d-printer/actions/workflows/ci.yml?query=branch%3Amaster)

This project provides Wi-Fi configuration and network discovery for an
ESP32-S3. It connects to a Marlin printer through native USB host mode to list
and transfer G-code to the printer's SD card.

## Features

- Wi-Fi setup and configuration reset.
- mDNS discovery and a device information endpoint for network clients.
- CH340 USB serial support for listing and uploading files to a Marlin printer.

## Requirements

- Python 3.x
    - htmlmin (library for minifying HTML)
- PlatformIO
- ESP32-S3 SuperMini

## Installation

1. Clone this repository to your local machine: `git clone <repository-url> wireless-3d-printer`
2. Navigate to the project directory: `cd wireless-3d-printer`
3. Install html minifier: `sudo pip install minify_html`

## Testing

```
// run tests
pio test -e tests -vvv
```

## Documentation

- [Build configuration](docs/configuration.md)
- [Continuous integration](docs/continuous-integration.md)
- [ESP32-S3 wiring](docs/hardware.md)
- [Marlin USB integration](docs/marlin-usb.md)
- [Configuration reset](docs/configuration-reset.md)
- [Network discovery and device information API](docs/discovery.md)
- [File management and file list API](docs/file-management.md)
- [Optional network debug logging](docs/network-logging.md)
- [Cura plugin installation and usage](docs/cura-plugin.md)
- [Firmware and Cura plugin versioning](docs/versioning.md)
- [Cura plugin overview](ultimate-cura-plugin/README.md)
- [Scripts](scripts/README.md)
- [Coding rules](docs/coding-rules.md)
