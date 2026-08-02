import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.LocalStorage 2.15

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 2000
    height: 800
    title: "Stock Database Browser"

    property var dbManager: databaseManager
    property var expController: exportController

    // Fokus-Handler für das Hauptfenster
    // Globaler Fokus-Handler
    onActiveChanged: if (active) listView.forceActiveFocus()

    Component.onCompleted: Qt.callLater(function() {
        portfolioWindow.show()
    })

    // Fokus-Recovery
    MouseArea {
        anchors.fill: parent
        onClicked: listView.forceActiveFocus()
        enabled: !listView.activeFocus
        z: -1
    }

    // Datenmodell
    ListModel {
        id: stockModel
        dynamicRoles: true
        function initSelection() {
            for (var i = 0; i < count; i++) {
                get(i).selected = false;
            }
        }
    }

    ListModel {
        id: detailQuoteModel
    }

    ListModel {
        id: boughtStockModel
    }

    ListModel {
        id: portfolioModel
        dynamicRoles: true
    }

    ListModel {
        id: stockAnalysisConfigModel
    }

    ListModel {
        id: stockAnalysisResultModel
        dynamicRoles: true
    }

    ListModel {
        id: stockAnalysisQuoteModel
    }

    // Speichert das aktuell selektierte Symbol & Exchange
    property string selectedSymbol: ""
    property string selectedExchange: ""
    property int selectedIndex: -1
    property int currentListViewIndex:0

    property int currentSortPeriod: 0
    property bool currentSortAsc: true
    property var detailStock: ({})
    property var detailPeriods: []
    property var detailQuotes: []
    property int detailFromDay: 1
    property int detailToDay: 1
    property string boughtStockMessage: ""
    property int selectedBoughtStockIndex: -1
    property bool detailStockBought: false
    property string pendingDeleteBoughtSymbol: ""
    property int selectedPortfolioIndex: -1
    property int selectedStockAnalysisConfigIndex: -1
    property int selectedStockAnalysisIndex: -1
    property var selectedStockAnalysisRows: []
    property int stockAnalysisSelectionAnchor: -1
    property bool stockAnalysisStockSelected: false
    property string stockAnalysisMessage: ""
    property real stockAnalysisCorridorPercent: 10
    property real stockAnalysisCorridorHitPercent: 0
    property real stockAnalysisActualIncreasePercent: 0
    property real stockAnalysisRequiredCorridorPercent: 0
    property real stockAnalysisMaxDrawdownPercent: 10
    property real stockAnalysisActualMaxDrawdownPercent: 0
    property real stockAnalysisCorridorRequiredPercent: 0
    property int stockAnalysisQuoteCount: 90
    property string stockAnalysisQuoteDateRangeText: ""
    property bool stockAnalysisScanActive: false
    property var stockAnalysisScanSymbols: []
    property int stockAnalysisScanIndex: 0
    property int stockAnalysisScanFound: 0
    onStockAnalysisCorridorPercentChanged: {
        if (stockAnalysisQuoteModel.count > 0) {
            updateStockAnalysisCorridorStats()
            stockAnalysisChartRefreshTimer.restart()
        }
    }
    function normalizedStockAnalysisQuoteCount(value) {
        return Math.max(10, Math.min(90, Math.round(Number(value) / 10) * 10))
    }

    function reloadSelectedStockAnalysisQuotes() {
        if (stockAnalysisStockSelected && selectedStockAnalysisIndex >= 0)
            selectStockAnalysisResult(selectedStockAnalysisIndex)
    }

    function setStockAnalysisQuoteCount(value, forceReload) {
        let normalizedQuoteCount = normalizedStockAnalysisQuoteCount(value)
        if (stockAnalysisQuoteCount !== normalizedQuoteCount) {
            stockAnalysisQuoteCount = normalizedQuoteCount
        } else if (forceReload) {
            reloadSelectedStockAnalysisQuotes()
        }
    }

    function updateStockAnalysisQuoteDateRangeText() {
        let count = stockAnalysisQuoteModel.count
        if (count === 0) {
            stockAnalysisQuoteDateRangeText = ""
            return
        }

        let newestDate = stockAnalysisQuoteModel.get(0).closedate || ""
        let oldestDate = stockAnalysisQuoteModel.get(count - 1).closedate || ""
        stockAnalysisQuoteDateRangeText = oldestDate.length > 0 && newestDate.length > 0
            ? oldestDate + " - " + newestDate
            : ""
    }

    onStockAnalysisQuoteCountChanged: reloadSelectedStockAnalysisQuotes()
    property var portfolioFields: [
        { heading: true, label: "Position" },
        { key: "symbol", label: "Symbol" },
        { key: "name", label: "Name" },
        { key: "buyDate", label: "Kaufdatum" },
        { key: "analysisConfigName", label: "Analyse-Konfiguration" },
        { key: "sellDate", label: "Verkaufsdatum" },
        { key: "currentValue", label: "Aktueller Wert", format: "money" },
        { key: "quantity", label: "Anzahl Positionen", format: "quantity" },
        { key: "entryValue", label: "Einstiegswert", format: "money" },
        { key: "valueIncreasePercent", label: "Wertentwicklung", format: "percent" },
        { key: "status", label: "Status", format: "status" },
        { key: "mic", label: "MIC" },
        { key: "isin", label: "ISIN" },
        { key: "exchange", label: "BÃ¶rse" },
        { key: "countryCode", label: "LÃ¤ndercode" },
        { key: "city", label: "BÃ¶rsenstadt" },

        { heading: true, label: "IBKR-Stammdaten" },
        { key: "ibkrConId", label: "IBKR ConId" },
        { key: "currency", label: "WÃ¤hrung" },
        { key: "primaryExchange", label: "PrimÃ¤rbÃ¶rse" },
        { key: "localSymbol", label: "Lokales Symbol" },
        { key: "securityType", label: "Wertpapierart" },
        { key: "tradingClass", label: "Handelsklasse" },
        { key: "stockType", label: "Aktienart" },
        { key: "industry", label: "Branche" },
        { key: "category", label: "Kategorie" },
        { key: "subcategory", label: "Unterkategorie" },
        { key: "timeZoneId", label: "Zeitzone" },
        { key: "minTick", label: "Minimale Preisstufe", format: "decimal8" },
        { key: "marketRuleIds", label: "Market-Rule-IDs" },
        { key: "validExchanges", label: "GÃ¼ltige HandelsplÃ¤tze" },
        { key: "marketName", label: "Marktname" },
        { key: "cusip", label: "CUSIP" },
        { key: "ibkrLastSyncAt", label: "Letzte IBKR-Synchronisierung" },

        { heading: true, label: "Kennzahlen" },
        { key: "asOfDate", label: "Stichtag" },
        { key: "fundamentalCurrency", label: "KennzahlenwÃ¤hrung" },
        { key: "marketCapitalization", label: "Marktkapitalisierung", format: "large" },
        { key: "enterpriseValue", label: "Unternehmenswert", format: "large" },
        { key: "peRatio", label: "KGV", format: "decimal2" },
        { key: "forwardPeRatio", label: "Erwartetes KGV", format: "decimal2" },
        { key: "priceToBookRatio", label: "KBV", format: "decimal2" },
        { key: "priceToSalesRatio", label: "KUV", format: "decimal2" },
        { key: "priceToCashFlowRatio", label: "KCV", format: "decimal2" },
        { key: "priceToDividendRatio", label: "Kurs-Dividenden-Verhältnis", format: "decimal2" },
        { key: "eps", label: "Gewinn je Aktie (EPS)", format: "decimal2" },
        { key: "forwardEps", label: "Erwartetes EPS", format: "decimal2" },
        { key: "dividendPerShare", label: "Dividende je Aktie", format: "money" },
        { key: "dividendYield", label: "Dividendenrendite", format: "percent" },
        { key: "payoutRatio", label: "AusschÃ¼ttungsquote", format: "percent" },
        { key: "beta", label: "Beta", format: "decimal2" },
        { key: "revenue", label: "Umsatz", format: "large" },
        { key: "netIncome", label: "Nettogewinn", format: "large" },
        { key: "ebitda", label: "EBITDA", format: "large" },
        { key: "returnOnEquity", label: "Eigenkapitalrendite", format: "percent" },
        { key: "returnOnAssets", label: "Gesamtkapitalrendite", format: "percent" },
        { key: "debtToEquity", label: "Verschuldungsgrad", format: "decimal2" },
        { key: "sharesOutstanding", label: "Ausstehende Aktien", format: "large" },
        { key: "week52High", label: "52-Wochen-Hoch", format: "money" },
        { key: "week52Low", label: "52-Wochen-Tief", format: "money" },
        { key: "source", label: "Datenquelle" },
        { key: "fundamentalUpdatedAt", label: "Kennzahlen aktualisiert" }
    ]

    Timer {
        id: clipboardTimer
        interval: 2000
        running: false
        repeat: false
        onTriggered: clipboardPopup.visible = false
    }

    Timer {
        id: stockAnalysisChartRefreshTimer
        interval: 80
        running: false
        repeat: false
        onTriggered: stockAnalysisChart.requestPaint()
    }

    Timer {
        id: stockAnalysisScanTimer
        interval: 1
        running: false
        repeat: true
        onTriggered: processStockAnalysisScanBatch()
    }

    Item {
        id: clipboardPopup
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 30
        visible: false
        z: 99999

        Rectangle {
            color: "#404040"
            radius: 10
            opacity: 0.9
            anchors.centerIn: parent
            width: textItem.implicitWidth + 20
            height: textItem.implicitHeight + 10

            Text {
                id: textItem
                anchors.centerIn: parent
                color: "white"
                font.bold: true
                font.pixelSize: 16
                text: ""
            }
        }
    }


    Item {
        id: loadingOverlay
        anchors.fill: parent
        visible: running
        property bool running: false
        property string message: "Laden..."
        z: 9999

        Rectangle {
            anchors.fill: parent
            color: "#66000000"  // halbtransparenter schwarzer Hintergrund
        }

        MouseArea {
            anchors.fill: parent
            enabled: true
            preventStealing: true
            hoverEnabled: true
            cursorShape: Qt.WaitCursor
            onClicked: {
                // Benutzer darf nichts anklicken
                console.log("â›” Eingabe blockiert wÃ¤hrend des Ladens")
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 12

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: true
                width: 80
                height: 80
            }

            Label {
                text: loadingOverlay.message
                color: "white"
                font.bold: true
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    // Entfernen Sie den anderen BusyIndicator (loadingIndicator) komplett
    // Fehlermeldung
    Label {
        id: errorMessage
        visible: false
        color: "red"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
    }

    Connections {
        target: dbManager
        function onGetSharesComplete(data) {
            console.log("ðŸ“¥ SIGNAL getSharesComplete:", data.length)
            loadingOverlay.running = false
            updateStockModel(data);
            if (data.length === 0) {
                textItem.text = "Keine Aktien fuer die aktuellen Filter gefunden";
                clipboardPopup.visible = true;
                clipboardTimer.restart();
            }
        }
        function onIbkrStockDataUpdated(symbol) {
            loadTestPortfolio(symbol)
        }
        function onFundamentalDataUpdated(symbol) {
            loadTestPortfolio(symbol)
        }
    }

    function sortStockModelByField(field, ascending) {
        const count = stockModel.count;

        // Hole alle EintrÃ¤ge als echte JS-Objekte
        let data = [];
        for (let i = 0; i < count; i++) {
            const raw = stockModel.get(i);
            data.push(normalizeStockData(raw));  // Jetzt haben wir echte lesbare Properties
        }

        // Sortiere nach dem gewÃ¼nschten Feld
        data.sort((a, b) => {
            let valA = a[field];
            let valB = b[field];
            return ascending ? valA - valB : valB - valA;
        });

        // Liste leeren und neu befÃ¼llen
        stockModel.clear();
        data.forEach(item => stockModel.append(item));
    }

    function fieldValue(raw, names, fallback) {
        for (let i = 0; i < names.length; i++) {
            let value = raw[names[i]]
            if (value !== undefined && value !== null)
                return value
        }
        return fallback
    }

    function normalizeStockData(raw) {
        return {
            symbol: fieldValue(raw, ["symbol", "Symbol"], ""),
            name: fieldValue(raw, ["name", "Name"], ""),
            mic: fieldValue(raw, ["mic", "MIC", "exchange"], ""),
            lastUpdateDate: fieldValue(raw, ["lastUpdateDate", "LastUpdateDate"], ""),
            lastClosePrice: fieldValue(raw, ["lastClosePrice", "lastcloseprice"], 0),
            lastClosePriceDate: fieldValue(raw, ["lastClosePriceDate", "lastclosepricedate"], ""),

            daysFirstPeriodSuccess: fieldValue(raw, ["daysFirstPeriodSuccess", "daysfirstperiodsuccess"], 0),
            daysSecondPeriodSuccess: fieldValue(raw, ["daysSecondPeriodSuccess", "dayssecondperiodsuccess"], 0),
            daysThirdPeriodSuccess: fieldValue(raw, ["daysThirdPeriodSuccess", "daysthirdperiodsuccess"], 0),
            daysFourthPeriodSuccess: fieldValue(raw, ["daysFourthPeriodSuccess", "daysfourthperiodsuccess"], 0),

            firstPeriodValueInc: fieldValue(raw, ["firstPeriodValueInc", "firstperiodvalueinc"], 0),
            secondPeriodValueInc: fieldValue(raw, ["secondPeriodValueInc", "secondperiodvalueinc"], 0),
            thirdPeriodValueInc: fieldValue(raw, ["thirdPeriodValueInc", "thirdperiodvalueinc"], 0),
            fourthPeriodValueInc: fieldValue(raw, ["fourthPeriodValueInc", "fourthperiodvalueinc"], 0),

            firstPeriodVolume: fieldValue(raw, ["firstPeriodVolume", "firstperiodvolume"], 0),
            secondPeriodVolume: fieldValue(raw, ["secondPeriodVolume", "secondperiodvolume"], 0),
            thirdPeriodVolume: fieldValue(raw, ["thirdPeriodVolume", "thirdperiodvolume"], 0),
            fourthPeriodVolume: fieldValue(raw, ["fourthPeriodVolume", "fourthperiodvolume"], 0),

            firstPeriodVolumePrice: fieldValue(raw, ["firstPeriodVolumePrice", "firstperiodvolumeprice"], 0),
            secondPeriodVolumePrice: fieldValue(raw, ["secondPeriodVolumePrice", "secondperiodvolumeprice"], 0),
            thirdPeriodVolumePrice: fieldValue(raw, ["thirdPeriodVolumePrice", "thirdperiodvolumeprice"], 0),
            fourthPeriodVolumePrice: fieldValue(raw, ["fourthPeriodVolumePrice", "fourthperiodvolumeprice"], 0),

            selected: fieldValue(raw, ["selected"], false)
        };
    }


    function updateStockAtIndex(modelIndex, stock) {
        if (modelIndex < 0 || modelIndex >= stockModel.count) return;

        stockModel.set(modelIndex, {
            "name": stock.Name,
            "lastUpdateDate": stock.LastUpdateDate,
            "lastClosePrice": stock.lastcloseprice,
            "lastClosePriceDate": stock.lastclosepricedate,
            "mic": stock.MIC,
            "daysFirstPeriodSuccess": stock.daysfirstperiodsuccess,
            "daysSecondPeriodSuccess": stock.dayssecondperiodsuccess,
            "daysThirdPeriodSuccess": stock.daysthirdperiodsuccess,
            "daysFourthPeriodSuccess": stock.daysfourthperiodsuccess,
            "firstPeriodValueInc": stock.firstperiodvalueinc,
            "secondPeriodValueInc": stock.secondperiodvalueinc,
            "thirdPeriodValueInc": stock.thirdperiodvalueinc,
            "fourthPeriodValueInc": stock.fourthperiodvalueinc,
            "firstPeriodVolume": stock.firstperiodvolume,
            "secondPeriodVolume": stock.secondperiodvolume,
            "thirdPeriodVolume": stock.thirdperiodvolume,
            "fourthPeriodVolume": stock.fourthperiodvolume,
            "firstPeriodVolumePrice": stock.firstperiodvolumeprice,
            "secondPeriodVolumePrice": stock.secondperiodvolumeprice,
            "thirdPeriodVolumePrice": stock.thirdperiodvolumeprice,
            "fourthPeriodVolumePrice": stock.fourthperiodvolumeprice,
            "selected": true
        });
        listView.positionViewAtIndex(modelIndex, ListView.Contain);
    }

    function updateStockModel(data) {
        console.log("Data received, first item:", JSON.stringify(data.length > 0 ? data[0] : {}))
        stockModel.clear()
        data.forEach(stock => {
            stockModel.append(normalizeStockData(stock))
        })
        resetSelection();
        listView.contentY += 1
        listView.contentY -= 1
        listView.currentIndex = stockModel.count > 0 ? 0 : -1;
    }

    function loadAllStocks() {
        loadingOverlay.running = true

        const data = dbManager.getAllStocks()
        stockModel.clear()
        data.forEach(stock => stockModel.append(stock))
        loadingOverlay.running = false
        resetSelection()
    }

    function searchByTickerAndExchange() {
        loadingOverlay.running = true
        loadingOverlay.message = "Suche Aktie..."

        let firstItem = firstPeriodLoader.item
        let secondItem = secondPeriodLoader.item
        let thirdItem = thirdPeriodLoader.item
        let fourthItem = fourthPeriodLoader.item
        let filterItem = filterSelectionLoader.item
        let tickerText = tickerInput.text.trim()
        let nameText = nameInput.text.trim()
        let searchSymbol = tickerText.replace(/\\/g, "\\\\").replace(/"/g, "\\\"")
        let searchName = nameText.replace(/\\/g, "\\\\").replace(/"/g, "\\\"")
        let searchByName = tickerText === "" && nameText !== ""

        Qt.callLater(() => {
            Qt.createQmlObject(`
                import QtQuick 2.0
                Timer {
                    interval: 50
                    running: true
                    repeat: false
                    onTriggered: {
                        dbManager.${searchByName ? "getSharesByNameAsync" : "getSharesAsync"}(
                            ${firstItem.toDay}, ${activeThreshold(firstItem)}, ${firstItem.greaterThan},
                            ${secondItem.toDay}, ${activeThreshold(secondItem)}, ${secondItem.greaterThan},
                            ${thirdItem.toDay}, ${activeThreshold(thirdItem)}, ${thirdItem.greaterThan},
                            ${fourthItem.toDay}, ${activeThreshold(fourthItem)}, ${fourthItem.greaterThan},
                            ${filterItem.salesPriceGreaterThan},
                            ${getSortPeriodIndex()},
                            ${filterItem.sortAscCheckBox.checked},
                            "${searchByName ? searchName : searchSymbol}"
                        )
                        timer.start()
                    }
                }
            `, mainWindow)
        })
    }

    function parseDecimal(text) {
        let value = Number(String(text).replace(",", "."))
        return isNaN(value) ? 0 : value
    }

    function updateBoughtStockModel(data) {
        boughtStockModel.clear()
        data.forEach(stock => {
            boughtStockModel.append({
                symbol: fieldValue(stock, ["Symbol", "symbol"], ""),
                name: fieldValue(stock, ["Name", "name"], ""),
                buyDate: fieldValue(stock, ["BuyDate", "buyDate"], ""),
                sellDate: fieldValue(stock, ["SellDate", "sellDate"], ""),
                currentValue: fieldValue(stock, ["CurrentValue", "currentValue"], 0),
                entryValue: fieldValue(stock, ["EntryValue", "entryValue"], 0),
                valueIncreasePercent: fieldValue(stock, ["ValueIncreasePercent", "valueIncreasePercent"], 0),
                quantity: fieldValue(stock, ["Quantity", "quantity"], 1),
                analysisConfigName: fieldValue(stock, ["AnalysisConfigName", "analysisConfigName"], ""),
                status: fieldValue(stock, ["Status", "status"], 0)
            })
        })
        selectedBoughtStockIndex = boughtStockModel.count > 0 ? 0 : -1
    }

    function loadBoughtStocks() {
        updateBoughtStockModel(dbManager.getBoughtStocks())
    }

    function loadTestPortfolio(preferredSymbol) {
        const data = dbManager.getTestPortfolio()
        portfolioModel.clear()
        let preferredIndex = -1
        data.forEach((item, index) => {
            portfolioModel.append(item)
            if (preferredSymbol && item.symbol === preferredSymbol)
                preferredIndex = index
        })
        selectedPortfolioIndex = preferredIndex >= 0
            ? preferredIndex
            : (portfolioModel.count > 0 ? 0 : -1)
    }

    function cleanDisplayText(value) {
        let text = String(value)
        return text
            .replace(/\u00c3\u00a4/g, "\u00e4")
            .replace(/\u00c3\u00b6/g, "\u00f6")
            .replace(/\u00c3\u00bc/g, "\u00fc")
            .replace(/\u00c3\u0084/g, "\u00c4")
            .replace(/\u00c3\u0096/g, "\u00d6")
            .replace(/\u00c3\u009c/g, "\u00dc")
            .replace(/\u00c3\u009f/g, "\u00df")
            .replace(/\u00c2\u00b7/g, "\u00b7")
            .replace(/\u00e2\u0080\u0093/g, "-")
            .replace(/\u00e2\u0080\u0094/g, "-")
            .replace(/\u00e2\u0080\u0099/g, "'")
            .replace(/\u00e2\u0080\u009e/g, "\u201e")
            .replace(/\u00e2\u0080\u009c/g, "\u201c")
            .replace(/\u00e2\u0080\u009d/g, "\u201d")
    }

    function selectedPortfolioValue(key) {
        if (selectedPortfolioIndex < 0 || selectedPortfolioIndex >= portfolioModel.count)
            return ""
        const row = portfolioModel.get(selectedPortfolioIndex)
        return row[key] === undefined || row[key] === null ? "" : row[key]
    }

    function portfolioTotalCurrentValue() {
        let total = 0
        for (let i = 0; i < portfolioModel.count; i++) {
            let row = portfolioModel.get(i)
            total += portfolioPositionQuantity(row) * Number(row.currentValue || 0)
        }
        return total
    }

    function portfolioTotalEntryValue() {
        let total = 0
        for (let i = 0; i < portfolioModel.count; i++) {
            let row = portfolioModel.get(i)
            total += portfolioPositionQuantity(row) * Number(row.entryValue || 0)
        }
        return total
    }

    function portfolioTotalGainValue() {
        return portfolioTotalCurrentValue() - portfolioTotalEntryValue()
    }

    function portfolioStatusCount(sold) {
        let count = 0
        for (let i = 0; i < portfolioModel.count; i++) {
            let isSold = Number(portfolioModel.get(i).status || 0) === 10
            if (isSold === sold)
                count++
        }
        return count
    }

    function portfolioPerformancePercent() {
        let entryValue = portfolioTotalEntryValue()
        let currentValue = portfolioTotalCurrentValue()
        return entryValue > 0 ? (currentValue - entryValue) / entryValue * 100 : 0
    }

    function portfolioPositionQuantity(row) {
        let candidates = ["quantity", "amount", "shares", "count", "anzahl"]
        for (let i = 0; i < candidates.length; i++) {
            let value = Number(row[candidates[i]] || 0)
            if (value > 0)
                return value
        }
        return 1
    }

    function portfolioPositionTotalValue(row) {
        return portfolioPositionQuantity(row) * Number(row.currentValue || 0)
    }

    function formatPercentValue(value) {
        let numberValue = Number(value)
        return isNaN(numberValue) ? "-" : numberValue.toLocaleString(Qt.locale(), "f", 2) + " %"
    }

    function formatPortfolioValue(key, format) {
        const value = selectedPortfolioValue(key)
        let displayValue = ""
        if (value === "")
            displayValue = "-"
        else {
            if (format === "status")
                displayValue = Number(value) === 10 ? "Verkauft" : "Aktiv"
            else if (format === "money")
                displayValue = Number(value).toLocaleString(Qt.locale(), "f", 2)
            else if (format === "large")
                displayValue = Number(value).toLocaleString(Qt.locale(), "f", 0)
            else if (format === "percent")
                displayValue = Number(value).toLocaleString(Qt.locale(), "f", 2) + " %"
            else if (format === "decimal2")
                displayValue = Number(value).toLocaleString(Qt.locale(), "f", 2)
            else if (format === "decimal8")
                displayValue = Number(value).toLocaleString(Qt.locale(), "f", 8)
            else if (format === "quantity") {
                let quantityText = Number(value).toLocaleString(Qt.locale(), "f", 6)
                while ((quantityText.indexOf(",") >= 0 || quantityText.indexOf(".") >= 0) && quantityText.endsWith("0"))
                    quantityText = quantityText.slice(0, -1)
                if (quantityText.endsWith(",") || quantityText.endsWith("."))
                    quantityText = quantityText.slice(0, -1)
                displayValue = quantityText
            }
            else
                displayValue = cleanDisplayText(value)
        }
        return cleanDisplayText(displayValue)
    }

    function portfolioFieldLabel(key, label) {
        const origin = selectedPortfolioValue(key + "Origin")
        return origin === "" ? cleanDisplayText(label) : cleanDisplayText(label + " (" + origin + ")")
    }

    function portfolioHeadingLabel(label) {
        if (label !== "Kennzahlen")
            return cleanDisplayText(label)

        const exchange = selectedPortfolioValue("fundamentalExchange")
        return exchange === "" ? cleanDisplayText(label) : cleanDisplayText(label + " (" + exchange + ")")
    }

    function currentIsoDate() {
        let now = new Date()
        return now.getFullYear() + "-" +
               String(now.getMonth() + 1).padStart(2, "0") + "-" +
               String(now.getDate()).padStart(2, "0")
    }

    function sellSelectedBoughtStock() {
        if (selectedBoughtStockIndex < 0 || selectedBoughtStockIndex >= boughtStockModel.count) {
            boughtStockMessage = "Keine Aktie ausgewÃ¤hlt"
            return
        }

        let stock = boughtStockModel.get(selectedBoughtStockIndex)
        let ok = dbManager.saveBoughtStock(
            stock.symbol,
            stock.name,
            stock.buyDate,
            currentIsoDate(),
            Number(stock.currentValue || 0),
            Number(stock.entryValue || 0),
            Number(stock.valueIncreasePercent || 0),
            10,
            Number(stock.quantity || 1),
            stock.analysisConfigName || ""
        )

        if (ok) {
            boughtStockMessage = "Verkauft"
            loadBoughtStocks()
        } else {
            boughtStockMessage = "Verkaufen fehlgeschlagen"
        }
    }

    function requestDeleteBoughtStock(index) {
        if (index < 0 || index >= boughtStockModel.count) {
            boughtStockMessage = "Keine Aktie ausgewÃ¤hlt"
            return
        }

        selectedBoughtStockIndex = index
        let stock = boughtStockModel.get(index)
        pendingDeleteBoughtSymbol = stock.symbol
        deleteBoughtStockDialog.open()
    }

    function deletePendingBoughtStock() {
        if (!pendingDeleteBoughtSymbol)
            return

        let ok = dbManager.deleteBoughtStock(pendingDeleteBoughtSymbol)
        if (ok) {
            boughtStockMessage = "Aktie entfernt"
            pendingDeleteBoughtSymbol = ""
            loadBoughtStocks()
        } else {
            boughtStockMessage = "Entfernen fehlgeschlagen"
        }
    }

    function resetSelection() {
        selectedIndices = [];
        selectedSymbols = [];
        selectedExchanges = [];
        selectionAnchor = -1;
        loadStockQuotesButton.enabled = false;
        updateSelectedItems(); // Model aktualisieren
    }

    // Properties für die Mehrfachselektion
    property var selectedIndices: []
    property var selectedSymbols: []
    property var selectedExchanges: []
    property int selectionAnchor: -1

    function updateSelection(index) {
        if (selectedIndices.includes(index)) {
            // Item bereits selektiert - entfernen
            selectedIndices = selectedIndices.filter(i => i !== index)
            selectedSymbols = selectedSymbols.filter((_, i) => selectedIndices.includes(i))
            selectedExchanges = selectedExchanges.filter((_, i) => selectedIndices.includes(i))
        } else {
            // Item hinzufÃ¼gen
            selectedIndices = [...selectedIndices, index]
            selectedSymbols = [...selectedSymbols, stockModel.get(index).symbol]
            selectedExchanges = [...selectedExchanges, stockModel.get(index).mic]
        }

        loadStockQuotesButton.enabled = selectedIndices.length > 0
        console.log("âœ… Selektion geÃ¤ndert: ", selectedSymbols, selectedExchanges)
    }

    function selectRange(from, to, isCtrlPressed) {
        const start = Math.min(from, to);
        const end = Math.max(from, to);

        let newSelection = [];
        for (let i = start; i <= end; i++) {
            newSelection.push(i);
        }

        if (isCtrlPressed) {
            if ((to < from && currentListViewIndex < selectionAnchor) ||
                (to > from && currentListViewIndex > selectionAnchor)) {
                // Richtungswechsel - deselektiere den Bereich
                selectedIndices = selectedIndices.filter(i => !newSelection.includes(i));
            } else {
                // FÃ¼ge neuen Bereich hinzu
                selectedIndices = [...new Set([...selectedIndices, ...newSelection])];
            }
        } else {
            selectedIndices = newSelection;
        }

        selectedIndices.sort((a, b) => a - b);
        updateSelectedItems();
    }


    function isSelected(index) {
        return selectedIndices.includes(index);
    }

    function updateSelectedItems() {
        // ZurÃ¼cksetzen aller Selektionen im Model
        for (var i = 0; i < stockModel.count; i++) {
            stockModel.setProperty(i, "selected", selectedIndices.includes(i));
        }

        selectedSymbols = selectedIndices.map(i => stockModel.get(i).symbol);
        selectedExchanges = selectedIndices.map(i => stockModel.get(i).mic);
        loadStockQuotesButton.enabled = selectedIndices.length > 0;
    }

    function toggleSelection(index) {
        if (selectedIndices.includes(index)) {
            selectedIndices = selectedIndices.filter(i => i !== index);
        } else {
            selectedIndices = [...selectedIndices, index];
        }
        selectionAnchor = index; // Anchor immer auf die zuletzt angewÃ¤hlte Zeile setzen
        updateSelectedItems();
    }

    function isMovingBackward(newIndex) {
        if (selectionAnchor === -1) return false;
        return (newIndex < currentIndex && currentIndex <= selectionAnchor) ||
               (newIndex > currentIndex && currentIndex >= selectionAnchor);
    }

    function updatePeriods(changedIndex, field) {
        let periods = [firstPeriodLoader, secondPeriodLoader, thirdPeriodLoader, fourthPeriodLoader]
        let values = []

        periods.forEach((loader, i) => {
            const item = loader.item
            values.push({
                from: parseInt(item.fromDay),
                to: parseInt(item.toDay),
                loader: loader
            })
        })

        let changed = values[changedIndex]
        if (isNaN(changed.from) || isNaN(changed.to)) return

        if (field === "from") {
            const item = changed.loader.item
            const oldFrom = parseInt(item.fromDayInput.propertyPreviousValue || changed.from)
            const delta = changed.from - oldFrom
            item.fromDayInput.propertyPreviousValue = changed.from.toString()
            const span = changed.to - oldFrom
            changed.to = changed.from + span
            changed.loader.item.toDay = String(changed.to)

            if (changedIndex > 0) {
                values[changedIndex - 1].to = changed.from - 1
                values[changedIndex - 1].loader.item.toDay = String(values[changedIndex - 1].to)
            }

            for (let k = changedIndex + 1; k < values.length; k++) {
                values[k].from += delta
                values[k].to += delta
                values[k].loader.item.fromDay = String(values[k].from)
                values[k].loader.item.toDay = String(values[k].to)
            }
        }

        if (field === "to") {
            if (changedIndex < values.length - 1) {
                const next = values[changedIndex + 1]
                const span = next.to - next.from
                const newFrom = changed.to + 1
                const newTo = newFrom + span

                next.from = newFrom
                next.to = newTo
                next.loader.item.fromDay = String(newFrom)
                next.loader.item.toDay = String(newTo)
            }


            for (let i = changedIndex + 1; i < values.length - 1; i++) {
                const current = values[i]
                const next = values[i + 1]
                const span = next.to - next.from
                const newFrom = current.to + 1
                const newTo = newFrom + span

                next.from = newFrom
                next.to = newTo
                next.loader.item.fromDay = String(newFrom)
                next.loader.item.toDay = String(newTo)
            }
        }
    }

    function processSelectedStocks(index) {
        if (index >= selectedSymbols.length) {
            loadingOverlay.running = false;
            timer.stop();
            console.log("âœ… Alle historischen Daten wurden geladen");
            return;
        }

        const symbol = selectedSymbols[index];
        const exchange = selectedExchanges[index];
        console.log("ðŸ“Š Lade Daten für:", symbol, exchange, `(${index+1}/${selectedSymbols.length})`);

        function onSaveComplete(receivedSymbol) {
            if (receivedSymbol !== symbol)
                return; // Warten bis der *richtige* Datensatz gespeichert ist

            dbManager.saveComplete.disconnect(onSaveComplete);
            console.log("âœ… Speicherung abgeschlossen:", symbol);


            let result = dbManager.getShares(
                parseInt(firstPeriodLoader.item.toDay),
                activeThreshold(firstPeriodLoader.item),
                firstPeriodLoader.item.greaterThan,

                parseInt(secondPeriodLoader.item.toDay),
                activeThreshold(secondPeriodLoader.item),
                secondPeriodLoader.item.greaterThan,

                parseInt(thirdPeriodLoader.item.toDay),
                activeThreshold(thirdPeriodLoader.item),
                thirdPeriodLoader.item.greaterThan,

                parseInt(fourthPeriodLoader.item.toDay),
                activeThreshold(fourthPeriodLoader.item),
                fourthPeriodLoader.item.greaterThan,

                parseInt(filterSelectionLoader.item.salesPriceGreaterThan),
                getSortPeriodIndex(),      // ðŸ‘ˆ Neue Hilfsfunktion, siehe unten
                filterSelectionLoader.item.sortAscCheckBox.checked,
                symbol
            );

            updateStockAtIndex(selectedIndices[index],result[0]);

            // Starte nÃ¤chsten Schritt nach minimalem Delay
            Qt.createQmlObject(`
                import QtQuick 2.0
                Timer {
                    interval: 200
                    running: true
                    repeat: false
                    onTriggered: {
                        processSelectedStocks(${index + 1});
                    }
                }
            `, mainWindow);
        }

        dbManager.saveComplete.connect(onSaveComplete);
        dbManager.createQuotesForStock(symbol, exchange);
    }

    function getSortPeriodIndex() {
        if (filterSelectionLoader.item.radioSortPeriod1.checked) return 1;
        if (filterSelectionLoader.item.radioSortPeriod2.checked) return 2;
        if (filterSelectionLoader.item.radioSortPeriod3.checked) return 3;
        if (filterSelectionLoader.item.radioSortPeriod4.checked) return 4;
        return 1; // Default-Fallback
    }

    function activeThreshold(periodItem) {
        if (!periodItem || !periodItem.active)
            return 0
        let threshold = parseInt(periodItem.successThreshold)
        return isNaN(threshold) ? 0 : threshold
    }

    function getConfiguredPeriods() {
        let loaders = [firstPeriodLoader, secondPeriodLoader, thirdPeriodLoader, fourthPeriodLoader]
        let fields = [
            { success: "daysFirstPeriodSuccess", valueInc: "firstPeriodValueInc", volume: "firstPeriodVolume", volumePrice: "firstPeriodVolumePrice" },
            { success: "daysSecondPeriodSuccess", valueInc: "secondPeriodValueInc", volume: "secondPeriodVolume", volumePrice: "secondPeriodVolumePrice" },
            { success: "daysThirdPeriodSuccess", valueInc: "thirdPeriodValueInc", volume: "thirdPeriodVolume", volumePrice: "thirdPeriodVolumePrice" },
            { success: "daysFourthPeriodSuccess", valueInc: "fourthPeriodValueInc", volume: "fourthPeriodVolume", volumePrice: "fourthPeriodVolumePrice" }
        ]
        let periods = []

        for (let i = 0; i < loaders.length; i++) {
            let item = loaders[i].item
            if (!item || !item.active)
                continue

            let fromDay = parseInt(item.fromDay)
            let toDay = parseInt(item.toDay)
            if (isNaN(fromDay) || isNaN(toDay))
                continue

            periods.push({
                index: i + 1,
                label: "Periode " + (i + 1),
                fromDay: fromDay,
                toDay: toDay,
                threshold: parseInt(item.successThreshold),
                greaterThan: item.greaterThan,
                successField: fields[i].success,
                valueIncField: fields[i].valueInc,
                volumeField: fields[i].volume,
                volumePriceField: fields[i].volumePrice
            })
        }

        return periods
    }

    function openDetailWindow(modelIndex) {
        if (modelIndex < 0 || modelIndex >= stockModel.count)
            return

        let periods = getConfiguredPeriods()
        if (periods.length === 0) {
            textItem.text = "Keine aktive Periode ausgewÃ¤hlt"
            clipboardPopup.visible = true
            clipboardTimer.restart()
            return
        }

        let stock = normalizeStockData(stockModel.get(modelIndex))
        detailStock = stock
        detailStockBought = dbManager.isBoughtStock(stock.symbol)
        detailPeriods = periods
        detailFromDay = periods[0].fromDay
        detailToDay = periods[periods.length - 1].toDay
        detailQuotes = dbManager.getQuoteDetails(stock.symbol, detailFromDay, detailToDay)
        detailQuoteModel.clear()
        detailQuotes.forEach(row => detailQuoteModel.append(row))
        detailWindow.title = stock.symbol + " - Details"
        detailWindow.show()
        detailChart.requestPaint()
    }

    function buyDetailStock() {
        if (!detailStock.symbol || detailStockBought)
            return

        let lastPrice = Number(detailStock.lastClosePrice || 0)
        let ok = dbManager.saveBoughtStock(
            detailStock.symbol,
            detailStock.name,
            currentIsoDate(),
            "",
            lastPrice,
            lastPrice,
            0,
            0,
            1,
            ""
        )

        if (ok) {
            detailStockBought = true
            boughtStockMessage = "Gekauft"
            loadBoughtStocks()
            boughtStocksDialog.show()
        } else {
            textItem.text = "Kaufen fehlgeschlagen"
            clipboardPopup.visible = true
            clipboardTimer.restart()
        }
    }

    function stockAnalysisDb() {
        return LocalStorage.openDatabaseSync("ShareSelectorStockAnalysis", "1.0", "Stock Analyse", 100000)
    }

    function migrateLocalStockAnalysisConfigsToPostgres() {
        let markerKey = "postgresConfigMigrationDone"
        let alreadyMigrated = false
        try {
            let localDb = stockAnalysisDb()
            localDb.transaction(function(tx) {
                tx.executeSql("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)")
                let migrated = tx.executeSql("SELECT value FROM meta WHERE key = ?", [markerKey])
                alreadyMigrated = migrated.rows.length > 0
                if (alreadyMigrated)
                    return

                tx.executeSql("CREATE TABLE IF NOT EXISTS configs (name TEXT PRIMARY KEY, increase_percent REAL NOT NULL)")
                let columns = tx.executeSql("PRAGMA table_info(configs)")
                let columnNames = []
                for (let i = 0; i < columns.rows.length; i++)
                    columnNames.push(columns.rows.item(i).name)
                if (columnNames.indexOf("corridor_percent") < 0)
                    tx.executeSql("ALTER TABLE configs ADD COLUMN corridor_percent REAL NOT NULL DEFAULT 10")
                if (columnNames.indexOf("corridor_required_percent") < 0)
                    tx.executeSql("ALTER TABLE configs ADD COLUMN corridor_required_percent REAL NOT NULL DEFAULT 0")
                if (columnNames.indexOf("max_drawdown_percent") < 0)
                    tx.executeSql("ALTER TABLE configs ADD COLUMN max_drawdown_percent REAL NOT NULL DEFAULT 10")
                if (columnNames.indexOf("quote_count") < 0)
                    tx.executeSql("ALTER TABLE configs ADD COLUMN quote_count INTEGER NOT NULL DEFAULT 90")

                let rs = tx.executeSql("SELECT name, increase_percent, corridor_percent, corridor_required_percent, max_drawdown_percent, quote_count FROM configs")
                for (let rowIndex = 0; rowIndex < rs.rows.length; rowIndex++) {
                    let row = rs.rows.item(rowIndex)
                    dbManager.saveStockAnalysisConfig(
                        row.name,
                        Number(row.increase_percent || 0),
                        Number(row.corridor_percent || 10),
                        Number(row.corridor_required_percent || 0),
                        Number(row.max_drawdown_percent || 10),
                        Number(row.quote_count || 90)
                    )
                }

                let last = tx.executeSql("SELECT value FROM meta WHERE key = ?", ["lastConfigName"])
                if (last.rows.length > 0)
                    dbManager.saveLastStockAnalysisConfigName(last.rows.item(0).value)
                tx.executeSql("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)", [markerKey, "1"])
            })
        } catch (err) {
            console.log("Migration lokaler Stockanalyse-Konfigurationen fehlgeschlagen:", err)
        }
    }

    function loadStockAnalysisConfigs() {
        migrateLocalStockAnalysisConfigsToPostgres()
        let selectedName = ""
        if (selectedStockAnalysisConfigIndex >= 0
                && selectedStockAnalysisConfigIndex < stockAnalysisConfigModel.count)
            selectedName = stockAnalysisConfigModel.get(selectedStockAnalysisConfigIndex).name

        stockAnalysisConfigModel.clear()
        selectedStockAnalysisConfigIndex = -1
        let rows = dbManager.getStockAnalysisConfigs()
        rows.forEach(row => {
            stockAnalysisConfigModel.append({
                name: row.name || "",
                increasePercent: Number(row.increasePercent || 0),
                corridorPercent: Number(row.corridorPercent || 10),
                corridorRequiredPercent: Number(row.corridorRequiredPercent || 0),
                maxDrawdownPercent: Number(row.maxDrawdownPercent || 10),
                quoteCount: Number(row.quoteCount || 90)
            })
            if (row.name === selectedName)
                selectedStockAnalysisConfigIndex = stockAnalysisConfigModel.count - 1
        })
    }

    function saveLastStockAnalysisConfigName(configName) {
        if (configName.length > 0)
            dbManager.saveLastStockAnalysisConfigName(configName)
    }

    function loadLastStockAnalysisConfig() {
        let lastName = dbManager.lastStockAnalysisConfigName()
        if (lastName.length === 0)
            return

        for (let i = 0; i < stockAnalysisConfigModel.count; i++) {
            if (stockAnalysisConfigModel.get(i).name === lastName) {
                selectStockAnalysisConfig(i, false)
                return
            }
        }
    }

    function saveStockAnalysisConfig() {
        let configName = stockAnalysisConfigNameInput.text.trim()
        let increasePercent = Number(stockAnalysisIncreaseInput.text.replace(",", "."))
        let corridorPercent = stockAnalysisCorridorPercent
        let corridorRequiredPercent = stockAnalysisCorridorRequiredPercent
        let maxDrawdownPercent = stockAnalysisMaxDrawdownPercent
        let quoteCount = stockAnalysisQuoteCount
        if (configName.length === 0 || isNaN(increasePercent) || isNaN(corridorPercent) || isNaN(corridorRequiredPercent) || isNaN(maxDrawdownPercent) || isNaN(quoteCount)) {
            stockAnalysisMessage = "Bitte Name, Steigerung und Korridorwerte eintragen"
            return
        }

        let ok = dbManager.saveStockAnalysisConfig(
            configName,
            increasePercent,
            corridorPercent,
            corridorRequiredPercent,
            maxDrawdownPercent,
            quoteCount
        )
        if (!ok) {
            stockAnalysisMessage = "Konfiguration speichern fehlgeschlagen"
            return
        }

        stockAnalysisMessage = "Konfiguration gespeichert"
        loadStockAnalysisConfigs()
        saveLastStockAnalysisConfigName(configName)
        for (let i = 0; i < stockAnalysisConfigModel.count; i++) {
            if (stockAnalysisConfigModel.get(i).name === configName) {
                selectStockAnalysisConfig(i, false)
                break
            }
        }
    }

    function selectStockAnalysisConfig(rowIndex, showMessage) {
        if (rowIndex < 0 || rowIndex >= stockAnalysisConfigModel.count)
            return

        if (showMessage === undefined)
            showMessage = true

        selectedStockAnalysisConfigIndex = rowIndex
        let cfg = stockAnalysisConfigModel.get(rowIndex)
        stockAnalysisConfigNameInput.text = cfg.name
        stockAnalysisIncreaseInput.text = String(cfg.increasePercent)
        stockAnalysisCorridorPercent = cfg.corridorPercent === undefined ? 10 : cfg.corridorPercent
        stockAnalysisCorridorRequiredPercent = cfg.corridorRequiredPercent === undefined ? 0 : cfg.corridorRequiredPercent
        stockAnalysisMaxDrawdownPercent = cfg.maxDrawdownPercent === undefined ? 10 : cfg.maxDrawdownPercent
        stockAnalysisQuoteCount = cfg.quoteCount === undefined ? 90 : cfg.quoteCount
        saveLastStockAnalysisConfigName(cfg.name)
        if (showMessage)
            stockAnalysisMessage = "Konfiguration geladen"
    }

    function newStockAnalysisConfig() {
        selectedStockAnalysisConfigIndex = -1
        stockAnalysisConfigNameInput.text = ""
        stockAnalysisMessage = "Neue Konfiguration"
    }

    function runStockAnalysis() {
        let increasePercent = Number(stockAnalysisIncreaseInput.text.replace(",", "."))
        if (isNaN(increasePercent)) {
            stockAnalysisMessage = "Steigerung um % ist ungueltig"
            return
        }

        stockAnalysisResultModel.clear()
        stockAnalysisQuoteModel.clear()
        stockAnalysisQuoteDateRangeText = ""
        selectedStockAnalysisIndex = -1
        selectedStockAnalysisRows = []
        stockAnalysisSelectionAnchor = -1
        stockAnalysisStockSelected = false
        stockAnalysisCorridorHitPercent = 0
        stockAnalysisActualIncreasePercent = 0
        stockAnalysisRequiredCorridorPercent = 0
        stockAnalysisActualMaxDrawdownPercent = 0
        let results = dbManager.getStockAnalysisResults(increasePercent, stockAnalysisQuoteCount)
        let found = 0
        results.forEach(row => {
            let quotes = dbManager.getQuoteDetails(row.symbol, 1, stockAnalysisQuoteCount)
            let trendIncreasePercent = trendIncreasePercentForQuotes(quotes)
            let hitPercent = corridorHitPercentForQuotes(quotes)
            let maxDrawdown = maxDrawdownForQuotes(quotes).percent
            row.increasepercent = trendIncreasePercent
            row.corridorhitpercent = hitPercent
            row.maxdrawdownpercent = maxDrawdown
            if (trendIncreasePercent >= increasePercent
                    && hitPercent >= stockAnalysisCorridorRequiredPercent
                    && maxDrawdown <= stockAnalysisMaxDrawdownPercent) {
                stockAnalysisResultModel.append(row)
                found++
            }
        })
        stockAnalysisMessage = found + " Aktien gefunden"
    }

    function updateStockAnalysisCorridorStats() {
        let count = stockAnalysisQuoteModel.count
        if (count === 0) {
            stockAnalysisCorridorHitPercent = 0
            stockAnalysisActualIncreasePercent = 0
            stockAnalysisRequiredCorridorPercent = 0
        stockAnalysisActualMaxDrawdownPercent = 0
            return
        }

        function quoteAt(chartIndex) {
            return stockAnalysisQuoteModel.get(count - 1 - chartIndex)
        }

        let averageWindow = Math.min(5, count)
        let oldestAverage = 0
        let newestAverage = 0
        let allAverage = 0

        for (let allAvgIndex = 0; allAvgIndex < count; allAvgIndex++)
            allAverage += Number(quoteAt(allAvgIndex).closeprice)
        allAverage /= count

        for (let avgIndex = 0; avgIndex < averageWindow; avgIndex++) {
            oldestAverage += Number(quoteAt(avgIndex).closeprice)
            newestAverage += Number(quoteAt(count - averageWindow + avgIndex).closeprice)
        }
        oldestAverage /= averageWindow
        newestAverage /= averageWindow

        let oldestCenterIndex = (averageWindow - 1) / 2
        let newestCenterIndex = count - averageWindow + (averageWindow - 1) / 2
        let trendDenominator = Math.max(1, newestCenterIndex - oldestCenterIndex)
        let trendSlope = (newestAverage - oldestAverage) / trendDenominator
        let bandOffset = allAverage * stockAnalysisCorridorPercent / 100
        let insideCount = 0

        for (let i = 0; i < count; i++) {
            let closePrice = Number(quoteAt(i).closeprice)
            let trendPrice = oldestAverage + trendSlope * (i - oldestCenterIndex)
            if (closePrice >= trendPrice - bandOffset && closePrice <= trendPrice + bandOffset)
                insideCount++
        }

        stockAnalysisCorridorHitPercent = insideCount / count * 100
        stockAnalysisActualIncreasePercent = trendIncreasePercentForQuoteAt(count, quoteAt)
        stockAnalysisRequiredCorridorPercent = requiredCorridorPercentForQuotesModel(stockAnalysisQuoteModel)
        stockAnalysisActualMaxDrawdownPercent = maxDrawdownForQuotesModel(stockAnalysisQuoteModel).percent
    }

    function corridorHitPercentForQuotes(quotes) {
        let count = quotes.length
        if (count === 0)
            return 0

        function quoteAt(chartIndex) {
            return quotes[count - 1 - chartIndex]
        }

        let averageWindow = Math.min(5, count)
        let oldestAverage = 0
        let newestAverage = 0
        let allAverage = 0

        for (let allAvgIndex = 0; allAvgIndex < count; allAvgIndex++)
            allAverage += Number(quoteAt(allAvgIndex).closeprice)
        allAverage /= count

        for (let avgIndex = 0; avgIndex < averageWindow; avgIndex++) {
            oldestAverage += Number(quoteAt(avgIndex).closeprice)
            newestAverage += Number(quoteAt(count - averageWindow + avgIndex).closeprice)
        }
        oldestAverage /= averageWindow
        newestAverage /= averageWindow

        let oldestCenterIndex = (averageWindow - 1) / 2
        let newestCenterIndex = count - averageWindow + (averageWindow - 1) / 2
        let trendDenominator = Math.max(1, newestCenterIndex - oldestCenterIndex)
        let trendSlope = (newestAverage - oldestAverage) / trendDenominator
        let bandOffset = allAverage * stockAnalysisCorridorPercent / 100
        let insideCount = 0

        for (let i = 0; i < count; i++) {
            let closePrice = Number(quoteAt(i).closeprice)
            let trendPrice = oldestAverage + trendSlope * (i - oldestCenterIndex)
            if (closePrice >= trendPrice - bandOffset && closePrice <= trendPrice + bandOffset)
                insideCount++
        }

        return insideCount / count * 100
    }

    function requiredCorridorPercentForQuotesModel(model) {
        let count = model.count
        if (count === 0)
            return 0

        function quoteAt(chartIndex) {
            return model.get(count - 1 - chartIndex)
        }

        return requiredCorridorPercentForQuoteAt(count, quoteAt)
    }

    function requiredCorridorPercentForQuotes(quotes) {
        let count = quotes.length
        if (count === 0)
            return 0

        function quoteAt(chartIndex) {
            return quotes[count - 1 - chartIndex]
        }

        return requiredCorridorPercentForQuoteAt(count, quoteAt)
    }
    function trendIncreasePercentForQuotes(quotes) {
        let count = quotes.length
        if (count === 0)
            return 0

        function quoteAt(chartIndex) {
            return quotes[count - 1 - chartIndex]
        }

        return trendIncreasePercentForQuoteAt(count, quoteAt)
    }

    function trendIncreasePercentForQuoteAt(count, quoteAt) {
        let averageWindow = Math.min(5, count)
        let oldestAverage = 0
        let newestAverage = 0

        for (let avgIndex = 0; avgIndex < averageWindow; avgIndex++) {
            oldestAverage += Number(quoteAt(avgIndex).closeprice)
            newestAverage += Number(quoteAt(count - averageWindow + avgIndex).closeprice)
        }
        oldestAverage /= averageWindow
        newestAverage /= averageWindow

        return oldestAverage > 0 ? (newestAverage - oldestAverage) / oldestAverage * 100 : 0
    }

    function maxDrawdownForQuotesModel(model) {
        let count = model.count
        if (count === 0)
            return { percent: 0, peakIndex: -1, troughIndex: -1 }

        function quoteAt(chartIndex) {
            return model.get(count - 1 - chartIndex)
        }

        return maxDrawdownForQuoteAt(count, quoteAt)
    }

    function maxDrawdownForQuotes(quotes) {
        let count = quotes.length
        if (count === 0)
            return { percent: 0, peakIndex: -1, troughIndex: -1 }

        function quoteAt(chartIndex) {
            return quotes[count - 1 - chartIndex]
        }

        return maxDrawdownForQuoteAt(count, quoteAt)
    }

    function maxDrawdownForQuoteAt(count, quoteAt) {
        if (count === 0)
            return { percent: 0, peakIndex: -1, troughIndex: -1 }

        let peakPrice = Number(quoteAt(0).closeprice)
        let peakIndex = 0
        let maxPercent = 0
        let maxPeakIndex = 0
        let maxTroughIndex = 0

        for (let i = 1; i < count; i++) {
            let price = Number(quoteAt(i).closeprice)
            if (price > peakPrice) {
                peakPrice = price
                peakIndex = i
                continue
            }

            let drawdownPercent = peakPrice > 0 ? (peakPrice - price) / peakPrice * 100 : 0
            if (drawdownPercent > maxPercent) {
                maxPercent = drawdownPercent
                maxPeakIndex = peakIndex
                maxTroughIndex = i
            }
        }

        return { percent: maxPercent, peakIndex: maxPeakIndex, troughIndex: maxTroughIndex }
    }
    function requiredCorridorPercentForQuoteAt(count, quoteAt) {
        let averageWindow = Math.min(5, count)
        let oldestAverage = 0
        let newestAverage = 0
        let allAverage = 0

        for (let allAvgIndex = 0; allAvgIndex < count; allAvgIndex++)
            allAverage += Number(quoteAt(allAvgIndex).closeprice)
        allAverage /= count

        if (allAverage <= 0)
            return 0

        for (let avgIndex = 0; avgIndex < averageWindow; avgIndex++) {
            oldestAverage += Number(quoteAt(avgIndex).closeprice)
            newestAverage += Number(quoteAt(count - averageWindow + avgIndex).closeprice)
        }
        oldestAverage /= averageWindow
        newestAverage /= averageWindow

        let oldestCenterIndex = (averageWindow - 1) / 2
        let newestCenterIndex = count - averageWindow + (averageWindow - 1) / 2
        let trendDenominator = Math.max(1, newestCenterIndex - oldestCenterIndex)
        let trendSlope = (newestAverage - oldestAverage) / trendDenominator
        let requiredBandOffset = 0

        for (let i = 0; i < count; i++) {
            let closePrice = Number(quoteAt(i).closeprice)
            let trendPrice = oldestAverage + trendSlope * (i - oldestCenterIndex)
            requiredBandOffset = Math.max(requiredBandOffset, Math.abs(closePrice - trendPrice))
        }

        return requiredBandOffset / allAverage * 100
    }

    function startStockAnalysisScan() {
        let increasePercent = Number(stockAnalysisIncreaseInput.text.replace(",", "."))
        if (isNaN(increasePercent)) {
            stockAnalysisMessage = "Steigerung um % ist ungueltig"
            return
        }

        stockAnalysisResultModel.clear()
        stockAnalysisQuoteModel.clear()
        stockAnalysisQuoteDateRangeText = ""
        selectedStockAnalysisIndex = -1
        selectedStockAnalysisRows = []
        stockAnalysisSelectionAnchor = -1
        stockAnalysisStockSelected = false
        stockAnalysisCorridorHitPercent = 0
        stockAnalysisActualIncreasePercent = 0
        stockAnalysisRequiredCorridorPercent = 0
        stockAnalysisActualMaxDrawdownPercent = 0
        stockAnalysisScanSymbols = dbManager.getStockAnalysisIbkrSymbols()
        stockAnalysisScanIndex = 0
        stockAnalysisScanFound = 0

        if (stockAnalysisScanSymbols.length === 0) {
            stockAnalysisMessage = "Keine IBKR-Stocks mit Quotes gefunden"
            stockAnalysisScanActive = false
            return
        }

        stockAnalysisScanActive = true
        stockAnalysisMessage = "Stock-Analyse gestartet"
        processStockAnalysisScanBatch()
        stockAnalysisScanTimer.restart()
    }

    function stopStockAnalysisScan() {
        stockAnalysisScanTimer.stop()
        stockAnalysisScanActive = false
        stockAnalysisMessage = "Stock-Analyse gestoppt: " + stockAnalysisScanIndex + "/" + stockAnalysisScanSymbols.length
            + " geprueft, " + stockAnalysisScanFound + " gefunden"
    }

    function processStockAnalysisScanBatch() {
        let increasePercent = Number(stockAnalysisIncreaseInput.text.replace(",", "."))
        let batchSize = 8
        let processed = 0

        while (stockAnalysisScanActive
               && stockAnalysisScanIndex < stockAnalysisScanSymbols.length
               && processed < batchSize) {
            let symbol = stockAnalysisScanSymbols[stockAnalysisScanIndex]
            stockAnalysisScanIndex++
            processed++

            try {
                let candidate = dbManager.getStockAnalysisCandidate(symbol, increasePercent, stockAnalysisQuoteCount)
                if (candidate.symbol !== undefined && candidate.symbol !== "") {
                    let quotes = dbManager.getQuoteDetails(candidate.symbol, 1, stockAnalysisQuoteCount)
                    let trendIncreasePercent = trendIncreasePercentForQuotes(quotes)
                    let hitPercent = corridorHitPercentForQuotes(quotes)
                    let maxDrawdown = maxDrawdownForQuotes(quotes).percent
                    candidate.increasepercent = trendIncreasePercent
                    candidate.corridorhitpercent = hitPercent
                    candidate.maxdrawdownpercent = maxDrawdown

                    if (trendIncreasePercent >= increasePercent
                            && hitPercent >= stockAnalysisCorridorRequiredPercent
                            && maxDrawdown <= stockAnalysisMaxDrawdownPercent) {
                        stockAnalysisResultModel.append(candidate)
                        stockAnalysisScanFound++
                    }
                }
            } catch (err) {
                stockAnalysisMessage = "Fehler bei " + symbol + ": " + err
            }
        }

        stockAnalysisMessage = "Stock-Analyse laeuft: " + stockAnalysisScanIndex + "/" + stockAnalysisScanSymbols.length
            + " geprueft, " + stockAnalysisScanFound + " gefunden"

        if (stockAnalysisScanIndex >= stockAnalysisScanSymbols.length) {
            stockAnalysisScanTimer.stop()
            stockAnalysisScanActive = false
            sortStockAnalysisResults()
            stockAnalysisMessage = "Stock-Analyse abgeschlossen: " + stockAnalysisScanIndex + "/" + stockAnalysisScanSymbols.length
                + " geprueft, " + stockAnalysisScanFound + " gefunden"
        }
    }

    function sortStockAnalysisResults() {
        let rows = []
        for (let i = 0; i < stockAnalysisResultModel.count; i++)
            rows.push(cloneStockAnalysisResult(stockAnalysisResultModel.get(i)))

        rows.sort((a, b) => {
            let turnoverA = Number(a.periodturnover || 0)
            let turnoverB = Number(b.periodturnover || 0)
            if (turnoverA !== turnoverB)
                return turnoverB - turnoverA

            let increaseA = Number(a.increasepercent || 0)
            let increaseB = Number(b.increasepercent || 0)
            if (increaseA !== increaseB)
                return increaseB - increaseA

            let peA = Number(a.peratio)
            let peB = Number(b.peratio)
            let peAValid = !isNaN(peA) && peA > 0
            let peBValid = !isNaN(peB) && peB > 0
            if (peAValid && peBValid)
                return peA - peB
            if (peAValid)
                return -1
            if (peBValid)
                return 1
            return String(a.name || "").localeCompare(String(b.name || ""))
        })

        stockAnalysisResultModel.clear()
        rows.forEach(row => stockAnalysisResultModel.append(row))
        selectedStockAnalysisIndex = -1
        selectedStockAnalysisRows = []
        stockAnalysisSelectionAnchor = -1
        stockAnalysisStockSelected = false
        stockAnalysisCorridorHitPercent = 0
        stockAnalysisActualIncreasePercent = 0
        stockAnalysisRequiredCorridorPercent = 0
        stockAnalysisActualMaxDrawdownPercent = 0
    }

    function cloneStockAnalysisResult(row) {
        return {
            symbol: row.symbol || "",
            isin: row.isin || "",
            name: row.name || "",
            mic: row.mic || "",
            quotesource: row.quotesource || "",
            increasepercent: Number(row.increasepercent || 0),
            firstquotedate: row.firstquotedate || "",
            firstcloseprice: Number(row.firstcloseprice || 0),
            lastquotedate: row.lastquotedate || "",
            lastcloseprice: Number(row.lastcloseprice || 0),
            periodturnover: Number(row.periodturnover || 0),
            totalquotecount: Number(row.totalquotecount || 0),
            quotecount: Number(row.quotecount || 0),
            revenue: Number(row.revenue || 0),
            peratio: row.peratio === undefined || row.peratio === null ? "" : row.peratio,
            corridorhitpercent: Number(row.corridorhitpercent || 0),
            maxdrawdownpercent: Number(row.maxdrawdownpercent || 0)
        }
    }



    function stockAnalysisIndexSelected(rowIndex) {
        return selectedStockAnalysisRows.indexOf(rowIndex) >= 0
    }

    function sortedStockAnalysisSelection() {
        let rows = selectedStockAnalysisRows.slice()
        rows.sort((a, b) => a - b)
        return rows
    }

    function selectSingleStockAnalysisRow(rowIndex) {
        selectedStockAnalysisRows = [rowIndex]
        stockAnalysisSelectionAnchor = rowIndex
        selectStockAnalysisResult(rowIndex)
    }

    function toggleStockAnalysisRow(rowIndex) {
        let rows = selectedStockAnalysisRows.slice()
        let pos = rows.indexOf(rowIndex)
        if (pos >= 0)
            rows.splice(pos, 1)
        else
            rows.push(rowIndex)
        rows.sort((a, b) => a - b)
        selectedStockAnalysisRows = rows
        stockAnalysisSelectionAnchor = rowIndex
        selectStockAnalysisResult(rowIndex)
    }

    function selectStockAnalysisRange(rowIndex, keepExisting) {
        if (stockAnalysisSelectionAnchor < 0) {
            selectSingleStockAnalysisRow(rowIndex)
            return
        }

        let from = Math.min(stockAnalysisSelectionAnchor, rowIndex)
        let to = Math.max(stockAnalysisSelectionAnchor, rowIndex)
        let rows = keepExisting ? selectedStockAnalysisRows.slice() : []
        for (let i = from; i <= to; i++) {
            if (rows.indexOf(i) < 0)
                rows.push(i)
        }
        rows.sort((a, b) => a - b)
        selectedStockAnalysisRows = rows
        selectStockAnalysisResult(rowIndex)
    }

    function stockAnalysisSelectedRowsData() {
        let rows = []
        sortedStockAnalysisSelection().forEach(rowIndex => {
            if (rowIndex >= 0 && rowIndex < stockAnalysisResultModel.count)
                rows.push(cloneStockAnalysisResult(stockAnalysisResultModel.get(rowIndex)))
        })
        return rows
    }

    function stockAnalysisDateToIso(dateText) {
        let text = String(dateText || "").trim()
        if (/^\d{4}-\d{2}-\d{2}$/.test(text))
            return text
        let match = /^(\d{1,2})\.(\d{1,2})\.(\d{4})$/.exec(text)
        if (!match)
            return ""
        return match[3] + "-" + match[2].padStart(2, "0") + "-" + match[1].padStart(2, "0")
    }

    function stockAnalysisOldestCommonBuyDate(rows) {
        let commonDate = ""
        rows.forEach(row => {
            let iso = stockAnalysisDateToIso(row.firstquotedate)
            if (iso.length > 0 && (commonDate.length === 0 || iso > commonDate))
                commonDate = iso
        })
        return commonDate.length > 0 ? commonDate : currentIsoDate()
    }

    function openStockAnalysisBuyDialog() {
        let rows = stockAnalysisSelectedRowsData()
        if (rows.length === 0) {
            stockAnalysisMessage = "Keine Aktie ausgewaehlt"
            return
        }
        stockAnalysisBuyDialog.stocks = rows
        stockAnalysisBuyAmountInput.text = "1000"
        stockAnalysisBuyDateInput.text = stockAnalysisOldestCommonBuyDate(rows)
        stockAnalysisBuyError.text = ""
        stockAnalysisBuyDialog.open()
    }


    function currentStockAnalysisConfigName() {
        if (selectedStockAnalysisConfigIndex >= 0
                && selectedStockAnalysisConfigIndex < stockAnalysisConfigModel.count)
            return stockAnalysisConfigModel.get(selectedStockAnalysisConfigIndex).name || ""
        return stockAnalysisConfigNameInput.text.trim()
    }

    function buySelectedStockAnalysisStocks() {
        let amount = parseDecimal(stockAnalysisBuyAmountInput.text)
        let buyDate = stockAnalysisBuyDateInput.text.trim()
        if (amount <= 0) {
            stockAnalysisBuyError.text = "Bitte einen Betrag groesser 0 eintragen"
            return
        }
        if (!/^\d{4}-\d{2}-\d{2}$/.test(buyDate)) {
            stockAnalysisBuyError.text = "Datum bitte als JJJJ-MM-TT eintragen"
            return
        }

        let saved = 0
        let analysisConfigName = currentStockAnalysisConfigName()
        stockAnalysisBuyDialog.stocks.forEach(stock => {
            let entryPrice = Number(dbManager.closePriceOnOrBefore(stock.symbol, buyDate) || 0)
            if (entryPrice <= 0)
                entryPrice = Number(stock.firstcloseprice || stock.lastcloseprice || 0)
            let currentPrice = Number(stock.lastcloseprice || entryPrice)
            let quantity = entryPrice > 0 ? amount / entryPrice : 0
            let gainPercent = entryPrice > 0 ? (currentPrice - entryPrice) / entryPrice * 100 : 0
            let ok = dbManager.saveBoughtStock(
                stock.symbol,
                stock.name,
                buyDate,
                "",
                currentPrice,
                entryPrice,
                gainPercent,
                0,
                quantity,
                analysisConfigName
            )
            if (ok)
                saved++
        })

        stockAnalysisBuyDialog.close()
        stockAnalysisMessage = saved + " von " + stockAnalysisBuyDialog.stocks.length + " Positionen gekauft"
        loadBoughtStocks()
        loadTestPortfolio("")
    }

    function selectStockAnalysisResult(rowIndex) {
        if (rowIndex < 0 || rowIndex >= stockAnalysisResultModel.count)
            return

        selectedStockAnalysisIndex = rowIndex
        stockAnalysisStockSelected = true
        let stock = stockAnalysisResultModel.get(rowIndex)
        let quotes = dbManager.getQuoteDetails(stock.symbol, 1, stockAnalysisQuoteCount)
        stockAnalysisQuoteModel.clear()
        quotes.forEach(row => stockAnalysisQuoteModel.append(row))
        updateStockAnalysisQuoteDateRangeText()
        updateStockAnalysisCorridorStats()
        stockAnalysisChart.requestPaint()
    }

    Component {
        id: periodSelectionComponent
        GroupBox {
            id: peridGbID
            property string periodLabel: peridGbID.title
            property alias fromDay: fromDayInput.text
            property alias toDay: toDayInput.text
            property alias successThreshold: successThresholdInput.text
            property alias greaterThan: greaterThanCheckBox.checked
            property alias fromDayInput: fromDayInput
            property alias toDayInput: toDayInput
            property alias active: disablePeriodSelectionID.checked
            property int index: -1
            Rectangle {
                height: periodTextID.implicitHeight
                width: periodTextID.implicitWidth + 5
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.top: parent.top
                anchors.topMargin: -21
                color:"white"
                Text {
                    id: periodTextID
                    //height:parent.height
                    //width: parent.width
                    text: periodLabel
                    font.bold: true
                }
            }
            //label: periodLabel
            ColumnLayout {
                Layout.fillHeight: true
                Layout.preferredWidth: 280
                spacing: 10

                RowLayout {
                    spacing: 10
                    Label { text: "Von Tag:"; Layout.preferredWidth: 100 }
                    TextField {
                        id: fromDayInput
                        property string propertyPreviousValue: text
                        placeholderText: "Von Tag"
                        onTextChanged: {
                            if (enabled) {
                                if (!propertyPreviousValue || propertyPreviousValue === "") {
                                    propertyPreviousValue = text
                                    return
                                }
                                updatePeriods(index, "from")
                            }
                        }
                    }
                }

                RowLayout {
                    spacing: 10
                    Label { text: "Bis Tag:"; Layout.preferredWidth: 100 }
                    TextField {
                        id: toDayInput
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhDigitsOnly
                        onTextChanged: if (enabled) updatePeriods(index, "to")
                    }
                }

                RowLayout {
                    spacing: 10
                    Label { text: "Erfolgsschwelle:"; Layout.preferredWidth: 100 }
                    RowLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        TextField {
                            id: successThresholdInput
                            Layout.fillWidth: true
                            inputMethodHints: Qt.ImhDigitsOnly
                            onTextChanged: {
                                text = text.replace(/[^0-9]/g, "")
                                if (text !== "") {
                                    let val = parseInt(text)
                                    if (val > 100) text = "100"
                                    else if (val < 0) text = "0"
                                }
                            }
                        }
                        Label { text: "%"; font.pixelSize: 16; padding: 6 }
                    }
                }

                RowLayout {
                    spacing: 10
                    Label { text: "Größer als:"; Layout.preferredWidth: 100 }
                    CheckBox { id: greaterThanCheckBox; text: ""; checked: true; Layout.alignment: Qt.AlignLeft }
                }

            }
            CheckBox {
                id: disablePeriodSelectionID
                checked: true
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.rightMargin: -10
                contentItem: Text {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -10
                    text: qsTr("aktiv")
                    font: disablePeriodSelectionID.font
                    color: disablePeriodSelectionID.enabled ? "black" : "gray"
                    leftPadding: disablePeriodSelectionID.indicator.width
                }
                indicator: Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -8
                    width: 20
                    height: 20
                    border.color: "black"
                    Rectangle {
                        width: 14
                        height: 14
                        anchors.centerIn: parent
                        color: disablePeriodSelectionID.checked ? "black" : "transparent"
                    }
                }
            }
        }
    }

    Component {
        id: filterSelectionComponent
        GroupBox {
            id: peridGbID
            property string periodLabel: peridGbID.title
            property alias salesPriceGreaterThan: salesPriceGreaterThan.text
            property alias sortAscCheckBox: sortAscCheckBox
            property alias radioSortPeriod1: radioSortPeriod1
            property alias radioSortPeriod2: radioSortPeriod2
            property alias radioSortPeriod3: radioSortPeriod3
            property alias radioSortPeriod4: radioSortPeriod4
            Rectangle {
                height: periodLabelID.implicitHeight
                width: periodLabelID.implicitWidth + 5
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.top: parent.top
                anchors.topMargin: -21
                color:"white"
                Text {
                    id: periodLabelID
                    text: periodLabel
                    font.bold: true
                }
            }
            //label: periodLabel
            ColumnLayout {
                id: salesID
                Layout.fillHeight: true
                Layout.preferredWidth: 280
                spacing: 10
                RowLayout {
                    spacing: 5
                    Layout.fillWidth: true
                    Label { text: "Umsatz (P1) größer als:"}
                    TextField {
                        id: salesPriceGreaterThan
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhDigitsOnly
                        Layout.preferredWidth: 100
                    }
                }
                RowLayout {
                    spacing: 5
                    Layout.fillWidth: true
                    Frame {
                        id: groupSortDaysID
                        Layout.fillWidth: true
                        background: Rectangle {
                            id: groupSortDaysRectID
                            border.width: 1
                            width: salesPriceGreaterThan.x - x
                            height: colSortDaysID.height + 15
                        }
                        Rectangle {
                            height: sortIncDaysID.implicitHeight
                            width: sortIncDaysID.implicitWidth + 5
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.top: parent.top
                            anchors.topMargin: -21
                            Text {
                                id: sortIncDaysID
                                text: "Anstiege sortieren"
                                font.bold: true
                            }
                        }
                        ColumnLayout {
                            id: colSortDaysID
                            spacing: 5
                            Layout.fillWidth: true
                            RadioButton {
                                id: radioSortPeriod1
                                text: "Per. 1"
                                checked: true
                            }
                            RadioButton {
                                id: radioSortPeriod2
                                text: "Per. 2"
                            }
                            RadioButton {
                                id: radioSortPeriod3
                                text: "Per. 3"
                            }
                            RadioButton {
                                id: radioSortPeriod4
                                text: "Per. 4"
                            }
                        }
                        Rectangle {
                            id: sortDirectionID
                            anchors.left: parent.left
                            anchors.leftMargin: parent.x + ((radioSortPeriod4.x + radioSortPeriod4.width) - parent.x)
                            anchors.top: parent.top
                            anchors.topMargin: radioSortPeriod4.y - groupSortDaysRectID.y
                            x: radioSortPeriod4.bottom
                            //Layout.alignment: Qt.AlignRight | Qt.AlignBottom // Positionierung im Layout
                            //Layout.margins: 5  // Abstand zum Rand

                            Row {
                                id: xyzID
                                CheckBox {
                                    id: sortAscCheckBox
                                    checked: false
                                }
                                Label {
                                    text: sortAscCheckBox.checked ? "â†‘" : "â†“"
                                    font.pixelSize: 30
                                    font.bold: true
                                    Component.onCompleted: {
                                        x = x - 10
                                        y = y - 5
                                    }
                                }
                            }
                            Component.onCompleted: {
                                xyzID.x = xyzID.x + 20
                            }
                        }
                    }
                }
            }
            CheckBox {
                id: disablePeriodSelectionID
                checked: true
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.rightMargin: -10
                contentItem: Text {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -10
                    text: qsTr("aktiv")
                    font: disablePeriodSelectionID.font
                    color: disablePeriodSelectionID.enabled ? "black" : "gray"
                    leftPadding: disablePeriodSelectionID.indicator.width
                }
                indicator: Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -8
                    width: 20
                    height: 20
                    border.color: "black"
                    Rectangle {
                        width: 14
                        height: 14
                        anchors.centerIn: parent
                        color: disablePeriodSelectionID.checked ? "black" : "transparent"
                    }
                }
            }
        }
    }

    Window {
        id: detailWindow
        width: 1250
        height: 760
        minimumWidth: 900
        minimumHeight: 580
        modality: Qt.NonModal

        Rectangle {
            anchors.fill: parent
            color: "#f4f6f7"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: (detailStock.symbol || "") + "  " + (detailStock.name || "")
                            font.pixelSize: 22
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Label {
                            text: "Tage " + detailFromDay + " bis " + detailToDay + " | letzter Preis: " + (detailStock.lastClosePrice || "-") + " vom " + (detailStock.lastClosePriceDate || "-")
                            color: "#4f5b62"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        text: "Kaufen"
                        enabled: Boolean(detailStock.symbol) && !detailStockBought
                        onClicked: buyDetailStock()
                    }

                    Button {
                        text: "Schließen"
                        onClicked: detailWindow.close()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: detailPeriods

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 96
                            color: "#ffffff"
                            border.color: "#d3d8dc"
                            radius: 4

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2

                                Label {
                                    text: modelData.label + " | Tag " + modelData.fromDay + "-" + modelData.toDay
                                    font.bold: true
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: "Anstiege: " + (detailStock[modelData.successField] || 0) + " | Grenze: " + (modelData.greaterThan ? ">" : "<") + " " + (modelData.threshold || 0)
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: "Gesamt: " + Number(detailStock[modelData.valueIncField] || 0).toFixed(2) + "% | Volumen: " + (detailStock[modelData.volumeField] || 0)
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: "Umsatz: " + (detailStock[modelData.volumePriceField] || 0)
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 230
                    color: "#ffffff"
                    border.color: "#d3d8dc"
                    radius: 4

                    Canvas {
                        id: detailChart
                        anchors.fill: parent
                        anchors.margins: 12

                        onPaint: {
                            let ctx = getContext("2d")
                            ctx.reset()
                            ctx.clearRect(0, 0, width, height)

                            let count = detailQuoteModel.count
                            ctx.strokeStyle = "#d7dde1"
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.moveTo(0, height - 24)
                            ctx.lineTo(width, height - 24)
                            ctx.stroke()

                            if (count === 0) {
                                ctx.fillStyle = "#66727a"
                                ctx.font = "14px sans-serif"
                                ctx.fillText("Keine Kursdaten für diesen Bereich", 12, 28)
                                return
                            }

                            let minPrice = Number(detailQuoteModel.get(0).closeprice)
                            let maxPrice = minPrice
                            for (let i = 1; i < count; i++) {
                                let price = Number(detailQuoteModel.get(i).closeprice)
                                minPrice = Math.min(minPrice, price)
                                maxPrice = Math.max(maxPrice, price)
                            }

                            let range = Math.max(0.0001, maxPrice - minPrice)
                            let leftPad = 52
                            let rightPad = 16
                            let topPad = 18
                            let bottomPad = 34
                            let plotWidth = Math.max(1, width - leftPad - rightPad)
                            let plotHeight = Math.max(1, height - topPad - bottomPad)

                            ctx.strokeStyle = "#5b8db8"
                            ctx.lineWidth = 2
                            ctx.beginPath()
                            for (let j = 0; j < count; j++) {
                                let row = detailQuoteModel.get(j)
                                let x = leftPad + (count === 1 ? plotWidth / 2 : (j / (count - 1)) * plotWidth)
                                let y = topPad + (1 - ((Number(row.closeprice) - minPrice) / range)) * plotHeight
                                if (j === 0)
                                    ctx.moveTo(x, y)
                                else
                                    ctx.lineTo(x, y)
                            }
                            ctx.stroke()

                            ctx.fillStyle = "#4f5b62"
                            ctx.font = "12px sans-serif"
                            ctx.fillText(maxPrice.toFixed(2), 4, topPad + 8)
                            ctx.fillText(minPrice.toFixed(2), 4, topPad + plotHeight)
                            ctx.fillText("Tag " + detailFromDay, leftPad, height - 8)
                            ctx.fillText("Tag " + detailToDay, Math.max(leftPad, width - 82), height - 8)
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#c9d0d5"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label { text: "Tag"; Layout.preferredWidth: 60; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "Datum"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "Open"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "Close"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "High"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "Low"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "Ã„nderung"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "Volumen"; Layout.preferredWidth: 140; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Item { Layout.fillWidth: true }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: detailQuoteModel
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 30
                        color: index % 2 === 0 ? "#ffffff" : "#eef2f4"

                        RowLayout {
                            anchors.fill: parent
                            spacing: 1
                            Label { text: model.dayindex; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignRight }
                            Label { text: model.closedate; Layout.preferredWidth: 110; horizontalAlignment: Text.AlignHCenter }
                            Label { text: Number(model.openprice || 0).toFixed(2); Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                            Label { text: Number(model.closeprice || 0).toFixed(2); Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                            Label { text: Number(model.highestprice || 0).toFixed(2); Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                            Label { text: Number(model.lowestprice || 0).toFixed(2); Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                            Label { text: Number(model.changepercent || 0).toFixed(2) + "%"; Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                            Label { text: model.volume || 0; Layout.preferredWidth: 140; horizontalAlignment: Text.AlignRight }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    }

    Window {
        id: portfolioWindow
        title: "Mein Depot (Test)"
        width: 1360
        height: 760
        minimumWidth: 980
        minimumHeight: 520

        onVisibleChanged: {
            if (visible)
                loadTestPortfolio()
        }

        Rectangle {
            anchors.fill: parent
            color: "#f4f6f7"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: "Mein Depot"
                        font.pixelSize: 22
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "Connect to IBKR"
                        Layout.preferredWidth: 118
                        font.pixelSize: 11
                        enabled: !dbManager.ibkrConnecting && !dbManager.ibkrDataLoading && !dbManager.ibkrBatchActive
                        onClicked: dbManager.connectToIbkr()
                    }
                }

                GroupBox {
                    title: "Depotstammdaten"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 18

                        GridLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            columns: 4
                            columnSpacing: 18
                            rowSpacing: 6

                            Label { text: "Depot"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label { text: "Gekaufte Positionen"; font.bold: true; Layout.preferredWidth: 180 }
                            Label { text: "Positionen"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label { text: portfolioModel.count + " gesamt, " + portfolioStatusCount(false) + " aktiv"; font.bold: true; Layout.preferredWidth: 180 }

                            Label { text: "Depotwert"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label { text: portfolioTotalCurrentValue().toLocaleString(Qt.locale(), "f", 2); font.bold: true; Layout.preferredWidth: 180 }
                            Label { text: "Investiert"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label { text: portfolioTotalEntryValue().toLocaleString(Qt.locale(), "f", 2); font.bold: true; Layout.preferredWidth: 180 }

                            Label { text: "Gewinn/Verlust"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label {
                                text: portfolioTotalGainValue().toLocaleString(Qt.locale(), "f", 2) + " / " + portfolioPerformancePercent().toLocaleString(Qt.locale(), "f", 2) + " %"
                                font.bold: true
                                color: portfolioTotalGainValue() >= 0 ? "#15803d" : "#b91c1c"
                                Layout.preferredWidth: 180
                            }
                            Label { text: "Datenstatus"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label {
                                text: dbManager.ibkrConnected ? "IBKR verbunden" : cleanDisplayText(dbManager.ibkrConnectionStatus || "IBKR getrennt")
                                font.bold: dbManager.ibkrConnected
                                color: dbManager.ibkrConnected ? "#15803d" : "#475569"
                                Layout.preferredWidth: 260
                                elide: Text.ElideRight
                            }
                        }

                        ColumnLayout {
                            Layout.alignment: Qt.AlignRight | Qt.AlignBottom
                            spacing: 8

                            Button {
                                text: "Konfiguration Par."
                                Layout.preferredWidth: 132
                                onClicked: portfolioBatchWindow.show()
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#c9d0d5"
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredWidth: Math.max(1, portfolioWindow.width * 0.75)
                        Layout.fillHeight: true
                        color: "#ffffff"
                        border.color: "#c9d0d5"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 0

                            Label {
                                text: "Positionen"
                                font.bold: true
                                padding: 10
                                Layout.fillWidth: true
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: "#c9d0d5"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Label { text: "Name"; Layout.fillWidth: true; font.bold: true; leftPadding: 10 }
                                Label { text: "Gesamtwert"; Layout.preferredWidth: 120; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "Gewinn (%)"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "Einstiegswert"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "20 Tage"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "40 Tage"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "60 Tage"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "90 Tage"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight; rightPadding: 10 }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: "#c9d0d5"
                            }

                            ListView {
                                id: portfolioListView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: portfolioModel
                                currentIndex: selectedPortfolioIndex
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Rectangle {
                                    width: ListView.view.width
                                    height: 36
                                    color: selectedPortfolioIndex === index
                                        ? "#dbeafe"
                                        : (index % 2 === 0 ? "#ffffff" : "#f8fafc")

                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 1

                                        Label {
                                            text: cleanDisplayText(model.name || model.symbol || "")
                                            Layout.fillWidth: true
                                            leftPadding: 10
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            text: portfolioPositionTotalValue(model).toLocaleString(Qt.locale(), "f", 2)
                                            Layout.preferredWidth: 120
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label { text: formatPercentValue(model.valueIncreasePercent); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: Number(model.entryValue || 0).toLocaleString(Qt.locale(), "f", 2); Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                                        Label { text: formatPercentValue(model.days20ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: formatPercentValue(model.days40ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: formatPercentValue(model.days60ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: formatPercentValue(model.days90ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight; rightPadding: 10 }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: function(mouse) {
                                            selectedPortfolioIndex = index
                                            if (mouse.button === Qt.RightButton)
                                                portfolioContextMenu.popup()
                                        }
                                    }

                                    Menu {
                                        id: portfolioContextMenu

                                        MenuItem {
                                            text: "Get Data from IBKR"
                                            enabled: dbManager.ibkrConnected
                                                && !dbManager.ibkrDataLoading
                                            onTriggered: dbManager.getIbkrData(
                                                portfolioModel.get(index).symbol)
                                        }

                                        MenuItem {
                                            text: "Get Fundamentals from Yahoo"
                                            enabled: !dbManager.fundamentalDataLoading
                                            onTriggered: dbManager.getAlphaVantageFundamentals(
                                                portfolioModel.get(index).symbol)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: Math.max(1, portfolioWindow.width * 0.25)
                        Layout.fillHeight: true
                        color: "#ffffff"
                        border.color: "#c9d0d5"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 0

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 88
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.topMargin: 8
                                Layout.bottomMargin: 7
                                spacing: 0

                                TextField {
                                    text: selectedPortfolioValue("isin") || "Keine ISIN"
                                    readOnly: true
                                    selectByMouse: true
                                    font.pixelSize: 16
                                    font.bold: true
                                    Layout.fillWidth: true
                                    leftPadding: 0
                                    rightPadding: 0
                                    topPadding: 0
                                    bottomPadding: 0
                                    background: null
                                }

                                Label {
                                    text: cleanDisplayText(selectedPortfolioValue("name"))
                                    color: "#1f2937"
                                    font.pixelSize: 14
                                    font.bold: true
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Item { Layout.preferredHeight: 6 }

                                Label {
                                    text: selectedPortfolioValue("analysisConfigName")
                                        ? cleanDisplayText("(Analyse:" + selectedPortfolioValue("analysisConfigName") + ")")
                                        : "(Analyse:-)"
                                    color: "#475569"
                                    font.italic: true
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: "#c9d0d5"
                            }

                            ScrollView {
                                id: portfolioPropertyScroll
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                contentWidth: availableWidth

                                Column {
                                    width: portfolioPropertyScroll.availableWidth

                                    Repeater {
                                        model: portfolioFields

                                        delegate: Rectangle {
                                            width: parent.width
                                            height: modelData.heading ? 40 : (modelData.key === "name" || modelData.key === "analysisConfigName" ? 46 : 34)
                                            color: modelData.heading
                                                ? "#e5e7eb"
                                                : (index % 2 === 0 ? "#ffffff" : "#f8fafc")

                                            Label {
                                                visible: Boolean(modelData.heading)
                                                anchors.fill: parent
                                                anchors.leftMargin: 10
                                                verticalAlignment: Text.AlignVCenter
                                                text: portfolioHeadingLabel(modelData.label)
                                                font.bold: true
                                                color: "#1f2937"
                                            }

                                            RowLayout {
                                                visible: !modelData.heading
                                                anchors.fill: parent
                                                anchors.leftMargin: 10
                                                anchors.rightMargin: 10
                                                spacing: 12

                                                Label {
                                                    text: portfolioFieldLabel(modelData.key, modelData.label)
                                                    color: "#475569"
                                                    Layout.preferredWidth: 135
                                                    elide: Text.ElideRight
                                                }

                                                TextField {
                                                    text: formatPortfolioValue(modelData.key, modelData.format || "")
                                                    readOnly: true
                                                    Layout.fillWidth: true
                                                    Layout.fillHeight: true
                                                    horizontalAlignment: Text.AlignLeft
                                                    verticalAlignment: Text.AlignVCenter
                                                    selectByMouse: true
                                                    activeFocusOnTab: true
                                                    leftPadding: 0
                                                    rightPadding: 0
                                                    topPadding: 0
                                                    bottomPadding: 0
                                                    background: null
                                                    clip: true
                                                    wrapMode: modelData.key === "name" || modelData.key === "analysisConfigName" ? TextInput.Wrap : TextInput.NoWrap
                                                    Component.onCompleted: cursorPosition = 0
                                                    onTextChanged: cursorPosition = 0
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "IBKR " + dbManager.ibkrBatchDone + "/" + dbManager.ibkrBatchTotal
                            visible: dbManager.ibkrBatchActive || dbManager.ibkrBatchTotal > 0
                            color: "#475569"
                            Layout.preferredWidth: 96
                        }

                        ProgressBar {
                            visible: dbManager.ibkrBatchActive || dbManager.ibkrBatchTotal > 0
                            from: 0
                            to: Math.max(1, dbManager.ibkrBatchTotal)
                            value: dbManager.ibkrBatchDone
                            Layout.preferredWidth: 160
                        }

                        TextField {
                            text: dbManager.ibkrConnectionStatus
                            readOnly: true
                            selectByMouse: true
                            color: dbManager.ibkrConnected
                                ? "#15803d"
                                : (dbManager.ibkrConnecting
                                    ? "#1d4ed8"
                                    : (dbManager.ibkrConnectionStatus.indexOf("Keine") === 0
                                        || dbManager.ibkrConnectionStatus.indexOf("getrennt") >= 0
                                        || dbManager.ibkrConnectionStatus.indexOf("Fehler:") === 0
                                        ? "#b91c1c"
                                        : "#475569"))
                            font.bold: dbManager.ibkrConnected
                            Layout.fillWidth: true
                            leftPadding: 0
                            rightPadding: 0
                            background: null
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: dbManager.ibkrGetStocksActive || dbManager.ibkrGetStocksTotal > 0

                        Label {
                            text: "Get Quotes " + dbManager.ibkrGetStocksDone + "/" + dbManager.ibkrGetStocksTotal
                            color: "#475569"
                            Layout.preferredWidth: 96
                        }

                        ProgressBar {
                            from: 0
                            to: Math.max(1, dbManager.ibkrGetStocksTotal)
                            value: dbManager.ibkrGetStocksDone
                            Layout.preferredWidth: 160
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: dbManager.marketstackBatchActive || dbManager.marketstackBatchTotal > 0

                        Label {
                            text: "Set Exchange " + dbManager.marketstackBatchDone + "/" + dbManager.marketstackBatchTotal
                            color: "#475569"
                            Layout.preferredWidth: 128
                        }

                        ProgressBar {
                            from: 0
                            to: Math.max(1, dbManager.marketstackBatchTotal)
                            value: dbManager.marketstackBatchDone
                            Layout.preferredWidth: 160
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: dbManager.marketstackQuotesBatchActive || dbManager.marketstackQuotesBatchTotal > 0

                        Label {
                            text: "MS Quotes " + dbManager.marketstackQuotesBatchDone + "/" + dbManager.marketstackQuotesBatchTotal
                            color: "#475569"
                            Layout.preferredWidth: 128
                        }

                        ProgressBar {
                            from: 0
                            to: Math.max(1, dbManager.marketstackQuotesBatchTotal)
                            value: dbManager.marketstackQuotesBatchDone
                            Layout.preferredWidth: 160
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: dbManager.marketstackValidationBatchActive || dbManager.marketstackValidationBatchTotal > 0

                        Label {
                            text: "MS Validate " + dbManager.marketstackValidationBatchDone + "/" + dbManager.marketstackValidationBatchTotal
                            color: "#475569"
                            Layout.preferredWidth: 128
                        }

                        ProgressBar {
                            from: 0
                            to: Math.max(1, dbManager.marketstackValidationBatchTotal)
                            value: dbManager.marketstackValidationBatchDone
                            Layout.preferredWidth: 160
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "Yahoo " + dbManager.yahooFundamentalsBatchDone + "/" + dbManager.yahooFundamentalsBatchTotal
                            visible: dbManager.yahooFundamentalsBatchActive || dbManager.yahooFundamentalsBatchTotal > 0
                            color: "#475569"
                            Layout.preferredWidth: 96
                        }

                        ProgressBar {
                            visible: dbManager.yahooFundamentalsBatchActive || dbManager.yahooFundamentalsBatchTotal > 0
                            from: 0
                            to: Math.max(1, dbManager.yahooFundamentalsBatchTotal)
                            value: dbManager.yahooFundamentalsBatchDone
                            Layout.preferredWidth: 160
                        }

                        TextField {
                            text: dbManager.fundamentalDataStatus
                            readOnly: true
                            selectByMouse: true
                            color: dbManager.fundamentalDataStatus.indexOf("Fehler:") === 0
                                || dbManager.fundamentalDataStatus.indexOf("Alpha-Vantage-Tageslimit") === 0
                                ? "#b91c1c"
                                : (dbManager.fundamentalDataLoading ? "#1d4ed8" : "#475569")
                            Layout.fillWidth: true
                            leftPadding: 0
                            rightPadding: 0
                            background: null
                        }

                        Button {
                            text: "Schließen"
                            onClicked: portfolioWindow.close()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: false

                        Label {
                            text: "Pruefung " + dbManager.ibkrNameCheckBatchDone + "/" + dbManager.ibkrNameCheckBatchTotal
                            color: "#475569"
                            Layout.preferredWidth: 96
                        }

                        ProgressBar {
                            from: 0
                            to: Math.max(1, dbManager.ibkrNameCheckBatchTotal)
                            value: dbManager.ibkrNameCheckBatchDone
                            Layout.preferredWidth: 160
                        }

                        TextField {
                            text: dbManager.ibkrConnectionStatus
                            readOnly: true
                            selectByMouse: true
                            color: dbManager.ibkrConnectionStatus.indexOf("Fehler:") === 0
                                || dbManager.ibkrConnectionStatus.indexOf("mismatch") >= 0
                                ? "#b91c1c"
                                : (dbManager.ibkrNameCheckBatchActive ? "#1d4ed8" : "#475569")
                            Layout.fillWidth: true
                            leftPadding: 0
                            rightPadding: 0
                            background: null
                        }

                        Button {
                            text: "Schließen"
                            onClicked: portfolioWindow.close()
                        }
                    }
                }
            }
        }
    }

    Window {
        id: stockAnalysisWindow
        title: "Stock Analyse"
        width: 1600
        height: 1220
        minimumWidth: 1200
        minimumHeight: 1100

        onVisibleChanged: {
            if (visible) {
                loadStockAnalysisConfigs()
                loadLastStockAnalysisConfig()
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "#f4f6f7"

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 12
                anchors.bottomMargin: 44
                spacing: 10

                GroupBox {
                    title: "Suchparameter"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 310
                    Layout.minimumHeight: 300
                    clip: false

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: Math.max(300, stockAnalysisWindow.width * 0.22)
                            Layout.maximumWidth: Math.max(300, stockAnalysisWindow.width * 0.22)
                            Layout.fillHeight: true
                            Layout.alignment: Qt.AlignTop
                            color: "#ffffff"
                            border.color: "#c9d0d5"
                            radius: 4

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4

                                Label {
                                    text: "Name"
                                    font.bold: true
                                    Layout.fillWidth: true
                                }

                                TextField {
                                    id: stockAnalysisConfigNameInput
                                    placeholderText: "Konfigurationsname"
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: "#c9d0d5"
                                }

                                Label {
                                    text: "Gespeicherte Konfigurationen"
                                    font.bold: true
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 130
                                    Layout.maximumHeight: 130
                                    color: "#ffffff"
                                    border.color: "#d3d8dc"
                                    radius: 4
                                    clip: true

                                    ListView {
                                        id: stockAnalysisConfigListView
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        clip: true
                                        model: stockAnalysisConfigModel
                                        currentIndex: selectedStockAnalysisConfigIndex
                                        boundsBehavior: Flickable.StopAtBounds
                                        ScrollBar.vertical: ScrollBar {
                                            policy: stockAnalysisConfigModel.count > stockAnalysisConfigListView.height / 32
                                                ? ScrollBar.AlwaysOn
                                                : ScrollBar.AsNeeded
                                        }

                                        delegate: Rectangle {
                                            width: ListView.view.width
                                            height: 32
                                            color: selectedStockAnalysisConfigIndex === index ? "lightsteelblue" : (index % 2 === 0 ? "#ffffff" : "#eef2f4")

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 8
                                                anchors.rightMargin: 8
                                                spacing: 8

                                                Label {
                                                    text: model.name
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                }

                                                Label {
                                                    text: Number(model.increasePercent || 0).toFixed(0) + "% / "
                                                        + Number(model.corridorPercent === undefined ? 10 : model.corridorPercent).toFixed(0) + "% / "
                                                        + Number(model.corridorRequiredPercent === undefined ? 0 : model.corridorRequiredPercent).toFixed(0) + "% / "
                                                        + Number(model.quoteCount === undefined ? 90 : model.quoteCount).toFixed(0) + " / "
                                                        + Number(model.maxDrawdownPercent === undefined ? 10 : model.maxDrawdownPercent).toFixed(0) + "%"
                                                    Layout.preferredWidth: 210
                                                    horizontalAlignment: Text.AlignRight
                                                    color: "#475569"
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: selectStockAnalysisConfig(index)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.alignment: Qt.AlignTop
                            spacing: 8

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 3
                                columnSpacing: 8
                                rowSpacing: 6

                                Item { Layout.preferredWidth: 330 }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 360
                                    spacing: 8

                                    Item { Layout.fillWidth: true }

                                    Label {
                                        text: "Soll"
                                        Layout.preferredWidth: 44
                                        font.bold: true
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: "Aktuell"
                                    Layout.preferredWidth: 100
                                    font.bold: true
                                    horizontalAlignment: Text.AlignLeft
                                }

                                Label {
                                    text: "Steigerung um %"
                                    Layout.preferredWidth: 330
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 360
                                    spacing: 8

                                    TextField {
                                        id: stockAnalysisIncreaseInput
                                        text: "10"
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                        horizontalAlignment: Text.AlignRight
                                        Layout.preferredWidth: 100
                                    }

                                    Item { Layout.fillWidth: true }

                                    Label {
                                        text: {
                                            let value = Number(stockAnalysisIncreaseInput.text.replace(",", "."))
                                            return isNaN(value) ? "" : value.toFixed(0) + "%"
                                        }
                                        Layout.preferredWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: stockAnalysisStockSelected ? Number(stockAnalysisActualIncreasePercent || 0).toFixed(1) + "%" : "---"
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    font.bold: true
                                    color: "#475569"
                                }

                                Label {
                                    text: "Korridorbreite in % Ø aller Quotes"
                                    Layout.preferredWidth: 330
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 360
                                    spacing: 8

                                    Slider {
                                        id: stockAnalysisCorridorSlider
                                        from: 0
                                        to: 50
                                        stepSize: 2
                                        snapMode: Slider.SnapAlways
                                        value: stockAnalysisCorridorPercent
                                        Layout.fillWidth: true
                                        onMoved: {
                                            stockAnalysisCorridorPercent = Math.round(value / 2) * 2
                                        }
                                    }

                                    Label {
                                        text: Number(stockAnalysisCorridorPercent || 0).toFixed(0) + "%"
                                        Layout.preferredWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: stockAnalysisStockSelected ? Number(stockAnalysisRequiredCorridorPercent || 0).toFixed(1) + "%" : "---"
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    font.bold: true
                                    color: "#475569"
                                }

                                Label {
                                    text: "Werte im Korridor"
                                    Layout.preferredWidth: 330
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 360
                                    spacing: 8

                                    Slider {
                                        id: stockAnalysisCorridorRequiredSlider
                                        from: 0
                                        to: 100
                                        stepSize: 2
                                        snapMode: Slider.SnapAlways
                                        value: stockAnalysisCorridorRequiredPercent
                                        Layout.fillWidth: true
                                        onMoved: {
                                            stockAnalysisCorridorRequiredPercent = Math.round(value / 2) * 2
                                        }
                                    }

                                    Label {
                                        text: Number(stockAnalysisCorridorRequiredPercent || 0).toFixed(0) + "%"
                                        Layout.preferredWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: stockAnalysisStockSelected ? Number(stockAnalysisCorridorHitPercent || 0).toFixed(1) + "%" : "---"
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    font.bold: true
                                    color: "#475569"
                                }

                                Label {
                                    text: "Anzahl Kurswerte"
                                    Layout.preferredWidth: 330
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 360
                                    spacing: 8

                                    Slider {
                                        id: stockAnalysisQuoteCountSlider
                                        from: 10
                                        to: 90
                                        stepSize: 10
                                        snapMode: Slider.SnapAlways
                                        live: true
                                        value: stockAnalysisQuoteCount
                                        Layout.fillWidth: true
                                        onMoved: setStockAnalysisQuoteCount(value, false)
                                        onValueChanged: {
                                            if (pressed)
                                                setStockAnalysisQuoteCount(value, false)
                                        }
                                        onPressedChanged: {
                                            if (!pressed)
                                                setStockAnalysisQuoteCount(value, true)
                                        }
                                    }

                                    Label {
                                        text: Number(stockAnalysisQuoteCount || 90).toFixed(0)
                                        Layout.preferredWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: stockAnalysisStockSelected
                                        ? stockAnalysisQuoteModel.count + " Werte" + (stockAnalysisQuoteDateRangeText.length > 0 ? " | " + stockAnalysisQuoteDateRangeText : "")
                                        : "---"
                                    Layout.preferredWidth: 220
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    font.bold: true
                                    color: "#475569"
                                }

                                Label {
                                    text: "Größter Kursrückgang während Laufzeit"
                                    Layout.preferredWidth: 330
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 360
                                    spacing: 8

                                    Slider {
                                        id: stockAnalysisMaxDrawdownSlider
                                        from: 0
                                        to: 100
                                        stepSize: 2
                                        snapMode: Slider.SnapAlways
                                        value: stockAnalysisMaxDrawdownPercent
                                        Layout.fillWidth: true
                                        onMoved: {
                                            stockAnalysisMaxDrawdownPercent = Math.round(value / 2) * 2
                                        }
                                    }

                                    Label {
                                        text: Number(stockAnalysisMaxDrawdownPercent || 0).toFixed(0) + "%"
                                        Layout.preferredWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: stockAnalysisStockSelected ? Number(stockAnalysisActualMaxDrawdownPercent || 0).toFixed(1) + "%" : "---"
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    font.bold: true
                                    color: "#475569"
                                }

                            }

                            Item { Layout.preferredHeight: 1 }
                        }

                        ColumnLayout {
                            Layout.alignment: Qt.AlignRight | Qt.AlignTop
                            Layout.preferredWidth: 110
                            spacing: 8

                            Button {
                                text: stockAnalysisScanActive ? "Stoppen" : "Suchen"
                                Layout.preferredWidth: 100
                                onClicked: stockAnalysisScanActive ? stopStockAnalysisScan() : startStockAnalysisScan()
                            }


                            Button {
                                text: "Neuanlage"
                                Layout.preferredWidth: 100
                                onClicked: newStockAnalysisConfig()
                            }

                            Button {
                                text: "Ändern"
                                Layout.preferredWidth: 100
                                onClicked: saveStockAnalysisConfig()
                            }
                        }
                    }
                }

                GroupBox {
                    title: "Gefundene Stocks (" + stockAnalysisResultModel.count + ")"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 250

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label { text: "Nr"; Layout.preferredWidth: 42; font.bold: true; horizontalAlignment: Text.AlignLeft; leftPadding: 4 }
                            Label { text: "ISIN"; Layout.preferredWidth: 130; font.bold: true }
                            Label { text: "Name"; Layout.preferredWidth: 340; font.bold: true }
                            Label { text: "Handelsumsatz"; Layout.preferredWidth: 150; font.bold: true; horizontalAlignment: Text.AlignRight }
                            Label { text: "KGV"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight }
                            Label { text: "Quotes"; Layout.preferredWidth: 80; font.bold: true; horizontalAlignment: Text.AlignRight }
                            Label { text: "IBKR/MS"; Layout.preferredWidth: 75; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                            Label { text: "Korridor"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight }
                            Label { text: "Steigerung"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignRight }
                            Item { Layout.fillWidth: true }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: "#c9d0d5"
                        }

                        ListView {
                            id: stockAnalysisListView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: stockAnalysisResultModel
                            currentIndex: selectedStockAnalysisIndex
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {
                                policy: stockAnalysisResultModel.count > stockAnalysisListView.height / 34
                                    ? ScrollBar.AlwaysOn
                                    : ScrollBar.AsNeeded
                            }

                            Menu {
                                id: stockAnalysisContextMenu
                                MenuItem {
                                    text: "Auswahl kaufen..."
                                    enabled: selectedStockAnalysisRows.length > 0
                                    onTriggered: openStockAnalysisBuyDialog()
                                }
                            }

                            delegate: Rectangle {
                                width: ListView.view.width
                                height: 34
                                color: stockAnalysisIndexSelected(index)
                                    ? "lightsteelblue"
                                    : (index % 2 === 0 ? "#ffffff" : "#eef2f4")

                                MouseArea {
                                    anchors.fill: parent
                                    z: 0
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: function(mouse) {
                                        if (mouse.button === Qt.RightButton) {
                                            if (!stockAnalysisIndexSelected(index))
                                                selectSingleStockAnalysisRow(index)
                                            stockAnalysisContextMenu.popup()
                                            return
                                        }

                                        if (mouse.modifiers & Qt.ShiftModifier)
                                            selectStockAnalysisRange(index, mouse.modifiers & Qt.ControlModifier)
                                        else if (mouse.modifiers & Qt.ControlModifier)
                                            toggleStockAnalysisRow(index)
                                        else
                                            selectSingleStockAnalysisRow(index)
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    z: 1
                                    spacing: 1
                                    Label { text: index + 1; Layout.preferredWidth: 42; horizontalAlignment: Text.AlignLeft; leftPadding: 4 }
                                    TextField {
                                        text: model.isin || ""
                                        readOnly: true
                                        selectByMouse: true
                                        Layout.preferredWidth: 130
                                        leftPadding: 0
                                        rightPadding: 0
                                        background: null
                                        color: "#111827"
                                    }
                                    Label { text: cleanDisplayText(model.name || ""); Layout.preferredWidth: 340; elide: Text.ElideRight }
                                    Label { text: model.periodturnover ? Number(model.periodturnover).toLocaleString(Qt.locale("de_DE"), "f", 0) : "-"; Layout.preferredWidth: 150; horizontalAlignment: Text.AlignRight }
                                    Label { text: model.peratio ? Number(model.peratio).toFixed(2) : "-"; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                    Label { text: model.quotecount || 0; Layout.preferredWidth: 80; horizontalAlignment: Text.AlignRight }
                                    Label { text: model.quotesource || "-"; Layout.preferredWidth: 75; horizontalAlignment: Text.AlignHCenter }
                                    Label { text: Number(model.corridorhitpercent || 0).toFixed(1) + "%"; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                    Label { text: Number(model.increasepercent || 0).toFixed(2) + "%"; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }
                    }
                }

                GroupBox {
                    title: selectedStockAnalysisIndex >= 0
                        ? "Darstellung: " + stockAnalysisResultModel.get(selectedStockAnalysisIndex).name
                            + (stockAnalysisQuoteDateRangeText.length > 0 ? " (" + stockAnalysisQuoteDateRangeText + ")" : "")
                        : "Darstellung"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: stockAnalysisWindow.height * 0.42

                    Canvas {
                        id: stockAnalysisChart
                        anchors.fill: parent
                        anchors.margins: 12
                        property int hoveredQuoteIndex: -1

                        onPaint: {
                            let ctx = getContext("2d")
                            ctx.reset()
                            ctx.clearRect(0, 0, width, height)

                            let count = stockAnalysisQuoteModel.count
                            if (count === 0) {
                                ctx.fillStyle = "#66727a"
                                ctx.font = "14px sans-serif"
                                ctx.fillText("Bitte einen Stock aus der Liste selektieren", 12, 28)
                                return
                            }

                            function quoteAt(chartIndex) {
                                return stockAnalysisQuoteModel.get(count - 1 - chartIndex)
                            }

                            let minPrice = Number(quoteAt(0).closeprice)
                            let maxPrice = minPrice
                            for (let i = 1; i < count; i++) {
                                let price = Number(quoteAt(i).closeprice)
                                minPrice = Math.min(minPrice, price)
                                maxPrice = Math.max(maxPrice, price)
                            }

                            let priceRange = maxPrice - minPrice
                            let leftPad = 68
                            let rightPad = 24
                            let topPad = 30
                            let bottomPad = 72
                            let plotWidth = Math.max(1, width - leftPad - rightPad)
                            let plotHeight = Math.max(1, height - topPad - bottomPad)

                            function xForIndex(chartIndex) {
                                return leftPad + (count === 1 ? plotWidth / 2 : (chartIndex / (count - 1)) * plotWidth)
                            }

                            function yForPrice(price) {
                                if (priceRange <= 0)
                                    return topPad + plotHeight / 2

                                return topPad + (1 - ((price - minPrice) / priceRange)) * plotHeight
                            }

                            ctx.strokeStyle = "#d7dde1"
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.moveTo(leftPad, topPad)
                            ctx.lineTo(leftPad, topPad + plotHeight)
                            ctx.lineTo(leftPad + plotWidth, topPad + plotHeight)
                            ctx.stroke()

                            ctx.fillStyle = "#4f5b62"
                            ctx.font = "12px sans-serif"
                            ctx.fillText("Schlusskurs", 4, topPad - 10)
                            for (let grid = 0; grid <= 4; grid++) {
                                let ratio = grid / 4
                                let yGrid = topPad + ratio * plotHeight
                                let gridPrice = priceRange <= 0 ? maxPrice : maxPrice - ratio * priceRange
                                ctx.strokeStyle = grid === 4 ? "#c9d0d5" : "#edf1f3"
                                ctx.beginPath()
                                ctx.moveTo(leftPad, yGrid)
                                ctx.lineTo(leftPad + plotWidth, yGrid)
                                ctx.stroke()
                                ctx.fillText(gridPrice.toFixed(2), 4, yGrid + 4)
                            }

                            let labelEvery = Math.max(1, Math.ceil(count / Math.max(2, Math.floor(plotWidth / 90))))
                            ctx.fillStyle = "#4f5b62"
                            ctx.font = "11px sans-serif"
                            for (let labelIndex = 0; labelIndex < count; labelIndex++) {
                                if (labelIndex !== 0 && labelIndex !== count - 1 && labelIndex % labelEvery !== 0)
                                    continue

                                let labelRow = quoteAt(labelIndex)
                                let labelText = labelRow.closedate || ""
                                let labelX = xForIndex(labelIndex)
                                ctx.strokeStyle = "#d7dde1"
                                ctx.beginPath()
                                ctx.moveTo(labelX, topPad + plotHeight)
                                ctx.lineTo(labelX, topPad + plotHeight + 5)
                                ctx.stroke()

                                ctx.save()
                                ctx.translate(labelX - 4, topPad + plotHeight + 58)
                                ctx.rotate(-Math.PI / 4)
                                ctx.fillText(labelText, 0, 0)
                                ctx.restore()
                            }

                            ctx.strokeStyle = "#2f7d62"
                            ctx.lineWidth = 2
                            ctx.beginPath()
                            for (let j = 0; j < count; j++) {
                                let row = quoteAt(j)
                                let x = xForIndex(j)
                                let y = yForPrice(Number(row.closeprice))
                                if (j === 0)
                                    ctx.moveTo(x, y)
                                else
                                    ctx.lineTo(x, y)
                            }
                            ctx.stroke()

                            let averageWindow = Math.min(5, count)
                            let oldestAverage = 0
                            let newestAverage = 0
                            let allAverage = 0
                            for (let allAvgIndex = 0; allAvgIndex < count; allAvgIndex++)
                                allAverage += Number(quoteAt(allAvgIndex).closeprice)
                            allAverage /= count

                            for (let avgIndex = 0; avgIndex < averageWindow; avgIndex++) {
                                oldestAverage += Number(quoteAt(avgIndex).closeprice)
                                newestAverage += Number(quoteAt(count - averageWindow + avgIndex).closeprice)
                            }
                            oldestAverage /= averageWindow
                            newestAverage /= averageWindow

                            let oldestAvgX = xForIndex((averageWindow - 1) / 2)
                            let newestAvgX = xForIndex(count - averageWindow + (averageWindow - 1) / 2)
                            let oldestAvgY = yForPrice(oldestAverage)
                            let newestAvgY = yForPrice(newestAverage)
                            let corridorPercent = stockAnalysisCorridorPercent
                            let bandOffset = allAverage * corridorPercent / 100
                            let oldestCenterIndex = (averageWindow - 1) / 2
                            let newestCenterIndex = count - averageWindow + (averageWindow - 1) / 2
                            let trendIndexDenominator = Math.max(1, newestCenterIndex - oldestCenterIndex)
                            let trendPriceSlope = (newestAverage - oldestAverage) / trendIndexDenominator
                            function trendPriceAtIndex(chartIndex) {
                                return oldestAverage + trendPriceSlope * (chartIndex - oldestCenterIndex)
                            }
                            let averageSlope = newestAvgX === oldestAvgX ? 0 : (newestAvgY - oldestAvgY) / (newestAvgX - oldestAvgX)
                            let firstTrendX = xForIndex(0)
                            let lastTrendX = xForIndex(count - 1)
                            let firstTrendY = oldestAvgY + averageSlope * (firstTrendX - oldestAvgX)
                            let lastTrendY = newestAvgY + averageSlope * (lastTrendX - newestAvgX)
                            let firstTrendPrice = trendPriceAtIndex(0)
                            let lastTrendPrice = trendPriceAtIndex(count - 1)
                            let tightBandOffset = allAverage * requiredCorridorPercentForQuotesModel(stockAnalysisQuoteModel) / 100
                            let currentCorridorCoversAll = tightBandOffset <= bandOffset + 0.000001

                            ctx.fillStyle = "rgba(120, 130, 140, 0.18)"
                            ctx.beginPath()
                            ctx.moveTo(firstTrendX, yForPrice(firstTrendPrice + bandOffset))
                            ctx.lineTo(lastTrendX, yForPrice(lastTrendPrice + bandOffset))
                            ctx.lineTo(lastTrendX, yForPrice(lastTrendPrice - bandOffset))
                            ctx.lineTo(firstTrendX, yForPrice(firstTrendPrice - bandOffset))
                            ctx.closePath()
                            ctx.fill()

                            ctx.strokeStyle = "#ea580c"
                            ctx.lineWidth = 1
                            ctx.setLineDash([4, 4])
                            ctx.beginPath()
                            ctx.moveTo(firstTrendX, yForPrice(firstTrendPrice + bandOffset))
                            ctx.lineTo(lastTrendX, yForPrice(lastTrendPrice + bandOffset))
                            ctx.moveTo(firstTrendX, yForPrice(firstTrendPrice - bandOffset))
                            ctx.lineTo(lastTrendX, yForPrice(lastTrendPrice - bandOffset))
                            ctx.stroke()

                            let maxDrawdown = maxDrawdownForQuotesModel(stockAnalysisQuoteModel)
                            if (maxDrawdown.percent > 0 && maxDrawdown.peakIndex >= 0 && maxDrawdown.troughIndex >= 0) {
                                let peakRow = quoteAt(maxDrawdown.peakIndex)
                                let troughRow = quoteAt(maxDrawdown.troughIndex)
                                let peakX = xForIndex(maxDrawdown.peakIndex)
                                let troughX = xForIndex(maxDrawdown.troughIndex)
                                let peakY = yForPrice(Number(peakRow.closeprice))
                                let troughY = yForPrice(Number(troughRow.closeprice))

                                ctx.lineCap = "round"
                                ctx.strokeStyle = "rgba(255, 255, 255, 0.95)"
                                ctx.lineWidth = 10
                                ctx.setLineDash([])
                                ctx.beginPath()
                                ctx.moveTo(peakX, peakY)
                                ctx.lineTo(troughX, troughY)
                                ctx.stroke()

                                ctx.strokeStyle = "#111827"
                                ctx.lineWidth = 6
                                ctx.setLineDash([10, 7])
                                ctx.lineDashOffset = 0
                                ctx.beginPath()
                                ctx.moveTo(peakX, peakY)
                                ctx.lineTo(troughX, troughY)
                                ctx.stroke()
                                ctx.setLineDash([])
                                ctx.lineDashOffset = 0
                                ctx.lineCap = "butt"

                                ctx.fillStyle = "#ffffff"
                                ctx.strokeStyle = "#111827"
                                ctx.lineWidth = 3
                                ctx.beginPath()
                                ctx.arc(peakX, peakY, 7, 0, Math.PI * 2)
                                ctx.arc(troughX, troughY, 7, 0, Math.PI * 2)
                                ctx.fill()
                                ctx.stroke()

                                let drawdownLabel = "Rückgang: " + maxDrawdown.percent.toFixed(1) + "%"
                                ctx.fillStyle = "#111827"
                                ctx.font = "11px sans-serif"
                                let labelX = Math.min(width - ctx.measureText(drawdownLabel).width - 6, Math.max(4, troughX + 8))
                                ctx.fillText(drawdownLabel, labelX, Math.min(topPad + plotHeight - 8, troughY + 14))
                            }

                            if (currentCorridorCoversAll) {
                                ctx.strokeStyle = "#111827"
                                ctx.lineWidth = 1
                                ctx.setLineDash([2, 4])
                                ctx.beginPath()
                                ctx.moveTo(firstTrendX, yForPrice(firstTrendPrice + tightBandOffset))
                                ctx.lineTo(lastTrendX, yForPrice(lastTrendPrice + tightBandOffset))
                                ctx.moveTo(firstTrendX, yForPrice(firstTrendPrice - tightBandOffset))
                                ctx.lineTo(lastTrendX, yForPrice(lastTrendPrice - tightBandOffset))
                                ctx.stroke()
                                ctx.setLineDash([])

                                let tightPercent = allAverage > 0 ? tightBandOffset / allAverage * 100 : 0
                                let tightLabel = "min: " + tightPercent.toFixed(1) + "%"
                                ctx.fillStyle = "#111827"
                                ctx.font = "11px sans-serif"
                                ctx.fillText(tightLabel, Math.max(4, lastTrendX - ctx.measureText(tightLabel).width - 4),
                                             Math.max(12, yForPrice(lastTrendPrice + tightBandOffset) - 6))
                            }

                            ctx.strokeStyle = "#c2410c"
                            ctx.lineWidth = 2
                            ctx.setLineDash([6, 5])
                            ctx.beginPath()
                            ctx.moveTo(firstTrendX, firstTrendY)
                            ctx.lineTo(oldestAvgX, oldestAvgY)
                            ctx.moveTo(newestAvgX, newestAvgY)
                            ctx.lineTo(lastTrendX, lastTrendY)
                            ctx.stroke()
                            ctx.setLineDash([])

                            ctx.strokeStyle = "#c2410c"
                            ctx.lineWidth = 3
                            ctx.beginPath()
                            ctx.moveTo(oldestAvgX, oldestAvgY)
                            ctx.lineTo(newestAvgX, newestAvgY)
                            ctx.stroke()

                            ctx.fillStyle = "#c2410c"
                            ctx.beginPath()
                            ctx.arc(oldestAvgX, oldestAvgY, 5, 0, Math.PI * 2)
                            ctx.arc(newestAvgX, newestAvgY, 5, 0, Math.PI * 2)
                            ctx.fill()

                            ctx.font = "11px sans-serif"
                            ctx.fillText("Ø alt: " + oldestAverage.toFixed(2), oldestAvgX + 8, Math.max(12, oldestAvgY - 8))
                            let newestLabel = "Ø neu: " + newestAverage.toFixed(2)
                            let newestLabelX = Math.min(width - ctx.measureText(newestLabel).width - 6, newestAvgX + 8)
                            ctx.fillText(newestLabel, newestLabelX, Math.max(12, newestAvgY - 8))

                            let pointRadius = count <= 60 ? 3 : 2
                            let valueEvery = Math.max(1, Math.ceil(count / Math.max(2, Math.floor(plotWidth / 46))))
                            for (let k = 0; k < count; k++) {
                                let pointRow = quoteAt(k)
                                let pointPrice = Number(pointRow.closeprice)
                                let px = xForIndex(k)
                                let py = yForPrice(pointPrice)

                                ctx.fillStyle = stockAnalysisChart.hoveredQuoteIndex === k ? "#b45309" : "#2f7d62"
                                ctx.beginPath()
                                ctx.arc(px, py, stockAnalysisChart.hoveredQuoteIndex === k ? pointRadius + 2 : pointRadius, 0, Math.PI * 2)
                                ctx.fill()

                                if (k === 0 || k === count - 1 || k % valueEvery === 0 || stockAnalysisChart.hoveredQuoteIndex === k) {
                                    ctx.fillStyle = "#374151"
                                    ctx.font = "10px sans-serif"
                                    ctx.fillText(pointPrice.toFixed(2), px - 14, Math.max(10, py - 7))
                                }
                            }

                            if (stockAnalysisChart.hoveredQuoteIndex >= 0 && stockAnalysisChart.hoveredQuoteIndex < count) {
                                let hoverRow = quoteAt(stockAnalysisChart.hoveredQuoteIndex)
                                let hoverPrice = Number(hoverRow.closeprice)
                                let hoverX = xForIndex(stockAnalysisChart.hoveredQuoteIndex)
                                let hoverY = yForPrice(hoverPrice)
                                let tooltipText = (hoverRow.closedate || "") + "  Schlusskurs: " + hoverPrice.toFixed(2)
                                let tooltipWidth = Math.min(260, ctx.measureText(tooltipText).width + 18)
                                let tooltipX = Math.min(width - tooltipWidth - 4, Math.max(4, hoverX - tooltipWidth / 2))
                                let tooltipY = Math.max(4, hoverY - 42)

                                ctx.fillStyle = "#263238"
                                ctx.fillRect(tooltipX, tooltipY, tooltipWidth, 26)
                                ctx.fillStyle = "#ffffff"
                                ctx.font = "12px sans-serif"
                                ctx.fillText(tooltipText, tooltipX + 9, tooltipY + 17)
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onPositionChanged: function(mouse) {
                                let count = stockAnalysisQuoteModel.count
                                if (count <= 0 || stockAnalysisChart.width <= 92) {
                                    stockAnalysisChart.hoveredQuoteIndex = -1
                                    stockAnalysisChart.requestPaint()
                                    return
                                }

                                let leftPad = 68
                                let rightPad = 24
                                let plotWidth = Math.max(1, stockAnalysisChart.width - leftPad - rightPad)
                                let ratio = Math.max(0, Math.min(1, (mouse.x - leftPad) / plotWidth))
                                stockAnalysisChart.hoveredQuoteIndex = Math.round(ratio * (count - 1))
                                stockAnalysisChart.requestPaint()
                            }
                            onExited: {
                                stockAnalysisChart.hoveredQuoteIndex = -1
                                stockAnalysisChart.requestPaint()
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 34
                color: "#e8edf0"
                border.color: "#c9d0d5"

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    text: stockAnalysisMessage
                    color: stockAnalysisMessage.indexOf("ungueltig") >= 0 ? "#b91c1c" : "#475569"
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }


    Window {
        id: stockAnalysisBuyDialog
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
            x = stockAnalysisWindow.x + Math.max(20, (stockAnalysisWindow.width - width) / 2)
            y = stockAnalysisWindow.y + Math.max(20, (stockAnalysisWindow.height - height) / 2)
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
                        onClicked: buySelectedStockAnalysisStocks()
                    }
                }
            }
        }
    }


    Window {
        id: portfolioBatchWindow
        title: "Depot-Batchaufrufe"
        width: 760
        height: 430
        minimumWidth: 680
        minimumHeight: 360

        Rectangle {
            anchors.fill: parent
            color: "#f4f6f7"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Label {
                    text: "Batchaufrufe"
                    font.pixelSize: 20
                    font.bold: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8

                    Button { text: "IBKR Batch starten"; Layout.fillWidth: true; enabled: dbManager.ibkrConnected && !dbManager.ibkrDataLoading && !dbManager.ibkrBatchActive; onClicked: dbManager.startIbkrBatch() }
                    Button { text: "IBKR Batch stoppen"; Layout.fillWidth: true; enabled: dbManager.ibkrBatchActive; onClicked: dbManager.stopIbkrBatch() }
                    Button { text: "IBKR Get Quotes starten"; Layout.fillWidth: true; enabled: dbManager.ibkrConnected && !dbManager.ibkrDataLoading && !dbManager.ibkrBatchActive && !dbManager.ibkrNameCheckBatchActive && !dbManager.ibkrGetStocksActive; onClicked: dbManager.startIbkrGetStocks() }
                    Button { text: "IBKR Get Quotes stoppen"; Layout.fillWidth: true; enabled: dbManager.ibkrGetStocksActive; onClicked: dbManager.stopIbkrGetStocks() }
                    Button { text: "Marketstack Set Exchange starten"; Layout.fillWidth: true; enabled: !dbManager.yahooFundamentalsBatchActive && !dbManager.marketstackBatchActive && !dbManager.marketstackQuotesBatchActive && !dbManager.marketstackValidationBatchActive; onClicked: dbManager.startMarketstackBatch() }
                    Button { text: "Marketstack Set Exchange stoppen"; Layout.fillWidth: true; enabled: dbManager.marketstackBatchActive; onClicked: dbManager.stopMarketstackBatch() }
                    Button { text: "Marketstack Get Quotes starten"; Layout.fillWidth: true; enabled: !dbManager.yahooFundamentalsBatchActive && !dbManager.marketstackBatchActive && !dbManager.marketstackQuotesBatchActive && !dbManager.marketstackValidationBatchActive; onClicked: dbManager.startMarketstackQuotesBatch() }
                    Button { text: "Marketstack Get Quotes stoppen"; Layout.fillWidth: true; enabled: dbManager.marketstackQuotesBatchActive; onClicked: dbManager.stopMarketstackQuotesBatch() }
                    Button { text: "Marketstack Validate starten"; Layout.fillWidth: true; enabled: !dbManager.yahooFundamentalsBatchActive && !dbManager.marketstackBatchActive && !dbManager.marketstackQuotesBatchActive && !dbManager.marketstackValidationBatchActive; onClicked: dbManager.startMarketstackValidationBatch() }
                    Button { text: "Marketstack Validate stoppen"; Layout.fillWidth: true; enabled: dbManager.marketstackValidationBatchActive; onClicked: dbManager.stopMarketstackValidationBatch() }
                    Button { text: "Yahoo Batch starten"; Layout.fillWidth: true; enabled: !dbManager.fundamentalDataLoading && !dbManager.yahooFundamentalsBatchActive && !dbManager.marketstackBatchActive && !dbManager.marketstackQuotesBatchActive && !dbManager.marketstackValidationBatchActive; onClicked: dbManager.startYahooFundamentalsBatch() }
                    Button { text: "Yahoo Batch stoppen"; Layout.fillWidth: true; enabled: dbManager.yahooFundamentalsBatchActive; onClicked: dbManager.stopYahooFundamentalsBatch() }
                }

                Label { text: "IBKR API \u00b7 Testwert \u00b7 Datenbank"; color: "#475569" }
                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button { text: "Schlie\u00dfen"; onClicked: portfolioBatchWindow.close() }
                }
            }
        }
    }

    Window {
        id: boughtStocksDialog
        title: "Gekaufte Aktien"
        width: 1180
        height: 620
        minimumWidth: 900
        minimumHeight: 420

        onVisibleChanged: {
            if (visible) {
                boughtStockMessage = ""
                loadBoughtStocks()
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "#f4f6f7"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label { text: "Symbol"; Layout.preferredWidth: 110; font.bold: true }
                    Label { text: "Name"; Layout.preferredWidth: 260; font.bold: true }
                    Label { text: "Gekauft"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "Verkauft"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "Aktuell"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "Einstieg"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "Steigerung"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "Status"; Layout.preferredWidth: 70; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                    Item { Layout.fillWidth: true }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#c9d0d5"
                }

                ListView {
                    id: boughtStockListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: boughtStockModel
                    currentIndex: selectedBoughtStockIndex
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 34
                        color: selectedBoughtStockIndex === index ? "lightsteelblue" : (index % 2 === 0 ? "#ffffff" : "#eef2f4")

                        RowLayout {
                            anchors.fill: parent
                            spacing: 1
                            Label { text: model.symbol; Layout.preferredWidth: 110; elide: Text.ElideRight }
                            Label { text: model.name; Layout.preferredWidth: 260; elide: Text.ElideRight }
                            Label { text: model.buyDate; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignHCenter }
                            Label { text: model.sellDate; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignHCenter }
                            Label { text: Number(model.currentValue || 0).toFixed(2); Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
                            Label { text: Number(model.entryValue || 0).toFixed(2); Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
                            Label { text: Number(model.valueIncreasePercent || 0).toFixed(2) + "%"; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
                            Label { text: model.status; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignHCenter }
                            Item { Layout.fillWidth: true }
                        }

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                selectedBoughtStockIndex = index
                                boughtStockMessage = ""
                                if (mouse.button === Qt.RightButton)
                                    boughtStockContextMenu.popup()
                            }
                        }

                        Menu {
                            id: boughtStockContextMenu
                            MenuItem {
                                text: "Aktie entfernen"
                                onTriggered: requestDeleteBoughtStock(index)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: "Aktie entfernen"
                        enabled: selectedBoughtStockIndex >= 0
                        onClicked: requestDeleteBoughtStock(selectedBoughtStockIndex)
                    }

                    Label {
                        text: boughtStockMessage
                        color: boughtStockMessage === "Verkauft" ? "darkgreen" : "darkred"
                        Layout.fillWidth: true
                    }

                    Button {
                        text: "Verkaufen"
                        enabled: selectedBoughtStockIndex >= 0
                        onClicked: sellSelectedBoughtStock()
                    }

                    Button {
                        text: "Abbrechen"
                        onClicked: boughtStocksDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: deleteBoughtStockDialog
        title: "Aktie entfernen"
        parent: boughtStocksDialog.contentItem
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        closePolicy: Popup.CloseOnEscape
        width: 420
        anchors.centerIn: parent
        onAccepted: deletePendingBoughtStock()
        onRejected: pendingDeleteBoughtSymbol = ""

        contentItem: Label {
            text: "Die Aktie " + pendingDeleteBoughtSymbol + " endgÃ¼ltig entfernen?"
            wrapMode: Text.Wrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            TextField {
                id: tickerInput
                Layout.preferredWidth: 200
                placeholderText: "Ticker eingeben"
            }

            TextField {
                id: nameInput
                Layout.preferredWidth: 200
                placeholderText: "Name eingeben"
            }

            Button {
                text: "Suchen"
                onClicked: searchByTickerAndExchange()
            }

            Button {
                text: "ZurÃ¼cksetzen"
                onClicked: {
                    dbManager.updateAllISINs()
                    return;
                    /*
                    tickerInput.text = ""
                    nameInput.text = ""
                    loadAllStocks()
                    */
                }

            }
            Button {
                id: loadStockQuotesButton
                text: "Lade Kurse für Selektion"
                enabled: false
                onClicked: {
                    if (selectedSymbols.length === 0) return;

                    console.log("ðŸ“Š Lade historische Daten für:", selectedSymbols.length, "Aktien");
                    loadingOverlay.running = true;

                    // Timer für das Fallback, falls etwas schief geht
                    timer.interval = Math.max(10000, selectedSymbols.length * 2000); // Mindestens 10s, plus 2s pro Aktie
                    timer.start();

                    // Alle selektierten Aktien nacheinander verarbeiten
                    processSelectedStocks(0);
                }
            }

            Timer {
                id: timer
                interval: 10000
                onTriggered: loadingOverlay.running = false
            }

            BusyIndicator {
                id: loadingIndicator
                running: false
                Layout.alignment: Qt.AlignCenter
            }

            Button {
                id: loadQuotesButton
                text: "Lade alle Aktienkurse"
                enabled: true
                onClicked: {
                    console.log("ðŸ“Š Lade historische Daten für alle Aktien")
                    loadingOverlay.running = true
                    dbManager.generateQuotesForAllStocks()
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Gekaufte Aktien"
                onClicked: {
                    boughtStockMessage = ""
                    boughtStocksDialog.show()
                }
            }

            Button {
                text: "Mein Depot"
                onClicked: portfolioWindow.show()
            }

            Button {
                text: "Stock Analyse"
                onClicked: stockAnalysisWindow.show()
            }
        }

        Frame {
            id: selectionArea
            Layout.fillWidth: true
            Layout.preferredHeight: 250
            padding: 10

            RowLayout {
                id: selectionRoad
                anchors.fill: parent
                spacing: 20

                Loader {
                    id: firstPeriodLoader
                    sourceComponent: periodSelectionComponent
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    onLoaded: {
                        Qt.callLater(() => {
                            if (item !== null) {
                                item.index = 0
                                item.periodLabel = "Erste Periode:"
                                item.fromDay = "1"
                                item.toDay = "5"
                                item.successThreshold = "3"
                                item.fromDayInput.enabled = false
                            }
                        })
                    }
                }

                Loader {
                    id: secondPeriodLoader
                    sourceComponent: periodSelectionComponent
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    onLoaded: {
                        Qt.callLater(() => {
                            if (item !== null) {
                                item.index = 1
                                item.periodLabel = "Zweite Periode:"
                                item.fromDay = "6"
                                item.toDay = "15"
                                item.successThreshold = "3"
                            }
                        })
                    }
                }

                Loader {
                    id: thirdPeriodLoader
                    sourceComponent: periodSelectionComponent
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    onLoaded: {
                        Qt.callLater(() => {
                            if (item !== null) {
                                item.index = 2
                                item.periodLabel = "Dritte Periode:"
                                item.fromDay = "16"
                                item.toDay = "35"
                                item.successThreshold = "3"
                            }
                        })
                    }
                }

                Loader {
                    id: fourthPeriodLoader
                    sourceComponent: periodSelectionComponent
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    onLoaded: {
                        Qt.callLater(() => {
                            if (item !== null) {
                                item.index = 3
                                item.periodLabel = "Vierte Periode:"
                                item.fromDay = "36"
                                item.toDay = "75"
                                item.successThreshold = "3"
                            }
                        })
                    }
                }
                Loader {
                    id: filterSelectionLoader
                    sourceComponent: filterSelectionComponent
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    onLoaded: {
                        Qt.callLater(() => {
                            if (item !== null) {
                                //item.index = 3
                                item.periodLabel = "Weitere Filter:"
                                item.salesPriceGreaterThan = 10000
                                //item.successThreshold = "3"
                            }
                        })
                    }
                }


                Item { Layout.fillWidth: true }

                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignBottom
                    Button {
                        text: "Aktien laden"
                        onClicked: {
                            let firstItem = firstPeriodLoader.item
                            let secondItem = secondPeriodLoader.item
                            let thirdItem = thirdPeriodLoader.item
                            let fourthItem = fourthPeriodLoader.item
                            let filterItem = filterSelectionLoader.item

                            if (!firstItem || !secondItem || !thirdItem || !fourthItem) {
                                console.warn("âš ï¸ Perioden-Komponenten nicht vollstÃ¤ndig geladen.")
                                return
                            }

                            loadingOverlay.message = "Lade Aktien..."
                            loadingOverlay.running = true

                            Qt.callLater(() => {
                                // Kleiner Timer-Delay, damit das UI das Overlay vorher zeigt
                                Qt.createQmlObject(`
                                    import QtQuick 2.0
                                    Timer {
                                        interval: 50
                                        running: true
                                        repeat: false
                                        onTriggered: {
                                            dbManager.getSharesAsync(
                                                ${firstItem.toDay}, ${activeThreshold(firstItem)}, ${firstItem.greaterThan},
                                                ${secondItem.toDay}, ${activeThreshold(secondItem)}, ${secondItem.greaterThan},
                                                ${thirdItem.toDay}, ${activeThreshold(thirdItem)}, ${thirdItem.greaterThan},
                                                ${fourthItem.toDay}, ${activeThreshold(fourthItem)}, ${fourthItem.greaterThan},
                                                ${filterItem.salesPriceGreaterThan},
                                                ${getSortPeriodIndex()},
                                                ${filterItem.sortAscCheckBox.checked},""
                                            )
                                            timer.start()
                                        }
                                    }
                                `, mainWindow)
                            })
                            Qt.callLater(() => listView.forceActiveFocus())
                        }
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            background: Rectangle {
                color: "#e8ede9"
                border.color: "#cccccc"
                radius: 2
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Header-Zeile
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label { text: "#"; Layout.preferredWidth: 40; horizontalAlignment: Text.AlignHCenter; font.bold: true }
                    Label {
                        text: "Symbol"
                        Layout.preferredWidth: 120
                        font.bold: true
                    }
                    Label {
                        text: "Name"
                        Layout.preferredWidth: 270
                        horizontalAlignment: Text.AlignLeft
                        font.bold: true
                    }
                    Label { text: "BÃ¶rse"; Layout.preferredWidth: 60; font.bold: true; horizontalAlignment: Text.AlignLeft }
                    Label { text: "Aktualisiert"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "letzter Preis"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "vom Datum"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label {
                        id: labelSortP1
                        property string baseText: "AnstiegePeriode-1(Ges.%)"
                        text: baseText + (currentSortPeriod === 1 ? (currentSortAsc ? "â†‘" : "â†“") : "")
                        Layout.preferredWidth: 190
                        horizontalAlignment: Text.AlignRight
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            hoverEnabled: true
                            onClicked: {
                                if (currentSortPeriod === 1) {
                                    currentSortAsc = !currentSortAsc
                                } else {
                                    currentSortPeriod = 1
                                    currentSortAsc = false
                                }
                                sortStockModelByField("firstPeriodValueInc", currentSortAsc)
                            }

                            ToolTip.visible: containsMouse
                            ToolTip.text: "Sortieren nach prozentueller ErhÃ¶hung"
                            ToolTip.delay: 300
                        }
                    }

                    Label {
                        id: labelSortP2
                        property string baseText: "-Periode-2(Ges.%)"
                        text: baseText + (currentSortPeriod === 2 ? (currentSortAsc ? "â†‘" : "â†“") : "")
                        Layout.preferredWidth: 140
                        horizontalAlignment: Text.AlignRight
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            hoverEnabled: true
                            onClicked: {
                                if (currentSortPeriod === 2) {
                                    currentSortAsc = !currentSortAsc
                                } else {
                                    currentSortPeriod = 2
                                    currentSortAsc = false
                                }
                                sortStockModelByField("secondPeriodValueInc", currentSortAsc)
                            }

                            ToolTip.visible: containsMouse
                            ToolTip.text: "Sortieren nach prozentueller ErhÃ¶hung"
                            ToolTip.delay: 300
                        }
                    }

                    Label {
                        id: labelSortP3
                        property string baseText: "-Periode-3(Ges.%)"
                        text: baseText + (currentSortPeriod === 3 ? (currentSortAsc ? "â†‘" : "â†“") : "")
                        Layout.preferredWidth: 140
                        horizontalAlignment: Text.AlignRight
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            hoverEnabled: true
                            onClicked: {
                                if (currentSortPeriod === 3) {
                                    currentSortAsc = !currentSortAsc
                                } else {
                                    currentSortPeriod = 3
                                    currentSortAsc = false
                                }
                                sortStockModelByField("thirdPeriodValueInc", currentSortAsc)
                            }

                            ToolTip.visible: containsMouse
                            ToolTip.text: "Sortieren nach prozentueller ErhÃ¶hung"
                            ToolTip.delay: 300
                        }
                    }

                    Label {
                        id: labelSortP4
                        property string baseText: "-Periode-4(Ges.%)"
                        text: baseText + (currentSortPeriod === 4 ? (currentSortAsc ? "â†‘" : "â†“") : "")
                        Layout.preferredWidth: 140
                        horizontalAlignment: Text.AlignRight
                        font.bold: true

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            hoverEnabled: true
                            onClicked: {
                                if (currentSortPeriod === 4) {
                                    currentSortAsc = !currentSortAsc
                                } else {
                                    currentSortPeriod = 4
                                    currentSortAsc = false
                                }
                                sortStockModelByField("fourthPeriodValueInc", currentSortAsc)
                            }

                            ToolTip.visible: containsMouse
                            ToolTip.text: "Sortieren nach prozentueller ErhÃ¶hung"
                            ToolTip.delay: 300
                        }
                    }
                    Label { text: "UmsatzP-1"; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; font.bold: true }
                    Label { text: "Volumen5T"; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; font.bold: true }
                    Label { text: "Volumen10T"; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; font.bold: true }
                    Label { text: "Volumen20T"; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; font.bold: true }
                    Label { text: "Volumen40T"; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; font.bold: true }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 2
                    color: "#aaaaaa"
                }

                // ListView mit Scrollbar
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: listView
                        anchors.fill: parent
                        model: stockModel
                        clip: true
                        focus: true
                        // Ihre ListView-Konfiguration
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            interactive: true
                        }
                        Keys.onPressed: function(event) {
                            let newIndex = currentListViewIndex;
                            const itemsPerPage = Math.floor(listView.height / 40); // ZeilenhÃ¶he ist 40

                            switch (event.key) {
                                case Qt.Key_Up:
                                    if (currentListViewIndex > 0) newIndex--;
                                    break;
                                case Qt.Key_Down:
                                    if (currentListViewIndex < listView.count - 1) newIndex++;
                                    break;
                                case Qt.Key_PageUp:
                                    newIndex = Math.max(0, currentListViewIndex - itemsPerPage);
                                    break;
                                case Qt.Key_PageDown:
                                    newIndex = Math.min(listView.count - 1, currentListViewIndex + itemsPerPage);
                                    break;
                                case Qt.Key_Home:
                                    newIndex = 0;
                                    break;
                                case Qt.Key_End:
                                    newIndex = listView.count - 1;
                                    break;
                                case Qt.Key_Space:
                                    event.accepted = true;
                                    if (currentListViewIndex >= 0) {
                                        toggleSelection(currentListViewIndex);
                                        updateSelectedItems();
                                    }
                                    return;
                                default:
                                    return;
                            }

                            if (newIndex !== currentListViewIndex) {
                                const previousIndex = currentListViewIndex;
                                currentListViewIndex = newIndex;
                                listView.currentIndex = newIndex;

                                if (event.modifiers & Qt.ShiftModifier) {
                                    if (selectionAnchor === -1) {
                                        selectionAnchor = previousIndex;
                                        if (!(event.modifiers & Qt.ControlModifier)) {
                                            selectedIndices = [previousIndex];
                                        }
                                    }
                                    selectRange(selectionAnchor, newIndex, event.modifiers & Qt.ControlModifier);
                                } else {
                                    selectionAnchor = -1;
                                }

                                // Scrollen, damit die Zeile sichtbar bleibt
                                listView.positionViewAtIndex(newIndex, ListView.Contain);
                            }

                            event.accepted = true;
                        }

                        function calculateItemsPerPage() {
                            return Math.max(1, Math.floor(listView.height / 40)); // 40 ist die ZeilenhÃ¶he
                        }

                        function selectSingle(index) {
                            selectedIndices = [index];
                            selectedSymbol = stockModel.get(index).symbol;
                            selectedExchange = stockModel.get(index).mic;
                            updateSelectedItems();
                        }

                        function navigateAndSelect(newIndex, event) {
                            const previousIndex = listView.currentIndex;

                            if (event.modifiers & Qt.ShiftModifier) {
                                if (selectionAnchor === -1) {
                                    selectionAnchor = previousIndex;
                                    selectedIndices = [previousIndex, newIndex];
                                } else {
                                    selectRange(selectionAnchor, newIndex);
                                }
                            } else {
                                selectionAnchor = -1;
                                if (!(event.modifiers & Qt.ControlModifier)) {
                                    selectedIndices = [newIndex];
                                }
                            }

                            listView.currentIndex = newIndex;
                            selectedSymbols = selectedIndices.map(i => stockModel.get(i).symbol);
                            selectedExchanges = selectedIndices.map(i => stockModel.get(i).mic);
                            listView.positionViewAtIndex(newIndex, ListView.Contain);
                        }

                        function handleKeyboardSelection(event) {
                            if (event.modifiers & Qt.ShiftModifier) {
                                if (selectionAnchor === -1) {
                                    selectionAnchor = currentListViewIndex;
                                    selectedIndices = [currentListViewIndex];
                                }
                                selectRange(selectionAnchor, currentListViewIndex);
                            } else {
                                selectionAnchor = -1;
                                if (!(event.modifiers & Qt.ControlModifier)) {
                                    selectedIndices = [currentListViewIndex];
                                }
                            }
                            updateSelectedItems();
                        }

                        function selectAll() {
                            selectedIndices = [];
                            selectedSymbols = [];
                            selectedExchanges = [];

                            for (let i = 0; i < stockModel.count; i++) {
                                selectedIndices.push(i);
                                selectedSymbols.push(stockModel.get(i).symbol);
                                selectedExchanges.push(stockModel.get(i).mic);
                            }

                            loadStockQuotesButton.enabled = selectedIndices.length > 0;
                            console.log("âœ… Alle selektiert: ", selectedSymbols.length + " Items");
                        }

                        Component.onCompleted: {
                            forceActiveFocus()
                            if (stockModel.count > 0) currentIndex = 0
                        }

                        highlight: Rectangle {
                        }

                        //highlightFollowsCurrentItem: true
                        highlightMoveDuration: 0
                        delegate: Item {
                            width: listView.width
                            height: 40
                            // Hintergrund für Selektion
                            Rectangle {
                                id: bgRect
                                anchors.fill: parent
                                color: model.selected ? "lightsteelblue" : "transparent"
                                radius: 2
                            }

                            // Fokus-Rahmen (sichtbar für aktuellen Fokus)
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.color: listView.currentIndex === index ? "steelblue" : "transparent"
                                border.width: 2
                                radius: 2
                            }

                            RowLayout {
                                spacing: 1

                                Label {
                                    text: (index + 1).toString()
                                    Layout.preferredWidth: 40
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Label {
                                    text: model.symbol
                                    Layout.preferredWidth: 120
                                    horizontalAlignment: Text.AlignLeft
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.RightButton
                                        onClicked: {
                                            clipboardHelper.setText(model.symbol);
                                            textItem.text = "Symbol \"" + model.symbol + "\" wurde kopiert";
                                            clipboardPopup.visible = true;
                                            clipboardTimer.restart();
                                            console.log("Symbol kopiert: " + model.symbol);
                                        }
                                    }                                }
                                Label {
                                    text: model.name
                                    Layout.preferredWidth: 270
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.RightButton
                                        onClicked: {
                                            clipboardHelper.setText(model.name);
                                            textItem.text = "Name \"" + model.name + "\" wurde kopiert";
                                            clipboardPopup.visible = true;
                                            clipboardTimer.restart();
                                            console.log("Name kopiert: " + model.name);
                                        }
                                    }                                }
                                Label {
                                    text: model.mic
                                    Layout.preferredWidth: 60
                                    horizontalAlignment: Text.AlignLeft
                                }
                                Label {
                                    text:model.lastUpdateDate
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Label {
                                    text: model.lastClosePrice
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignRight
                                }
                                Label {
                                    text: model.lastClosePriceDate
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Label {
                                    text: model.daysFirstPeriodSuccess + "(" + model.firstPeriodValueInc.toFixed(2) + "%)"
                                    Layout.preferredWidth: 190
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Label {
                                    text: model.daysSecondPeriodSuccess + "(" + model.secondPeriodValueInc.toFixed(2) + "%)"
                                    Layout.preferredWidth: 140
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Label {
                                    text: model.daysThirdPeriodSuccess + "(" + model.thirdPeriodValueInc.toFixed(2) + "%)"
                                    Layout.preferredWidth: 140
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Label {
                                    text: model.daysFourthPeriodSuccess + "(" + model.fourthPeriodValueInc.toFixed(2) + "%)"
                                    Layout.preferredWidth: 140
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Label {
                                    text: model.firstPeriodVolumePrice
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignRight
                                }
                                Label {
                                    text: model.firstPeriodVolume
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignRight
                                }
                                Label {
                                    text: model.secondPeriodVolume
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignRight
                                }
                                Label {
                                    text: model.thirdPeriodVolume
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignRight
                                }
                                Label {
                                    text: model.fourthPeriodVolume
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: function(mouse) {
                                    const clickedIndex = index;
                                    currentListViewIndex = clickedIndex;
                                    listView.currentIndex = clickedIndex;
                                    listView.forceActiveFocus();

                                    if (mouse.modifiers & Qt.ShiftModifier) {
                                        if (selectionAnchor === -1) {
                                            selectionAnchor = currentListViewIndex;
                                        }
                                        selectRange(selectionAnchor, clickedIndex, mouse.modifiers & Qt.ControlModifier);
                                    } else if (mouse.modifiers & Qt.ControlModifier) {
                                        toggleSelection(clickedIndex);
                                    } else {
                                        selectedIndices = [clickedIndex];
                                        selectionAnchor = clickedIndex;
                                    }
                                    updateSelectedItems();
                                }
                                onDoubleClicked: function(mouse) {
                                    mouse.accepted = true;
                                    currentListViewIndex = index;
                                    listView.currentIndex = index;
                                    if (!selectedIndices.includes(index)) {
                                        selectedIndices = [index];
                                        selectionAnchor = index;
                                        updateSelectedItems();
                                    }
                                    openDetailWindow(index);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
