import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path


@unittest.skipUnless(shutil.which("unzip") and shutil.which("bash"), "bash and unzip are required")
class UploadPluginTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / "home"
        self.plugin_dir = self.root / "source"
        (self.plugin_dir / "dist").mkdir(parents=True)
        shutil.copy(Path(__file__).resolve().parents[1] / "upload-plugin.sh", self.plugin_dir)
        self.archive = self.plugin_dir / "dist/Wireless3DPrinter.zip"
        self.cura = self.home / ".local/share/cura"

    def run_script(self):
        return subprocess.run(
            ["bash", str(self.plugin_dir / "upload-plugin.sh")],
            env=dict(os.environ, HOME=str(self.home)), capture_output=True, text=True)

    def make_archive(self, path="Wireless3DPrinter/plugin.json"):
        with zipfile.ZipFile(self.archive, "w") as archive:
            archive.writestr(path, '{"version":"new"}')

    def test_latest_numeric_version_is_replaced(self):
        old = self.cura / "5.9/plugins/Wireless3DPrinter"
        current = self.cura / "5.10/plugins/Wireless3DPrinter"
        ignored = self.cura / "backup"
        old.mkdir(parents=True)
        current.mkdir(parents=True)
        ignored.mkdir(parents=True)
        (old / "keep").write_text("old version")
        (current / "obsolete").write_text("old plugin")
        self.make_archive()
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Cura 5.10", result.stdout)
        self.assertTrue((old / "keep").exists())
        self.assertFalse((current / "obsolete").exists())
        self.assertTrue((current / "plugin.json").exists())

    def test_invalid_archive_preserves_existing_plugin(self):
        current = self.cura / "5.4/plugins/Wireless3DPrinter"
        current.mkdir(parents=True)
        (current / "keep").write_text("installed")
        self.make_archive("wrong/file")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid plugin archive", result.stderr)
        self.assertEqual((current / "keep").read_text(), "installed")

    def test_missing_archive_or_version_fails(self):
        self.assertNotEqual(self.run_script().returncode, 0)
        self.make_archive()
        self.cura.mkdir(parents=True)
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No Cura version", result.stderr)
