import os
import sys
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    from PyQt6.QtWidgets import QApplication
    from Wireless3DPrinter import client, local_printer
except ImportError:
    local_printer = None


@unittest.skipIf(local_printer is None, "Cura/Uranium and PyQt6 are required")
class LocalPrinterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        application = Mock()
        with patch.object(local_printer.Application, "getInstance", return_value=application):
            self.stage = local_printer.LocalPrinterStage()
        self.one = client.Device("one", "Workshop", "127.0.0.1", 80)
        self.two = client.Device("two", "Other", "127.0.0.2", 80)
        self.files = [{"name": "Long name", "path": "LONG.GCO", "size": 12}]

    def wait_done(self):
        deadline = time.monotonic() + 5
        while self.stage.busy and time.monotonic() < deadline:
            self.app.processEvents()
            time.sleep(0.005)
        self.assertFalse(self.stage.busy)

    def test_single_printer_is_selected_and_listed(self):
        with patch.object(client, "discover", return_value=[self.one]), \
                patch.object(client, "list_files", return_value=self.files) as listing:
            self.stage.search()
            self.wait_done()
        listing.assert_called_once_with(self.one)
        self.assertEqual(self.stage.printerName, "Workshop")
        self.assertEqual(self.stage.files, self.files)

    def test_multiple_printers_require_choice(self):
        with patch.object(client, "discover", return_value=[self.one, self.two]), \
                patch.object(client, "list_files", return_value=self.files) as listing:
            self.stage.search()
            self.wait_done()
            self.assertEqual(self.stage.printerName, "")
            listing.assert_not_called()
            self.stage.connectPrinter(-1)
            self.stage.connectPrinter(1)
            self.wait_done()
            listing.assert_called_once_with(self.two)
        self.assertEqual(self.stage.printerName, "Other")

    def test_delete_uses_listed_path_and_refreshes(self):
        with patch.object(client, "discover", return_value=[self.one]), \
                patch.object(client, "list_files", side_effect=[self.files, []]), \
                patch.object(client, "delete_file") as delete:
            self.stage.search()
            self.wait_done()
            self.stage.deleteFile("unlisted")
            delete.assert_not_called()
            self.stage.deleteFile("LONG.GCO")
            self.wait_done()
        delete.assert_called_once_with(self.one, "LONG.GCO")
        self.assertEqual(self.stage.files, [])

    def test_errors_are_visible_and_clear_stale_files(self):
        with patch.object(client, "discover", return_value=[self.one]), \
                patch.object(client, "list_files", return_value=self.files), \
                patch.object(client, "delete_file", side_effect=RuntimeError("busy")):
            self.stage.search()
            self.wait_done()
            self.stage.deleteFile("LONG.GCO")
            self.wait_done()
        self.assertEqual(self.stage.files, [])
        self.assertIn("busy", self.stage.status)
        with patch.object(client, "discover", side_effect=RuntimeError("network down")):
            self.stage.search()
            self.wait_done()
        self.assertEqual(self.stage.printerName, "")
        self.assertIn("network down", self.stage.status)

    def test_bulk_selection_delete_and_partial_error(self):
        files = self.files + [{"name": "Other", "path": "OTHER.GCO", "size": 1}]
        with patch.object(client, "discover", return_value=[self.one]), \
                patch.object(client, "list_files", return_value=files), \
                patch.object(client, "delete_files", side_effect=RuntimeError("Confirmed 1 of 2 deletions")) as delete:
            self.stage.search()
            self.wait_done()
            self.stage.selectFile("unlisted", True)
            self.assertEqual(self.stage.selectedCount, 0)
            self.stage.selectFile("LONG.GCO", True)
            self.assertEqual(self.stage.selectedPaths, ["LONG.GCO"])
            self.stage.toggleSelectAll()
            self.assertEqual(self.stage.selectedCount, 2)
            self.stage.toggleSelectAll()
            self.assertEqual(self.stage.selectedCount, 0)
            self.stage.toggleSelectAll()
            self.stage.deleteSelected()
            self.wait_done()
            delete.assert_called_once_with(self.one, ["LONG.GCO", "OTHER.GCO"])
            self.assertEqual(self.stage.files, [])
            self.assertEqual(self.stage.selectedCount, 0)
            self.assertIn("Confirmed 1 of 2 deletions", self.stage.status)

    def test_empty_discovery_and_list_failure(self):
        with patch.object(client, "discover", return_value=[]):
            self.stage.search()
            self.wait_done()
        self.assertIn("No printers found", self.stage.status)
        with patch.object(client, "discover", return_value=[self.one]), \
                patch.object(client, "list_files", side_effect=RuntimeError("offline")):
            self.stage.search()
            self.wait_done()
        self.assertEqual(self.stage.printerName, "Workshop")
        self.assertIn("offline", self.stage.status)

    def test_registration_preserves_save_device_and_adds_leftmost_stage(self):
        import Wireless3DPrinter
        metadata = Wireless3DPrinter.getMetaData()
        self.assertEqual(metadata["stage"]["name"], "Local Printer")
        self.assertLess(metadata["stage"]["weight"], 10)
        with patch.object(local_printer.Application, "getInstance", return_value=Mock()):
            plugins = Wireless3DPrinter.register(Mock())
        self.assertEqual(set(plugins), {"stage", "output_device"})
        self.assertIsInstance(plugins["stage"], local_printer.LocalPrinterStage)
        plugins["stage"]._on_engine_created()
        self.assertTrue(plugins["stage"].mainComponent.toLocalFile().endswith("LocalPrinter.qml"))
