import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: positionEditDialog

    property var app
    property var hostWindow
    property var positionRow: ({})
    property bool entryEditedByUser: false
    property bool buyDateEditedByUser: false
    property bool updatingEntryFromDate: false
    property bool largeInvestmentWarningVisible: false

    title: "Ändere Daten"
    width: 430
    height: 500
    minimumWidth: 400
    minimumHeight: 480
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    visible: false

    function enteredInvestmentAmount() {
        return app ? Number(app.parseDecimal(positionEditInvestedInput.text) || 0) : 0
    }

    function enteredEntryValue() {
        return app ? Number(app.parseDecimal(positionEditEntryInput.text) || 0) : 0
    }

    function totalInvestmentText() {
        const invested = enteredInvestmentAmount()
        if (invested <= 0)
            return "-"
        return (invested + app.portfolioOrderFee).toLocaleString(Qt.locale(), "f", 2) + " €"
    }

    function calculatedQuantityText() {
        const invested = enteredInvestmentAmount()
        const entry = enteredEntryValue()
        if (invested <= 0 || entry <= 0)
            return "-"
        return (invested / entry).toLocaleString(Qt.locale(), "f", 6)
            .replace(/([,.]\d*?[1-9])0+$/, "$1")
            .replace(/[,.]0+$/, "")
    }

    function openForRow(row) {
        positionRow = row || ({})
        entryEditedByUser = false
        buyDateEditedByUser = false
        positionEditNameLabel.text = app.cleanDisplayText(positionRow.name || positionRow.symbol || "")
        positionEditBuyDateInput.text = positionRow.buyDate || ""
        const quantity = Number(positionRow.quantity || 0)
        const entryValue = Number(positionRow.entryValue || 0)
        positionEditInvestedInput.text = (quantity * entryValue).toLocaleString(Qt.locale(), "f", 2)
        updatingEntryFromDate = true
        positionEditEntryInput.text = Number(positionRow.entryValue || 0).toLocaleString(Qt.locale(), "f", 2)
        updatingEntryFromDate = false
        largeInvestmentWarningVisible = false
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
        if (!app || !app.dbManager || entryEditedByUser || !buyDateEditedByUser)
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
        buyDateEditedByUser = false
    }

    function availableCashForPosition() {
        if (!app)
            return Number.NaN

        const investmentBudget = Number(app.selectedDepotInvestmentAmount || 0)
        if (investmentBudget <= 0)
            return Number.NaN

        const editedSymbol = String(positionRow.symbol || "").trim()
        let investedInOtherPositions = 0
        const rows = app.portfolioRows || []
        rows.forEach(function(row) {
            if (Number(row.status || 0) === 10)
                return
            if (String(row.symbol || "").trim() === editedSymbol)
                return
            investedInOtherPositions += Number(app.portfolioPositionEntryTotal(row) || 0)
        })
        return investmentBudget - investedInOtherPositions
    }

    function savePosition() {
        updateEntryFromBuyDate()
        const buyDate = positionEditBuyDateInput.text.trim()
        const invested = app.parseDecimal(positionEditInvestedInput.text)
        const entry = app.parseDecimal(positionEditEntryInput.text)
        if (!/^\d{4}-\d{2}-\d{2}$/.test(buyDate)) {
            largeInvestmentWarningVisible = false
            positionEditError.text = "Bitte Kaufdatum im Format JJJJ-MM-TT eingeben."
            return
        }
        if (invested <= 0) {
            largeInvestmentWarningVisible = false
            positionEditError.text = "Bitte eine Investitionssumme groesser 0 eingeben."
            return
        }
        if (entry <= 0) {
            largeInvestmentWarningVisible = false
            positionEditError.text = "Bitte einen Einstiegswert groesser 0 eingeben."
            return
        }

        const totalInvested = invested + app.portfolioOrderFee
        const availableCash = availableCashForPosition()
        if (!isNaN(availableCash) && totalInvested > availableCash && !largeInvestmentWarningVisible) {
            largeInvestmentWarningVisible = true
            positionEditError.text = "Warnung: Die Investitionssumme inklusive Gebühr ("
                + totalInvested.toLocaleString(Qt.locale(), "f", 2) + " €) ist höher als das verfügbare Guthaben ("
                + availableCash.toLocaleString(Qt.locale(), "f", 2) + " €). "
                + "Bitte nur bei Absicht auf ‚Trotzdem speichern‘ klicken."
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
                onTextEdited: {
                    positionEditDialog.buyDateEditedByUser = true
                    positionEditDialog.entryEditedByUser = false
                    positionEditDialog.largeInvestmentWarningVisible = false
                }
                onEditingFinished: positionEditDialog.updateEntryFromBuyDate()
            }

            Label { text: "Investitionssumme (ohne Gebühr)" }
            TextField {
                id: positionEditInvestedInput
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onTextEdited: {
                    positionEditDialog.largeInvestmentWarningVisible = false
                }
            }

            Label { text: "Einstiegswert" }
            TextField {
                id: positionEditEntryInput
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onTextEdited: {
                    positionEditDialog.largeInvestmentWarningVisible = false
                    if (!positionEditDialog.updatingEntryFromDate)
                        positionEditDialog.entryEditedByUser = true
                }
            }

            Label { text: "Gebühr" }
            TextField {
                Layout.fillWidth: true
                text: app ? app.portfolioOrderFee.toLocaleString(Qt.locale(), "f", 2) + " €" : "1,00 €"
                horizontalAlignment: Text.AlignRight
                readOnly: true
                focusPolicy: Qt.NoFocus
            }

            Label { text: "Investitionssumme + Gebühr" }
            TextField {
                Layout.fillWidth: true
                text: positionEditDialog.totalInvestmentText()
                horizontalAlignment: Text.AlignRight
                readOnly: true
                focusPolicy: Qt.NoFocus
                font.bold: true
            }

            Label { text: "Errechnete Stückzahl" }
            TextField {
                Layout.fillWidth: true
                text: positionEditDialog.calculatedQuantityText()
                horizontalAlignment: Text.AlignRight
                readOnly: true
                focusPolicy: Qt.NoFocus
            }

            Label {
                id: positionEditError
                Layout.fillWidth: true
                color: positionEditDialog.largeInvestmentWarningVisible ? "#b45309" : "#b91c1c"
                wrapMode: Text.WordWrap
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                Layout.topMargin: 6
                Item { Layout.fillWidth: true }
                Button {
                    text: "Abbrechen"
                    onClicked: positionEditDialog.close()
                }
                Button {
                    text: positionEditDialog.largeInvestmentWarningVisible ? "Trotzdem speichern" : "OK"
                    onClicked: positionEditDialog.savePosition()
                }
            }
        }
    }
}
