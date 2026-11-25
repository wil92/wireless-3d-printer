import os
import sys
import tempfile
import threading

from PyQt6.QtCore import QObject, pyqtSignal
from PyQt6.QtWidgets import QMessageBox
from UM.Application import Application
from UM.FileHandler.WriteFileJob import WriteFileJob
from UM.Mesh.MeshWriter import MeshWriter
from UM.Message import Message
from UM.OutputDevice.OutputDevice import OutputDevice
from UM.OutputDevice.OutputDevicePlugin import OutputDevicePlugin
from UM.PluginRegistry import PluginRegistry

from . import client

vendor = os.path.join(os.path.dirname(__file__), "vendor")
if os.path.isdir(vendor) and vendor not in sys.path:
    sys.path.insert(0, vendor)


class Bridge(QObject):
    uploaded = pyqtSignal(object)
    verification_failed = pyqtSignal(str)
    progress = pyqtSignal(int)


class Wireless3DPrinterPlugin(OutputDevicePlugin):
    def __init__(self, stage):
        super().__init__()
        self._stage = stage
        self._device = Wireless3DPrinterOutputDevice(stage)
        self._registered = False
        self._started = False

    def start(self):
        if self._started:
            return
        self._started = True
        self._stage.printerChanged.connect(self._sync_device)
        self._sync_device()

    def stop(self):
        if not self._started:
            return
        self._started = False
        self._stage.printerChanged.disconnect(self._sync_device)
        if self._registered:
            self.getOutputDeviceManager().removeOutputDevice("wireless_3d_printer")
            self._registered = False

    def _sync_device(self):
        if self._stage.selected_device and not self._registered:
            self.getOutputDeviceManager().addOutputDevice(self._device)
            self._registered = True
        elif not self._stage.selected_device and self._registered:
            self.getOutputDeviceManager().removeOutputDevice("wireless_3d_printer")
            self._registered = False


class Wireless3DPrinterOutputDevice(OutputDevice):
    def __init__(self, stage):
        super().__init__("wireless_3d_printer")
        self._stage = stage
        self.setName("wireless-3d-printer")
        self.setShortDescription("Save to wireless-3d-printer")
        self.setDescription("Save to wireless-3d-printer")
        self.setIconName("save")
        self._bridge = Bridge()
        self._bridge.uploaded.connect(self._on_uploaded)
        self._bridge.verification_failed.connect(self._on_verification_failed)
        self._bridge.progress.connect(self._on_progress)
        self._busy = False
        self._path = None
        self._message = None
        self._upload_result = None

    def requestWrite(self, nodes, file_name=None, limit_mimetypes=False,
                     file_handler=None, filter_by_machine=False, **kwargs):
        if self._busy:
            Message("A wireless-3d-printer upload is already in progress.", title="wireless-3d-printer").show()
            return
        device = self._stage.selected_device
        if not device:
            Message("Select a printer in the Local Printer tab before uploading.",
                    title="wireless-3d-printer").show()
            return
        information = Application.getInstance().getPrintInformation()
        model_name = information.baseName if information and information.baseName else file_name
        try:
            filename = client.model_filename(model_name)
        except ValueError as error:
            Message(str(error), title="wireless-3d-printer").show()
            return
        self._busy = True
        self._upload_result = None
        self._nodes = nodes
        self._filename = filename
        confirmation = QMessageBox.question(
            None, "Save to wireless-3d-printer",
            "Upload {} to {} ({}, {}:{})?\n\n"
            "The printer must support long filenames for uploads; collisions are rejected. "
            "Keep the printer idle and do not start a print during the upload."
            .format(self._filename, device.name, device.id, device.address, device.port),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if confirmation != QMessageBox.StandardButton.Yes:
            self._finish()
            return
        if self._stage.selected_device != device:
            self._finish("The selected printer changed. Confirm the destination and try again.", failed=True)
            return
        self._device = device
        self._message = Message("Preparing G-code...", title="wireless-3d-printer",
                                progress=-1, lifetime=0, dismissable=False,
                                use_inactivity_timer=False)
        self._message.show()
        self.writeStarted.emit(self)
        try:
            writer = PluginRegistry.getInstance().getPluginObject("GCodeWriter")
            if not writer:
                raise RuntimeError("Cura's GCodeWriter is unavailable.")
            stream = tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                                  suffix=".gcode", delete=False)
            self._path = stream.name
            self._job = WriteFileJob(writer, stream, self._nodes, MeshWriter.OutputMode.TextMode)
            self._job.finished.connect(self._on_written)
            self._job.start()
        except Exception as exc:
            if "stream" in locals():
                stream.close()
            self._finish("Could not prepare G-code: " + str(exc), failed=True, started=True)

    def _on_written(self, job):
        try:
            job.getStream().close()
            if job.hasError() or not job.getResult():
                raise RuntimeError(str(job.getError() or "G-code writer failed"))
            self._message.setText("Uploading G-code...")
            self._message.setProgress(0)
            threading.Thread(target=self._upload, daemon=True).start()
        except Exception as error:
            self._finish("Could not prepare G-code: " + str(error), failed=True, started=True)

    def _upload(self):
        try:
            current = client.device_at(self._device.address, self._device.port)
            if current.id != self._device.id:
                raise RuntimeError("The selected printer's address now belongs to a different device. "
                                   "Search for printers again.")
            warning = client.version_warning(self._device)
            if warning:
                raise RuntimeError(warning)
        except Exception as error:
            self._bridge.verification_failed.emit(str(error) or type(error).__name__)
            return
        try:
            self._upload_result = client.upload(
                self._device, self._path, self._filename, self._bridge.progress.emit)
        except Exception as error:
            self._bridge.uploaded.emit(str(error))
        else:
            self._bridge.uploaded.emit(None)

    def _on_verification_failed(self, error):
        self._finish("Could not verify the selected printer: {}. No G-code was uploaded."
                     .format(error), failed=True, started=True)

    def _on_progress(self, progress):
        self._message.setProgress(progress)
        if progress == 100:
            self._message.setText("Upload sent; waiting for the device to finish...")
        self.writeProgress.emit(self, progress)

    def _on_uploaded(self, error):
        if error:
            self._finish("Upload failed: {}. A partial file may remain.".format(error),
                         failed=True, started=True)
        else:
            self._finish(self._upload_result or "Upload complete.", started=True)

    def _finish(self, text=None, failed=False, started=False):
        if self._message:
            self._message.hide()
            self._message = None
        if self._path:
            try:
                os.unlink(self._path)
            except OSError as error:
                Message("Could not remove temporary G-code: " + str(error), title="wireless-3d-printer").show()
            self._path = None
        self._busy = False
        if text:
            Message(text, title="wireless-3d-printer").show()
        if started:
            if failed:
                self.writeError.emit(self)
            else:
                self.writeSuccess.emit(self)
            self.writeFinished.emit(self)
