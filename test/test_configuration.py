import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ConfigurationHeadersTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.root = Path(self.temp_dir.name)
        for directory in ("src", "include", "lib/network"):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        for name in (
            "src/main.h",
            "lib/network/access_point_config.h",
            "include/access_point_config.example.h",
            "include/hardware_pins.h",
            "include/reset_configuration_pin.example.h",
        ):
            shutil.copyfile(ROOT / name, self.root / name)

    def compile(self, source):
        result = subprocess.run(
            ["g++", "-std=c++11", "-x", "c++", "-fsyntax-only", "-"],
            input=source,
            text=True,
            capture_output=True,
            cwd=self.root,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_reset_pin_default_and_local_override(self):
        header = '#include "include/hardware_pins.h"\n'
        self.compile(header + """
            static_assert(HardwarePins::resetConfig == 8, "default reset pin");
            static_assert(HardwarePins::usbDataMinus == 19, "USB D-");
            static_assert(HardwarePins::usbDataPlus == 20, "USB D+");
        """)
        (self.root / "include/reset_configuration_pin.h").write_text(
            "#define RESET_CONFIG_PIN 4\n"
        )
        self.compile(header + """
            static_assert(HardwarePins::resetConfig == 4, "custom reset pin");
            static_assert(HardwarePins::usbDataMinus == 19, "USB D-");
            static_assert(HardwarePins::usbDataPlus == 20, "USB D+");
        """)

    def test_reset_pin_cannot_use_native_usb_pins(self):
        for pin in (19, 20):
            with self.subTest(pin=pin):
                (self.root / "include/reset_configuration_pin.h").write_text(
                    f"#define RESET_CONFIG_PIN {pin}\n"
                )
                result = subprocess.run(
                    ["g++", "-std=c++11", "-x", "c++", "-fsyntax-only", "-"],
                    input='#include "include/hardware_pins.h"\n',
                    text=True, capture_output=True, cwd=self.root,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Configuration reset pin must not use the native USB pins",
                              result.stderr)

    def test_usb_defaults_without_local_header(self):
        self.compile("""
            #include "src/main.h"
            static_assert(ARDUINO_USB_MODE == 1, "USB mode");
            static_assert(ARDUINO_USB_CDC_ON_BOOT == 0, "USB CDC");
        """)

    def test_usb_local_values_override_defaults(self):
        (self.root / "include/usb_config.h").write_text(
            "#define ARDUINO_USB_MODE 0\n"
            "#define ARDUINO_USB_CDC_ON_BOOT 0\n"
        )
        self.compile("""
            #include "src/main.h"
            static_assert(ARDUINO_USB_MODE == 0, "USB mode");
            static_assert(ARDUINO_USB_CDC_ON_BOOT == 0, "USB CDC");
        """)

    def test_usb_missing_local_value_uses_default(self):
        (self.root / "include/usb_config.h").write_text(
            "#define ARDUINO_USB_CDC_ON_BOOT 0\n"
        )
        self.compile("""
            #include "src/main.h"
            static_assert(ARDUINO_USB_MODE == 1, "USB mode");
            static_assert(ARDUINO_USB_CDC_ON_BOOT == 0, "USB CDC");
        """)

    def test_access_point_example_and_local_override(self):
        header = '#include "lib/network/access_point_config.h"\n'
        self.compile(header + """
            constexpr bool same(const char *a, const char *b) {
                return *a == *b && (*a == '\\0' || same(a + 1, b + 1));
            }
            static_assert(same(AP_SSID, "Wireless3DPrinter"), "example SSID");
            static_assert(same(AP_PASSWORD, "wireless3dprinter"), "example password");
        """)
        (self.root / "include/access_point_config.h").write_text(
            '#define AP_SSID "local access point"\n'
            '#define AP_PASSWORD "local password"\n'
        )
        self.compile(header + """
            static_assert(AP_SSID[0] == 'l', "local SSID");
            static_assert(AP_PASSWORD[0] == 'l', "local password");
        """)


if __name__ == "__main__":
    unittest.main()
