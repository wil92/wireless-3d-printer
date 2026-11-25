import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

try:
    from PyQt6.QtCore import QObject, pyqtSignal
    from PyQt6.QtWidgets import QApplication, QMessageBox
    from Wireless3DPrinter import output_device
except ImportError:
    output_device = None


class FakeMessage:
    messages = []

    def __init__(self, text, **kwargs):
        self.text = text
        self.messages.append(self)

    def show(self):
        pass

    def hide(self):
        pass

    def setText(self, text):
        self.text = text

    def setProgress(self, progress):
        self.progress = progress


@unittest.skipIf(output_device is None, "Cura/Uranium and PyQt6 are required")
class OutputDeviceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        FakeMessage.messages = []
        self.messages = patch.object(output_device, "Message", FakeMessage)
        self.messages.start()
        self.addCleanup(self.messages.stop)
        class Stage(QObject):
            printerChanged = pyqtSignal()
            selected_device = None

        self.stage = Stage()
        self.information = unittest.mock.Mock()
        self.information.baseName = ""
        application = unittest.mock.Mock()
        application.getPrintInformation.return_value = self.information
        self.application = patch.object(output_device.Application, "getInstance", return_value=application)
        self.application.start()
        self.addCleanup(self.application.stop)
        self.device = output_device.client.Device("id", "Printer", "127.0.0.1", 80)
        self.output = output_device.Wireless3DPrinterOutputDevice(self.stage)

    def wait_done(self):
        deadline = time.monotonic() + 5
        while self.output._busy and time.monotonic() < deadline:
            self.app.processEvents()
            time.sleep(0.005)
        self.assertFalse(self.output._busy)

    def test_no_selection_is_rejected(self):
        with patch.object(output_device.client, "upload") as upload, \
                patch.object(output_device.QMessageBox, "question") as question:
            self.output.requestWrite([], "part")
        upload.assert_not_called()
        question.assert_not_called()
        self.assertFalse(self.output._busy)
        self.assertIn("Select a printer", FakeMessage.messages[-1].text)

    def test_plugin_registers_and_removes_save_option(self):
        manager = unittest.mock.Mock()
        application = unittest.mock.Mock()
        application.getOutputDeviceManager.return_value = manager
        with patch("UM.OutputDevice.OutputDevicePlugin.Application.getInstance", return_value=application):
            plugin = output_device.Wireless3DPrinterPlugin(self.stage)
            plugin.stop()
            plugin.start()
            plugin.start()
            manager.addOutputDevice.assert_not_called()
            self.stage.selected_device = self.device
            self.stage.printerChanged.emit()
            registered = manager.addOutputDevice.call_args.args[0]
            self.assertEqual(registered.getShortDescription(), "Save to wireless-3d-printer")
            self.assertEqual(registered.getDescription(), "Save to wireless-3d-printer")
            self.stage.printerChanged.emit()
            self.assertEqual(manager.addOutputDevice.call_count, 1)
            self.stage.selected_device = None
            self.stage.printerChanged.emit()
            manager.removeOutputDevice.assert_called_once_with("wireless_3d_printer")
            self.stage.selected_device = self.device
            self.stage.printerChanged.emit()
            plugin.stop()
            self.assertEqual(manager.removeOutputDevice.call_count, 2)
            self.stage.printerChanged.emit()
            self.assertEqual(manager.addOutputDevice.call_count, 2)
            plugin.start()
            self.assertEqual(manager.addOutputDevice.call_count, 3)
            plugin.stop()

    def test_confirmation_cancel_does_not_write(self):
        self.stage.selected_device = self.device
        with patch.object(output_device.QMessageBox, "question", return_value=QMessageBox.StandardButton.No), \
                patch.object(output_device.client, "upload") as upload:
            self.output.requestWrite([], "part")
            self.wait_done()
            upload.assert_not_called()

    def test_selected_printer_writer_upload_and_cleanup(self):
        devices = [output_device.client.Device("a", "One", "127.0.0.1", 80),
                   output_device.client.Device("b", "Two", "127.0.0.2", 80)]
        self.stage.selected_device = devices[1]
        self.information.baseName = "esp32-s3-supermini-box"
        stage = self.stage
        captured = {}

        class FakeSignal:
            def connect(self, callback):
                self.callback = callback

        class FakeJob:
            def __init__(self, writer, stream, nodes, mode):
                self.stream = stream
                self.finished = FakeSignal()
                captured["nodes"] = nodes

            def start(self):
                self.stream.write("G28\n")
                stage.selected_device = devices[0]
                self.finished.callback(self)

            def getStream(self):
                return self.stream

            def getResult(self):
                return True

            def hasError(self):
                return False

        def fake_upload(device, path, filename, progress):
            captured.update(device=device, path=path, filename=filename, data=Path(path).read_text())
            progress(100)

        class Registry:
            def getPluginObject(self, name):
                self.name = name
                return object()

        registry = Registry()
        with patch.object(output_device.client, "discover") as discover, \
                patch.object(output_device.client, "device_at", return_value=devices[1]) as verify, \
                patch.object(output_device.client, "version_warning", return_value=""), \
                patch.object(output_device.QMessageBox, "question", return_value=QMessageBox.StandardButton.Yes), \
                patch.object(output_device.PluginRegistry, "getInstance", return_value=registry), \
                patch.object(output_device, "WriteFileJob", FakeJob), \
                patch.object(output_device.client, "upload", side_effect=fake_upload):
            self.output.requestWrite(["scene"], "UMS5-ESP")
            self.wait_done()
        discover.assert_not_called()
        verify.assert_called_once_with("127.0.0.2", 80)
        self.assertEqual(registry.name, "GCodeWriter")
        self.assertEqual(captured["device"], devices[1])
        self.assertEqual(captured["data"], "G28\n")
        self.assertEqual(captured["nodes"], ["scene"])
        self.assertEqual(captured["filename"], "WRP-ESP32_S3_SUPERMINI_BOX.gcode")
        self.assertFalse(Path(captured["path"]).exists())
        self.assertIn("Upload complete", FakeMessage.messages[-1].text)

    def test_upload_error_is_visible_and_temp_file_removed(self):
        with tempfile.NamedTemporaryFile(delete=False) as file:
            path = file.name
        self.output._path = path
        self.output._busy = True
        self.output._on_uploaded("HTTP 500")
        self.assertFalse(Path(path).exists())
        self.assertIn("partial file", FakeMessage.messages[-1].text)

    def test_writer_failure_cleans_up_and_does_not_upload(self):
        self.stage.selected_device = self.device
        paths = []

        class FakeSignal:
            def connect(self, callback):
                self.callback = callback

        class FailingJob:
            def __init__(self, writer, stream, nodes, mode):
                self.stream = stream
                paths.append(stream.name)
                self.finished = FakeSignal()

            def start(self):
                self.finished.callback(self)

            def getStream(self):
                return self.stream

            def getResult(self):
                return False

            def hasError(self):
                return True

            def getError(self):
                return RuntimeError("no sliced G-code")

        registry = unittest.mock.Mock()
        registry.getPluginObject.return_value = object()
        with patch.object(output_device.QMessageBox, "question", return_value=QMessageBox.StandardButton.Yes), \
                patch.object(output_device.PluginRegistry, "getInstance", return_value=registry), \
                patch.object(output_device, "WriteFileJob", FailingJob), \
                patch.object(output_device.client, "upload") as upload:
            self.output.requestWrite([], "part")
            self.wait_done()
            upload.assert_not_called()
        self.assertIn("no sliced G-code", FakeMessage.messages[-1].text)
        self.assertFalse(Path(paths[0]).exists())

    def test_selection_change_during_confirmation_aborts(self):
        self.stage.selected_device = self.device

        def change_selection(*args):
            self.stage.selected_device = None
            return QMessageBox.StandardButton.Yes

        with patch.object(output_device.QMessageBox, "question", side_effect=change_selection), \
                patch.object(output_device, "WriteFileJob") as job:
            self.output.requestWrite([], "part")
        job.assert_not_called()
        self.assertFalse(self.output._busy)
        self.assertIn("selected printer changed", FakeMessage.messages[-1].text)

    def test_verification_failure_never_uploads(self):
        other = output_device.client.Device("other", "Other", self.device.address, self.device.port)
        for result in (other, RuntimeError("offline")):
            with self.subTest(result=result), tempfile.NamedTemporaryFile(delete=False) as file:
                path = file.name
                self.output._device = self.device
                self.output._path = path
                self.output._busy = True
                kwargs = {"side_effect": result} if isinstance(result, Exception) else {"return_value": result}
                with patch.object(output_device.client, "device_at", **kwargs), \
                        patch.object(output_device.client, "upload") as upload:
                    self.output._upload()
                    self.wait_done()
                    upload.assert_not_called()
                self.assertIn("No G-code was uploaded", FakeMessage.messages[-1].text)
                self.assertFalse(Path(path).exists())

    def test_incompatible_firmware_never_uploads(self):
        with tempfile.NamedTemporaryFile(delete=False) as file:
            path = file.name
        self.output._device = self.device
        self.output._path = path
        self.output._busy = True
        with patch.object(output_device.client, "device_at", return_value=self.device), \
                patch.object(output_device.client, "version_warning", return_value="incompatible"), \
                patch.object(output_device.client, "upload") as upload:
            self.output._upload()
            self.wait_done()
        upload.assert_not_called()
        self.assertIn("incompatible", FakeMessage.messages[-1].text)

    def test_oversized_name_is_rejected_before_confirmation(self):
        self.stage.selected_device = self.device
        self.information.baseName = "a" * 82
        with patch.object(output_device.QMessageBox, "question") as question:
            self.output.requestWrite([], "short")
        question.assert_not_called()
        self.assertIn("too long", FakeMessage.messages[-1].text)


if __name__ == "__main__":
    unittest.main()
