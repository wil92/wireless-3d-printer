import QtQuick 2.10
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import UM 1.5 as UM

Rectangle {
    id: root
    color: UM.Theme.getColor("viewport_overlay")
    property var stage: UM.Controller.activeStage

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function(wheel) { wheel.accepted = true }
    }

    ColumnLayout {
        objectName: "printerContent"
        anchors.centerIn: parent
        width: Math.max(0, Math.min(800, parent.width - 48))
        height: Math.min(implicitHeight, Math.max(0, parent.height - 48))
        spacing: 12

        Label {
            objectName: "printerTitle"
            text: "Local Printer"
            font: UM.Theme.getFont("medium_bold")
            color: UM.Theme.getColor("text")
            Layout.alignment: Qt.AlignHCenter
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 420
            Layout.alignment: Qt.AlignHCenter
            Button {
                objectName: "searchPrinters"
                text: "Search for printers"
                font: UM.Theme.getFont("medium")
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                enabled: !root.stage.busy
                onClicked: root.stage.search()
            }
            Button {
                objectName: "refreshFiles"
                text: "Refresh files"
                font: UM.Theme.getFont("medium")
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: root.stage.printerName !== ""
                enabled: !root.stage.busy
                onClicked: root.stage.refresh()
            }
        }
        RowLayout {
            visible: root.stage.devices.length > 1
            Layout.fillWidth: true
            Layout.maximumWidth: 500
            Layout.alignment: Qt.AlignHCenter
            ComboBox {
                id: printerChoice
                objectName: "printerChoice"
                font: UM.Theme.getFont("medium")
                enabled: !root.stage.busy
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 350
                model: root.stage.devices
                currentIndex: -1
                displayText: currentIndex < 0 ? "Select a printer" : currentText
                onModelChanged: currentIndex = -1
            }
            Button {
                objectName: "connectPrinter"
                text: "Connect"
                font: UM.Theme.getFont("medium")
                enabled: !root.stage.busy && printerChoice.currentIndex >= 0
                onClicked: root.stage.connectPrinter(printerChoice.currentIndex)
            }
        }
        Label {
            objectName: "printerName"
            text: root.stage.printerName ? "Printer: " + root.stage.printerName : "No printer selected"
            color: UM.Theme.getColor("text")
            font: UM.Theme.getFont("medium_bold")
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width
            elide: Text.ElideRight
        }
        Label {
            objectName: "versionWarning"
            visible: root.stage.versionWarning !== ""
            text: root.stage.versionWarning
            color: "#b91c1c"
            font: UM.Theme.getFont("medium_bold")
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
        Label {
            text: root.stage.status
            color: UM.Theme.getColor("text")
            font: UM.Theme.getFont("medium")
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
        Label {
            visible: root.stage.printerName !== ""
            text: root.stage.printerStatus
            color: UM.Theme.getColor("text")
            font: UM.Theme.getFont("medium")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            visible: root.stage.printerName !== ""
            text: "SD status only: keep the printer idle and do not start a job during file operations."
            color: UM.Theme.getColor("text")
            font: UM.Theme.getFont("medium")
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Button {
            objectName: "deleteSelected"
            visible: root.stage.selectedCount > 0
            enabled: root.stage.canDelete && root.stage.selectedCount > 0
            text: "Delete selected (" + root.stage.selectedCount + ")"
            font: UM.Theme.getFont("medium")
            Layout.alignment: Qt.AlignHCenter
            onClicked: bulkDeleteDialog.open()
        }
        Rectangle {
            id: fileTable
            objectName: "printerFileTable"
            visible: root.stage.printerName !== ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: 316
            color: UM.Theme.getColor("main_background")
            border.color: UM.Theme.getColor("thick_lining")
            border.width: 1
            property real dataWidth: Math.max(0, width - 2 - fileScrollBar.width)
            property real sizeWidth: Math.min(120, dataWidth / 3)
            property real actionWidth: Math.min(190, dataWidth / 2)
            property real selectWidth: Math.min(48, dataWidth / 4)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0

                RowLayout {
                    objectName: "fileTableHeader"
                    Layout.fillWidth: true
                    Layout.rightMargin: fileScrollBar.width
                    spacing: 0
                    Repeater {
                        model: ["", "Name", "Size (bytes)", "Action"]
                        Rectangle {
                            objectName: "fileHeaderCell"
                            Layout.fillWidth: index === 1
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: index === 0 ? fileTable.selectWidth :
                                                   index === 2 ? fileTable.sizeWidth : fileTable.actionWidth
                            Layout.maximumWidth: index === 1 ? Infinity : Layout.preferredWidth
                            implicitHeight: 36
                            color: UM.Theme.getColor("main_background")
                            border.color: UM.Theme.getColor("thick_lining")
                            border.width: 1
                            Label {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                text: modelData
                                visible: index !== 0
                                font: UM.Theme.getFont("medium_bold")
                                color: UM.Theme.getColor("text")
                            }
                            CheckBox {
                                objectName: "selectAllFiles"
                                anchors.centerIn: parent
                                visible: index === 0
                                enabled: !root.stage.busy && root.stage.files.length > 0
                                checked: root.stage.files.length > 0 &&
                                         root.stage.selectedCount === root.stage.files.length
                                onClicked: root.stage.toggleSelectAll()
                            }
                        }
                    }
                }

                ListView {
                    id: fileList
                    objectName: "printerFiles"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    clip: true
                    model: root.stage.files
                    ScrollBar.vertical: ScrollBar {
                        id: fileScrollBar
                        objectName: "fileScrollBar"
                        policy: ScrollBar.AlwaysOn
                        active: true
                        implicitWidth: 16
                        minimumSize: 0.1
                        background: Rectangle {
                            color: UM.Theme.getColor("main_background")
                            border.color: UM.Theme.getColor("thick_lining")
                            border.width: 1
                        }
                        contentItem: Rectangle {
                            objectName: "fileScrollHandle"
                            implicitWidth: 12
                            radius: 4
                            color: fileScrollBar.pressed ? UM.Theme.getColor("scrollbar_handle_down") :
                                   fileScrollBar.hovered ? UM.Theme.getColor("scrollbar_handle_hover") :
                                   UM.Theme.getColor("scrollbar_handle")
                        }
                    }
                    delegate: RowLayout {
                        id: fileRow
                        objectName: "fileRow"
                        width: fileTable.dataWidth
                        height: 44
                        spacing: 0
                        property color textColor: UM.Theme.getColor("text")
                        property color rowColor: modelData.currentModel ?
                            Qt.tint(UM.Theme.getColor("main_background"),
                                    Qt.rgba(textColor.r, textColor.g, textColor.b, 0.08)) :
                            UM.Theme.getColor("main_background")
                        Rectangle {
                            objectName: "fileSelectCell"
                            Layout.preferredWidth: fileTable.selectWidth
                            Layout.fillHeight: true
                            color: fileRow.rowColor
                            border.color: UM.Theme.getColor("thick_lining")
                            border.width: 1
                            CheckBox {
                                objectName: "selectFile"
                                anchors.centerIn: parent
                                enabled: !root.stage.busy
                                checked: root.stage.selectedPaths.indexOf(modelData.path) !== -1
                                onClicked: root.stage.selectFile(modelData.path, checked)
                            }
                        }
                        Rectangle {
                            objectName: "fileNameCell"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.fillHeight: true
                            color: fileRow.rowColor
                            border.color: UM.Theme.getColor("thick_lining")
                            border.width: 1
                            Label {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.name + (modelData.currentModel ? "  (current model name)" : "")
                                color: UM.Theme.getColor("text")
                                font: UM.Theme.getFont("medium")
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            objectName: "fileSizeCell"
                            Layout.preferredWidth: fileTable.sizeWidth
                            Layout.fillHeight: true
                            color: fileRow.rowColor
                            border.color: UM.Theme.getColor("thick_lining")
                            border.width: 1
                            Label {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.size
                                color: UM.Theme.getColor("text")
                                font: UM.Theme.getFont("medium")
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            objectName: "fileActionCell"
                            Layout.preferredWidth: fileTable.actionWidth
                            Layout.fillHeight: true
                            color: fileRow.rowColor
                            border.color: UM.Theme.getColor("thick_lining")
                            border.width: 1
                            Button {
                                id: deleteButton
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.margins: 3
                                width: Math.max(0, (parent.width - 9) / 2)
                                objectName: "deleteFile"
                                text: "Delete"
                                font: UM.Theme.getFont("medium")
                                enabled: root.stage.canDelete
                                background: Rectangle {
                                    objectName: "deleteFileBackground"
                                    radius: 4
                                    color: !deleteButton.enabled ?
                                           Qt.tint(fileRow.rowColor, Qt.rgba(0.9, 0.3, 0.3, 0.12)) :
                                           deleteButton.down ? "#e99ba4" :
                                           deleteButton.hovered ? "#efadb4" : "#f5c2c7"
                                    border.color: deleteButton.visualFocus ? "#813943" :
                                                  deleteButton.enabled ? "#c66a73" :
                                                  UM.Theme.getColor("thick_lining")
                                    border.width: deleteButton.visualFocus ? 2 : 1
                                }
                                contentItem: Text {
                                    text: deleteButton.text
                                    font: deleteButton.font
                                    color: deleteButton.enabled ? "#3d1a1e" : UM.Theme.getColor("text")
                                    opacity: deleteButton.enabled ? 1 : 0.65
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                onClicked: {
                                    deleteDialog.filePath = modelData.path
                                    deleteDialog.fileName = modelData.name
                                    deleteDialog.open()
                                }
                            }
                            Button {
                                objectName: "printFile"
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.margins: 3
                                width: Math.max(0, (parent.width - 9) / 2)
                                text: "Print"
                                font: UM.Theme.getFont("medium")
                                enabled: root.stage.canDelete
                                onClicked: {
                                    printDialog.filePath = modelData.path
                                    printDialog.fileName = modelData.name
                                    printDialog.open()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: printDialog
        objectName: "printDialog"
        property string filePath: ""
        property string fileName: ""
        title: "Start printer file"
        font: UM.Theme.getFont("medium")
        modal: true
        implicitWidth: 450
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: "Start " + printDialog.fileName + "?\nMake sure the printer is idle and ready to print."
            font: UM.Theme.getFont("medium")
            wrapMode: Text.Wrap
        }
        onAccepted: root.stage.printFile(filePath)
    }
    Dialog {
        id: deleteDialog
        objectName: "deleteDialog"
        property string filePath: ""
        property string fileName: ""
        title: "Delete printer file"
        font: UM.Theme.getFont("medium")
        modal: true
        implicitWidth: 450
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: "Delete " + deleteDialog.fileName + " from the printer?\nKeep the printer idle."
            font: UM.Theme.getFont("medium")
            wrapMode: Text.Wrap
        }
        onAccepted: root.stage.deleteFile(filePath)
    }
    Dialog {
        id: bulkDeleteDialog
        objectName: "bulkDeleteDialog"
        title: "Delete printer files"
        font: UM.Theme.getFont("medium")
        modal: true
        implicitWidth: 450
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: "Delete " + root.stage.selectedCount + " selected file(s) from the printer?\nKeep the printer idle."
            font: UM.Theme.getFont("medium")
            wrapMode: Text.Wrap
        }
        onAccepted: root.stage.deleteSelected()
    }
}
