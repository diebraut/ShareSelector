pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: positionManagementDialog

    property var app
    property var hostWindow
    property var stockAnalysisWindow
    property int buyPositionVersion: 0
    property int sellPositionVersion: 0
    property int checkedPairVersion: 0
    property bool exchangeActive: false
    property string exchangeStatusText: ""

    title: "Positionen verwalten"
    width: hostWindow ? hostWindow.width : 1360
    height: 320
    minimumWidth: 980
    minimumHeight: 220
    flags: Qt.Dialog
    modality: Qt.NonModal
    visible: false

    function positionKey(row) {
        if (!row)
            return ""
        return String(row.symbol || row.isin || row.name || "").trim()
    }

    function modelContainsPosition(model, row) {
        const key = positionKey(row)
        if (key.length === 0)
            return false

        for (let i = 0; i < model.count; i++) {
            if (positionKey(model.get(i)) === key)
                return true
        }
        return false
    }

    function containsBuyPosition(row) {
        return modelContainsPosition(buyPositionModel, row)
    }

    function containsSellPosition(row) {
        return modelContainsPosition(sellPositionModel, row)
    }

    function syncPairCheckModel() {
        const pairCount = Math.min(buyPositionModel.count, sellPositionModel.count)
        while (pairCheckModel.count < pairCount)
            pairCheckModel.append({ checked: false })
    }

    function resetPositions() {
        buyPositionModel.clear()
        sellPositionModel.clear()
        pairCheckModel.clear()
        buyPositionVersion++
        sellPositionVersion++
        checkedPairVersion++
    }

    function listIndexAtMouse(listView, mouseArea, mouseX, mouseY) {
        const point = mouseArea.mapToItem(listView.contentItem, mouseX, mouseY)
        let targetIndex = listView.indexAt(1, point.y)
        if (targetIndex >= 0)
            return targetIndex
        if (point.y < 24)
            return 0
        if (listView.count > 0)
            return listView.count - 1
        return -1
    }

    function checkedPairCount() {
        let count = 0
        for (let i = 0; i < pairCheckModel.count; i++) {
            if (pairCheckModel.get(i).checked)
                count++
        }
        return count
    }

    function checkedExchangePairs() {
        const pairs = []
        const pairCount = Math.min(buyPositionModel.count, sellPositionModel.count, pairCheckModel.count)
        for (let i = 0; i < pairCount; i++) {
            if (pairCheckModel.get(i).checked) {
                pairs.push({
                    index: i,
                    buy: buyPositionModel.get(i),
                    sell: sellPositionModel.get(i)
                })
            }
        }
        return pairs
    }

    function removeExchangeRows(indexes) {
        const sorted = indexes.slice().sort((a, b) => b - a)
        sorted.forEach(index => {
            if (index >= 0 && index < buyPositionModel.count)
                buyPositionModel.remove(index)
            if (index >= 0 && index < sellPositionModel.count)
                sellPositionModel.remove(index)
            if (index >= 0 && index < pairCheckModel.count)
                pairCheckModel.remove(index)
        })
        buyPositionVersion++
        sellPositionVersion++
        checkedPairVersion++
    }

    function setExchangeStatus(active, text) {
        exchangeActive = active
        exchangeStatusText = text || ""
    }

    function addBuyPosition(row) {
        if (!row)
            return false
        if (containsBuyPosition(row))
            return false
        buyPositionModel.append({
            symbol: row.symbol || "",
            isin: row.isin || "",
            name: row.name || row.symbol || "",
            value: row.lastcloseprice || row.currentValue || ""
        })
        buyPositionVersion++
        syncPairCheckModel()
        open()
        return true
    }

    function addSellPosition(row) {
        if (!row)
            return false
        if (containsSellPosition(row))
            return false
        const currentValue = Number(row.currentValue || row.lastcloseprice || 0)
        const quantity = app ? app.portfolioPositionQuantity(row) : Number(row.quantity || 1)
        sellPositionModel.append({
            symbol: row.symbol || "",
            isin: row.isin || "",
            name: row.name || row.symbol || "",
            quantity: quantity,
            currentValue: currentValue,
            totalValue: currentValue * quantity
        })
        sellPositionVersion++
        syncPairCheckModel()
        open()
        return true
    }

    function positionFromDrop(drop, expectedType) {
        if (drop.source && drop.source.positionData && drop.source.positionType === expectedType)
            return drop.source.positionData

        if (drop.formats
                && drop.formats.indexOf("application/x-shareselector-position") >= 0
                && typeof drop.getDataAsString === "function") {
            try {
                const payload = JSON.parse(drop.getDataAsString("application/x-shareselector-position"))
                if (payload.positionType === expectedType)
                    return payload.positionData || null
            } catch (error) {
                return null
            }
        }

        return null
    }

    function updateDropAcceptance(drop, expectedType) {
        if (positionFromDrop(drop, expectedType)) {
            drop.acceptProposedAction()
        } else {
            drop.accepted = false
            drop.action = Qt.IgnoreAction
        }
    }

    function open() {
        const spacing = 40
        if (hostWindow) {
            width = hostWindow.width
            x = hostWindow.x
            y = hostWindow.y + hostWindow.height + spacing
        }

        if (stockAnalysisWindow && hostWindow) {
            const targetBottom = stockAnalysisWindow.y + stockAnalysisWindow.height
            const availableHeight = targetBottom - y
            if (availableHeight >= minimumHeight)
                height = availableHeight
        }

        show()
        raise()
        requestActivate()
    }

    ListModel {
        id: buyPositionModel
    }

    ListModel {
        id: sellPositionModel
    }

    ListModel {
        id: pairCheckModel
    }

    onClosing: resetPositions()

    Rectangle {
        anchors.fill: parent
        color: "#f4f6f7"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            Label {
                text: "Positionen verwalten"
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: "#c9d0d5"
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                GroupBox {
                    id: buyDropBox
                    title: "Kauf"
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    DropArea {
                        anchors.fill: parent
                        onEntered: function(drop) {
                            positionManagementDialog.updateDropAcceptance(drop, "stock-analysis")
                        }
                        onPositionChanged: function(drop) {
                            positionManagementDialog.updateDropAcceptance(drop, "stock-analysis")
                        }
                        onDropped: function(drop) {
                            const position = positionManagementDialog.positionFromDrop(drop, "stock-analysis")
                            if (position) {
                                positionManagementDialog.addBuyPosition(position)
                                drop.acceptProposedAction()
                            } else {
                                drop.accepted = false
                                drop.action = Qt.IgnoreAction
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Label {
                            visible: buyPositionModel.count === 0
                            text: "Stock-Analyse-Positionen hier ablegen"
                            color: "#475569"
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ListView {
                            id: buyPositionListView
                            property int draggedIndex: -1
                            property int dropIndex: -1

                            visible: buyPositionModel.count > 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: buyPositionModel
                            header: RowLayout {
                                width: ListView.view ? ListView.view.width : 0
                                height: 24
                                spacing: 1
                                Label { text: "ISIN"; Layout.preferredWidth: 120; font.bold: true; leftPadding: 8 }
                                Label { text: "Name"; Layout.fillWidth: true; font.bold: true }
                                Label { text: "Aktueller Wert"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight; rightPadding: 8 }
                            }

                            delegate: Rectangle {
                                id: buyPositionDelegate
                                required property int index
                                required property string isin
                                required property string name
                                required property var value

                                width: ListView.view ? ListView.view.width : 0
                                height: 30
                                color: buyPositionDelegate.index % 2 === 0 ? "#ffffff" : "#f8fafc"
                                z: buyPositionListView.draggedIndex === buyPositionDelegate.index ? 2 : 0

                                MouseArea {
                                    id: buyPositionReorderMouse
                                    anchors.fill: parent
                                    z: 4
                                    acceptedButtons: Qt.LeftButton
                                    preventStealing: buyPositionListView.draggedIndex >= 0
                                    onPressed: {
                                        buyPositionListView.draggedIndex = buyPositionDelegate.index
                                        buyPositionListView.dropIndex = buyPositionDelegate.index
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (buyPositionListView.draggedIndex >= 0)
                                            buyPositionListView.dropIndex = positionManagementDialog.listIndexAtMouse(
                                                buyPositionListView,
                                                buyPositionReorderMouse,
                                                mouse.x,
                                                mouse.y)
                                    }
                                    onReleased: {
                                        if (buyPositionListView.draggedIndex >= 0
                                                && buyPositionListView.dropIndex >= 0
                                                && buyPositionListView.draggedIndex !== buyPositionListView.dropIndex)
                                            buyPositionModel.move(
                                                buyPositionListView.draggedIndex,
                                                buyPositionListView.dropIndex,
                                                1)
                                        buyPositionListView.draggedIndex = -1
                                        buyPositionListView.dropIndex = -1
                                    }
                                    onCanceled: {
                                        buyPositionListView.draggedIndex = -1
                                        buyPositionListView.dropIndex = -1
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    z: 1
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    Label { text: buyPositionDelegate.isin || "-"; Layout.preferredWidth: 120; font.bold: true }
                                    Label { text: buyPositionDelegate.name || ""; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: buyPositionDelegate.value ? Number(buyPositionDelegate.value).toLocaleString(Qt.locale(), "f", 2) : "-"; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                }

                                Rectangle {
                                    visible: buyPositionListView.draggedIndex === buyPositionDelegate.index
                                        || buyPositionListView.dropIndex === buyPositionDelegate.index
                                    anchors.fill: parent
                                    z: 3
                                    color: "transparent"
                                    border.color: buyPositionListView.draggedIndex === buyPositionDelegate.index
                                        ? "#2563eb"
                                        : "#94a3b8"
                                    border.width: 2
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 54
                    Layout.fillHeight: true
                    Layout.topMargin: 33
                    color: "#ffffff"
                    border.color: "#c9d0d5"
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 0
                        anchors.rightMargin: 0
                        anchors.topMargin: 8
                        anchors.bottomMargin: 8
                        spacing: 0

                        ListView {
                            id: pairCheckListView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            interactive: false
                            model: pairCheckModel
                            header: Item {
                                width: ListView.view ? ListView.view.width : 0
                                height: 36
                            }

                            delegate: Rectangle {
                                id: pairCheckDelegate
                                required property int index
                                required property bool checked

                                width: ListView.view ? ListView.view.width : 0
                                height: 30
                                color: pairCheckDelegate.index % 2 === 0 ? "#ffffff" : "#f8fafc"

                                CheckBox {
                                    id: pairCheckBox
                                    anchors.centerIn: parent
                                    checked: pairCheckDelegate.checked
                                    onToggled: pairCheckListView.model.setProperty(
                                        pairCheckDelegate.index, "checked", pairCheckBox.checked)
                                    onCheckedChanged: positionManagementDialog.checkedPairVersion++
                                }
                            }
                        }
                    }
                }

                GroupBox {
                    id: sellDropBox
                    title: "Verkauf"
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    DropArea {
                        anchors.fill: parent
                        onEntered: function(drop) {
                            positionManagementDialog.updateDropAcceptance(drop, "portfolio")
                        }
                        onPositionChanged: function(drop) {
                            positionManagementDialog.updateDropAcceptance(drop, "portfolio")
                        }
                        onDropped: function(drop) {
                            const position = positionManagementDialog.positionFromDrop(drop, "portfolio")
                            if (position) {
                                positionManagementDialog.addSellPosition(position)
                                drop.acceptProposedAction()
                            } else {
                                drop.accepted = false
                                drop.action = Qt.IgnoreAction
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Label {
                            visible: sellPositionModel.count === 0
                            text: "Depot-Positionen hier ablegen"
                            color: "#475569"
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ListView {
                            id: sellPositionListView
                            property int draggedIndex: -1
                            property int dropIndex: -1

                            visible: sellPositionModel.count > 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: sellPositionModel
                            header: RowLayout {
                                width: ListView.view ? ListView.view.width : 0
                                height: 24
                                spacing: 1
                                Label { text: "ISIN"; Layout.preferredWidth: 120; font.bold: true; leftPadding: 8 }
                                Label { text: "Name"; Layout.fillWidth: true; font.bold: true }
                                Label { text: "Aktueller Wert"; Layout.preferredWidth: 90; font.bold: true; horizontalAlignment: Text.AlignRight }
                                Label { text: "Gesamtwert"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight; rightPadding: 8 }
                            }

                            delegate: Rectangle {
                                id: sellPositionDelegate
                                required property int index
                                required property string isin
                                required property string name
                                required property var currentValue
                                required property var totalValue

                                width: ListView.view ? ListView.view.width : 0
                                height: 30
                                color: sellPositionDelegate.index % 2 === 0 ? "#ffffff" : "#f8fafc"
                                z: sellPositionListView.draggedIndex === sellPositionDelegate.index ? 2 : 0

                                MouseArea {
                                    id: sellPositionReorderMouse
                                    anchors.fill: parent
                                    z: 4
                                    acceptedButtons: Qt.LeftButton
                                    preventStealing: sellPositionListView.draggedIndex >= 0
                                    onPressed: {
                                        sellPositionListView.draggedIndex = sellPositionDelegate.index
                                        sellPositionListView.dropIndex = sellPositionDelegate.index
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (sellPositionListView.draggedIndex >= 0)
                                            sellPositionListView.dropIndex = positionManagementDialog.listIndexAtMouse(
                                                sellPositionListView,
                                                sellPositionReorderMouse,
                                                mouse.x,
                                                mouse.y)
                                    }
                                    onReleased: {
                                        if (sellPositionListView.draggedIndex >= 0
                                                && sellPositionListView.dropIndex >= 0
                                                && sellPositionListView.draggedIndex !== sellPositionListView.dropIndex)
                                            sellPositionModel.move(
                                                sellPositionListView.draggedIndex,
                                                sellPositionListView.dropIndex,
                                                1)
                                        sellPositionListView.draggedIndex = -1
                                        sellPositionListView.dropIndex = -1
                                    }
                                    onCanceled: {
                                        sellPositionListView.draggedIndex = -1
                                        sellPositionListView.dropIndex = -1
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    z: 1
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    Label { text: sellPositionDelegate.isin || "-"; Layout.preferredWidth: 120; font.bold: true }
                                    Label { text: sellPositionDelegate.name || ""; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: sellPositionDelegate.currentValue ? Number(sellPositionDelegate.currentValue).toLocaleString(Qt.locale(), "f", 2) : "-"; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                                    Label { text: sellPositionDelegate.totalValue ? Number(sellPositionDelegate.totalValue).toLocaleString(Qt.locale(), "f", 2) : "-"; Layout.preferredWidth: 110; horizontalAlignment: Text.AlignRight }
                                }

                                Rectangle {
                                    visible: sellPositionListView.draggedIndex === sellPositionDelegate.index
                                        || sellPositionListView.dropIndex === sellPositionDelegate.index
                                    anchors.fill: parent
                                    z: 3
                                    color: "transparent"
                                    border.color: sellPositionListView.draggedIndex === sellPositionDelegate.index
                                        ? "#2563eb"
                                        : "#94a3b8"
                                    border.width: 2
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: "Aktien austauschen"
                    enabled: !positionManagementDialog.exchangeActive
                        && positionManagementDialog.checkedPairVersion >= 0
                        && positionManagementDialog.checkedPairCount() > 0
                    onClicked: positionManagementDialog.app.exchangeCheckedPositions()
                }

                BusyIndicator {
                    running: positionManagementDialog.exchangeActive
                    visible: positionManagementDialog.exchangeActive
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }

                Label {
                    text: positionManagementDialog.exchangeStatusText
                    color: positionManagementDialog.exchangeStatusText.indexOf("Fehler") >= 0
                        || positionManagementDialog.exchangeStatusText.indexOf("fehlgeschlagen") >= 0
                        ? "#b91c1c"
                        : (positionManagementDialog.exchangeStatusText.indexOf("Warnung") >= 0
                           ? "#b45309"
                           : "#475569")
                    elide: Text.ElideRight
                    Layout.preferredWidth: 360
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Schliessen"
                    onClicked: positionManagementDialog.close()
                }
            }
        }
    }
}
