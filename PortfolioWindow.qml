import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: portfolioWindow
    property var app
    property var dbManager
    property var portfolioModel

    function positionListAtBeginning() {
        portfolioListView.positionViewAtBeginning()
    }

    function portfolioListContentY() {
        return portfolioListView.contentY
    }

    function restorePortfolioListContentY(contentY) {
        Qt.callLater(function() {
            const maxY = Math.max(0, portfolioListView.contentHeight - portfolioListView.height)
            portfolioListView.contentY = Math.max(0, Math.min(contentY, maxY))
        })
    }
        title: "Mein Depot (Test)"
        width: 1360
        height: 760
        minimumWidth: 980
        minimumHeight: 520

        onVisibleChanged: {
            if (visible && !app.portfolioLoaded)
                app.loadTestPortfolio()
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
                            Label { text: app.portfolioRows.length + " gesamt, " + app.portfolioActiveCountValue + " aktiv"; font.bold: true; Layout.preferredWidth: 180 }

                            Label { text: "Depotwert"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label { text: app.portfolioTotalCurrentAmount.toLocaleString(Qt.locale(), "f", 2); font.bold: true; Layout.preferredWidth: 180 }
                            Label { text: "Investiert"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label { text: app.portfolioTotalEntryAmount.toLocaleString(Qt.locale(), "f", 2); font.bold: true; Layout.preferredWidth: 180 }

                            Label { text: "Gewinn/Verlust"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label {
                                text: app.portfolioTotalGainValue().toLocaleString(Qt.locale(), "f", 2) + " / " + app.portfolioPerformancePercent().toLocaleString(Qt.locale(), "f", 2) + " %"
                                font.bold: true
                                color: app.portfolioTotalGainValue() >= 0 ? "#15803d" : "#b91c1c"
                                Layout.preferredWidth: 180
                            }
                            Label { text: "Datenstatus"; color: "#475569"; Layout.preferredWidth: 130 }
                            Label {
                                text: dbManager.ibkrConnected ? "IBKR verbunden" : app.cleanDisplayText(dbManager.ibkrConnectionStatus || "IBKR getrennt")
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
                                onClicked: app.openPortfolioBatchWindow()
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
                                Layout.preferredHeight: 28
                                Layout.minimumHeight: 28
                                Layout.maximumHeight: 28
                                Label { text: "Name"; Layout.fillWidth: true; font.bold: true; leftPadding: 10 }
                                Rectangle {
                                    Layout.preferredWidth: 120
                                    Layout.preferredHeight: 28
                                    Layout.minimumHeight: 28
                                    Layout.maximumHeight: 28
                                    color: app.portfolioSortKey === "totalValue" ? "#e0f2fe" : "transparent"
                                    Label {
                                        anchors.fill: parent
                                        text: "Gesamtwert " + app.portfolioSortIcon("totalValue")
                                        font.bold: true
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: app.sortPortfolioBy("totalValue")
                                    }
                                }
                                Rectangle {
                                    Layout.preferredWidth: 90
                                    Layout.preferredHeight: 28
                                    Layout.minimumHeight: 28
                                    Layout.maximumHeight: 28
                                    color: app.portfolioSortKey === "gainPercent" ? "#e0f2fe" : "transparent"
                                    Label {
                                        anchors.fill: parent
                                        text: "Gewinn (%) " + app.portfolioSortIcon("gainPercent")
                                        font.bold: true
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: app.sortPortfolioBy("gainPercent")
                                    }
                                }
                                Label { text: "Einstiegswert"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "Aktualisiert"; Layout.preferredWidth: 95; font.bold: true; horizontalAlignment: Text.AlignRight }
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
                                model: app.sortedPortfolioRows()
                                currentIndex: app.selectedPortfolioIndex
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Rectangle {
                                    id: portfolioPositionDelegate
                                    required property int index
                                    required property var modelData
                                    property var rowData: modelData || ({})

                                    width: ListView.view ? ListView.view.width : 0
                                    height: 36
                                    color: portfolioWindow.app.selectedPortfolioIndex === portfolioPositionDelegate.index
                                        ? "#dbeafe"
                                        : (portfolioPositionDelegate.index % 2 === 0 ? "#ffffff" : "#f8fafc")

                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 1

                                        Label {
                                            text: app.cleanDisplayText(portfolioPositionDelegate.rowData.name || portfolioPositionDelegate.rowData.symbol || "")
                                            Layout.fillWidth: true
                                            leftPadding: 10
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            text: app.portfolioPositionTotalValue(portfolioPositionDelegate.rowData).toLocaleString(Qt.locale(), "f", 2)
                                            Layout.preferredWidth: 120
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label { text: app.formatPercentValue(portfolioPositionDelegate.rowData.valueIncreasePercent); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: Number(portfolioPositionDelegate.rowData.entryValue || 0).toLocaleString(Qt.locale(), "f", 2); Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                                        Label { text: portfolioPositionDelegate.rowData.quoteLastDate || "-"; Layout.preferredWidth: 95; horizontalAlignment: Text.AlignRight }
                                        Label { text: app.formatPercentValue(portfolioPositionDelegate.rowData.days20ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: app.formatPercentValue(portfolioPositionDelegate.rowData.days40ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: app.formatPercentValue(portfolioPositionDelegate.rowData.days60ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: app.formatPercentValue(portfolioPositionDelegate.rowData.days90ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight; rightPadding: 10 }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: function(mouse) {
                                            app.selectedPortfolioIndex = portfolioPositionDelegate.index
                                            app.schedulePortfolioDetailsLoad()
                                            if (mouse.button === Qt.RightButton)
                                                portfolioContextMenu.popup()
                                        }
                                        onDoubleClicked: function(mouse) {
                                            mouse.accepted = true
                                            app.selectedPortfolioIndex = portfolioPositionDelegate.index
                                            app.schedulePortfolioDetailsLoad()
                                            app.openPortfolioChartWindow(portfolioPositionDelegate.rowData)
                                        }
                                    }

                                    Menu {
                                        id: portfolioContextMenu

                                        MenuItem {
                                            text: "Get Data from IBKR"
                                            enabled: dbManager.ibkrConnected
                                                && !dbManager.ibkrDataLoading
                                            onTriggered: dbManager.getIbkrData(
                                                portfolioPositionDelegate.rowData.symbol)
                                        }

                                        MenuItem {
                                            text: "Get Fundamentals from Yahoo"
                                            enabled: !dbManager.fundamentalDataLoading
                                            onTriggered: dbManager.getAlphaVantageFundamentals(
                                                portfolioPositionDelegate.rowData.symbol)
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
                                    text: app.selectedPortfolioValue("isin") || "Keine ISIN"
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
                                    text: app.cleanDisplayText(app.selectedPortfolioValue("name"))
                                    color: "#1f2937"
                                    font.pixelSize: 14
                                    font.bold: true
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Item { Layout.preferredHeight: 6 }

                                Label {
                                    text: app.selectedPortfolioValue("analysisConfigName")
                                        ? app.cleanDisplayText("(Analyse:" + app.selectedPortfolioValue("analysisConfigName") + ")")
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
                                            id: portfolioFieldDelegate
                                            required property int index
                                            required property var modelData

                                            width: parent.width
                                            height: portfolioFieldDelegate.modelData.heading ? 40 : (portfolioFieldDelegate.modelData.key === "name" || portfolioFieldDelegate.modelData.key === "analysisConfigName" ? 46 : 34)
                                            color: portfolioFieldDelegate.modelData.heading
                                                ? "#e5e7eb"
                                                : (portfolioFieldDelegate.index % 2 === 0 ? "#ffffff" : "#f8fafc")

                                            Label {
                                                visible: Boolean(portfolioFieldDelegate.modelData.heading)
                                                anchors.fill: parent
                                                anchors.leftMargin: 10
                                                verticalAlignment: Text.AlignVCenter
                                                text: app.portfolioHeadingLabel(portfolioFieldDelegate.modelData.label)
                                                font.bold: true
                                                color: "#1f2937"
                                            }

                                            RowLayout {
                                                visible: !portfolioFieldDelegate.modelData.heading
                                                anchors.fill: parent
                                                anchors.leftMargin: 10
                                                anchors.rightMargin: 10
                                                spacing: 12

                                                Label {
                                                    text: app.portfolioFieldLabel(portfolioFieldDelegate.modelData.key, portfolioFieldDelegate.modelData.label)
                                                    color: "#475569"
                                                    Layout.preferredWidth: 135
                                                    elide: Text.ElideRight
                                                }

                                                TextField {
                                                    text: app.formatPortfolioValue(portfolioFieldDelegate.modelData.key, portfolioFieldDelegate.modelData.format || "")
                                                    readOnly: true
                                                    Layout.fillWidth: true
                                                    Layout.fillHeight: true
                                                    implicitWidth: 0
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
                                                    wrapMode: portfolioFieldDelegate.modelData.key === "name" || portfolioFieldDelegate.modelData.key === "analysisConfigName" ? TextInput.Wrap : TextInput.NoWrap
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
                            text: dbManager.ibkrGetStocksBatchName + " " + dbManager.ibkrGetStocksDone + "/" + dbManager.ibkrGetStocksTotal
                            color: "#475569"
                            Layout.preferredWidth: 210
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
