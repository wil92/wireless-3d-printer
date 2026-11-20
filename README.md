# Remote SD

This project provides Wi-Fi configuration and network discovery for an
ESP32-S3. SD-card access has been removed; file management and printer USB
integration are not currently available.

## Features

- Wi-Fi setup and configuration reset.
- mDNS discovery and a device information endpoint for network clients.
- Lightweight and easy to deploy on microcontrollers.

## Requirements

- Python 3.x
    - htmlmin (library for minifying HTML)
- PlatformIO
- ESP32-S3 SuperMini

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
- [ESP32-S3 wiring](docs/hardware.md)
- [Configuration reset](docs/configuration-reset.md)
- [Network discovery and device information API](docs/discovery.md)
- [File management and file list API](docs/file-management.md)
- [Cura plugin installation and usage](docs/cura-plugin.md)
- [Coding rules](docs/coding-rules.md)
