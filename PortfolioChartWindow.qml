import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

    Window {
    id: portfolioChartWindow

    property var portfolioChartStock: ({})
    property var portfolioChartData: ({})

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

    function openForStock(row, data) {
        if (!row || !row.symbol)
            return

        portfolioChartStock = row
        portfolioChartData = data || ({})
        portfolioChartQuoteModel.clear()
        let quotes = portfolioChartData.quotes || []
        quotes.forEach(quote => portfolioChartQuoteModel.append(quote))
        title = row.symbol + " - Depot Verlauf"
        show()
        raise()
        requestActivate()
        portfolioChart.requestPaint()
    }

    ListModel {
        id: portfolioChartQuoteModel
        dynamicRoles: true
    }
        title: "Depot Verlauf"
        width: 1180
        height: 720
        minimumWidth: 900
        minimumHeight: 560
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
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: cleanDisplayText(portfolioChartStock.name || portfolioChartStock.symbol || "")
                            font.pixelSize: 22
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Label {
                            text: "Zeitraum: " + (portfolioChartData.start90 || "-") + " bis " + (portfolioChartData.latestDate || "-")
                                + " | letzter Schlusskurs: " + Number(portfolioChartData.latestClose || 0).toFixed(2)
                            color: "#4f5b62"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        text: "Schließen"
                        onClicked: portfolioChartWindow.close()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: [
                            { label: "20", start: portfolioChartData.start20, avg: portfolioChartData.avg20, inc: portfolioChartData.inc20, count: portfolioChartData.quoteCount20, color: "#2563eb" },
                            { label: "40", start: portfolioChartData.start40, avg: portfolioChartData.avg40, inc: portfolioChartData.inc40, count: portfolioChartData.quoteCount40, color: "#059669" },
                            { label: "60", start: portfolioChartData.start60, avg: portfolioChartData.avg60, inc: portfolioChartData.inc60, count: portfolioChartData.quoteCount60, color: "#d97706" },
                            { label: "90", start: portfolioChartData.start90, avg: portfolioChartData.avg90, inc: portfolioChartData.inc90, count: portfolioChartData.quoteCount90, color: "#7c3aed" }
                        ]

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 82
                            color: "#ffffff"
                            border.color: "#d3d8dc"
                            radius: 4

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2

                                Label {
                                    text: modelData.label + " Handelstage ab " + (modelData.start || "-")
                                    font.bold: true
                                    color: modelData.color
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: "Mittel " + Number(modelData.avg || 0).toFixed(2) + " | " + (Number(modelData.inc || 0) >= 0 ? "+" : "") + Number(modelData.inc || 0).toFixed(2) + "%"
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: Number(modelData.count || 0).toFixed(0) + " Quotes im Bereich"
                                    color: "#4f5b62"
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#ffffff"
                    border.color: "#c9d0d5"
                    border.width: 1

                    Canvas {
                        id: portfolioChart
                        anchors.fill: parent
                        anchors.margins: 12
                        property int hoveredQuoteIndex: -1

                        onPaint: {
                            let ctx = getContext("2d")
                            ctx.reset()
                            ctx.clearRect(0, 0, width, height)

                            let count = portfolioChartQuoteModel.count
                            if (count === 0) {
                                ctx.fillStyle = "#66727a"
                                ctx.font = "14px sans-serif"
                                ctx.fillText("Keine Kursdaten für diese Depot-Position", 12, 28)
                                return
                            }

                            function dateMs(value) {
                                let parts = String(value || "").split("-")
                                if (parts.length !== 3)
                                    return NaN
                                return new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2])).getTime()
                            }

                            let firstMs = dateMs(portfolioChartData.start90 || portfolioChartQuoteModel.get(0).closeDate)
                            let lastMs = dateMs(portfolioChartData.latestDate || portfolioChartQuoteModel.get(count - 1).closeDate)
                            if (!isFinite(firstMs))
                                firstMs = dateMs(portfolioChartQuoteModel.get(0).closeDate)
                            if (!isFinite(lastMs) || lastMs <= firstMs)
                                lastMs = dateMs(portfolioChartQuoteModel.get(count - 1).closeDate)
                            let timeRange = Math.max(1, lastMs - firstMs)

                            let minPrice = Number(portfolioChartQuoteModel.get(0).closePrice)
                            let maxPrice = minPrice
                            let periods = [
                                { label: "90", start: portfolioChartData.start90, avg: Number(portfolioChartData.avg90 || 0), inc: Number(portfolioChartData.inc90 || 0), color: "#7c3aed" },
                                { label: "60", start: portfolioChartData.start60, avg: Number(portfolioChartData.avg60 || 0), inc: Number(portfolioChartData.inc60 || 0), color: "#d97706" },
                                { label: "40", start: portfolioChartData.start40, avg: Number(portfolioChartData.avg40 || 0), inc: Number(portfolioChartData.inc40 || 0), color: "#059669" },
                                { label: "20", start: portfolioChartData.start20, avg: Number(portfolioChartData.avg20 || 0), inc: Number(portfolioChartData.inc20 || 0), color: "#2563eb" }
                            ]
                            for (let i = 0; i < count; i++) {
                                let price = Number(portfolioChartQuoteModel.get(i).closePrice)
                                minPrice = Math.min(minPrice, price)
                                maxPrice = Math.max(maxPrice, price)
                            }
                            for (let p = 0; p < periods.length; p++) {
                                if (periods[p].avg > 0) {
                                    minPrice = Math.min(minPrice, periods[p].avg)
                                    maxPrice = Math.max(maxPrice, periods[p].avg)
                                }
                            }

                            let priceRange = Math.max(0.0001, maxPrice - minPrice)
                            let padding = priceRange * 0.08
                            minPrice -= padding
                            maxPrice += padding
                            priceRange = Math.max(0.0001, maxPrice - minPrice)

                            let leftPad = 70
                            let rightPad = 28
                            let topPad = 30
                            let bottomPad = 72
                            let plotWidth = Math.max(1, width - leftPad - rightPad)
                            let plotHeight = Math.max(1, height - topPad - bottomPad)

                            function xForDate(value) {
                                let ms = dateMs(value)
                                if (!isFinite(ms))
                                    return leftPad
                                return leftPad + Math.max(0, Math.min(1, (ms - firstMs) / timeRange)) * plotWidth
                            }

                            function yForPrice(price) {
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
                                let gridPrice = maxPrice - ratio * priceRange
                                ctx.strokeStyle = grid === 4 ? "#c9d0d5" : "#edf1f3"
                                ctx.beginPath()
                                ctx.moveTo(leftPad, yGrid)
                                ctx.lineTo(leftPad + plotWidth, yGrid)
                                ctx.stroke()
                                ctx.fillStyle = "#4f5b62"
                                ctx.fillText(gridPrice.toFixed(2), 4, yGrid + 4)
                            }

                            for (let band = 0; band < periods.length; band++) {
                                let startX = xForDate(periods[band].start)
                                ctx.fillStyle = band % 2 === 0 ? "rgba(37, 99, 235, 0.035)" : "rgba(5, 150, 105, 0.035)"
                                ctx.fillRect(startX, topPad, leftPad + plotWidth - startX, plotHeight)
                            }

                            ctx.strokeStyle = "#cbd5dc"
                            ctx.lineWidth = 1.5
                            ctx.beginPath()
                            for (let j = 0; j < count; j++) {
                                let row = portfolioChartQuoteModel.get(j)
                                let x = xForDate(row.closeDate)
                                let y = yForPrice(Number(row.closePrice))
                                if (j === 0)
                                    ctx.moveTo(x, y)
                                else
                                    ctx.lineTo(x, y)
                            }
                            ctx.stroke()

                            function signedPercent(value) {
                                let numberValue = Number(value || 0)
                                return (numberValue >= 0 ? "+" : "") + numberValue.toFixed(2) + "%"
                            }

                            function quoteIndexOnOrAfter(startDate) {
                                let start = dateMs(startDate)
                                if (!isFinite(start))
                                    return 0
                                for (let i = 0; i < count; i++) {
                                    if (dateMs(portfolioChartQuoteModel.get(i).closeDate) >= start)
                                        return i
                                }
                                return Math.max(0, count - 1)
                            }

                            for (let b = 0; b < periods.length; b++) {
                                let period = periods[b]
                                let startIndex = quoteIndexOnOrAfter(period.start)
                                let startXLine = xForDate(period.start)

                                ctx.strokeStyle = period.color
                                ctx.lineWidth = 1
                                ctx.setLineDash([5, 5])
                                ctx.beginPath()
                                ctx.moveTo(startXLine, topPad)
                                ctx.lineTo(startXLine, topPad + plotHeight)
                                ctx.stroke()
                                ctx.setLineDash([])

                                ctx.strokeStyle = period.color
                                ctx.lineWidth = 1.5
                                ctx.globalAlpha = 0.35
                                ctx.beginPath()
                                for (let q = startIndex; q < count; q++) {
                                    let segmentRow = portfolioChartQuoteModel.get(q)
                                    let sx = xForDate(segmentRow.closeDate)
                                    let sy = yForPrice(Number(segmentRow.closePrice))
                                    if (q === startIndex)
                                        ctx.moveTo(sx, sy)
                                    else
                                        ctx.lineTo(sx, sy)
                                }
                                ctx.stroke()
                                ctx.globalAlpha = 1

                                if (period.avg > 0) {
                                    let latestClose = Number(portfolioChartData.latestClose || portfolioChartQuoteModel.get(count - 1).closePrice || 0)
                                    let latestX = xForDate(portfolioChartData.latestDate || portfolioChartQuoteModel.get(count - 1).closeDate)
                                    let avgY = yForPrice(period.avg)
                                    let latestY = yForPrice(latestClose)

                                    ctx.strokeStyle = period.color
                                    ctx.lineWidth = 3.5
                                    ctx.beginPath()
                                    ctx.moveTo(startXLine, avgY)
                                    ctx.lineTo(latestX, latestY)
                                    ctx.stroke()

                                    ctx.fillStyle = period.color
                                    ctx.beginPath()
                                    ctx.arc(startXLine, avgY, 4, 0, Math.PI * 2)
                                    ctx.arc(latestX, latestY, 4, 0, Math.PI * 2)
                                    ctx.fill()

                                    let labelText = period.label + "T " + signedPercent(period.inc)
                                    let labelX = startXLine + (latestX - startXLine) * 0.52 + 6
                                    let labelY = avgY + (latestY - avgY) * 0.52 - 8 - b * 9
                                    ctx.font = "bold 12px sans-serif"
                                    let labelWidth = ctx.measureText(labelText).width + 10
                                    labelX = Math.min(width - labelWidth - 4, Math.max(leftPad + 4, labelX))
                                    labelY = Math.min(topPad + plotHeight - 8, Math.max(topPad + 14, labelY))
                                    ctx.fillStyle = "rgba(255, 255, 255, 0.88)"
                                    ctx.fillRect(labelX - 4, labelY - 13, labelWidth, 17)
                                    ctx.fillStyle = period.color
                                    ctx.fillText(labelText, labelX, labelY)

                                    ctx.font = "10px sans-serif"
                                    ctx.fillText("Mittel " + period.avg.toFixed(2), Math.min(width - 84, startXLine + 5), Math.max(topPad + 12, avgY - 6))
                                }
                            }

                            let pointEvery = Math.max(1, Math.ceil(count / Math.max(2, Math.floor(plotWidth / 42))))
                            for (let k = 0; k < count; k++) {
                                let point = portfolioChartQuoteModel.get(k)
                                let px = xForDate(point.closeDate)
                                let py = yForPrice(Number(point.closePrice))
                                ctx.fillStyle = portfolioChart.hoveredQuoteIndex === k ? "#b45309" : "#374151"
                                ctx.beginPath()
                                ctx.arc(px, py, portfolioChart.hoveredQuoteIndex === k ? 5 : 2.5, 0, Math.PI * 2)
                                ctx.fill()
                                if (k === 0 || k === count - 1 || k % pointEvery === 0 || portfolioChart.hoveredQuoteIndex === k) {
                                    ctx.fillStyle = "#374151"
                                    ctx.font = "10px sans-serif"
                                    ctx.fillText(Number(point.closePrice).toFixed(2), px - 14, Math.max(10, py - 7))
                                }
                            }

                            let labelDates = [
                                { text: portfolioChartData.start90 || "", color: "#7c3aed" },
                                { text: portfolioChartData.start60 || "", color: "#d97706" },
                                { text: portfolioChartData.start40 || "", color: "#059669" },
                                { text: portfolioChartData.start20 || "", color: "#2563eb" },
                                { text: portfolioChartData.latestDate || "", color: "#4f5b62" }
                            ]
                            for (let d = 0; d < labelDates.length; d++) {
                                if (!labelDates[d].text)
                                    continue
                                let lx = xForDate(labelDates[d].text)
                                ctx.fillStyle = labelDates[d].color
                                ctx.font = "10px sans-serif"
                                ctx.save()
                                ctx.translate(lx - 4, topPad + plotHeight + 58)
                                ctx.rotate(-Math.PI / 4)
                                ctx.fillText(labelDates[d].text, 0, 0)
                                ctx.restore()
                            }

                            if (portfolioChart.hoveredQuoteIndex >= 0 && portfolioChart.hoveredQuoteIndex < count) {
                                let hover = portfolioChartQuoteModel.get(portfolioChart.hoveredQuoteIndex)
                                let hoverPrice = Number(hover.closePrice)
                                let hoverX = xForDate(hover.closeDate)
                                let hoverY = yForPrice(hoverPrice)
                                let tooltipText = (hover.displayDate || hover.closeDate || "") + "  Schlusskurs: " + hoverPrice.toFixed(2)
                                ctx.font = "12px sans-serif"
                                let tooltipWidth = Math.min(270, ctx.measureText(tooltipText).width + 18)
                                let tooltipX = Math.min(width - tooltipWidth - 4, Math.max(4, hoverX - tooltipWidth / 2))
                                let tooltipY = Math.max(4, hoverY - 42)
                                ctx.fillStyle = "#263238"
                                ctx.fillRect(tooltipX, tooltipY, tooltipWidth, 26)
                                ctx.fillStyle = "#ffffff"
                                ctx.fillText(tooltipText, tooltipX + 9, tooltipY + 17)
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onPositionChanged: function(mouse) {
                                let count = portfolioChartQuoteModel.count
                                if (count <= 0) {
                                    portfolioChart.hoveredQuoteIndex = -1
                                    portfolioChart.requestPaint()
                                    return
                                }
                                let bestIndex = -1
                                let bestDistance = 999999
                                let leftPad = 70
                                let rightPad = 28
                                let plotWidth = Math.max(1, portfolioChart.width - leftPad - rightPad)
                                function dateMs(value) {
                                    let parts = String(value || "").split("-")
                                    if (parts.length !== 3)
                                        return NaN
                                    return new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2])).getTime()
                                }
                                let firstMs = dateMs(portfolioChartData.start90 || portfolioChartQuoteModel.get(0).closeDate)
                                let lastMs = dateMs(portfolioChartData.latestDate || portfolioChartQuoteModel.get(count - 1).closeDate)
                                let range = Math.max(1, lastMs - firstMs)
                                for (let i = 0; i < count; i++) {
                                    let ms = dateMs(portfolioChartQuoteModel.get(i).closeDate)
                                    let x = leftPad + Math.max(0, Math.min(1, (ms - firstMs) / range)) * plotWidth
                                    let dist = Math.abs(mouse.x - x)
                                    if (dist < bestDistance) {
                                        bestDistance = dist
                                        bestIndex = i
                                    }
                                }
                                portfolioChart.hoveredQuoteIndex = bestDistance <= 18 ? bestIndex : -1
                                portfolioChart.requestPaint()
                            }
                            onExited: {
                                portfolioChart.hoveredQuoteIndex = -1
                                portfolioChart.requestPaint()
                            }
                        }
                    }
                }
            }
        }
    }
