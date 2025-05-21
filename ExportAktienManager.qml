// ExportAktienManager.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: exportWindow
    title: "Aktienexport"
    width: 300
    height: 150
    modality: Qt.ApplicationModal

    signal closed()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10

        ComboBox {
            id: countryBox
            Layout.fillWidth: true
            model: ["US", "DE"]
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 10

            Button {
                text: "Export Aktienstamm"
                onClicked: {
                    //exportController.exportShares(countryBox.currentText)
                    exportController.exportShares("XFRA")
                }
            }

            Button {
                text: "Abbrechen"
                onClicked: exportWindow.close()
            }
        }

        Label {
            id: errorLabel
            color: "red"
            visible: false
        }
    }

    Connections {
        target: exportController

        function onExportFinished() {
            exportWindow.close()
        }

        function onErrorOccurred(error) {
            errorLabel.text = error
            errorLabel.visible = true
        }
    }
}
