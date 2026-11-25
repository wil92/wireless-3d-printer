import os
import sys
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    from PyQt6.QtCore import QObject, QPointF, QUrl, Qt, pyqtProperty, pyqtSlot
    from PyQt6.QtGui import QColor, QFont
    from PyQt6.QtQml import QQmlComponent, QQmlEngine, qmlRegisterSingletonInstance
    from PyQt6.QtQuick import QQuickWindow
    from PyQt6.QtTest import QTest
    from PyQt6.QtWidgets import QApplication
    from Wireless3DPrinter import client, local_printer
except ImportError:
    local_printer = None


@unittest.skipIf(local_printer is None, "Cura/Uranium and PyQt6 are required")
class LocalPrinterQmlTests(unittest.TestCase):
    def test_stage_qml_loads_and_buttons_drive_management(self):
        self.app = QApplication.instance() or QApplication([])
        app = self.app
        application = Mock()
        application.getPrintInformation.return_value.jobName = ""
        application.getPrintInformation.return_value.baseName = ""
        status = patch.object(client, "printer_status", return_value={
            "printer_name": "Printer", "printer_status": "not_sd_printing",
            "nozzle_temperature": 20, "bed_temperature": 21})
        status.start()
        self.addCleanup(status.stop)
        version = patch.object(client, "version_warning", return_value="")
        version.start()
        self.addCleanup(version.stop)
        with patch.object(local_printer.Application, "getInstance", return_value=application):
            stage = local_printer.LocalPrinterStage()
            stage._on_engine_created()
        self.addCleanup(stage._timer.stop)

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
        warning = root.findChild(QObject, "versionWarning")
        self.assertFalse(warning.property("visible"))
        stage._version_warning = "WARNING: major versions differ"
        stage.changed.emit()
        app.processEvents()
        self.assertTrue(warning.property("visible"))
        self.assertIn("major versions differ", warning.property("text"))
        stage._version_warning = ""
        stage.changed.emit()
        app.processEvents()
        for name in ("printerTitle", "printerName", "searchPrinters"):
            font = root.findChild(QObject, name).property("font")
            self.assertEqual(font.family(), "DejaVu Sans")
            self.assertEqual(font.pointSize(), 14)

        def wait_done():
            deadline = time.monotonic() + 5
            while (stage.busy or stage._polling) and time.monotonic() < deadline:
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
            self.assertEqual(stage.files, [dict(files[0], currentModel=False)])
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
            print_button = find_visual(file_list, "printFile")
            self.assertIsNotNone(print_button)
            self.assertIsNotNone(button, "list count={}, size={}x{}".format(
                file_list.property("count"), file_list.property("width"), file_list.property("height")))
            delete_background = find_visual(button, "deleteFileBackground")
            self.assertEqual(delete_background.property("color").name(), "#f5c2c7")
            self.assertEqual(button.property("contentItem").property("color").name(), "#3d1a1e")
            self.assertTrue(button.property("enabled"))
            header = root.findChild(QObject, "fileTableHeader")
            headers = [item for item in header.childItems() if item.objectName() == "fileHeaderCell"]
            self.assertEqual(len(headers), 4)
            for header_cell, row_name in zip(headers, ("fileSelectCell", "fileNameCell", "fileSizeCell", "fileActionCell")):
                row_cell = find_visual(file_list, row_name)
                self.assertIsNotNone(row_cell)
                self.assertAlmostEqual(header_cell.mapToScene(QPointF()).x(),
                                       row_cell.mapToScene(QPointF()).x(), delta=1)
                self.assertAlmostEqual(header_cell.property("width"), row_cell.property("width"), delta=1)
            select_all = find_visual(headers[0], "selectAllFiles")
            select_all.clicked.emit()
            app.processEvents()
            self.assertEqual(stage.selectedCount, 1)
            self.assertTrue(root.findChild(QObject, "deleteSelected").property("visible"))
            select_all.clicked.emit()
            app.processEvents()
            self.assertEqual(stage.selectedCount, 0)
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
            button_fill = button.mapToScene(QPointF(8, 8))
            self.assertEqual(image.pixelColor(round(button_fill.x() * scale),
                                              round(button_fill.y() * scale)).name(), "#f5c2c7")
            self.assertEqual(image.pixelColor(round(border.x() * scale), round(border.y() * scale)),
                             QColor("#000000"))
            row_border = find_visual(file_list, "fileSizeCell").mapToScene(QPointF(0, 20))
            self.assertEqual(image.pixelColor(round(row_border.x() * scale), round(row_border.y() * scale)),
                             QColor("#000000"))
            handle_center = handle.mapToScene(QPointF(handle.property("width") / 2,
                                                     handle.property("height") / 2))
            self.assertEqual(image.pixelColor(round(handle_center.x() * scale), round(handle_center.y() * scale)),
                             QColor("#2345aa"))
            dialog = root.findChild(QObject, "deleteDialog")
            stage._printer_status["printer_status"] = "sd_printing"
            stage.changed.emit()
            app.processEvents()
            self.assertFalse(button.property("enabled"))
            self.assertFalse(print_button.property("enabled"))
            self.assertNotEqual(delete_background.property("color").name(), "#f5c2c7")
            button_position = button.mapToScene(QPointF(button.property("width") / 2,
                                                       button.property("height") / 2)).toPoint()
            QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, button_position)
            self.assertFalse(dialog.property("visible"))
            stage._printer_status["printer_status"] = "not_sd_printing"
            stage.changed.emit()
            app.processEvents()
            print_position = print_button.mapToScene(QPointF(print_button.property("width") / 2,
                                                            print_button.property("height") / 2)).toPoint()
            with patch.object(client, "start_print") as start_print:
                QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, print_position)
                app.processEvents()
                print_dialog = root.findChild(QObject, "printDialog")
                self.assertTrue(print_dialog.property("visible"))
                self.assertEqual(print_dialog.property("filePath"), "SHORT.GCO")
                print_dialog.accept()
                wait_done()
                start_print.assert_called_once_with(device, "SHORT.GCO")
            QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, button_position)
            app.processEvents()
            self.assertTrue(dialog.property("visible"))
            self.assertEqual(dialog.property("filePath"), "SHORT.GCO")
            dialog.accept()
            wait_done()
            delete.assert_called_once_with(device, "SHORT.GCO")
            self.assertEqual(file_list.property("count"), 0)
            self.assertTrue(scrollbar.property("visible"))
            self.assertTrue(handle.property("visible"))
        application.getPrintInformation.return_value.jobName = "short"
        with patch.object(client, "discover", return_value=[device]), \
                patch.object(client, "list_files", side_effect=[files + [
                    {"name": "Second", "path": "SECOND.GCO", "size": 21}], []]), \
                patch.object(client, "delete_files") as delete_many:
            root.findChild(QObject, "searchPrinters").clicked.emit()
            wait_done()

            def rows():
                return sorted((item for item in file_list.property("contentItem").childItems()
                               if item.objectName() == "fileRow"), key=lambda item: item.property("y"))

            def assert_highlight(matched_index):
                current_rows = rows()
                self.assertEqual(len(current_rows), 2)
                background = QColor("#ffffff")
                for index, row in enumerate(current_rows):
                    colors = [find_visual(row, name).property("color") for name in
                              ("fileSelectCell", "fileNameCell", "fileSizeCell", "fileActionCell")]
                    self.assertTrue(all(color == colors[0] for color in colors))
                    if index == matched_index:
                        difference = background.red() - colors[0].red()
                        self.assertGreaterEqual(difference, 5)
                        self.assertLessEqual(difference, 30)
                    else:
                        self.assertEqual(colors[0], background)
                image = window.grabWindow()
                for row in current_rows:
                    cell = find_visual(row, "fileNameCell")
                    point = cell.mapToScene(QPointF(3, 3))
                    scale = image.devicePixelRatio()
                    self.assertEqual(image.pixelColor(round(point.x() * scale), round(point.y() * scale)).name(),
                                     cell.property("color").name())
                    border = cell.mapToScene(QPointF(0, 20))
                    self.assertEqual(image.pixelColor(round(border.x() * scale), round(border.y() * scale)),
                                     QColor("#000000"))

            assert_highlight(0)
            application.getPrintInformation.return_value.jobName = "second"
            stage.filesChanged.emit()
            app.processEvents()
            assert_highlight(1)

            def click_item(item):
                position = item.mapToScene(QPointF(item.property("width") / 2,
                                                    item.property("height") / 2)).toPoint()
                QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, position)
                app.processEvents()

            checkbox = find_visual(file_list, "selectFile")
            click_item(checkbox)
            self.assertEqual(stage.selectedPaths, ["SHORT.GCO"])
            click_item(select_all)
            self.assertEqual(stage.selectedCount, 2)
            bulk_button = root.findChild(QObject, "deleteSelected")
            self.assertTrue(bulk_button.property("visible"))
            bulk_button.clicked.emit()
            dialog = root.findChild(QObject, "bulkDeleteDialog")
            dialog.reject()
            self.assertEqual(stage.selectedCount, 2)
            bulk_button.clicked.emit()
            dialog.accept()
            wait_done()
            delete_many.assert_called_once_with(device, ["SECOND.GCO", "SHORT.GCO"])
            self.assertEqual(stage.selectedCount, 0)
            self.assertEqual(stage.files, [])
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
        stage.filesChanged.emit()
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
        position = file_list.property("contentY")
        with patch.object(client, "printer_status", return_value={
                "printer_name": "Other", "printer_status": "not_sd_printing",
                "nozzle_temperature": 30, "bed_temperature": 40}):
            stage._poll_status()
            wait_done()
        self.assertIn("30.0 °C", stage.printerStatus)
        self.assertAlmostEqual(file_list.property("contentY"), position, delta=1)
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
