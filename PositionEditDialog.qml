import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: positionEditDialog

    property var app
    property var hostWindow
    property var positionRow: ({})
    property bool entryEditedByUser: false
    property bool updatingEntryFromDate: false

    title: "Ändere Daten"
    width: 430
    height: 285
    minimumWidth: 400
    minimumHeight: 260
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    visible: false

    function openForRow(row) {
        positionRow = row || ({})
        entryEditedByUser = false
        positionEditNameLabel.text = app.cleanDisplayText(positionRow.name || positionRow.symbol || "")
        positionEditBuyDateInput.text = positionRow.buyDate || ""
        positionEditInvestedInput.text = app.portfolioPositionEntryTotal(positionRow).toLocaleString(Qt.locale(), "f", 2)
        updatingEntryFromDate = true
        positionEditEntryInput.text = Number(positionRow.entryValue || 0).toLocaleString(Qt.locale(), "f", 2)
        updatingEntryFromDate = false
        positionEditError.text = ""

        if (hostWindow) {
            x = hostWindow.x + Math.max(20, (hostWindow.width - width) / 2)
            y = hostWindow.y + Math.max(20, (hostWindow.height - height) / 2)
        }
        show()
        raise()
        requestActivate()
        positionEditBuyDateInput.forceActiveFocus()
        positionEditBuyDateInput.selectAll()
    }

    function updateEntryFromBuyDate() {
        if (!app || !app.dbManager || entryEditedByUser)
            return

        const buyDate = positionEditBuyDateInput.text.trim()
        const symbol = String(positionRow.symbol || "").trim()
        if (!/^\d{4}-\d{2}-\d{2}$/.test(buyDate) || symbol.length === 0)
            return

        const entry = Number(app.dbManager.closePriceOnOrBefore(symbol, buyDate) || 0)
        if (entry <= 0)
            return

        updatingEntryFromDate = true
        positionEditEntryInput.text = entry.toLocaleString(Qt.locale(), "f", 2)
        updatingEntryFromDate = false
    }

    function savePosition() {
        updateEntryFromBuyDate()
        const buyDate = positionEditBuyDateInput.text.trim()
        const invested = app.parseDecimal(positionEditInvestedInput.text)
        const entry = app.parseDecimal(positionEditEntryInput.text)
        if (!/^\d{4}-\d{2}-\d{2}$/.test(buyDate)) {
            positionEditError.text = "Bitte Kaufdatum im Format JJJJ-MM-TT eingeben."
            return
        }
        if (invested <= 0) {
            positionEditError.text = "Bitte eine investierte Summe groesser 0 eingeben."
            return
        }
        if (entry <= 0) {
            positionEditError.text = "Bitte einen Einstiegswert groesser 0 eingeben."
            return
        }

        const ok = app.updatePortfolioPositionData(positionRow, buyDate, invested, entry)
        if (!ok) {
            positionEditError.text = "Position konnte nicht gespeichert werden."
            return
        }
        close()
    }

    Rectangle {
        anchors.fill: parent
        color: "#f4f6f7"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8

            Label {
                id: positionEditNameLabel
                Layout.fillWidth: true
                font.bold: true
                elide: Text.ElideRight
            }

            Label { text: "Kaufdatum" }
            TextField {
                id: positionEditBuyDateInput
                Layout.fillWidth: true
                placeholderText: "JJJJ-MM-TT"
                selectByMouse: true
                onEditingFinished: positionEditDialog.updateEntryFromBuyDate()
            }

            Label { text: "Investierte Summe" }
            TextField {
                id: positionEditInvestedInput
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onTextEdited: {
                    if (!positionEditDialog.updatingEntryFromDate)
                        positionEditDialog.entryEditedByUser = true
                }
            }

            Label { text: "Einstiegswert" }
            TextField {
                id: positionEditEntryInput
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
            }

            Label {
                id: positionEditError
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
                    onClicked: positionEditDialog.close()
                }
                Button {
                    text: "OK"
                    onClicked: positionEditDialog.savePosition()
                }
            }
        }
    }
}
