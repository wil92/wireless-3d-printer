import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_artifacts", ROOT / "scripts/release_artifacts.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseArtifactsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for directory in ("include", "ultimate-cura-plugin/Wireless3DPrinter",
                          "ultimate-cura-plugin/dist", ".pio/build/esp32dev",
                          ".pio/build/esp32dev-debug"):
            (self.root / directory).mkdir(parents=True)
        (self.root / "include/version.h").write_text('#define FIRMWARE_VERSION "1.2"\n')
        (self.root / "ultimate-cura-plugin/Wireless3DPrinter/plugin.json").write_text(
            json.dumps({"version": "3.4"})
        )
        for env in ("esp32dev", "esp32dev-debug"):
            image = b"\xe9" + bytes(0x10000) + b"1.2"
            if env == "esp32dev-debug":
                image += release.DEBUG_MARKER
            (self.root / f".pio/build/{env}/firmware.factory.bin").write_bytes(image)
        with ZipFile(self.root / "ultimate-cura-plugin/dist/Wireless3DPrinter.zip", "w") as archive:
            archive.writestr("Wireless3DPrinter/plugin.json", '{"version": "3.4"}')
            archive.writestr("Wireless3DPrinter/vendor/zeroconf/__init__.py", "")
        self.output = self.root / "assets"

    def test_package_uses_independent_versions_and_factory_images(self):
        release.package(self.output, self.root)
        self.assertEqual({path.name for path in self.output.iterdir()}, {
            "wireless-3d-printer_1.2.bin",
            "wireless-3d-printer_debug-mode.1.2.bin",
            "Wireless3DPrinter_3.4.zip",
        })
        release.verify(self.output, "1.2", "3.4")

    def test_bad_image_or_extra_asset_is_rejected(self):
        release.package(self.output, self.root)
        image = self.output / "wireless-3d-printer_1.2.bin"
        image.write_bytes(b"bad")
        with self.assertRaises(ValueError):
            release.verify(self.output, "1.2", "3.4")
        image.write_bytes(b"\xe9" + bytes(0x10000) + b"1.2")
        (self.output / "unexpected").write_text("")
        with self.assertRaises(ValueError):
            release.verify(self.output, "1.2", "3.4")

    def test_swapped_or_duplicated_firmware_variants_are_rejected(self):
        release.package(self.output, self.root)
        normal = self.output / "wireless-3d-printer_1.2.bin"
        debug = self.output / "wireless-3d-printer_debug-mode.1.2.bin"
        debug.write_bytes(normal.read_bytes())
        with self.assertRaises(ValueError):
            release.verify(self.output, "1.2", "3.4")

    def test_archive_version_and_dependencies_are_checked(self):
        release.package(self.output, self.root)
        archive_path = self.output / "Wireless3DPrinter_3.4.zip"
        with ZipFile(archive_path, "w") as archive:
            archive.writestr("Wireless3DPrinter/plugin.json", '{"version": "3.5"}')
            archive.writestr("Wireless3DPrinter/vendor/zeroconf/__init__.py", "")
        with self.assertRaises(ValueError):
            release.verify(self.output, "1.2", "3.4")
        with ZipFile(archive_path, "w") as archive:
            archive.writestr("Wireless3DPrinter/plugin.json", '{"version": "3.4"}')
        with self.assertRaises(ValueError):
            release.verify(self.output, "1.2", "3.4")

    def test_invalid_component_versions_are_rejected(self):
        header = self.root / "include/version.h"
        for content in ('#define FIRMWARE_VERSION "01.2"\n',
                        '#define FIRMWARE_VERSION "1.2.3"\n',
                        '#define FIRMWARE_VERSION "1.2"\n#define FIRMWARE_VERSION "1.3"\n'):
            with self.subTest(content=content):
                header.write_text(content)
                with self.assertRaises(ValueError):
                    release.versions(self.root)
        header.write_text('#define FIRMWARE_VERSION "1.2"\n')
        (self.root / "ultimate-cura-plugin/Wireless3DPrinter/plugin.json").write_text('{"version":"1.00"}')
        with self.assertRaises(ValueError):
            release.versions(self.root)

    def test_tag_format(self):
        self.assertEqual("v1.2-cura3.4", release.release_tag("1.2", "3.4"))
        release.validate_tag("v1.2-cura3.4", "1.2", "3.4")
        for tag in ("", "v1.2", "v01.2-cura3.4", "v1.2-cura3.5",
                    "v1.3-cura3.4", "v1.2-cura3.4\n"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.validate_tag(tag, "1.2", "3.4")


if __name__ == "__main__":
    unittest.main()
