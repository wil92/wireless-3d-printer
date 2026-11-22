"""Cura stage for discovering printers and managing their files."""

import os
import threading

from PyQt6.QtCore import pyqtProperty, pyqtSignal, pyqtSlot
from UM.Application import Application
from cura.Stages.CuraStage import CuraStage

from . import client


class LocalPrinterStage(CuraStage):
    changed = pyqtSignal()
    devicesChanged = pyqtSignal()
    selectionChanged = pyqtSignal()
    _completed = pyqtSignal(str, object, object)

    def __init__(self):
        super().__init__()
        self._devices = []
        self._device = None
        self._files = []
        self._selected = set()
        self._busy = False
        self._status = "Search for a printer on the local network."
        self._completed.connect(self._on_completed)
        Application.getInstance().engineCreatedSignal.connect(self._on_engine_created)

    def _on_engine_created(self):
        self.addDisplayComponent("main", os.path.join(os.path.dirname(__file__), "LocalPrinter.qml"))

    @pyqtProperty("QVariantList", notify=devicesChanged)
    def devices(self):
        return ["{} ({}, {}:{})".format(d.name, d.id, d.address, d.port) for d in self._devices]

    @pyqtProperty("QVariantList", notify=changed)
    def files(self):
        return self._files

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

    def _start(self, operation, status, function, *args):
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
        self._clear_selection()
        self._start("search", "Searching for printers...", client.discover)

    @pyqtSlot(int)
    def connectPrinter(self, index):
        if self._busy or index < 0 or index >= len(self._devices):
            return
        self._device = self._devices[index]
        self._files = []
        self._clear_selection()
        self.refresh()

    @pyqtSlot()
    def refresh(self):
        if not self._busy and self._device:
            self._clear_selection()
            self._start("list", "Loading printer files...", client.list_files, self._device)

    @pyqtSlot(str)
    def deleteFile(self, path):
        if self._busy or not self._device or not any(f["path"] == path for f in self._files):
            return
        self._start("delete", "Deleting printer file...", client.delete_file, self._device, path)

    @pyqtSlot()
    def deleteSelected(self):
        paths = sorted(self._selected)
        if (self._busy or not self._device or not paths
                or not set(paths).issubset({f["path"] for f in self._files})):
            return
        self._start("delete", "Deleting printer files...", client.delete_files, self._device, paths)

    @pyqtSlot(str, object, object)
    def _on_completed(self, operation, result, error):
        self._busy = False
        if error:
            if operation in ("list", "delete"):
                self._files = []
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
            self._status = "{} file(s) on the printer.".format(len(result))
        else:
            self.refresh()
            return
        self.changed.emit()
