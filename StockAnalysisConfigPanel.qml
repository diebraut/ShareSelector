import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
GroupBox {
    id: stockAnalysisSearchParametersGroupBox
    property var app
    property var hostWindow
    property var configModel
    property var quoteModel
    property alias configNameText: stockAnalysisConfigNameInput.text
    property alias increaseText: stockAnalysisIncreaseInput.text
    property alias directSearchIsinText: directSearchIsinInput.text
    property alias directSearchNameText: directSearchNameInput.text
    property bool directSearchActive: directSearchActivateCheckBox.checked
    property var directSearchTradingDayValues: [10, 20, 30, 40, 50, 60, 70, 80, 90]
    function directSearchTradingDayIndex(days) {
        let normalizedDays = Number(days || 90)
        for (let i = 0; i < directSearchTradingDayValues.length; i++) {
            if (directSearchTradingDayValues[i] === normalizedDays)
                return i
        }
        return directSearchTradingDayValues.length - 1
    }
                    title: "Suchparameter"
                    topPadding: 22
                    label: Label {
                        text: stockAnalysisSearchParametersGroupBox.title
                        x: 10
                        y: 0
                        padding: 2
                        font.bold: true
                        background: Rectangle { color: "#f4f6f7" }
                    }
                    background: Rectangle {
                        y: stockAnalysisSearchParametersGroupBox.label.height / 2
                        width: parent.width
                        height: parent.height - y
                        color: "transparent"
                        border.color: "#8b8b8b"
                        border.width: 1
                    }
                    Layout.fillWidth: true
                    Layout.preferredHeight: 310
                    Layout.minimumHeight: 300
                    clip: false

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: Math.max(300, hostWindow.width * 0.22)
                            Layout.maximumWidth: Math.max(300, hostWindow.width * 0.22)
                            Layout.fillHeight: true
                            Layout.alignment: Qt.AlignTop
                            color: "#ffffff"
                            opacity: directSearchActive ? 0.55 : 1
                            enabled: !directSearchActive
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
                                    enabled: !directSearchActive
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
                                        enabled: !directSearchActive
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        clip: true
                                        model: configModel
                                        currentIndex: app.selectedStockAnalysisConfigIndex
                                        boundsBehavior: Flickable.StopAtBounds
                                        ScrollBar.vertical: ScrollBar {
                                            policy: configModel.count > stockAnalysisConfigListView.height / 32
                                                ? ScrollBar.AlwaysOn
                                                : ScrollBar.AsNeeded
                                        }

                                        delegate: Rectangle {
                                            width: ListView.view.width
                                            height: 32
                                            color: app.selectedStockAnalysisConfigIndex === index ? "lightsteelblue" : (index % 2 === 0 ? "#ffffff" : "#eef2f4")

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
                                                onClicked: app.selectStockAnalysisConfig(index)
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

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                spacing: 8

                                GridLayout {
                                    id: stockAnalysisSearchParameterGrid
                                    Layout.preferredWidth: 734
                                    Layout.alignment: Qt.AlignTop
                                    columns: 3
                                    columnSpacing: 8
                                    rowSpacing: 6

                                Item { Layout.preferredWidth: 330 }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 446
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
                                    Layout.preferredWidth: 446
                                    spacing: 8

                                    TextField {
                                        id: stockAnalysisIncreaseInput
                                        enabled: !directSearchActive
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
                                    text: app.stockAnalysisStockSelected ? Number(app.stockAnalysisActualIncreasePercent || 0).toFixed(1) + "%" : "---"
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
                                    Layout.preferredWidth: 446
                                    spacing: 8

                                    Slider {
                                        id: stockAnalysisCorridorSlider
                                        enabled: !directSearchActive
                                        from: 0
                                        to: 50
                                        stepSize: 2
                                        snapMode: Slider.SnapAlways
                                        value: app.stockAnalysisCorridorPercent
                                        Layout.fillWidth: true
                                        onMoved: {
                                            app.stockAnalysisCorridorPercent = Math.round(value / 2) * 2
                                        }
                                    }

                                    Label {
                                        text: Number(app.stockAnalysisCorridorPercent || 0).toFixed(0) + "%"
                                        Layout.preferredWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: app.stockAnalysisStockSelected ? Number(app.stockAnalysisRequiredCorridorPercent || 0).toFixed(1) + "%" : "---"
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
                                    Layout.preferredWidth: 446
                                    spacing: 8

                                    Slider {
                                        id: stockAnalysisCorridorRequiredSlider
                                        enabled: !directSearchActive
                                        from: 0
                                        to: 100
                                        stepSize: 2
                                        snapMode: Slider.SnapAlways
                                        value: app.stockAnalysisCorridorRequiredPercent
                                        Layout.fillWidth: true
                                        onMoved: {
                                            app.stockAnalysisCorridorRequiredPercent = Math.round(value / 2) * 2
                                        }
                                    }

                                    Label {
                                        text: Number(app.stockAnalysisCorridorRequiredPercent || 0).toFixed(0) + "%"
                                        Layout.preferredWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: app.stockAnalysisStockSelected ? Number(app.stockAnalysisCorridorHitPercent || 0).toFixed(1) + "%" : "---"
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
                                    Layout.preferredWidth: 446
                                    spacing: 8

                                    Slider {
                                        id: stockAnalysisQuoteCountSlider
                                        enabled: true
                                        from: 10
                                        to: 90
                                        stepSize: 10
                                        snapMode: Slider.SnapAlways
                                        live: true
                                        value: app.stockAnalysisQuoteCount
                                        Layout.fillWidth: true
                                        onMoved: app.setStockAnalysisQuoteCount(value, false)
                                        onValueChanged: {
                                            if (pressed)
                                                app.setStockAnalysisQuoteCount(value, false)
                                        }
                                        onPressedChanged: {
                                            if (!pressed)
                                                app.setStockAnalysisQuoteCount(value, true)
                                        }
                                    }

                                    Label {
                                        text: Number(app.stockAnalysisQuoteCount || 90).toFixed(0)
                                        Layout.preferredWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: app.stockAnalysisStockSelected
                                        ? quoteModel.count + " Werte"
                                        : "---"
                                    Layout.preferredWidth: 170
                                    Layout.maximumWidth: 170
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    font.bold: true
                                    color: "#475569"
                                    elide: Text.ElideRight
                                    clip: true
                                }

                                Label {
                                    text: "Größter Kursrückgang während Laufzeit"
                                    Layout.preferredWidth: 330
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 446
                                    spacing: 8

                                    Slider {
                                        id: stockAnalysisMaxDrawdownSlider
                                        enabled: !directSearchActive
                                        from: 0
                                        to: 100
                                        stepSize: 2
                                        snapMode: Slider.SnapAlways
                                        value: app.stockAnalysisMaxDrawdownPercent
                                        Layout.fillWidth: true
                                        onMoved: {
                                            app.stockAnalysisMaxDrawdownPercent = Math.round(value / 2) * 2
                                        }
                                    }

                                    Label {
                                        text: Number(app.stockAnalysisMaxDrawdownPercent || 0).toFixed(0) + "%"
                                        Layout.preferredWidth: 44
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                Label {
                                    text: app.stockAnalysisStockSelected ? Number(app.stockAnalysisActualMaxDrawdownPercent || 0).toFixed(1) + "%" : "---"
                                    Layout.preferredWidth: 100
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                    font.bold: true
                                    color: "#475569"
                                }

                                }

                                ColumnLayout {
                                    Layout.fillWidth: false
                                    Layout.leftMargin: -115
                                    Layout.preferredWidth: 368
                                    Layout.minimumWidth: 368
                                    Layout.maximumWidth: 368
                                    Layout.preferredHeight: stockAnalysisSearchParameterGrid.implicitHeight + 36
                                    Layout.maximumHeight: stockAnalysisSearchParameterGrid.implicitHeight + 36
                                    spacing: 2

                                    CheckBox {
                                        id: stockAnalysisHideBoughtCheckBox
                                        enabled: !directSearchActive
                                        text: "Gekaufte Stocks nicht anzeigen"
                                        checked: app.stockAnalysisHideBoughtStocks
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 28
                                        onCheckedChanged: {
                                            app.stockAnalysisHideBoughtStocks = checked
                                            if (checked) {
                                                let removed = app.removeBoughtStocksFromStockAnalysisResults()
                                                if (removed > 0)
                                                    app.stockAnalysisMessage = removed + " gekaufte Stocks ausgeblendet"
                                            }
                                        }
                                    }

                                    GroupBox {
                                        id: directSearchGroupBox
                                        title: "Direkt Suche"
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        Layout.minimumHeight: 184
                                        topPadding: 22
                                        leftPadding: 12
                                        rightPadding: 12
                                        bottomPadding: 10

                                        label: Label {
                                            visible: directSearchActive
                                            text: directSearchGroupBox.title
                                            x: 10
                                            y: 0
                                            padding: 2
                                            font.bold: true
                                            background: Rectangle { color: "#f4f6f7" }
                                        }

                                        background: Rectangle {
                                            visible: directSearchActive
                                            y: directSearchGroupBox.label.height / 2
                                            width: parent.width
                                            height: parent.height - y
                                            color: "transparent"
                                            border.color: "#8b8b8b"
                                            border.width: 1
                                        }

                                        GridLayout {
                                            anchors.fill: parent
                                            columns: 2
                                            columnSpacing: 8
                                            rowSpacing: 6

                                            Item { Layout.columnSpan: 2; Layout.preferredHeight: 0 }

                                            Label {
                                                visible: directSearchActive
                                                text: "ISIN"
                                                opacity: directSearchActive ? 1 : 0.45
                                                Layout.preferredWidth: 156
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            TextField {
                                                id: directSearchIsinInput
                                                visible: directSearchActive
                                                enabled: directSearchActive
                                                opacity: directSearchActive ? 1 : 0.55
                                                Layout.preferredWidth: 180
                                                Layout.maximumWidth: 180
                                                placeholderText: "ISIN"
                                            }

                                            Label {
                                                visible: directSearchActive
                                                text: "Name"
                                                opacity: directSearchActive ? 1 : 0.45
                                                Layout.preferredWidth: 156
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            TextField {
                                                id: directSearchNameInput
                                                visible: directSearchActive
                                                enabled: directSearchActive
                                                opacity: directSearchActive ? 1 : 0.55
                                                Layout.preferredWidth: 180
                                                Layout.maximumWidth: 180
                                                placeholderText: "Name, Wildcard *"
                                            }

                                            Label {
                                                visible: directSearchActive
                                                text: "Anzeige f\u00fcr Handelstage"
                                                opacity: directSearchActive ? 1 : 0.45
                                                Layout.preferredWidth: 156
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            RowLayout {
                                                visible: directSearchActive
                                                enabled: directSearchActive
                                                opacity: directSearchActive ? 1 : 0.55
                                                Layout.preferredWidth: 180
                                                Layout.maximumWidth: 180
                                                spacing: 8

                                                Slider {
                                                    id: directSearchTradingDaySlider
                                                    from: 0
                                                    to: stockAnalysisSearchParametersGroupBox.directSearchTradingDayValues.length - 1
                                                    stepSize: 1
                                                    snapMode: Slider.SnapAlways
                                                    live: true
                                                    value: stockAnalysisSearchParametersGroupBox.directSearchTradingDayIndex(app.stockAnalysisQuoteCount)
                                                    Layout.fillWidth: true
                                                    onMoved: app.setStockAnalysisQuoteCount(
                                                        stockAnalysisSearchParametersGroupBox.directSearchTradingDayValues[Math.round(value)],
                                                        false
                                                    )
                                                    onPressedChanged: {
                                                        if (!pressed)
                                                            app.setStockAnalysisQuoteCount(
                                                                stockAnalysisSearchParametersGroupBox.directSearchTradingDayValues[Math.round(value)],
                                                                true
                                                            )
                                                    }
                                                }

                                                Label {
                                                    text: Number(app.stockAnalysisQuoteCount || 90).toFixed(0)
                                                    Layout.preferredWidth: 34
                                                    horizontalAlignment: Text.AlignRight
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                            }

                                            Item { Layout.columnSpan: 2; Layout.preferredHeight: 4 }

                                            RowLayout {
                                                Layout.columnSpan: 2
                                                Layout.fillWidth: true
                                                Layout.alignment: Qt.AlignRight | Qt.AlignBottom

                                                Item { Layout.fillWidth: true }

                                                Label {
                                                    text: "Direktsuche aktivieren"
                                                    verticalAlignment: Text.AlignVCenter
                                                }

                                                CheckBox {
                                                    id: directSearchActivateCheckBox
                                                    text: ""
                                                    checked: false
                                                    onCheckedChanged: app.resetStockAnalysisForDirectSearch(checked)
                                                }
                                            }
                                        }
                                    }
                                }

                            }

                            Item { Layout.preferredHeight: 1 }
                        }

                        ColumnLayout {
                            Layout.alignment: Qt.AlignRight | Qt.AlignTop
                            Layout.preferredWidth: 110
                            Layout.fillHeight: true
                            spacing: 8

                            Button {
                                text: app.stockAnalysisScanActive ? "Stoppen" : "Suchen"
                                Layout.preferredWidth: 100
                                onClicked: app.stockAnalysisScanActive ? app.stopStockAnalysisScan() : app.startStockAnalysisSearch()
                            }

                            Item { Layout.preferredHeight: directSearchActive ? 80 : 92 }

                            Button {
                                text: "Depot"
                                Layout.preferredWidth: 100
                                onClicked: app.openPortfolioWindow()
                            }

                            Item { Layout.preferredHeight: 10 }

                            Button {
                                enabled: !directSearchActive
                                text: "Neuanlage"
                                Layout.preferredWidth: 100
                                onClicked: app.newStockAnalysisConfig()
                            }

                            Button {
                                enabled: !directSearchActive
                                text: "\u00c4ndern"
                                Layout.preferredWidth: 100
                                onClicked: app.saveStockAnalysisConfig()
                            }
                        }
                    }
                }

