pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

Window {
    id: liveWindow
    property var dbManager
    property int depotId: 1
    property string filterMode: "active"
    property bool updatesEnabled: false
    property var stocks: []

    title: "IBKR Geld/Brief – Depot"
    width: 1180
    height: 700
    minimumWidth: 950
    minimumHeight: 350
    visible: false
    color: "#f4f6f7"

    function applySubscription() {
        dbManager.stopIbkrLiveQuotes()
        stocks = filterMode === "sold" ? []
            : dbManager.getActiveDepotLiveQuoteStocks(depotId, filterMode === "observed")
        if (updatesEnabled && filterMode !== "sold")
            dbManager.startIbkrLiveQuotes(depotId, filterMode === "observed")
    }

    function selectPortfolio(selectedDepotId, selectedFilter) {
        if (depotId === selectedDepotId && filterMode === selectedFilter)
            return
        depotId = selectedDepotId
        filterMode = selectedFilter
        applySubscription()
    }

    function startIfEnabled() {
        if (updatesEnabled && filterMode !== "sold" && !dbManager.ibkrLiveQuotesActive)
            applySubscription()
    }

    function setUpdatesEnabled(enabled) {
        if (updatesEnabled === enabled)
            return
        updatesEnabled = enabled
        applySubscription()
    }

    function openForDepot(selectedDepotId, selectedFilter) {
        const changed = depotId !== selectedDepotId || filterMode !== selectedFilter
        const wasHidden = !visible
        depotId = selectedDepotId
        filterMode = selectedFilter
        show()
        raise()
        requestActivate()
        if (changed || (wasHidden && !dbManager.ibkrLiveQuotesActive))
            applySubscription()
    }

    function quoteFor(symbol) {
        const quotes = dbManager ? dbManager.ibkrLiveQuotes : ({})
        return quotes[symbol] || ({})
    }

    function priceText(value) {
        if (value === null || value === undefined || Number(value) <= 0)
            return "—"
        return Number(value).toLocaleString(Qt.locale(), "f", 5)
            .replace(/0+$/, "").replace(/[,.]$/, "")
    }

    function receivedText(value) {
        if (!value)
            return "—"
        return Qt.formatDateTime(new Date(value), "dd.MM.yyyy HH:mm:ss")
    }

    Timer {
        interval: 30000
        running: liveWindow.updatesEnabled && liveWindow.filterMode !== "sold"
        repeat: true
        onTriggered: liveWindow.startIfEnabled()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: liveWindow.dbManager ? liveWindow.dbManager.ibkrLiveQuotesStatus : ""
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        Label {
            text: liveWindow.filterMode === "sold"
                  ? "Verkaufte Positionen: keine Live-Aktualisierung."
                  : (liveWindow.filterMode === "observed" ? "Unter Beobachtung: " : "Gekauft: ")
                    + "SMART wenn verfügbar, sonst Direktbörse. Bis zu 40 IBKR-Abfragen gleichzeitig; "
                    + liveWindow.stocks.length
                    + " Positionen in etwa " + (Math.ceil(liveWindow.stocks.length / 40) * 12)
                    + " Sekunden reihum. Zeit = Empfang in der App. EUR-Mittelwerte werden etwa alle 20 s gesammelt gespeichert."
            color: "#475569"
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "#dbe4ea"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                Label { text: "Aktie"; Layout.fillWidth: true; font.bold: true }
                Label { text: "Börse"; Layout.preferredWidth: 90; font.bold: true }
                Label { text: "Geld"; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; font.bold: true }
                Label { text: "Brief"; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; font.bold: true }
                Label { text: "Kurs (Mitte)"; Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight; font.bold: true }
                Label { text: "Empfang Geld"; Layout.preferredWidth: 170; font.bold: true }
                Label { text: "Empfang Brief"; Layout.preferredWidth: 170; font.bold: true }
                Label { text: "Status"; Layout.preferredWidth: 150; font.bold: true }
            }
        }

        ListView {
            id: stockList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: liveWindow.stocks
            delegate: Rectangle {
                id: stockDelegate
                required property var modelData
                required property int index
                readonly property var quote: liveWindow.quoteFor(modelData.symbol)
                width: stockList.width
                height: 30
                color: index % 2 ? "#eef2f4" : "#ffffff"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    Label {
                        text: stockDelegate.modelData.name || stockDelegate.modelData.symbol
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label { text: stockDelegate.quote.exchange || stockDelegate.modelData.exchange || "—"; Layout.preferredWidth: 90 }
                    Label {
                        text: liveWindow.priceText(stockDelegate.quote.bid)
                        Layout.preferredWidth: 100
                        horizontalAlignment: Text.AlignRight
                    }
                    Label {
                        text: liveWindow.priceText(stockDelegate.quote.ask)
                        Layout.preferredWidth: 100
                        horizontalAlignment: Text.AlignRight
                    }
                    Label {
                        text: liveWindow.priceText(stockDelegate.quote.mid)
                        Layout.preferredWidth: 110
                        horizontalAlignment: Text.AlignRight
                    }
                    Label { text: liveWindow.receivedText(stockDelegate.quote.bidReceivedUtc); Layout.preferredWidth: 170 }
                    Label { text: liveWindow.receivedText(stockDelegate.quote.askReceivedUtc); Layout.preferredWidth: 170 }
                    Label {
                        text: stockDelegate.quote.status || "—"
                        Layout.preferredWidth: 150
                        elide: Text.ElideRight
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
