# Remote SD Card Access over Network

This project provides network access to a microSD card connected to an
ESP32-S3 over 4-bit SDMMC.

This project is useful for applications where you need to manage files on an SD card without physically accessing the
device, like 3D printers, IoT devices, or embedded systems.

## Features

- Read and write files on the SD card over the network via HTTP.
- Simple web interface for file management.
- mDNS discovery and a device information endpoint for network clients.
- Lightweight and easy to deploy on microcontrollers.

## Requirements

- Python 3.x
    - htmlmin (library for minifying HTML)
- PlatformIO
- ESP32-S3 SuperMini with compatible 3.3 V microSD hardware
- External SD bus switching hardware for sharing the card with a printer

## Installation

1. Clone this repository to your local machine: `git clone`
2. Navigate to the project directory: `cd remote-sd-card-access`
3. Install html minifier: `sudo pip install minify_html`

## Testing

```
// run tests
pio test -e tests -vvv
```

## Documentation

- [Build configuration](docs/configuration.md)
- [ESP32-S3 wiring and SDMMC](docs/hardware.md)
- [Configuration reset](docs/configuration-reset.md)
- [Network discovery and device information API](docs/discovery.md)
- [File management and file list API](docs/file-management.md)
- [Cura plugin installation and usage](docs/cura-plugin.md)
- [Coding rules](docs/coding-rules.md)
