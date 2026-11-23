# Resetting device configuration

The device stores its configured network credentials and setup status in
EEPROM. To return to the setup access point, press the active-low configuration
reset input (GPIO 8 by default, internally pulled up and connected to GND
through a normally open button) during startup or operation. To choose another
GPIO, see [hardware wiring](hardware.md).
The firmware clears the setup-status flag, commits the change, and restarts.
On the next startup, the missing status flag causes the setup access point to
be started. The button is also checked while waiting for Wi-Fi and processing
web requests; hold it until the device restarts, since individual network or
storage operations may delay the check.

If clearing the EEPROM flag fails, the firmware logs an error and does not
restart, avoiding a reboot that would reload the old configuration. Release
and press the button again to retry.
