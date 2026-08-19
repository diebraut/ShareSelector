import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: portfolioBatchWindow
    property var app
    property var dbManager
        title: "Depot-Batchaufrufe"
        width: 900
        height: 600
        minimumWidth: 820
        minimumHeight: 560

        onVisibleChanged: if (visible) {
            app.updateIbkrQuoteScheduleStatus()
            app.checkIbkrGatewayOnly()
        }

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

                Label {
                    text: "Geplanter IBKR-Quote-Batch über IB Gateway"
                    font.bold: true
                    Layout.fillWidth: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 5
                    columnSpacing: 10
                    rowSpacing: 8

                    CheckBox {
                        id: ibkrQuoteScheduleEnabledInput
                        text: "Automatisch an aktiven Handelstagen"
                        checked: app.ibkrQuoteScheduleEnabled
                        Layout.columnSpan: 2
                        onToggled: {
                            app.ibkrQuoteScheduleEnabled = checked
                            app.saveIbkrQuoteSchedule()
                        }
                    }

                    Label {
                        text: "IB Gateway EXE"
                        horizontalAlignment: Text.AlignRight
                        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                    }

                    TextField {
                        id: ibkrTradingAppPathInput
                        text: app.ibkrTradingAppPath
                        placeholderText: "Pfad zu ibgateway.exe (TWS nur alternativ)"
                        selectByMouse: true
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        onEditingFinished: {
                            app.ibkrTradingAppPath = text.trim()
                            dbManager.saveAppSetting("ibkrTradingAppPath", app.ibkrTradingAppPath)
                        }
                    }

                    Button {
                        text: "Starten"
                        Layout.fillWidth: true
                        enabled: ibkrTradingAppPathInput.text.trim().length > 0
                        onClicked: {
                            app.ibkrTradingAppPath = ibkrTradingAppPathInput.text.trim()
                            dbManager.saveAppSetting("ibkrTradingAppPath", app.ibkrTradingAppPath)
                            dbManager.startIbkrTradingApp(app.ibkrTradingAppPath)
                        }
                    }

                    Item { Layout.columnSpan: 4; Layout.fillWidth: true }
                    Button {
                        text: "Gateway/API prüfen"
                        Layout.fillWidth: true
                        enabled: app.canProbeIbkrGateway()
                        onClicked: app.checkIbkrGatewayOnly()
                    }

                    Label {
                        text: "Uhrzeit"
                        horizontalAlignment: Text.AlignRight
                        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                    }

                    TextField {
                        id: ibkrQuoteScheduleTimeInput
                        text: app.ibkrQuoteScheduleTime
                        placeholderText: "HH:MM"
                        selectByMouse: true
                        horizontalAlignment: Text.AlignHCenter
                        inputMask: "99:99"
                        Layout.preferredWidth: 82
                        onEditingFinished: {
                            app.ibkrQuoteScheduleTime = text.trim()
                            app.updateIbkrQuoteScheduleStatus()
                        }
                    }

                    Button {
                        text: "Speichern"
                        Layout.fillWidth: true
                        onClicked: {
                            app.ibkrQuoteScheduleTime = ibkrQuoteScheduleTimeInput.text.trim()
                            app.saveIbkrQuoteSchedule()
                        }
                    }

                    Label {
                        text: app.ibkrQuoteScheduleStatus
                        color: app.ibkrQuoteScheduleStatus.indexOf("Format") >= 0 || app.ibkrQuoteScheduleStatus.indexOf("nicht gestartet") >= 0 ? "#b91c1c" : "#475569"
                        wrapMode: Text.WordWrap
                        Layout.columnSpan: 4
                        Layout.fillWidth: true
                    }

                    Label {
                        text: dbManager.ibkrConnectionStatus
                        color: dbManager.ibkrConnected ? "#15803d" : (dbManager.ibkrConnectionStatus.indexOf("Fehler:") === 0 || dbManager.ibkrConnectionStatus.indexOf("Keine") === 0 ? "#b91c1c" : "#475569")
                        wrapMode: Text.WordWrap
                        Layout.columnSpan: 4
                        Layout.fillWidth: true
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#cbd5e1"
                }

                Label {
                    text: "Manuelle Batchaufrufe"
                    font.bold: true
                    Layout.fillWidth: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8

                    Button { text: "Get New Quotes for Depot starten"; Layout.fillWidth: true; enabled: app.canStartIbkrQuoteBatch(); onClicked: dbManager.startIbkrGetStocks() }
                    Button { text: "Get New Quotes for Depot stoppen"; Layout.fillWidth: true; enabled: dbManager.ibkrGetStocksActive && dbManager.ibkrGetStocksBatchName === "Get New Quotes for Depot"; onClicked: dbManager.stopIbkrGetStocks() }
                    Button { text: "IBKR Gesamtbatch extern starten"; Layout.fillWidth: true; enabled: dbManager.ibkrConnected && !dbManager.ibkrGetStocksActive; onClicked: dbManager.startIbkrQuoteWorkerAll() }
                    Button { text: "Get new Quotes for IBKR Data stoppen"; Layout.fillWidth: true; enabled: dbManager.ibkrGetStocksActive && dbManager.ibkrGetStocksBatchName === "Get new Quotes for IBKR Data"; onClicked: dbManager.stopIbkrGetStocks() }
                    Button { text: "IBKR Stammdaten Batch starten"; Layout.fillWidth: true; enabled: dbManager.ibkrConnected && !dbManager.ibkrDataLoading && !dbManager.ibkrBatchActive && !dbManager.ibkrNameCheckBatchActive && !dbManager.ibkrGetStocksActive; onClicked: dbManager.startIbkrBatch() }
                    Button { text: "IBKR Stammdaten Batch stoppen"; Layout.fillWidth: true; enabled: dbManager.ibkrBatchActive; onClicked: dbManager.stopIbkrBatch() }
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
                    Button { text: "Schließen"; onClicked: portfolioBatchWindow.close() }
                }
            }
        }
    }

