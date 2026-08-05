import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

GroupBox {
    id: stockAnalysisDisplayGroupBox
    property var app
    property var resultModel
    property var quoteModel

    function requestPaint() {
        stockAnalysisChartCanvas.requestPaint()
    }
                    title: app.selectedStockAnalysisIndex >= 0
                        ? "Darstellung: " + resultModel.get(app.selectedStockAnalysisIndex).name
                            + (app.stockAnalysisQuoteDateRangeText.length > 0 ? " (" + app.stockAnalysisQuoteDateRangeText + ")" : "")
                        : "Darstellung"
                    topPadding: 22
                    label: Label {
                        text: stockAnalysisDisplayGroupBox.title
                        x: 10
                        y: 0
                        padding: 2
                        font.bold: true
                        background: Rectangle { color: "#f4f6f7" }
                    }
                    background: Rectangle {
                        y: stockAnalysisDisplayGroupBox.label.height / 2
                        width: parent.width
                        height: parent.height - y
                        color: "transparent"
                        border.color: "#8b8b8b"
                        border.width: 1
                    }
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: app.height * 0.42

                    Canvas {
                        id: stockAnalysisChartCanvas
                        anchors.fill: parent
                        anchors.margins: 12
                        property int hoveredQuoteIndex: -1

                        onPaint: {
                            let ctx = getContext("2d")
                            ctx.reset()
                            ctx.clearRect(0, 0, width, height)

                            let count = quoteModel.count
                            if (count === 0) {
                                ctx.fillStyle = "#66727a"
                                ctx.font = "14px sans-serif"
                                ctx.fillText("Bitte einen Stock aus der Liste selektieren", 12, 28)
                                return
                            }

                            function quoteAt(chartIndex) {
                                return quoteModel.get(count - 1 - chartIndex)
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
                            let corridorPercent = app.stockAnalysisCorridorPercent
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
                            let tightBandOffset = allAverage * app.requiredCorridorPercentForQuotesModel(quoteModel) / 100
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

                            let maxDrawdown = app.maxDrawdownForQuotesModel(quoteModel)
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

                                ctx.fillStyle = stockAnalysisChartCanvas.hoveredQuoteIndex === k ? "#b45309" : "#2f7d62"
                                ctx.beginPath()
                                ctx.arc(px, py, stockAnalysisChartCanvas.hoveredQuoteIndex === k ? pointRadius + 2 : pointRadius, 0, Math.PI * 2)
                                ctx.fill()

                                if (k === 0 || k === count - 1 || k % valueEvery === 0 || stockAnalysisChartCanvas.hoveredQuoteIndex === k) {
                                    ctx.fillStyle = "#374151"
                                    ctx.font = "10px sans-serif"
                                    ctx.fillText(pointPrice.toFixed(2), px - 14, Math.max(10, py - 7))
                                }
                            }

                            if (stockAnalysisChartCanvas.hoveredQuoteIndex >= 0 && stockAnalysisChartCanvas.hoveredQuoteIndex < count) {
                                let hoverRow = quoteAt(stockAnalysisChartCanvas.hoveredQuoteIndex)
                                let hoverPrice = Number(hoverRow.closeprice)
                                let hoverX = xForIndex(stockAnalysisChartCanvas.hoveredQuoteIndex)
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
                                let count = quoteModel.count
                                if (count <= 0 || stockAnalysisChartCanvas.width <= 92) {
                                    stockAnalysisChartCanvas.hoveredQuoteIndex = -1
                                    stockAnalysisChartCanvas.requestPaint()
                                    return
                                }

                                let leftPad = 68
                                let rightPad = 24
                                let plotWidth = Math.max(1, stockAnalysisChartCanvas.width - leftPad - rightPad)
                                let ratio = Math.max(0, Math.min(1, (mouse.x - leftPad) / plotWidth))
                                stockAnalysisChartCanvas.hoveredQuoteIndex = Math.round(ratio * (count - 1))
                                stockAnalysisChartCanvas.requestPaint()
                            }
                            onExited: {
                                stockAnalysisChartCanvas.hoveredQuoteIndex = -1
                                stockAnalysisChartCanvas.requestPaint()
                            }
                        }
                    }
                }
