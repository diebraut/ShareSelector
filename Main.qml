import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.LocalStorage 2.15

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1600
    height: 1220
    minimumWidth: 1200
    minimumHeight: 1100
    title: "Stock Analyse"

    property var dbManager: databaseManager

    Component.onCompleted: Qt.callLater(function() {
        loadIbkrQuoteSchedule()
        loadStockAnalysisConfigs()
        loadLastStockAnalysisConfig()
    })

    Timer {
        id: ibkrQuoteScheduleTimer
        interval: 30000
        running: true
        repeat: true
        onTriggered: checkIbkrQuoteSchedule()
    }
    // Datenmodell
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

    property int selectedPortfolioIndex: -1
    property string portfolioSortKey: "gainPercent"
    property bool portfolioSortAscending: true
    property var portfolioRows: []
    property double portfolioTotalCurrentAmount: 0
    property double portfolioTotalEntryAmount: 0
    property int portfolioActiveCountValue: 0
    property bool portfolioLoaded: false
    property var portfolioDetails: ({})
    property string portfolioDetailsSymbol: ""
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
    property bool stockAnalysisHideBoughtStocks: true
    property bool ibkrQuoteScheduleEnabled: false
    property string ibkrQuoteScheduleTime: "18:00"
    property string ibkrQuoteScheduleLastRunDate: ""
    property string ibkrTradingAppPath: ""
    property string ibkrQuoteScheduleStatus: "Automatischer IBKR-Quote-Batch ist nicht aktiv."
    property bool ibkrQuoteSchedulePendingStart: false
    property bool ibkrQuoteSchedulePendingManualStart: false
    property bool ibkrQuoteScheduleTradingAppStarted: false
    property double ibkrQuoteSchedulePendingUntilMs: 0

    function twoDigits(value) {
        value = Number(value)
        return value < 10 ? "0" + value : "" + value
    }

    function todayIsoDate(date) {
        return date.getFullYear() + "-" + twoDigits(date.getMonth() + 1) + "-" + twoDigits(date.getDate())
    }

    function parseIbkrQuoteScheduleMinutes(timeText) {
        let match = /^([01][0-9]|2[0-3]):([0-5][0-9])$/.exec(String(timeText).trim())
        if (!match)
            return -1
        return Number(match[1]) * 60 + Number(match[2])
    }

    function isActiveTradingDay(date) {
        let day = date.getDay()
        return day >= 1 && day <= 5
    }

    function canRequestIbkrQuoteBatchStart() {
        return !dbManager.ibkrDataLoading
            && !dbManager.ibkrBatchActive
            && !dbManager.ibkrNameCheckBatchActive
            && !dbManager.ibkrGetStocksActive
    }

    function canStartIbkrQuoteBatch() {
        return dbManager.ibkrConnected && canRequestIbkrQuoteBatchStart()
    }

    function canProbeIbkrGateway() {
        return !dbManager.ibkrConnecting
            && !dbManager.ibkrDataLoading
            && !dbManager.ibkrBatchActive
            && !dbManager.ibkrNameCheckBatchActive
            && !dbManager.ibkrGetStocksActive
    }

    function checkIbkrGatewayOnly() {
        if (!canProbeIbkrGateway()) {
            ibkrQuoteScheduleStatus = "Gateway/API-Pruefung kann gerade nicht gestartet werden."
            return false
        }
        ibkrQuoteScheduleStatus = "Prüfe, ob IB Gateway/TWS bereits läuft und per API erreichbar ist ..."
        dbManager.connectToIbkr()
        return true
    }

    function loadIbkrQuoteSchedule() {
        ibkrQuoteScheduleEnabled = dbManager.appSetting("ibkrQuoteScheduleEnabled") === "1"
        let savedTime = dbManager.appSetting("ibkrQuoteScheduleTime")
        if (parseIbkrQuoteScheduleMinutes(savedTime) >= 0)
            ibkrQuoteScheduleTime = savedTime
        ibkrQuoteScheduleLastRunDate = dbManager.appSetting("ibkrQuoteScheduleLastRunDate")
        ibkrTradingAppPath = dbManager.appSetting("ibkrTradingAppPath")
        if (ibkrTradingAppPath.length === 0)
            ibkrTradingAppPath = "K:/Jts/ibgateway/1049/ibgateway.exe"
        updateIbkrQuoteScheduleStatus()
        checkIbkrQuoteSchedule()
    }

    function saveIbkrQuoteSchedule() {
        let minutes = parseIbkrQuoteScheduleMinutes(ibkrQuoteScheduleTime)
        if (minutes < 0) {
            ibkrQuoteScheduleStatus = "Bitte Uhrzeit im Format HH:MM eingeben."
            return false
        }

        dbManager.saveAppSetting("ibkrQuoteScheduleEnabled", ibkrQuoteScheduleEnabled ? "1" : "0")
        dbManager.saveAppSetting("ibkrQuoteScheduleTime", ibkrQuoteScheduleTime)
        dbManager.saveAppSetting("ibkrTradingAppPath", ibkrTradingAppPath)
        updateIbkrQuoteScheduleStatus()
        return true
    }

    function updateIbkrQuoteScheduleStatus() {
        let minutes = parseIbkrQuoteScheduleMinutes(ibkrQuoteScheduleTime)
        if (minutes < 0) {
            ibkrQuoteScheduleStatus = "Bitte Uhrzeit im Format HH:MM eingeben."
            return
        }

        if (!ibkrQuoteScheduleEnabled) {
            ibkrQuoteScheduleStatus = "Automatischer IBKR-Quote-Batch ist nicht aktiv."
            return
        }

        let now = new Date()
        let today = todayIsoDate(now)
        if (!isActiveTradingDay(now)) {
            ibkrQuoteScheduleStatus = "Heute kein aktiver Handelstag (Mo-Fr). Geplant: " + ibkrQuoteScheduleTime + " Uhr."
        } else if (ibkrQuoteScheduleLastRunDate === today) {
            ibkrQuoteScheduleStatus = "Heute bereits gestartet. Geplant: " + ibkrQuoteScheduleTime + " Uhr."
        } else {
            ibkrQuoteScheduleStatus = "Naechster automatischer Start heute um " + ibkrQuoteScheduleTime + " Uhr."
        }
    }

    function startIbkrQuoteBatchFromSchedule(manualStart) {
        if (!canRequestIbkrQuoteBatchStart()) {
            ibkrQuoteScheduleStatus = "IBKR Get Quotes kann gerade nicht gestartet werden."
            return false
        }

        if (!dbManager.ibkrConnected) {
            if (!ibkrQuoteSchedulePendingStart) {
                ibkrQuoteScheduleTradingAppStarted = false
                ibkrQuoteSchedulePendingUntilMs = Date.now() + 30 * 60 * 1000
            }
            ibkrQuoteSchedulePendingStart = true
            ibkrQuoteSchedulePendingManualStart = manualStart

            let programPath = ibkrTradingAppPath.trim()
            if (programPath.length > 0 && !ibkrQuoteScheduleTradingAppStarted) {
                ibkrQuoteScheduleTradingAppStarted = dbManager.startIbkrTradingApp(programPath)
            }

            continuePendingIbkrQuoteBatchStart()
            return true
        }

        dbManager.startIbkrGetStocks()
        Qt.callLater(function() {
            if (dbManager.ibkrConnectionStatus && dbManager.ibkrConnectionStatus.length > 0)
                ibkrQuoteScheduleStatus = dbManager.ibkrConnectionStatus
        })
        if (!manualStart) {
            ibkrQuoteScheduleLastRunDate = todayIsoDate(new Date())
            dbManager.saveAppSetting("ibkrQuoteScheduleLastRunDate", ibkrQuoteScheduleLastRunDate)
        }
        ibkrQuoteSchedulePendingStart = false
        ibkrQuoteScheduleTradingAppStarted = false
        ibkrQuoteScheduleStatus = manualStart
            ? "IBKR Get Quotes manuell gestartet."
            : "IBKR Get Quotes automatisch gestartet."
        return true
    }

    function checkIbkrQuoteSchedule() {
        if (ibkrQuoteSchedulePendingStart) {
            continuePendingIbkrQuoteBatchStart()
            return
        }

        if (!ibkrQuoteScheduleEnabled) {
            updateIbkrQuoteScheduleStatus()
            return
        }

        let minutes = parseIbkrQuoteScheduleMinutes(ibkrQuoteScheduleTime)
        if (minutes < 0) {
            updateIbkrQuoteScheduleStatus()
            return
        }

        let now = new Date()
        let today = todayIsoDate(now)
        if (!isActiveTradingDay(now) || ibkrQuoteScheduleLastRunDate === today) {
            updateIbkrQuoteScheduleStatus()
            return
        }

        let currentMinutes = now.getHours() * 60 + now.getMinutes()
        if (currentMinutes >= minutes) {
            startIbkrQuoteBatchFromSchedule(false)
        } else {
            updateIbkrQuoteScheduleStatus()
        }
    }

    function continuePendingIbkrQuoteBatchStart() {
        if (!ibkrQuoteSchedulePendingStart)
            return

        if (dbManager.ibkrConnected) {
            startIbkrQuoteBatchFromSchedule(ibkrQuoteSchedulePendingManualStart)
            return
        }

        if (Date.now() > ibkrQuoteSchedulePendingUntilMs) {
            ibkrQuoteSchedulePendingStart = false
            ibkrQuoteScheduleTradingAppStarted = false
            ibkrQuoteScheduleStatus = "IBKR-Verbindung nach 30 Minuten nicht erreichbar. Anmeldung/API bitte prüfen."
            return
        }

        ibkrQuoteScheduleStatus = "Warte auf IB-Gateway-Anmeldung und API-Verbindung ..."
        if (!dbManager.ibkrConnecting)
            dbManager.connectToIbkr()
    }

    Connections {
        target: dbManager
        function onIbkrConnectionChanged() {
            continuePendingIbkrQuoteBatchStart()
        }
    }
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
        { key: "exchange", label: "Börse" },
        { key: "countryCode", label: "Ländercode" },
        { key: "city", label: "Börsenstadt" },

        { heading: true, label: "IBKR-Stammdaten" },
        { key: "ibkrConId", label: "IBKR ConId" },
        { key: "currency", label: "Währung" },
        { key: "primaryExchange", label: "Primärbörse" },
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
        { key: "validExchanges", label: "Gültige Handelsplätze" },
        { key: "marketName", label: "Marktname" },
        { key: "cusip", label: "CUSIP" },
        { key: "ibkrLastSyncAt", label: "Letzte IBKR-Synchronisierung" },

        { heading: true, label: "Kennzahlen" },
        { key: "asOfDate", label: "Stichtag" },
        { key: "fundamentalCurrency", label: "Kennzahlenwährung" },
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
        { key: "payoutRatio", label: "Ausschüttungsquote", format: "percent" },
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
        id: stockAnalysisChartRefreshTimer
        interval: 80
        running: false
        repeat: false
        onTriggered: stockAnalysisChart.requestPaint()
    }
    Timer {
        id: portfolioDetailsLoadTimer
        interval: 120
        running: false
        repeat: false
        onTriggered: loadSelectedPortfolioDetails()
    }

    Timer {
        id: stockAnalysisScanTimer
        interval: 1
        running: false
        repeat: true
        onTriggered: processStockAnalysisScanBatch()
    }

    Connections {
        target: dbManager
        function onIbkrStockDataUpdated(symbol) {
            loadTestPortfolio(symbol)
        }
        function onFundamentalDataUpdated(symbol) {
            loadTestPortfolio(symbol)
        }
    }

    function parseDecimal(text) {
        let value = Number(String(text).replace(",", "."))
        return isNaN(value) ? 0 : value
    }

    function loadTestPortfolio(preferredSymbol) {
        const data = dbManager.getTestPortfolioSummary()
        let rows = []
        data.forEach(item => rows.push(item))
        portfolioRows = rows
        updatePortfolioTotals()
        portfolioLoaded = true
        portfolioDetails = ({})
        portfolioDetailsSymbol = ""
        rebuildPortfolioModel(preferredSymbol, true)
    }

    function sortedPortfolioRows() {
        let rows = portfolioRows.slice()
        if (portfolioSortKey.length === 0)
            return rows

        rows.sort((a, b) => {
            let valueA = 0
            let valueB = 0
            if (portfolioSortKey === "totalValue") {
                valueA = portfolioPositionTotalValue(a)
                valueB = portfolioPositionTotalValue(b)
            } else if (portfolioSortKey === "gainPercent") {
                valueA = Number(a.valueIncreasePercent || 0)
                valueB = Number(b.valueIncreasePercent || 0)
            }

            if (valueA === valueB)
                return String(a.name || a.symbol || "").localeCompare(String(b.name || b.symbol || ""))
            return portfolioSortAscending ? valueA - valueB : valueB - valueA
        })
        return rows
    }

    function rebuildPortfolioModel(preferredSymbol, refreshDetails, scrollToTop) {
        portfolioModel.clear()
        let preferredIndex = -1
        sortedPortfolioRows().forEach((item, index) => {
            portfolioModel.append(item)
            if (preferredSymbol && item.symbol === preferredSymbol)
                preferredIndex = index
        })
        selectedPortfolioIndex = scrollToTop && portfolioModel.count > 0
            ? 0
            : (preferredIndex >= 0 ? preferredIndex : (portfolioModel.count > 0 ? 0 : -1))
        if (scrollToTop) {
            Qt.callLater(function() {
                portfolioWindow.positionListAtBeginning()
            })
        }
        if (refreshDetails !== false)
            portfolioDetailsLoadTimer.restart()
    }

    function schedulePortfolioDetailsLoad() {
        portfolioDetailsLoadTimer.restart()
    }

    function loadSelectedPortfolioDetails() {
        if (selectedPortfolioIndex < 0 || selectedPortfolioIndex >= portfolioModel.count) {
            portfolioDetails = ({})
            portfolioDetailsSymbol = ""
            return
        }

        const row = portfolioModel.get(selectedPortfolioIndex)
        const symbol = row.symbol || ""
        if (symbol === "" || portfolioDetailsSymbol === symbol)
            return

        const details = dbManager.getPortfolioDetails(symbol)
        if (selectedPortfolioIndex >= 0
                && selectedPortfolioIndex < portfolioModel.count
                && portfolioModel.get(selectedPortfolioIndex).symbol === symbol) {
            portfolioDetails = details
            portfolioDetailsSymbol = symbol
        }
    }

    function portfolioSortIcon(key) {
        if (portfolioSortKey !== key)
            return ""
        return portfolioSortAscending ? "\u25b2" : "\u25bc"
    }

    function sortPortfolioBy(key) {
        if (portfolioSortKey === key)
            portfolioSortAscending = !portfolioSortAscending
        else {
            portfolioSortKey = key
            portfolioSortAscending = true
        }
        rebuildPortfolioModel("", false, true)
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

    function updatePortfolioTotals() {
        let currentTotal = 0
        let entryTotal = 0
        let activeCount = 0
        portfolioRows.forEach(row => {
            const quantity = portfolioPositionQuantity(row)
            currentTotal += quantity * Number(row.currentValue || 0)
            entryTotal += quantity * Number(row.entryValue || 0)
            if (Number(row.status || 0) !== 10)
                activeCount++
        })
        portfolioTotalCurrentAmount = currentTotal
        portfolioTotalEntryAmount = entryTotal
        portfolioActiveCountValue = activeCount
    }
    function selectedPortfolioValue(key) {
        if (selectedPortfolioIndex < 0 || selectedPortfolioIndex >= portfolioModel.count)
            return ""
        const row = portfolioModel.get(selectedPortfolioIndex)
        if (portfolioDetailsSymbol === row.symbol
                && portfolioDetails[key] !== undefined
                && portfolioDetails[key] !== null)
            return portfolioDetails[key]
        return row[key] === undefined || row[key] === null ? "" : row[key]
    }
    function portfolioTotalCurrentValue() {
        return portfolioTotalCurrentAmount
    }

    function portfolioTotalEntryValue() {
        return portfolioTotalEntryAmount
    }

    function portfolioTotalGainValue() {
        return portfolioTotalCurrentAmount - portfolioTotalEntryAmount
    }

    function portfolioStatusCount(sold) {
        return sold ? portfolioRows.length - portfolioActiveCountValue : portfolioActiveCountValue
    }

    function portfolioPerformancePercent() {
        return portfolioTotalEntryAmount > 0 ? (portfolioTotalCurrentAmount - portfolioTotalEntryAmount) / portfolioTotalEntryAmount * 100 : 0
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

    // Properties für die Mehrfachselektion

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
        let configName = stockAnalysisConfigPanel.configNameText.trim()
        let increasePercent = Number(stockAnalysisConfigPanel.increaseText.replace(",", "."))
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
        stockAnalysisConfigPanel.configNameText = cfg.name
        stockAnalysisConfigPanel.increaseText = String(cfg.increasePercent)
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
        stockAnalysisConfigPanel.configNameText = ""
        stockAnalysisMessage = "Neue Konfiguration"
    }


    function resetStockAnalysisSelectionState() {
        selectedStockAnalysisIndex = -1
        selectedStockAnalysisRows = []
        stockAnalysisSelectionAnchor = -1
        stockAnalysisStockSelected = false
        stockAnalysisQuoteModel.clear()
        stockAnalysisQuoteDateRangeText = ""
        stockAnalysisCorridorHitPercent = 0
        stockAnalysisActualIncreasePercent = 0
        stockAnalysisRequiredCorridorPercent = 0
        stockAnalysisActualMaxDrawdownPercent = 0
        stockAnalysisChartRefreshTimer.restart()
    }

    function resetStockAnalysisForDirectSearch(active) {
        stockAnalysisResultModel.clear()
        resetStockAnalysisSelectionState()
        stockAnalysisMessage = active ? "Direktsuche aktiviert" : "Direktsuche deaktiviert"
    }
    function stockAnalysisCandidateVisible(row) {
        if (!stockAnalysisHideBoughtStocks)
            return true
        return !dbManager.isBoughtStock(row.symbol || "")
    }

    function removeBoughtStocksFromStockAnalysisResults() {
        let removed = 0
        for (let i = stockAnalysisResultModel.count - 1; i >= 0; i--) {
            let row = stockAnalysisResultModel.get(i)
            if (dbManager.isBoughtStock(row.symbol || "")) {
                stockAnalysisResultModel.remove(i)
                removed++
            }
        }
        if (removed > 0)
            resetStockAnalysisSelectionState()
        return removed
    }

    function removeStockAnalysisResultsBySymbols(symbols) {
        if (!stockAnalysisHideBoughtStocks || symbols.length === 0)
            return 0
        let normalizedSymbols = symbols.map(symbol => String(symbol || ""))
        let removed = 0
        for (let i = stockAnalysisResultModel.count - 1; i >= 0; i--) {
            let row = stockAnalysisResultModel.get(i)
            if (normalizedSymbols.indexOf(String(row.symbol || "")) >= 0) {
                stockAnalysisResultModel.remove(i)
                removed++
            }
        }
        if (removed > 0)
            resetStockAnalysisSelectionState()
        return removed
    }

    function runStockAnalysis() {
        let increasePercent = Number(stockAnalysisConfigPanel.increaseText.replace(",", "."))
        if (isNaN(increasePercent)) {
            stockAnalysisMessage = "Steigerung um % ist ungueltig"
            return
        }

        stockAnalysisResultModel.clear()
        resetStockAnalysisSelectionState()
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
            if (stockAnalysisCandidateVisible(row)
                    && trendIncreasePercent >= increasePercent
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

    function startStockAnalysisSearch() {
        if (stockAnalysisConfigPanel.directSearchActive) {
            runStockAnalysisDirectSearch()
            return
        }
        startStockAnalysisScan()
    }

    function runStockAnalysisDirectSearch() {
        let isinText = stockAnalysisConfigPanel.directSearchIsinText.trim()
        let nameText = stockAnalysisConfigPanel.directSearchNameText.trim()

        if (isinText.length === 0 && nameText.length === 0) {
            stockAnalysisMessage = "Direktsuche: Bitte ISIN oder Name eingeben"
            return
        }

        stockAnalysisScanTimer.stop()
        stockAnalysisScanActive = false
        stockAnalysisResultModel.clear()
        resetStockAnalysisSelectionState()

        let rows = dbManager.findStockAnalysisDirectSearchStocks(isinText, nameText)
        let found = 0

        rows.forEach(baseRow => {
            let row = dbManager.getStockAnalysisCandidate(baseRow.symbol || "", -1000000, stockAnalysisQuoteCount)
            if (row.symbol === undefined || row.symbol === "") {
                row = {
                    symbol: baseRow.symbol || "",
                    isin: baseRow.isin || "",
                    name: baseRow.name || "",
                    mic: baseRow.mic || "",
                    quotesource: baseRow.quotesource || "-",
                    increasepercent: 0,
                    firstquotedate: "",
                    firstcloseprice: 0,
                    lastquotedate: "",
                    lastcloseprice: 0,
                    periodturnover: 0,
                    totalquotecount: 0,
                    quotecount: 0,
                    revenue: 0,
                    peratio: "",
                    corridorhitpercent: 0,
                    maxdrawdownpercent: 0
                }
            }

            let quotes = dbManager.getQuoteDetails(row.symbol, 1, stockAnalysisQuoteCount)
            if (quotes.length > 0) {
                let newestQuote = quotes[0]
                let oldestQuote = quotes[quotes.length - 1]
                let turnover = 0
                quotes.forEach(quote => {
                    turnover += Number(quote.volume || 0) * Number(quote.closeprice || 0)
                })

                row.firstquotedate = oldestQuote.closedate || row.firstquotedate || ""
                row.firstcloseprice = Number(oldestQuote.closeprice || row.firstcloseprice || 0)
                row.lastquotedate = newestQuote.closedate || row.lastquotedate || ""
                row.lastcloseprice = Number(newestQuote.closeprice || row.lastcloseprice || 0)
                row.periodturnover = Number(row.periodturnover || turnover)
                row.totalquotecount = Number(row.totalquotecount || quotes.length)
                row.quotecount = quotes.length
                row.increasepercent = trendIncreasePercentForQuotes(quotes)
                row.corridorhitpercent = corridorHitPercentForQuotes(quotes)
                row.maxdrawdownpercent = maxDrawdownForQuotes(quotes).percent
            }

            stockAnalysisResultModel.append(row)
            found++
        })

        stockAnalysisMessage = found + " Treffer in der Direktsuche"
    }
    function startStockAnalysisScan() {
        let increasePercent = Number(stockAnalysisConfigPanel.increaseText.replace(",", "."))
        if (isNaN(increasePercent)) {
            stockAnalysisMessage = "Steigerung um % ist ungueltig"
            return
        }

        stockAnalysisResultModel.clear()
        resetStockAnalysisSelectionState()
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
            + " geprüft, " + stockAnalysisScanFound + " gefunden"
    }

    function processStockAnalysisScanBatch() {
        let increasePercent = Number(stockAnalysisConfigPanel.increaseText.replace(",", "."))
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

                    if (stockAnalysisCandidateVisible(candidate)
                            && trendIncreasePercent >= increasePercent
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

        stockAnalysisMessage = "Stock-Analyse läuft: " + stockAnalysisScanIndex + "/" + stockAnalysisScanSymbols.length
            + " geprüft, " + stockAnalysisScanFound + " gefunden"

        if (stockAnalysisScanIndex >= stockAnalysisScanSymbols.length) {
            stockAnalysisScanTimer.stop()
            stockAnalysisScanActive = false
            sortStockAnalysisResults()
            stockAnalysisMessage = "Stock-Analyse abgeschlossen: " + stockAnalysisScanIndex + "/" + stockAnalysisScanSymbols.length
                + " geprüft, " + stockAnalysisScanFound + " gefunden"
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
        stockAnalysisBuyDialog.amountText = "1000"
        stockAnalysisBuyDialog.dateText = stockAnalysisOldestCommonBuyDate(rows)
        stockAnalysisBuyDialog.errorText = ""
        stockAnalysisBuyDialog.open()
    }


    function currentStockAnalysisConfigName() {
        if (selectedStockAnalysisConfigIndex >= 0
                && selectedStockAnalysisConfigIndex < stockAnalysisConfigModel.count)
            return stockAnalysisConfigModel.get(selectedStockAnalysisConfigIndex).name || ""
        return stockAnalysisConfigPanel.configNameText.trim()
    }

    function buySelectedStockAnalysisStocks() {
        let amount = parseDecimal(stockAnalysisBuyDialog.amountText)
        let buyDate = stockAnalysisBuyDialog.dateText.trim()
        if (amount <= 0) {
            stockAnalysisBuyDialog.errorText = "Bitte einen Betrag größer 0 eintragen"
            return
        }
        if (!/^\d{4}-\d{2}-\d{2}$/.test(buyDate)) {
            stockAnalysisBuyDialog.errorText = "Datum bitte als JJJJ-MM-TT eintragen"
            return
        }

        let saved = 0
        let boughtSymbols = []
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
            if (ok) {
                saved++
                boughtSymbols.push(stock.symbol)
            }
        })

        stockAnalysisBuyDialog.close()
        let removed = stockAnalysisHideBoughtStocks ? removeBoughtStocksFromStockAnalysisResults() : 0
        if (removed === 0)
            removed = removeStockAnalysisResultsBySymbols(boughtSymbols)
        stockAnalysisMessage = saved + " von " + stockAnalysisBuyDialog.stocks.length + " Positionen gekauft"
            + (removed > 0 ? ", " + removed + " aus Liste entfernt" : "")
        loadTestPortfolio("")
    }

    function openPortfolioBatchWindow() {
        portfolioBatchWindow.show()
        portfolioBatchWindow.raise()
        portfolioBatchWindow.requestActivate()
    }

    function openPortfolioWindow() {
        portfolioWindow.show()
        portfolioWindow.raise()
        portfolioWindow.requestActivate()
    }
    function openPortfolioChartWindow(row) {
        if (!row || !row.symbol)
            return

        portfolioChartWindow.openForStock(row, dbManager.getPortfolioChartData(row.symbol))
    }
    function showPortfolioStockInAnalysis(row) {
        if (!row || !row.symbol)
            return

        let targetIndex = -1
        for (let i = 0; i < stockAnalysisResultModel.count; i++) {
            if (stockAnalysisResultModel.get(i).symbol === row.symbol) {
                targetIndex = i
                break
            }
        }

        if (targetIndex < 0) {
            stockAnalysisResultModel.append({
                nr: stockAnalysisResultModel.count + 1,
                symbol: row.symbol,
                isin: row.isin || "",
                name: row.name || row.symbol,
                turnover: 0,
                peratio: row.fundamentalPERatio || "",
                quotecount: stockAnalysisQuoteCount,
                ibkrsource: "Depot",
                corridorhitpercent: 0,
                increasepercent: Number(row.valueIncreasePercent || 0),
                maxdrawdownpercent: 0
            })
            targetIndex = stockAnalysisResultModel.count - 1
        }

        mainWindow.show()
        mainWindow.raise()
        mainWindow.requestActivate()
        selectedStockAnalysisRows = [targetIndex]
        stockAnalysisSelectionAnchor = targetIndex
        selectStockAnalysisResult(targetIndex)
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

    PortfolioChartWindow {
        id: portfolioChartWindow
    }
    PortfolioWindow {
        id: portfolioWindow
        app: mainWindow
        dbManager: mainWindow.dbManager
        portfolioModel: mainWindow.portfolioModel
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

                StockAnalysisConfigPanel {
                    id: stockAnalysisConfigPanel
                    app: mainWindow
                    hostWindow: mainWindow
                    configModel: stockAnalysisConfigModel
                    quoteModel: stockAnalysisQuoteModel
                    Layout.fillWidth: true
                    Layout.preferredHeight: 310
                    Layout.minimumHeight: 300
                }
                GroupBox {
                    id: stockAnalysisResultsGroupBox
                    title: "Gefundene Stocks (" + stockAnalysisResultModel.count + ")"
                    topPadding: 22
                    label: Label {
                        text: stockAnalysisResultsGroupBox.title
                        x: 10
                        y: 0
                        padding: 2
                        font.bold: true
                        background: Rectangle { color: "#f4f6f7" }
                    }
                    background: Rectangle {
                        y: stockAnalysisResultsGroupBox.label.height / 2
                        width: parent.width
                        height: parent.height - y
                        color: "transparent"
                        border.color: "#8b8b8b"
                        border.width: 1
                    }
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
                StockAnalysisChartPanel {
                    id: stockAnalysisChart
                    app: mainWindow
                    resultModel: stockAnalysisResultModel
                    quoteModel: stockAnalysisQuoteModel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: mainWindow.height * 0.42
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
    StockAnalysisBuyDialog {
        id: stockAnalysisBuyDialog
        app: mainWindow
        hostWindow: mainWindow
    }
    PortfolioBatchWindow {
        id: portfolioBatchWindow
        app: mainWindow
        dbManager: mainWindow.dbManager
    }



}
