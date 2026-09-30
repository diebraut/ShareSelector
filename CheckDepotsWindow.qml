import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

Window {
    id: checkDepotsWindow

    property var dbManager
    property var browserWindow
    property var observedStocks: []
    property int currentDepotId: 1
    property bool checkingPositions: false
    property string checkMessage: ""
    property string checkMessageColor: "#475569"

    title: "Check Depots"
    visible: false
    flags: Qt.Window
    minimumWidth: 320
    minimumHeight: 160
    color: "#f4f6f7"

    function priceText(value) {
        if (value === null || value === undefined || value === "")
            return "---"
        const number = Number(value)
        if (isNaN(number))
            return "---"
        const decimals = Math.abs(number) < 10 ? 3 : 2
        return number.toLocaleString(Qt.locale(), "f", decimals)
    }

    function quantityText(value) {
        if (value === null || value === undefined || value === "")
            return "---"
        const number = Number(value)
        if (isNaN(number))
            return "---"
        return number.toLocaleString(Qt.locale(), "f", 6)
            .replace(/([,.]\d*?[1-9])0+$/, "$1")
            .replace(/[,.]0+$/, "")
    }

    function positionValueText(value) {
        if (value === null || value === undefined || value === "")
            return "---"
        const number = Number(value)
        return isNaN(number)
            ? "---"
            : number.toLocaleString(Qt.locale(), "f", 2)
    }

    function positionValue(stock) {
        const price = Number(stock.currentPrice)
        const quantity = Number(stock.quantity)
        if (isNaN(price) || isNaN(quantity))
            return 0
        return price * quantity
    }

    function totalPositionValue() {
        let total = 0
        for (let index = 0; index < observedStocks.length; ++index)
            total += positionValue(observedStocks[index])
        return total
    }

    function tradeRepublicPositionValue(stock) {
        if (stock.trCurrentPrice === null
                || stock.trCurrentPrice === undefined
                || stock.trQuantity === null
                || stock.trQuantity === undefined)
            return null
        const price = Number(stock.trCurrentPrice)
        const quantity = Number(stock.trQuantity)
        if (isNaN(price) || isNaN(quantity))
            return null
        return price * quantity
    }

    function totalTradeRepublicPositionValue() {
        let total = 0
        let valueCount = 0
        for (let index = 0; index < observedStocks.length; ++index) {
            const value = tradeRepublicPositionValue(observedStocks[index])
            if (value !== null) {
                total += value
                valueCount += 1
            }
        }
        return valueCount > 0 ? total : null
    }

    function loadObservedStocks() {
        const rows = dbManager
            ? dbManager.getObservedStocksForDepotCheck(currentDepotId)
            : []
        const stocks = []
        for (let index = 0; index < rows.length; ++index) {
            stocks.push({
                name: rows[index].name || "",
                isin: rows[index].isin || "",
                currentPrice: rows[index].currentPrice,
                quantity: rows[index].quantity,
                trCurrentPrice: null,
                trQuantity: null,
                checked: false
            })
        }
        observedStocks = stocks
        checkMessage = ""
        checkMessageColor = "#475569"
    }

    function normalizedName(value) {
        let text = String(value || "").toLowerCase()
        if (text.normalize)
            text = text.normalize("NFD").replace(/[\u0300-\u036f]/g, "")
        return text
            .replace(/ß/g, "ss")
            .replace(/&/g, " und ")
            .replace(/\b(gds|gdr)\b/g, "depositaryreceipt")
            .replace(/\b(ag|se|inc|incorporated|corp|corporation|co|company|ltd|limited|plc|nv|sa|spa|oyj|holdings?|hldg|na|nk|nn|on|o|n)\b/g, " ")
            .replace(/[^a-z0-9]+/g, " ")
            .replace(/\s+/g, " ")
            .trim()
    }

    function shareClass(value) {
        const text = String(value || "").toUpperCase().trim()
        let match = /\(([A-Z])\)\s*$/.exec(text)
        if (!match)
            match = /A\/S[-\s]+([A-Z])\s*$/.exec(text)
        if (!match)
            match = /(?:CLASS|CL)[-\s]+([A-Z])\s*$/.exec(text)
        if (!match)
            match = /(?:^|[^A-Z0-9])([A-D])(?:\s+(?:N[AKN]|O\.?N\.?|[0-9]+))*\s*$/.exec(text)
        return match ? match[1] : ""
    }

    function companyNamePartMatches(left, right) {
        if (left === right)
            return true
        const shorterLength = Math.min(left.length, right.length)
        return shorterLength >= 5
            && (left.indexOf(right) === 0 || right.indexOf(left) === 0)
    }

    function isGenericCompanyNamePart(part) {
        const genericParts = [
            "bank", "group", "holding", "holdings", "international",
            "energy", "pharma", "pharmaceutical", "solutions", "systems"
        ]
        return genericParts.some(function(genericPart) {
            return companyNamePartMatches(part, genericPart)
        })
    }

    function positionMatchStrength(stock, position) {
        const stockIsin = String(stock.isin || "").toUpperCase()
        const positionIsin = String(position.isin || "").toUpperCase()
        if (stockIsin && positionIsin)
            return stockIsin === positionIsin ? 3 : 0

        const stockClass = shareClass(stock.name)
        const positionClass = shareClass(position.name)
        if (stockClass && positionClass && stockClass !== positionClass)
            return 0

        const stockName = normalizedName(stock.name)
        const positionName = normalizedName(position.name)
        if (!stockName || !positionName)
            return 0
        if (stockName === positionName)
            return 2
        // Generic terms (including abbreviations) cannot identify a company.
        const hasCompanyPart = stockName.split(" ").some(function(stockPart) {
            return stockPart.length >= 3 && !/^\d+$/.test(stockPart)
                && !isGenericCompanyNamePart(stockPart)
                && positionName.split(" ").some(function(positionPart) {
                    return !isGenericCompanyNamePart(positionPart)
                        && companyNamePartMatches(stockPart, positionPart)
                })
        })
        if (!hasCompanyPart)
            return 0
        if (Math.min(stockName.length, positionName.length) >= 4
                && (stockName.indexOf(positionName) >= 0
                    || positionName.indexOf(stockName) >= 0))
            return 1

        const stockParts = stockName.split(" ").filter(function(part) {
            return part.length >= 3 && !/^\d+$/.test(part)
        })
        const positionParts = positionName.split(" ").filter(function(part) {
            return part.length >= 3 && !/^\d+$/.test(part)
        })
        const commonParts = []
        for (let stockPartIndex = 0;
                stockPartIndex < stockParts.length;
                ++stockPartIndex) {
            const stockPart = stockParts[stockPartIndex]
            for (let positionPartIndex = 0;
                    positionPartIndex < positionParts.length;
                    ++positionPartIndex) {
                const positionPart = positionParts[positionPartIndex]
                if (companyNamePartMatches(stockPart, positionPart)
                        && commonParts.indexOf(stockPart) < 0)
                    commonParts.push(stockPart)
            }
        }

        if (commonParts.length >= 2) {
            const shorterNamePartCount = Math.min(stockParts.length, positionParts.length)
            if (commonParts.length / Math.max(1, shorterNamePartCount) >= 0.5)
                return 1
        }

        if (commonParts.length === 1
                && commonParts[0].length >= 6
                && !isGenericCompanyNamePart(commonParts[0]))
            return 1
        return 0
    }

    function applyPortfolioCheck(result) {
        checkingPositions = false

        if (!result || !result.success) {
            const resetRows = []
            for (let index = 0; index < observedStocks.length; ++index) {
                const stock = observedStocks[index]
                resetRows.push({
                    name: stock.name,
                    isin: stock.isin,
                    currentPrice: stock.currentPrice,
                    quantity: stock.quantity,
                    trCurrentPrice: null,
                    trQuantity: null,
                    checked: false
                })
            }
            observedStocks = resetRows
            checkMessage = result && result.message
                ? result.message
                : "Die Trade-Republic-Positionen konnten nicht gelesen werden."
            checkMessageColor = "#b91c1c"
            return
        }

        const positions = result.positions || []
        const usedPositions = {}
        const checkedRows = []
        const missingLocal = []
        let matchedCount = 0

        // Reserve ISIN and exact name matches before considering fuzzy names.
        const matchedPositions = {}
        for (let strength = 3; strength >= 1; --strength) {
            for (let stockIndex = 0; stockIndex < observedStocks.length; ++stockIndex) {
                if (matchedPositions[stockIndex] !== undefined)
                    continue
                for (let positionIndex = 0; positionIndex < positions.length; ++positionIndex) {
                    if (!usedPositions[positionIndex]
                            && positionMatchStrength(observedStocks[stockIndex],
                                positions[positionIndex]) === strength) {
                        matchedPositions[stockIndex] = positionIndex
                        usedPositions[positionIndex] = true
                        matchedCount += 1
                        break
                    }
                }
            }
        }

        for (let stockIndex = 0; stockIndex < observedStocks.length; ++stockIndex) {
            const stock = observedStocks[stockIndex]
            const matched = matchedPositions[stockIndex] !== undefined
            const matchedPosition = matched ? positions[matchedPositions[stockIndex]] : null
            checkedRows.push({
                name: stock.name,
                isin: stock.isin,
                currentPrice: stock.currentPrice,
                quantity: stock.quantity,
                trCurrentPrice: matchedPosition
                    ? matchedPosition.currentPrice
                    : null,
                trQuantity: matchedPosition
                    ? matchedPosition.quantity
                    : null,
                checked: matched
            })
            if (!matched)
                missingLocal.push(stock.name)
        }
        observedStocks = checkedRows

        const onlyAtTradeRepublic = []
        for (let positionIndex = 0; positionIndex < positions.length; ++positionIndex) {
            if (!usedPositions[positionIndex]) {
                const position = positions[positionIndex]
                onlyAtTradeRepublic.push(position.name || position.isin || "Unbekannt")
            }
        }

        const isinCount = Number(result.isinCount || 0)
        const quantityCount = Number(result.quantityCount || 0)
        const priceCount = Number(result.priceCount || 0)
        const messages = [
            positions.length + " Trade-Republic-Positionen gelesen, davon "
                + isinCount + " mit ISIN, " + quantityCount
                + " mit Stückzahl und " + priceCount + " mit Kurs.",
            matchedCount + " von " + observedStocks.length
                + " lokalen Positionen stimmen überein."
        ]
        if (missingLocal.length > 0)
            messages.push("Lokal vorhanden, bei Trade Republic fehlend: " + missingLocal.join(", "))
        if (onlyAtTradeRepublic.length > 0)
            messages.push("Bei Trade Republic vorhanden, lokal fehlend: " + onlyAtTradeRepublic.join(", "))
        if (missingLocal.length === 0 && onlyAtTradeRepublic.length === 0)
            messages.push("Alle Positionen stimmen überein.")

        checkMessage = messages.join("  ")
        checkMessageColor = missingLocal.length > 0 || onlyAtTradeRepublic.length > 0
            ? "#b91c1c"
            : "#15803d"
    }

    function checkTradeRepublicPositions() {
        if (!browserWindow) {
            applyPortfolioCheck({
                success: false,
                message: "Das Trade-Republic-Fenster ist nicht verfügbar."
            })
            return
        }
        checkingPositions = true
        checkMessage = "Trade-Republic-Positionen werden gelesen ..."
        checkMessageColor = "#475569"
        browserWindow.readPortfolioPositions(function(result) {
            checkDepotsWindow.applyPortfolioCheck(result)
        })
    }

    function openBelowBrowser(left, top, targetWidth, targetHeight, depotId) {
        currentDepotId = depotId
        loadObservedStocks()

        checkDepotsWindow.showNormal()
        checkDepotsWindow.show()
        checkDepotsWindow.x = Number(left)
        checkDepotsWindow.y = Number(top)
        checkDepotsWindow.width = Math.max(minimumWidth, Number(targetWidth))
        checkDepotsWindow.height = Math.max(minimumHeight, Number(targetHeight))
        checkDepotsWindow.raise()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28

            Label {
                text: "Beobachtete Aktien (" + checkDepotsWindow.observedStocks.length + ")"
                font.bold: true
                Layout.fillWidth: true
            }

            Button {
                text: "Check Positionen gegen TR Positionen"
                Layout.preferredWidth: 235
                enabled: !checkDepotsWindow.checkingPositions
                    && checkDepotsWindow.browserWindow !== null
                onClicked: checkDepotsWindow.checkTradeRepublicPositions()
            }

            Button {
                text: "Aktualisieren"
                onClicked: checkDepotsWindow.loadObservedStocks()
            }
        }

        ScrollView {
            visible: checkDepotsWindow.checkMessage.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 58 : 0
            contentWidth: availableWidth

            TextArea {
                width: parent.width
                text: checkDepotsWindow.checkMessage
                color: checkDepotsWindow.checkMessageColor
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#dbe4ea"
            border.color: "#b4c0c8"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Label {
                    text: "Checked"
                    font.bold: true
                    Layout.preferredWidth: 70
                    horizontalAlignment: Text.AlignHCenter
                }
                Label { text: "Name"; font.bold: true; Layout.fillWidth: true }
                Label {
                    text: "Aktueller Preis"
                    font.bold: true
                    Layout.preferredWidth: 120
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    text: "Stückzahl"
                    font.bold: true
                    Layout.preferredWidth: 90
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    text: "Positionswert"
                    font.bold: true
                    Layout.preferredWidth: 120
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    text: "Aktueller Preis (TR)"
                    font.bold: true
                    Layout.preferredWidth: 135
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    text: "Stückzahl (TR)"
                    font.bold: true
                    Layout.preferredWidth: 100
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    text: "Positionswert (TR)"
                    font.bold: true
                    Layout.preferredWidth: 130
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        ListView {
            id: observedStocksList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: checkDepotsWindow.observedStocks

            delegate: Rectangle {
                id: observedStockDelegate
                required property var modelData
                required property int index
                width: observedStocksList.width
                height: 30
                color: index % 2 === 0 ? "#ffffff" : "#eef2f4"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8

                    Item {
                        Layout.preferredWidth: 70
                        Layout.fillHeight: true

                        Rectangle {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            radius: 9
                            color: Boolean(modelData.checked) ? "#16a34a" : "#dc2626"

                            Label {
                                anchors.centerIn: parent
                                text: Boolean(modelData.checked) ? "\u2713" : "\u00d7"
                                color: "#ffffff"
                                font.bold: true
                                font.pixelSize: 13
                            }
                        }
                    }
                    Label {
                        text: modelData.name || "-"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: checkDepotsWindow.priceText(modelData.currentPrice)
                        Layout.preferredWidth: 120
                        horizontalAlignment: Text.AlignRight
                        font.bold: true
                    }
                    Label {
                        text: checkDepotsWindow.quantityText(modelData.quantity)
                        Layout.preferredWidth: 90
                        horizontalAlignment: Text.AlignRight
                    }
                    Label {
                        text: checkDepotsWindow.positionValueText(
                            checkDepotsWindow.positionValue(modelData)
                        )
                        Layout.preferredWidth: 120
                        horizontalAlignment: Text.AlignRight
                        font.bold: true
                    }
                    Label {
                        text: checkDepotsWindow.priceText(
                            modelData.trCurrentPrice
                        )
                        Layout.preferredWidth: 135
                        horizontalAlignment: Text.AlignRight
                        font.bold: true
                    }
                    Label {
                        text: checkDepotsWindow.quantityText(
                            modelData.trQuantity
                        )
                        Layout.preferredWidth: 100
                        horizontalAlignment: Text.AlignRight
                    }
                    Label {
                        text: checkDepotsWindow.positionValueText(
                            checkDepotsWindow.tradeRepublicPositionValue(
                                modelData
                            )
                        )
                        Layout.preferredWidth: 130
                        horizontalAlignment: Text.AlignRight
                        font.bold: true
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "#dbe4ea"
            border.color: "#9aa8b2"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Item { Layout.preferredWidth: 70 }
                Label {
                    text: "Gesamtwert"
                    font.bold: true
                    Layout.fillWidth: true
                }
                Item { Layout.preferredWidth: 120 }
                Item { Layout.preferredWidth: 90 }
                Label {
                    text: checkDepotsWindow.positionValueText(
                        checkDepotsWindow.totalPositionValue()
                    )
                    font.bold: true
                    Layout.preferredWidth: 120
                    horizontalAlignment: Text.AlignRight
                }
                Item { Layout.preferredWidth: 135 }
                Item { Layout.preferredWidth: 100 }
                Label {
                    text: checkDepotsWindow.positionValueText(
                        checkDepotsWindow.totalTradeRepublicPositionValue()
                    )
                    font.bold: true
                    Layout.preferredWidth: 130
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }
}
