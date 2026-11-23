"""Cura stage for discovering printers and managing their files."""

import os
import threading

from PyQt6.QtCore import QTimer, pyqtProperty, pyqtSignal, pyqtSlot
from UM.Application import Application
from cura.Stages.CuraStage import CuraStage

from . import client


class LocalPrinterStage(CuraStage):
    changed = pyqtSignal()
    filesChanged = pyqtSignal()
    printerStatusChanged = pyqtSignal()
    devicesChanged = pyqtSignal()
    selectionChanged = pyqtSignal()
    _completed = pyqtSignal(str, object, object)
    _status_completed = pyqtSignal(int, object, object)

    def __init__(self):
        super().__init__()
        self._devices = []
        self._device = None
        self._files = []
        self._selected = set()
        self._busy = False
        self._polling = False
        self._generation = 0
        self._printer_status = None
        self._status_error = None
        self._status = "Search for a printer on the local network."
        self._completed.connect(self._on_completed)
        self._status_completed.connect(self._on_status_completed)
        application = Application.getInstance()
        application.engineCreatedSignal.connect(self._on_engine_created)
        self._print_information = None
        self._timer = QTimer(self)
        self._timer.setInterval(client.STATUS_INTERVAL_MS)
        self._timer.timeout.connect(self._poll_status)
        self._timer.start()

    def _on_engine_created(self):
        self._print_information = Application.getInstance().getPrintInformation()
        if self._print_information is not None:
            self._print_information.jobNameChanged.connect(self.filesChanged.emit)
        self.filesChanged.emit()
        self.addDisplayComponent("main", os.path.join(os.path.dirname(__file__), "LocalPrinter.qml"))

    @pyqtProperty("QVariantList", notify=devicesChanged)
    def devices(self):
        return ["{} ({}, {}:{})".format(d.name, d.id, d.address, d.port) for d in self._devices]

    @pyqtProperty("QVariantList", notify=filesChanged)
    def files(self):
        name = self._print_information.jobName if self._print_information else ""
        return [dict(entry, currentModel=client.matches_model(entry, name)) for entry in self._files]

    @pyqtProperty("QVariantList", notify=selectionChanged)
    def selectedPaths(self):
        return sorted(self._selected)

    @pyqtProperty(int, notify=selectionChanged)
    def selectedCount(self):
        return len(self._selected)

    def _clear_selection(self):
        if self._selected:
            self._selected.clear()
            self.selectionChanged.emit()

    @pyqtSlot(str, bool)
    def selectFile(self, path, selected):
        if self._busy or not any(f["path"] == path for f in self._files):
            return
        if selected:
            self._selected.add(path)
        else:
            self._selected.discard(path)
        self.selectionChanged.emit()

    @pyqtSlot()
    def toggleSelectAll(self):
        if self._busy or not self._files:
            return
        if len(self._selected) == len(self._files):
            self._selected.clear()
        else:
            self._selected = {f["path"] for f in self._files}
        self.selectionChanged.emit()

    @pyqtProperty(str, notify=changed)
    def printerName(self):
        return self._device.name if self._device else ""

    @pyqtProperty(bool, notify=changed)
    def busy(self):
        return self._busy

    @pyqtProperty(str, notify=changed)
    def status(self):
        return self._status

    @pyqtProperty(bool, notify=changed)
    def canDelete(self):
        return not self._busy and self._printer_status is not None and \
            self._printer_status["printer_status"] == "not_sd_printing"

    @pyqtProperty(str, notify=printerStatusChanged)
    def printerStatus(self):
        if not self._printer_status:
            return "Printer status unavailable"
        data = self._printer_status
        def display(value):
            return "{:.1f} °C".format(value) if value is not None else "unavailable"
        state = "SD printing (or paused)" if data["printer_status"] == "sd_printing" else "Not SD printing"
        return "{} · Nozzle: {} · Bed: {}".format(
            state, display(data["nozzle_temperature"]), display(data["bed_temperature"]))

    @pyqtSlot()
    def _poll_status(self):
        if not self._device or self._busy or self._polling:
            return
        self._polling = True
        generation, device = self._generation, self._device

        def run():
            try:
                result = client.printer_status(device)
            except Exception as error:
                self._status_completed.emit(generation, None, str(error) or type(error).__name__)
            else:
                self._status_completed.emit(generation, result, None)

        threading.Thread(target=run, daemon=True).start()

    @pyqtSlot(int, object, object)
    def _on_status_completed(self, generation, result, error):
        self._polling = False
        if generation != self._generation:
            if self._device and not self._busy and self._printer_status is None:
                self._poll_status()
            return

        previous_text = self.printerStatus
        previous_can_delete = self.canDelete
        previous_message = self._status
        self._printer_status = None if error else result
        if error:
            self._status_error = "Printer status failed: {}".format(error)
            self._status = self._status_error
        elif self._status_error is not None:
            if self._status == self._status_error:
                self._status = "{} file(s) on the printer.".format(len(self._files))
            self._status_error = None
        if self.printerStatus != previous_text:
            self.printerStatusChanged.emit()
        if self.canDelete != previous_can_delete or self._status != previous_message:
            self.changed.emit()

    def _start(self, operation, status, function, *args):
        self._generation += 1
        self._status_error = None
        self._busy = True
        self._status = status
        self.changed.emit()

        def run():
            try:
                result = function(*args)
            except Exception as error:
                self._completed.emit(operation, None, str(error) or type(error).__name__)
            else:
                self._completed.emit(operation, result, None)

        threading.Thread(target=run, daemon=True).start()

    @pyqtSlot()
    def search(self):
        if self._busy:
            return
        self._devices = []
        self.devicesChanged.emit()
        self._device = None
        self._files = []
        self._printer_status = None
        self.filesChanged.emit()
        self.printerStatusChanged.emit()
        self._clear_selection()
        self._start("search", "Searching for printers...", client.discover)

    @pyqtSlot(int)
    def connectPrinter(self, index):
        if self._busy or index < 0 or index >= len(self._devices):
            return
        self._device = self._devices[index]
        self._files = []
        self._printer_status = None
        self.filesChanged.emit()
        self.printerStatusChanged.emit()
        self._clear_selection()
        self.refresh()

    @pyqtSlot()
    def refresh(self):
        if not self._busy and self._device:
            self._clear_selection()
            self._start("list", "Loading printer files...", client.list_files, self._device)

    @pyqtSlot(str)
    def deleteFile(self, path):
        if not self.canDelete or not self._device or not any(f["path"] == path for f in self._files):
            return
        self._start("delete", "Deleting printer file...", client.delete_file, self._device, path)

    @pyqtSlot()
    def deleteSelected(self):
        paths = sorted(self._selected)
        if (not self.canDelete or not self._device or not paths
                or not set(paths).issubset({f["path"] for f in self._files})):
            return
        self._start("delete", "Deleting printer files...", client.delete_files, self._device, paths)

    @pyqtSlot(str, object, object)
    def _on_completed(self, operation, result, error):
        self._busy = False
        if error:
            if operation in ("list", "delete"):
                self._files = []
                self.filesChanged.emit()
                self._clear_selection()
            self._status = "{} failed: {}".format(
                {"search": "Search", "list": "Loading files", "delete": "Delete"}[operation], error)
        elif operation == "search":
            self._devices = result
            self.devicesChanged.emit()
            if len(result) == 1:
                self.connectPrinter(0)
                return
            self._status = ("Select a printer and connect." if result else
                            "No printers found. Check the network and mDNS.")
        elif operation == "list":
            self._files = result
            self.filesChanged.emit()
            self._status = "{} file(s) on the printer.".format(len(result))
            self.changed.emit()
            self._poll_status()
            return
        else:
            self.refresh()
            return
        self.changed.emit()
