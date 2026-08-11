import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: portfolioWindow
    property var app
    property var dbManager
    property bool localPortfolioBusyVisible: false
    property string localPortfolioBusyMessage: ""
    property bool portfolioBusyVisible: localPortfolioBusyVisible || (app && app.portfolioBusyVisible)
    property string portfolioBusyMessage: localPortfolioBusyVisible
        ? localPortfolioBusyMessage
        : (app ? app.portfolioBusyMessage : "")
    property int pendingPortfolioFilterIndex: -1
    property int soldNameColumnWidth: 300
    property int soldNrColumnWidth: 36
    property int soldGainTotalColumnX: soldNrColumnWidth + soldNameColumnWidth
    property int soldGainTotalColumnWidth: 120
    property int soldGainPercentColumnX: soldGainTotalColumnX + soldGainTotalColumnWidth + 14
    property int soldGainPercentColumnWidth: 100
    property int soldEntryColumnX: soldGainPercentColumnX + soldGainPercentColumnWidth + 14
    property int soldEntryColumnWidth: 110
    property int soldExitColumnX: soldEntryColumnX + soldEntryColumnWidth + 14
    property int soldExitColumnWidth: 110
    property int soldBuyDateColumnX: soldExitColumnX + soldExitColumnWidth + 16
    property int soldBuyDateColumnWidth: 105
    property int soldSellDateColumnX: soldBuyDateColumnX + soldBuyDateColumnWidth + 22
    property int soldSellDateColumnWidth: 115

    function showLocalPortfolioBusy(message) {
        localPortfolioBusyMessage = message
        localPortfolioBusyVisible = true
        localPortfolioBusyTimer.restart()
    }

    function positionListAtBeginning() {
        portfolioListView.positionViewAtBeginning()
    }

    function positionListAtIndex(index) {
        if (index < 0)
            return
        portfolioListView.positionViewAtIndex(index, ListView.Beginning)
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

        Timer {
            id: localPortfolioBusyTimer
            interval: 700
            running: false
            repeat: false
            onTriggered: portfolioWindow.localPortfolioBusyVisible = false
        }

        Timer {
            id: applyPortfolioFilterTimer
            interval: 80
            running: false
            repeat: false
            onTriggered: {
                const index = portfolioWindow.pendingPortfolioFilterIndex
                portfolioWindow.pendingPortfolioFilterIndex = -1
                app.setPortfolioStatusFilter(index === 1 ? "sold" : "active")
            }
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

                    ComboBox {
                        model: app.depotListModel
                        textRole: "name"
                        valueRole: "depotId"
                        currentIndex: app.selectedDepotIndex
                        Layout.preferredWidth: 180
                        onActivated: function(index) {
                            app.selectedDepotIndex = index
                            app.selectedDepotId = app.depotListModel.get(index).depotId
                            app.selectedDepotName = app.depotListModel.get(index).name
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "Konfiguration Par."
                        Layout.preferredWidth: 132
                        ToolTip.visible: hovered
                        ToolTip.text: dbManager.ibkrConnected ? "IBKR verbunden" : "IBKR getrennt"
                        contentItem: Text {
                            text: parent.text
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            color: dbManager.ibkrConnected ? "#15803d" : "#b91c1c"
                            border.color: dbManager.ibkrConnected ? "#166534" : "#991b1b"
                            border.width: 1
                        }
                        onClicked: app.openPortfolioBatchWindow()
                    }
                }

                GroupBox {
                    id: portfolioMasterDataGroupBox
                    title: "Depotstammdaten"
                    topPadding: 22
                    label: Label {
                        text: portfolioMasterDataGroupBox.title
                        x: 10
                        y: 0
                        padding: 2
                        font.bold: true
                        background: Rectangle { color: "#f4f6f7" }
                    }
                    background: Rectangle {
                        y: portfolioMasterDataGroupBox.label.height / 2
                        width: parent.width
                        height: parent.height - y
                        color: "transparent"
                        border.color: "#8b8b8b"
                        border.width: 1
                    }
                    clip: false
                    Layout.fillWidth: true
                    Layout.preferredHeight: 170

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label { text: "Letzte Änderung (%)"; color: "#475569"; font.bold: true; Layout.preferredWidth: 150; horizontalAlignment: Text.AlignRight }
                            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                            Label { text: "Letzte Änderung (Euro)"; color: "#475569"; font.bold: true; Layout.preferredWidth: 160; horizontalAlignment: Text.AlignRight }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label { text: "Depotwert"; color: "#475569"; font.bold: true; Layout.preferredWidth: 115; horizontalAlignment: Text.AlignRight }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label { text: "Gewinn"; color: "#475569"; font.bold: true; Layout.preferredWidth: 115; horizontalAlignment: Text.AlignRight }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label { text: "20 Tage"; color: "#475569"; font.bold: true; Layout.preferredWidth: 82; horizontalAlignment: Text.AlignRight }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label { text: "40 Tage"; color: "#475569"; font.bold: true; Layout.preferredWidth: 82; horizontalAlignment: Text.AlignRight }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label { text: "60 Tage"; color: "#475569"; font.bold: true; Layout.preferredWidth: 82; horizontalAlignment: Text.AlignRight }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label { text: "90 Tage"; color: "#475569"; font.bold: true; Layout.preferredWidth: 82; horizontalAlignment: Text.AlignRight }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label { text: "Investiert seit"; color: "#475569"; font.bold: true; Layout.preferredWidth: 115; horizontalAlignment: Text.AlignRight }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: "#c9d0d5"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                        Label {
                            text: app.formatPercentValue(app.portfolioLatestChangePercent)
                            font.bold: true
                            color: app.portfolioSignedPercentColor(app.portfolioLatestChangePercent)
                            Layout.preferredWidth: 150
                            horizontalAlignment: Text.AlignRight
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label {
                            text: app.portfolioLatestChangeAmount.toLocaleString(Qt.locale(), "f", 2)
                            font.bold: true
                            color: app.portfolioLatestChangeAmount > 0 ? "#15803d" : (app.portfolioLatestChangeAmount < 0 ? "#b91c1c" : "#475569")
                            Layout.preferredWidth: 160
                            horizontalAlignment: Text.AlignRight
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label {
                            text: app.portfolioTotalCurrentAmount.toLocaleString(Qt.locale(), "f", 2)
                            font.bold: true
                            Layout.preferredWidth: 115
                            horizontalAlignment: Text.AlignRight
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label {
                            text: app.portfolioTotalGainValue() > 0 ? app.portfolioTotalGainValue().toLocaleString(Qt.locale(), "f", 2) : "-"
                            font.bold: true
                            color: app.portfolioTotalGainValue() > 0 ? "#15803d" : "#475569"
                            Layout.preferredWidth: 115
                            horizontalAlignment: Text.AlignRight
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label {
                            text: app.formatPercentValue(app.portfolioDays20Percent)
                            font.bold: true
                            color: app.portfolioSignedPercentColor(app.portfolioDays20Percent)
                            Layout.preferredWidth: 82
                            horizontalAlignment: Text.AlignRight
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label {
                            text: app.formatPercentValue(app.portfolioDays40Percent)
                            font.bold: true
                            color: app.portfolioSignedPercentColor(app.portfolioDays40Percent)
                            Layout.preferredWidth: 82
                            horizontalAlignment: Text.AlignRight
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label {
                            text: app.formatPercentValue(app.portfolioDays60Percent)
                            font.bold: true
                            color: app.portfolioSignedPercentColor(app.portfolioDays60Percent)
                            Layout.preferredWidth: 82
                            horizontalAlignment: Text.AlignRight
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label {
                            text: app.formatPercentValue(app.portfolioDays90Percent)
                            font.bold: true
                            color: app.portfolioSignedPercentColor(app.portfolioDays90Percent)
                            Layout.preferredWidth: 82
                            horizontalAlignment: Text.AlignRight
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#c9d0d5" }
                        Label {
                            text: app.portfolioStartInvest || "-"
                            font.bold: true
                            Layout.preferredWidth: 115
                            horizontalAlignment: Text.AlignRight
                        }

                        }

                        Item { Layout.fillHeight: true }
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
                            spacing: 8

                    GroupBox {
                        id: portfolioPositionsGroupBox
                        title: "Positionen"
                        topPadding: 22
                        label: Label {
                            text: portfolioPositionsGroupBox.title
                            x: 10
                            y: 0
                            padding: 2
                            font.bold: true
                            background: Rectangle { color: "#f4f6f7" }
                        }
                        background: Rectangle {
                            y: portfolioPositionsGroupBox.label.height / 2
                            width: parent.width
                            height: parent.height - y
                            color: "transparent"
                            border.color: "#8b8b8b"
                            border.width: 1
                        }
                        clip: false
                        Layout.fillWidth: true
                        Layout.preferredWidth: Math.max(1, portfolioWindow.width * 0.75)
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                spacing: 8

                                Item { Layout.fillWidth: true }
                                Label {
                                    text: app.portfolioListModel.count + " angezeigt"
                                    color: "#475569"
                                    verticalAlignment: Text.AlignVCenter
                                }

                                ComboBox {
                                    model: ["Gekauft", "Verkauft"]
                                    currentIndex: app.portfolioStatusFilterIndex()
                                    Layout.preferredWidth: 118
                                    onActivated: function(index) {
                                        portfolioWindow.showLocalPortfolioBusy("Aktualisiere ...")
                                        portfolioWindow.pendingPortfolioFilterIndex = index
                                        applyPortfolioFilterTimer.restart()
                                    }
                                }

                                BusyIndicator {
                                    running: portfolioWindow.portfolioBusyVisible
                                    visible: portfolioWindow.portfolioBusyVisible
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 24
                                }

                                Label {
                                    visible: portfolioWindow.portfolioBusyVisible
                                    text: portfolioWindow.portfolioBusyMessage
                                    color: "#475569"
                                    rightPadding: 10
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: "#c9d0d5"
                            }

                            RowLayout {
                                visible: app.portfolioStatusFilter !== "sold"
                                Layout.fillWidth: true
                                spacing: 1
                                Layout.preferredHeight: 28
                                Layout.minimumHeight: 28
                                Layout.maximumHeight: 28
                                Label { text: "Nr"; Layout.preferredWidth: 36; font.bold: true; leftPadding: 10 }
                                Label { text: "Name"; Layout.fillWidth: true; font.bold: true; leftPadding: 10 }
                                Rectangle {
                                    Layout.preferredWidth: 112
                                    Layout.preferredHeight: 28
                                    Layout.minimumHeight: 28
                                    Layout.maximumHeight: 28
                                    color: app.portfolioSortKey === "latestChangePercent" ? "#e0f2fe" : "transparent"
                                    Label {
                                        anchors.fill: parent
                                        text: "Letzte Änderung " + app.portfolioSortIcon("latestChangePercent")
                                        font.bold: true
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            portfolioWindow.showLocalPortfolioBusy("Sortiere ...")
                                            app.sortPortfolioBy("latestChangePercent")
                                        }
                                    }
                                }
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
                                    Layout.leftMargin: 12
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
                                Label { text: "Investiert"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label {
                                    text: app.portfolioPositionDateColumnTitle()
                                    Layout.preferredWidth: 110
                                    Layout.leftMargin: 14
                                    font.bold: true
                                    horizontalAlignment: Text.AlignRight
                                }
                                Rectangle {
                                    Layout.preferredWidth: 90
                                    Layout.preferredHeight: 28
                                    Layout.minimumHeight: 28
                                    Layout.maximumHeight: 28
                                    color: app.portfolioSortKey === "days20ValueInc" ? "#e0f2fe" : "transparent"
                                    Label {
                                        anchors.fill: parent
                                        text: "20 Tage " + app.portfolioSortIcon("days20ValueInc")
                                        font.bold: true
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: app.sortPortfolioBy("days20ValueInc")
                                    }
                                }
                                Rectangle {
                                    Layout.preferredWidth: 90
                                    Layout.preferredHeight: 28
                                    Layout.minimumHeight: 28
                                    Layout.maximumHeight: 28
                                    color: app.portfolioSortKey === "days40ValueInc" ? "#e0f2fe" : "transparent"
                                    Label {
                                        anchors.fill: parent
                                        text: "40 Tage " + app.portfolioSortIcon("days40ValueInc")
                                        font.bold: true
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: app.sortPortfolioBy("days40ValueInc")
                                    }
                                }
                                Label { text: "60 Tage"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "90 Tage"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "Aktualisiert"; Layout.preferredWidth: 95; font.bold: true; horizontalAlignment: Text.AlignRight; rightPadding: 10 }
                            }

                            Item {
                                visible: app.portfolioStatusFilter === "sold"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                Layout.minimumHeight: 28
                                Layout.maximumHeight: 28
                                Label { x: 0; width: portfolioWindow.soldNrColumnWidth; anchors.verticalCenter: parent.verticalCenter; text: "Nr"; font.bold: true; leftPadding: 10 }
                                Label { x: portfolioWindow.soldNrColumnWidth; width: portfolioWindow.soldNameColumnWidth; anchors.verticalCenter: parent.verticalCenter; text: "Name"; font.bold: true; leftPadding: 10 }
                                Label { x: portfolioWindow.soldGainTotalColumnX; width: portfolioWindow.soldGainTotalColumnWidth; anchors.verticalCenter: parent.verticalCenter; text: "Gewinn (total)"; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { x: portfolioWindow.soldGainPercentColumnX; width: portfolioWindow.soldGainPercentColumnWidth; anchors.verticalCenter: parent.verticalCenter; text: "Gewinn (%)"; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { x: portfolioWindow.soldEntryColumnX; width: portfolioWindow.soldEntryColumnWidth; anchors.verticalCenter: parent.verticalCenter; text: "Einstiegswert"; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { x: portfolioWindow.soldExitColumnX; width: portfolioWindow.soldExitColumnWidth; anchors.verticalCenter: parent.verticalCenter; text: "Ausstiegswert"; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { x: portfolioWindow.soldBuyDateColumnX; width: portfolioWindow.soldBuyDateColumnWidth; anchors.verticalCenter: parent.verticalCenter; text: "Kaufdatum"; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { x: portfolioWindow.soldSellDateColumnX; width: portfolioWindow.soldSellDateColumnWidth; anchors.verticalCenter: parent.verticalCenter; text: "Verkaufsdatum"; font.bold: true; horizontalAlignment: Text.AlignRight; rightPadding: 10 }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: "#c9d0d5"
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                ListView {
                                    id: portfolioListView
                                    anchors.fill: parent
                                    clip: true
                                    model: app.portfolioListModel
                                    currentIndex: app.selectedPortfolioIndex
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                    delegate: Rectangle {
                                        id: portfolioPositionDelegate
                                        required property int index
                                        property var rowData: app.portfolioListModel && portfolioPositionDelegate.index >= 0
                                            ? app.portfolioListModel.get(portfolioPositionDelegate.index)
                                            : ({})
                                        property string positionType: "portfolio"
                                        property bool positionUsed: app.positionManagementSellVersion >= 0
                                            && app.portfolioPositionInSell(rowData)
                                        property bool positionSold: app.portfolioRowSold(rowData)
                                        property bool dragActive: false
                                        property real pressX: 0
                                        property real pressY: 0

                                        width: ListView.view ? ListView.view.width : 0
                                        height: 36
                                        color: portfolioWindow.app.selectedPortfolioIndex === portfolioPositionDelegate.index
                                            ? (portfolioPositionDelegate.positionSold ? "#fecaca" : "#dbeafe")
                                            : (portfolioPositionDelegate.positionSold
                                                ? "#fff1f2"
                                                : (portfolioPositionDelegate.positionUsed
                                                ? "#e5e7eb"
                                                : (portfolioPositionDelegate.index % 2 === 0 ? "#ffffff" : "#f8fafc")))

                                    Item {
                                        id: portfolioPositionDragProxy
                                        width: portfolioPositionDelegate.width
                                        height: portfolioPositionDelegate.height
                                        opacity: 0
                                        property string positionType: portfolioPositionDelegate.positionType
                                        property var positionData: portfolioPositionDelegate.rowData

                                        Drag.active: portfolioPositionDelegate.dragActive
                                        Drag.dragType: Drag.Automatic
                                        Drag.keys: ["portfolio-position"]
                                        Drag.mimeData: {
                                            "application/x-shareselector-position": JSON.stringify({
                                                positionType: portfolioPositionDragProxy.positionType,
                                                positionData: portfolioPositionDragProxy.positionData
                                            })
                                        }
                                        Drag.supportedActions: Qt.CopyAction
                                        Drag.hotSpot.x: width / 2
                                        Drag.hotSpot.y: height / 2
                                        Drag.onDragFinished: {
                                            portfolioPositionDragProxy.x = 0
                                            portfolioPositionDragProxy.y = 0
                                        }
                                    }

                                    RowLayout {
                                        visible: app.portfolioStatusFilter !== "sold"
                                        anchors.fill: parent
                                        spacing: 1
                                        Label {
                                            text: Number(portfolioPositionDelegate.index + 1).toFixed(0)
                                            Layout.preferredWidth: 36
                                            leftPadding: 10
                                            horizontalAlignment: Text.AlignLeft
                                        }

                                        Rectangle {
                                            visible: portfolioPositionDelegate.positionUsed
                                            Layout.preferredWidth: 6
                                            Layout.preferredHeight: 6
                                            radius: 3
                                            color: "#64748b"
                                            Layout.leftMargin: 6
                                        }

                                        Rectangle {
                                            visible: portfolioPositionDelegate.positionSold
                                            Layout.preferredWidth: 58
                                            Layout.preferredHeight: 20
                                            radius: 3
                                            color: "#fee2e2"
                                            border.color: "#fca5a5"
                                            border.width: 1

                                            Label {
                                                anchors.fill: parent
                                                text: "Verkauft"
                                                color: "#991b1b"
                                                font.pixelSize: 11
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }

                                        Label {
                                            text: app.cleanDisplayText(portfolioPositionDelegate.rowData.name || portfolioPositionDelegate.rowData.symbol || "")
                                            Layout.fillWidth: true
                                            leftPadding: portfolioPositionDelegate.positionUsed || portfolioPositionDelegate.positionSold ? 4 : 10
                                            color: portfolioPositionDelegate.positionSold ? "#64748b" : "#0f172a"
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            text: app.formatPercentValue(portfolioPositionDelegate.rowData.latestChangePercent)
                                            color: app.portfolioSignedPercentColor(portfolioPositionDelegate.rowData.latestChangePercent)
                                            font.bold: true
                                            Layout.preferredWidth: 112
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label {
                                            text: app.portfolioPositionTotalValue(portfolioPositionDelegate.rowData).toLocaleString(Qt.locale(), "f", 2)
                                            Layout.preferredWidth: 120
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label { text: app.formatPercentValue(portfolioPositionDelegate.rowData.valueIncreasePercent); Layout.preferredWidth: 90; Layout.leftMargin: 12; horizontalAlignment: Text.AlignRight }
                                        Label { text: app.portfolioPositionEntryTotal(portfolioPositionDelegate.rowData).toLocaleString(Qt.locale(), "f", 2); Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                                        Label {
                                            text: portfolioPositionDelegate.positionSold
                                                ? (portfolioPositionDelegate.rowData.sellDate || "-")
                                                : (portfolioPositionDelegate.rowData.buyDate || "-")
                                            Layout.preferredWidth: 110
                                            Layout.leftMargin: 14
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label {
                                            text: app.formatPercentValue(portfolioPositionDelegate.rowData.days20ValueInc)
                                            color: app.portfolioTwentyDayTrendColor(portfolioPositionDelegate.rowData.days20ValueInc)
                                            font.bold: true
                                            Layout.preferredWidth: 90
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label { text: app.formatPercentValue(portfolioPositionDelegate.rowData.days40ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: app.formatPercentValue(portfolioPositionDelegate.rowData.days60ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label { text: app.formatPercentValue(portfolioPositionDelegate.rowData.days90ValueInc); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                        Label {
                                            text: portfolioPositionDelegate.positionSold ? "" : (portfolioPositionDelegate.rowData.quoteLastDate || "-")
                                            Layout.preferredWidth: 95
                                            horizontalAlignment: Text.AlignRight
                                            rightPadding: 10
                                        }
                                    }

                                    Item {
                                        visible: app.portfolioStatusFilter === "sold"
                                        anchors.fill: parent
                                        Label {
                                            text: Number(portfolioPositionDelegate.index + 1).toFixed(0)
                                            x: 0
                                            width: portfolioWindow.soldNrColumnWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            leftPadding: 10
                                            horizontalAlignment: Text.AlignLeft
                                        }

                                        Label {
                                            text: app.cleanDisplayText(portfolioPositionDelegate.rowData.name || portfolioPositionDelegate.rowData.symbol || "")
                                            x: portfolioWindow.soldNrColumnWidth
                                            width: portfolioWindow.soldNameColumnWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            leftPadding: 10
                                            color: "#64748b"
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            text: app.portfolioPositionGainTotal(portfolioPositionDelegate.rowData).toLocaleString(Qt.locale(), "f", 2)
                                            color: app.portfolioPositionGainTotal(portfolioPositionDelegate.rowData) >= 0 ? "#15803d" : "#b91c1c"
                                            font.bold: true
                                            x: portfolioWindow.soldGainTotalColumnX
                                            width: portfolioWindow.soldGainTotalColumnWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label {
                                            text: app.formatPercentValue(portfolioPositionDelegate.rowData.valueIncreasePercent)
                                            color: Number(portfolioPositionDelegate.rowData.valueIncreasePercent || 0) >= 0 ? "#15803d" : "#b91c1c"
                                            font.bold: true
                                            x: portfolioWindow.soldGainPercentColumnX
                                            width: portfolioWindow.soldGainPercentColumnWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label {
                                            text: app.portfolioPositionEntryTotal(portfolioPositionDelegate.rowData).toLocaleString(Qt.locale(), "f", 2)
                                            x: portfolioWindow.soldEntryColumnX
                                            width: portfolioWindow.soldEntryColumnWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label {
                                            text: app.portfolioPositionTotalValue(portfolioPositionDelegate.rowData).toLocaleString(Qt.locale(), "f", 2)
                                            x: portfolioWindow.soldExitColumnX
                                            width: portfolioWindow.soldExitColumnWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label {
                                            text: portfolioPositionDelegate.rowData.buyDate || "-"
                                            x: portfolioWindow.soldBuyDateColumnX
                                            width: portfolioWindow.soldBuyDateColumnWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Label {
                                            text: portfolioPositionDelegate.rowData.sellDate || "-"
                                            x: portfolioWindow.soldSellDateColumnX
                                            width: portfolioWindow.soldSellDateColumnWidth
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignRight
                                            rightPadding: 10
                                        }
                                    }

                                    MouseArea {
                                        id: portfolioPositionDragMouse
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        preventStealing: portfolioPositionDelegate.positionUsed || portfolioPositionDelegate.positionSold
                                        drag.target: portfolioPositionDelegate.positionUsed || portfolioPositionDelegate.positionSold ? null : portfolioPositionDragProxy
                                        onPressed: function(mouse) {
                                            portfolioPositionDelegate.pressX = mouse.x
                                            portfolioPositionDelegate.pressY = mouse.y
                                            portfolioPositionDelegate.dragActive = false
                                        }
                                        onPositionChanged: function(mouse) {
                                            if (portfolioPositionDelegate.positionUsed || portfolioPositionDelegate.positionSold) {
                                                mouse.accepted = true
                                                return
                                            }
                                            if (mouse.buttons & Qt.LeftButton) {
                                                const dx = mouse.x - portfolioPositionDelegate.pressX
                                                const dy = mouse.y - portfolioPositionDelegate.pressY
                                                if (!portfolioPositionDelegate.dragActive
                                                        && dx * dx + dy * dy >= 64)
                                                    portfolioPositionDelegate.dragActive = true
                                            }
                                        }
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
                                        onReleased: {
                                            portfolioPositionDelegate.dragActive = false
                                            portfolioPositionDragProxy.x = 0
                                            portfolioPositionDragProxy.y = 0
                                        }
                                        onCanceled: {
                                            portfolioPositionDelegate.dragActive = false
                                            portfolioPositionDragProxy.x = 0
                                            portfolioPositionDragProxy.y = 0
                                        }
                                    }

                                    Menu {
                                        id: portfolioContextMenu

                                        MenuItem {
                                            text: "In Verkauf ablegen"
                                            enabled: !portfolioPositionDelegate.positionUsed
                                                && !portfolioPositionDelegate.positionSold
                                            onTriggered: app.addPortfolioPositionToSell(portfolioPositionDelegate.rowData)
                                        }

                                        MenuItem {
                                            text: "Get Data from IBKR"
                                            enabled: dbManager.ibkrConnected
                                                && !dbManager.ibkrDataLoading
                                                && !portfolioPositionDelegate.positionSold
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

                                Rectangle {
                                    visible: portfolioWindow.portfolioBusyVisible
                                    anchors.centerIn: parent
                                    width: 180
                                    height: 48
                                    radius: 4
                                    color: "#f8fafc"
                                    border.color: "#94a3b8"
                                    border.width: 1
                                    z: 10

                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: 10

                                        BusyIndicator {
                                            running: portfolioWindow.portfolioBusyVisible
                                            Layout.preferredWidth: 24
                                            Layout.preferredHeight: 24
                                        }

                                        Label {
                                            text: portfolioWindow.portfolioBusyMessage
                                            font.bold: true
                                            color: "#334155"
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: Math.max(1, portfolioWindow.width * 0.1875)
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

                        ColumnLayout {
                            spacing: 4

                            Button {
                                text: "Positionen verwalten"
                                Layout.fillWidth: true
                                onClicked: app.openPositionManagementDialog()
                            }

                            Button {
                                text: "Schließen"
                                Layout.fillWidth: true
                                onClicked: portfolioWindow.close()
                            }
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
