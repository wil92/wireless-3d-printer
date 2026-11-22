import os
import sys
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    from PyQt6.QtCore import QObject, QPointF, QUrl, pyqtProperty, pyqtSlot
    from PyQt6.QtGui import QColor, QFont
    from PyQt6.QtQml import QQmlComponent, QQmlEngine, qmlRegisterSingletonInstance
    from PyQt6.QtQuick import QQuickWindow
    from PyQt6.QtWidgets import QApplication
    from Wireless3DPrinter import client, local_printer
except ImportError:
    local_printer = None


@unittest.skipIf(local_printer is None, "Cura/Uranium and PyQt6 are required")
class LocalPrinterQmlTests(unittest.TestCase):
    def test_stage_qml_loads_and_buttons_drive_management(self):
        app = QApplication.instance() or QApplication([])
        with patch.object(local_printer.Application, "getInstance", return_value=Mock()):
            stage = local_printer.LocalPrinterStage()

        class Controller(QObject):
            @pyqtProperty(QObject, constant=True)
            def activeStage(self):
                return stage

        class Theme(QObject):
            @pyqtSlot(str, result=QColor)
            def getColor(self, name):
                return QColor({"thick_lining": "#000000", "main_background": "#ffffff",
                               "scrollbar_handle": "#2345aa"}.get(name, "#222222"))

            @pyqtSlot(str, result=QFont)
            def getFont(self, name):
                font = QFont("DejaVu Sans", 14)
                font.setBold(name.endswith("_bold"))
                return font

        controller, theme = Controller(), Theme()
        qmlRegisterSingletonInstance("UM", 1, 5, "Controller", controller)
        qmlRegisterSingletonInstance("UM", 1, 5, "Theme", theme)
        engine = QQmlEngine()
        warnings = []
        engine.warnings.connect(lambda errors: warnings.extend(error.toString() for error in errors))
        component = QQmlComponent(engine, QUrl.fromLocalFile(
            str(Path(__file__).resolve().parents[1] / "Wireless3DPrinter/LocalPrinter.qml")))
        root = component.create()
        self.assertIsNotNone(root, "\n".join(error.toString() for error in component.errors()))
        root.setProperty("width", 800)
        root.setProperty("height", 600)
        window = QQuickWindow()
        window.resize(800, 600)
        root.setParentItem(window.contentItem())
        window.show()
        app.processEvents()

        def assert_centered():
            content = root.findChild(QObject, "printerContent")
            self.assertAlmostEqual(content.property("x") + content.property("width") / 2,
                                   root.property("width") / 2, delta=1)
            self.assertAlmostEqual(content.property("y") + content.property("height") / 2,
                                   root.property("height") / 2, delta=1)
            self.assertGreaterEqual(content.property("x"), 0)
            self.assertGreaterEqual(content.property("y"), 0)

        assert_centered()
        for name in ("printerTitle", "printerName", "searchPrinters"):
            font = root.findChild(QObject, name).property("font")
            self.assertEqual(font.family(), "DejaVu Sans")
            self.assertEqual(font.pointSize(), 14)

        def wait_done():
            deadline = time.monotonic() + 5
            while stage.busy and time.monotonic() < deadline:
                app.processEvents()
                time.sleep(0.005)
            app.processEvents()
            self.assertFalse(stage.busy)

        device = client.Device("id", "Printer", "127.0.0.1", 80)
        files = [{"name": "Long name", "path": "SHORT.GCO", "size": 20}]
        with patch.object(client, "discover", return_value=[device]), \
                patch.object(client, "list_files", side_effect=[files, []]), \
                patch.object(client, "delete_file") as delete:
            root.findChild(QObject, "searchPrinters").clicked.emit()
            wait_done()
            self.assertEqual(stage.files, files)
            file_list = root.findChild(QObject, "printerFiles")
            assert_centered()
            def find_visual(item, name):
                if item.objectName() == name:
                    return item
                for child in item.childItems():
                    found = find_visual(child, name)
                    if found:
                        return found
                return None

            button = find_visual(file_list, "deleteFile")
            self.assertIsNotNone(button, "list count={}, size={}x{}".format(
                file_list.property("count"), file_list.property("width"), file_list.property("height")))
            header = root.findChild(QObject, "fileTableHeader")
            headers = [item for item in header.childItems() if item.objectName() == "fileHeaderCell"]
            self.assertEqual(len(headers), 3)
            for header_cell, row_name in zip(headers, ("fileNameCell", "fileSizeCell", "fileActionCell")):
                row_cell = find_visual(file_list, row_name)
                self.assertIsNotNone(row_cell)
                self.assertAlmostEqual(header_cell.mapToScene(QPointF()).x(),
                                       row_cell.mapToScene(QPointF()).x(), delta=1)
                self.assertAlmostEqual(header_cell.property("width"), row_cell.property("width"), delta=1)
            scrollbar = root.findChild(QObject, "fileScrollBar")
            handle = root.findChild(QObject, "fileScrollHandle")
            self.assertTrue(scrollbar.property("visible"))
            self.assertTrue(scrollbar.property("active"))
            self.assertTrue(handle.property("visible"))
            self.assertGreater(handle.property("opacity"), 0)
            self.assertGreater(scrollbar.property("width"), 0)
            self.assertLessEqual(button.mapToScene(QPointF()).x() + button.property("width"),
                                 scrollbar.mapToScene(QPointF()).x())
            image = window.grabWindow()
            self.assertFalse(image.isNull())
            border = headers[1].mapToScene(QPointF(0, headers[1].property("height") / 2))
            scale = image.devicePixelRatio()
            self.assertEqual(image.pixelColor(round(border.x() * scale), round(border.y() * scale)),
                             QColor("#000000"))
            row_border = find_visual(file_list, "fileSizeCell").mapToScene(QPointF(0, 20))
            self.assertEqual(image.pixelColor(round(row_border.x() * scale), round(row_border.y() * scale)),
                             QColor("#000000"))
            handle_center = handle.mapToScene(QPointF(handle.property("width") / 2,
                                                     handle.property("height") / 2))
            self.assertEqual(image.pixelColor(round(handle_center.x() * scale), round(handle_center.y() * scale)),
                             QColor("#2345aa"))
            button.clicked.emit()
            dialog = root.findChild(QObject, "deleteDialog")
            self.assertEqual(dialog.property("filePath"), "SHORT.GCO")
            dialog.accept()
            wait_done()
            delete.assert_called_once_with(device, "SHORT.GCO")
            self.assertEqual(file_list.property("count"), 0)
            self.assertTrue(scrollbar.property("visible"))
            self.assertTrue(handle.property("visible"))
        other = client.Device("other", "Other", "127.0.0.2", 80)
        with patch.object(client, "discover", return_value=[device, other]), \
                patch.object(client, "list_files", return_value=[]) as listing:
            root.findChild(QObject, "searchPrinters").clicked.emit()
            wait_done()
            choice = root.findChild(QObject, "printerChoice")
            self.assertEqual(choice.property("currentIndex"), -1)
            choice.setProperty("currentIndex", 1)
            root.findChild(QObject, "connectPrinter").clicked.emit()
            wait_done()
            listing.assert_called_once_with(other)
            self.assertEqual(stage.printerName, "Other")
        stage._files = [{"name": "Part {}".format(i), "path": "{}.GCO".format(i), "size": i}
                        for i in range(100)]
        stage.changed.emit()
        app.processEvents()
        self.assertEqual(file_list.property("count"), 100)
        self.assertGreater(file_list.property("contentHeight"), file_list.property("height"))
        self.assertLess(scrollbar.property("size"), 1)
        scrollbar.setProperty("position", 0.5)
        app.processEvents()
        self.assertGreater(file_list.property("contentY"), 0)
        file_list.setProperty("contentY", file_list.property("contentHeight") - file_list.property("height"))
        app.processEvents()
        self.assertGreater(file_list.property("contentY"), 0)
        root.setProperty("width", 480)
        root.setProperty("height", 420)
        window.resize(480, 420)
        app.processEvents()
        assert_centered()
        self.assertGreater(file_list.property("height"), 0)
        root.deleteLater()
        window.close()
        app.processEvents()
        self.assertEqual(warnings, [])
