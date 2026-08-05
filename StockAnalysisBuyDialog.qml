import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: stockAnalysisBuyDialog
    property var app
    property var hostWindow
    property alias amountText: stockAnalysisBuyAmountInput.text
    property alias dateText: stockAnalysisBuyDateInput.text
    property alias errorText: stockAnalysisBuyError.text
        title: "Auswahl kaufen"
        width: 460
        height: 250
        minimumWidth: 420
        minimumHeight: 240
        flags: Qt.Dialog
        modality: Qt.ApplicationModal
        visible: false
        property var stocks: []

        function open() {
            x = hostWindow.x + Math.max(20, (hostWindow.width - width) / 2)
            y = hostWindow.y + Math.max(20, (hostWindow.height - height) / 2)
            show()
            raise()
            requestActivate()
        }

        Rectangle {
            anchors.fill: parent
            color: "#f4f6f7"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Label {
                    text: stockAnalysisBuyDialog.stocks.length + " Stock(s) ausgewaehlt"
                    font.bold: true
                    Layout.fillWidth: true
                }

                Label { text: "Betrag pro Stock" }
                TextField {
                    id: stockAnalysisBuyAmountInput
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }

                Label { text: "Kaufdatum" }
                TextField {
                    id: stockAnalysisBuyDateInput
                    Layout.fillWidth: true
                    selectByMouse: true
                    placeholderText: "JJJJ-MM-TT"
                }

                Label {
                    id: stockAnalysisBuyError
                    Layout.fillWidth: true
                    color: "#b91c1c"
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "Abbrechen"
                        onClicked: stockAnalysisBuyDialog.close()
                    }
                    Button {
                        text: "Kaufen"
                        onClicked: app.buySelectedStockAnalysisStocks()
                    }
                }
            }
        }
    }
