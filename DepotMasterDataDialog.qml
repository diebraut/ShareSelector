import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: depotMasterDataDialog

    property var app
    property var hostWindow
    property var depotData: ({})
    property int investmentYear: new Date().getFullYear()

    title: "Depotstammdaten"
    width: 540
    height: 510
    minimumWidth: 500
    minimumHeight: 480
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    visible: false

    function openForDepot() {
        if (!app || !app.dbManager)
            return

        investmentYear = new Date().getFullYear()
        depotData = app.dbManager.getDepotMasterData(app.selectedDepotId, investmentYear)
        depotNameInput.text = depotData.name || app.selectedDepotName || ""
        depotStartInvestInput.text = depotData.startInvest || ""
        depotInvestmentYearInput.text = String(depotData.investmentYear || investmentYear)
        depotInvestmentAmountInput.text = Number(depotData.investmentAmount || 0).toLocaleString(Qt.locale(), "f", 2)
        depotYearEndValueInput.text = Number(depotData.yearEndValue || 0).toLocaleString(Qt.locale(), "f", 2)
        depotCurrencyInput.text = depotData.currency || "EUR"
        depotDescriptionInput.text = depotData.description || ""
        depotActiveInput.checked = depotData.isActive === undefined ? true : Boolean(depotData.isActive)
        depotMasterDataError.text = ""

        if (hostWindow) {
            x = hostWindow.x + Math.max(20, (hostWindow.width - width) / 2)
            y = hostWindow.y + Math.max(20, (hostWindow.height - height) / 2)
        }
        show()
        raise()
        requestActivate()
        depotNameInput.forceActiveFocus()
        depotNameInput.selectAll()
    }

    function saveDepot() {
        const name = depotNameInput.text.trim()
        const startInvest = depotStartInvestInput.text.trim()
        const year = Number(depotInvestmentYearInput.text.trim())
        const investmentAmount = app.parseDecimal(depotInvestmentAmountInput.text)
        const yearEndValue = app.parseDecimal(depotYearEndValueInput.text)
        const currency = depotCurrencyInput.text.trim().toUpperCase()

        if (name.length === 0) {
            depotMasterDataError.text = "Bitte einen Depotnamen eingeben."
            return
        }
        if (startInvest.length > 0 && !/^\d{4}-\d{2}-\d{2}$/.test(startInvest)) {
            depotMasterDataError.text = "Bitte Startdatum im Format JJJJ-MM-TT eingeben."
            return
        }
        if (!Number.isInteger(year) || year < 1900 || year > 3000) {
            depotMasterDataError.text = "Bitte ein gueltiges Jahr eingeben."
            return
        }
        if (investmentAmount < 0) {
            depotMasterDataError.text = "Bitte eine Investitionssumme groesser oder gleich 0 eingeben."
            return
        }
        if (yearEndValue < 0) {
            depotMasterDataError.text = "Bitte einen Jahresendwert groesser oder gleich 0 eingeben."
            return
        }
        if (currency.length === 0) {
            depotMasterDataError.text = "Bitte eine Waehrung eingeben."
            return
        }

        if (!app.saveSelectedDepotMasterData(
                name,
                startInvest,
                year,
                investmentAmount,
                yearEndValue,
                currency,
                depotDescriptionInput.text,
                depotActiveInput.checked)) {
            depotMasterDataError.text = "Depotstammdaten konnten nicht gespeichert werden."
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
                text: "Depot " + app.selectedDepotId
                Layout.fillWidth: true
                color: "#475569"
                font.bold: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 8

                Label { text: "Name"; Layout.preferredWidth: 130 }
                TextField {
                    id: depotNameInput
                    Layout.fillWidth: true
                    selectByMouse: true
                }

                Label { text: "Investiert seit" }
                TextField {
                    id: depotStartInvestInput
                    Layout.fillWidth: true
                    placeholderText: "JJJJ-MM-TT"
                    selectByMouse: true
                }

                Label { text: "Jahr" }
                TextField {
                    id: depotInvestmentYearInput
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                }

                Label { text: "Investitionssumme" }
                TextField {
                    id: depotInvestmentAmountInput
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }

                Label { text: "Wert am Ende des Jahres" }
                TextField {
                    id: depotYearEndValueInput
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }

                Label { text: "Waehrung" }
                TextField {
                    id: depotCurrencyInput
                    Layout.fillWidth: true
                    maximumLength: 8
                    selectByMouse: true
                }

                Label { text: "Aktiv" }
                CheckBox {
                    id: depotActiveInput
                    checked: true
                }

                Label {
                    text: "Beschreibung"
                    Layout.alignment: Qt.AlignTop
                }
                TextArea {
                    id: depotDescriptionInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 82
                    selectByMouse: true
                    wrapMode: TextArea.Wrap
                }
            }

            Label {
                id: depotMasterDataError
                Layout.fillWidth: true
                color: "#b91c1c"
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
                    onClicked: depotMasterDataDialog.close()
                }
                Button {
                    text: "OK"
                    onClicked: depotMasterDataDialog.saveDepot()
                }
            }
        }
    }
}
