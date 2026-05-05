import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

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
        function initSelection() {
            for (var i = 0; i < count; i++) {
                get(i).selected = false;
            }
        }
    }

    ListModel {
        id: detailQuoteModel
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

    Timer {
        id: clipboardTimer
        interval: 2000
        running: false
        repeat: false
        onTriggered: clipboardPopup.visible = false
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
                console.log("⛔ Eingabe blockiert während des Ladens")
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: true
            width: 80
            height: 80
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
            console.log("📥 SIGNAL getSharesComplete:", data.length)
            loadingOverlay.running = false
            updateStockModel(data);
            if (data.length === 0) {
                textItem.text = "Keine Aktien fuer die aktuellen Filter gefunden";
                clipboardPopup.visible = true;
                clipboardTimer.restart();
            }
        }
    }

    function sortStockModelByField(field, ascending) {
        const count = stockModel.count;

        // Hole alle Einträge als echte JS-Objekte
        let data = [];
        for (let i = 0; i < count; i++) {
            const raw = stockModel.get(i);
            data.push(normalizeStockData(raw));  // Jetzt haben wir echte lesbare Properties
        }

        // Sortiere nach dem gewünschten Feld
        data.sort((a, b) => {
            let valA = a[field];
            let valB = b[field];
            return ascending ? valA - valB : valB - valA;
        });

        // Liste leeren und neu befüllen
        stockModel.clear();
        data.forEach(item => stockModel.append(item));
    }

    function normalizeStockData(raw) {
        return {
            symbol: raw.symbol || "",
            name: raw.name || "",
            mic: raw.mic || "",
            lastUpdateDate: raw.lastUpdateDate || "",
            lastClosePrice: raw.lastClosePrice || 0,
            lastClosePriceDate: raw.lastClosePriceDate || "",

            daysFirstPeriodSuccess: raw.daysFirstPeriodSuccess || 0,
            daysSecondPeriodSuccess: raw.daysSecondPeriodSuccess || 0,
            daysThirdPeriodSuccess: raw.daysThirdPeriodSuccess || 0,
            daysFourthPeriodSuccess: raw.daysFourthPeriodSuccess || 0,

            firstPeriodValueInc: raw.firstPeriodValueInc || 0,
            secondPeriodValueInc: raw.secondPeriodValueInc || 0,
            thirdPeriodValueInc: raw.thirdPeriodValueInc || 0,
            fourthPeriodValueInc: raw.fourthPeriodValueInc || 0,

            firstPeriodVolume: raw.firstPeriodVolume || 0,
            secondPeriodVolume: raw.secondPeriodVolume || 0,
            secondPeriodVolume: raw.secondPeriodVolume || 0,
            fourthPeriodVolume: raw.fourthPeriodVolume || 0,

            firstPeriodVolumePrice: raw.firstPeriodVolumePrice || 0,
            secondPeriodVolumePrice: raw.secondPeriodVolumePrice || 0,
            thirdPeriodVolumePrice: raw.thirdPeriodVolumePrice || 0,
            fourthPeriodVolumePrice: raw.fourthPeriodVolumePrice || 0,

            selected: false
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
            stockModel.append({
                "symbol": stock.Symbol,
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
                "firstPeriodVolumePrice": stock.firstperiodvolumeprice,
                selected: false // Initial nicht selektiert
            })
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
        const results = dbManager.searchByTickerAndExchange(tickerInput.text, exchangeInput.text)
        stockModel.clear()
        if (results.length === 0) {
            console.log("❌ Keine Daten gefunden!")
        }

        results.forEach(stock => {
            stockModel.append({
                "symbol": stock.symbol,
                "name": stock.name,
                "exchange": stock.exchange,
                "lastUpdateDate": stock.lastUpdateDate,
                "lastClosePrice": stock.lastClosePrice,
                "lastClosePriceDate": stock.lastClosePriceDate,
                "daysFirstPeriodSuccess": stock.daysFirstPeriodSuccess,
                "daysSecondPeriodSuccess": stock.daysSecondPeriodSuccess,
                "daysThirdPeriodSuccess": stock.daysThirdPeriodSuccess,
                "daysFourthPeriodSuccess": stock.daysFourthPeriodSuccess,
                "firstPeriodValueInc": stock.firstPeriodValueInc,
                "secondPeriodValueInc": stock.secondPeriodValueInc,
                "thirdPeriodValueInc": stock.thirdPeriodValueInc,
                "fourthPeriodValueInc": stock.fourthPeriodValueInc,
                "firstPeriodVolume": stock.firstPeriodVolume,
                "secondPeriodVolume": stock.secondPeriodVolume,
                "thirdPeriodVolume": stock.thirdPeriodVolume,
                "fourthPeriodVolume": stock.fourthPeriodVolume,
                "firstPeriodVolumePrice": stock.firstPeriodVolumePrice,
                "secondPeriodVolumePrice": stock.secondPeriodVolumePrice,
                "thirdPeriodVolumePrice": stock.thirdPeriodVolumePrice,
                "fourthPeriodVolumePrice": stock.fourthPeriodVolumePrice
            })
        })
        loadingOverlay.running = false
        resetSelection()
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
            // Item hinzufügen
            selectedIndices = [...selectedIndices, index]
            selectedSymbols = [...selectedSymbols, stockModel.get(index).symbol]
            selectedExchanges = [...selectedExchanges, stockModel.get(index).mic]
        }

        loadStockQuotesButton.enabled = selectedIndices.length > 0
        console.log("✅ Selektion geändert: ", selectedSymbols, selectedExchanges)
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
                // Füge neuen Bereich hinzu
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
        // Zurücksetzen aller Selektionen im Model
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
        selectionAnchor = index; // Anchor immer auf die zuletzt angewählte Zeile setzen
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
            console.log("✅ Alle historischen Daten wurden geladen");
            return;
        }

        const symbol = selectedSymbols[index];
        const exchange = selectedExchanges[index];
        console.log("📊 Lade Daten für:", symbol, exchange, `(${index+1}/${selectedSymbols.length})`);

        function onSaveComplete(receivedSymbol) {
            if (receivedSymbol !== symbol)
                return; // Warten bis der *richtige* Datensatz gespeichert ist

            dbManager.saveComplete.disconnect(onSaveComplete);
            console.log("✅ Speicherung abgeschlossen:", symbol);


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
                getSortPeriodIndex(),      // 👈 Neue Hilfsfunktion, siehe unten
                filterSelectionLoader.item.sortAscCheckBox.checked,
                symbol
            );

            updateStockAtIndex(selectedIndices[index],result[0]);

            // Starte nächsten Schritt nach minimalem Delay
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
            textItem.text = "Keine aktive Periode ausgewählt"
            clipboardPopup.visible = true
            clipboardTimer.restart()
            return
        }

        let stock = normalizeStockData(stockModel.get(modelIndex))
        detailStock = stock
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
                                    text: sortAscCheckBox.checked ? "↑" : "↓"
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
                    height: 1
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
                    Label { text: "Änderung"; Layout.preferredWidth: 110; font.bold: true; horizontalAlignment: Text.AlignRight }
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


    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            TextField {
                id: tickerInput
                Layout.preferredWidth: 200
                text: "APC.XFRA"
                placeholderText: "Ticker eingeben"
            }

            TextField {
                id: exchangeInput
                Layout.preferredWidth: 200
                placeholderText: "Exchange eingeben"
            }

            Button {
                text: "Suchen"
                onClicked: searchByTickerAndExchange()
            }

            Button {
                text: "Zurücksetzen"
                onClicked: {
                    dbManager.updateAllISINs()
                    return;
                    /*
                    tickerInput.text = ""
                    exchangeInput.text = ""
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

                    console.log("📊 Lade historische Daten für:", selectedSymbols.length, "Aktien");
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
                    console.log("📊 Lade historische Daten für alle Aktien")
                    loadingOverlay.running = true
                    dbManager.generateQuotesForAllStocks()
                }
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
                                console.warn("⚠️ Perioden-Komponenten nicht vollständig geladen.")
                                return
                            }

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
                    Label { text: "Börse"; Layout.preferredWidth: 60; font.bold: true; horizontalAlignment: Text.AlignLeft }
                    Label { text: "Aktualisiert"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "letzter Preis"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label { text: "vom Datum"; Layout.preferredWidth: 100; font.bold: true; horizontalAlignment: Text.AlignRight }
                    Label {
                        id: labelSortP1
                        property string baseText: "AnstiegePeriode-1(Ges.%)"
                        text: baseText + (currentSortPeriod === 1 ? (currentSortAsc ? "↑" : "↓") : "")
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
                            ToolTip.text: "Sortieren nach prozentueller Erhöhung"
                            ToolTip.delay: 300
                        }
                    }

                    Label {
                        id: labelSortP2
                        property string baseText: "-Periode-2(Ges.%)"
                        text: baseText + (currentSortPeriod === 2 ? (currentSortAsc ? "↑" : "↓") : "")
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
                            ToolTip.text: "Sortieren nach prozentueller Erhöhung"
                            ToolTip.delay: 300
                        }
                    }

                    Label {
                        id: labelSortP3
                        property string baseText: "-Periode-3(Ges.%)"
                        text: baseText + (currentSortPeriod === 3 ? (currentSortAsc ? "↑" : "↓") : "")
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
                            ToolTip.text: "Sortieren nach prozentueller Erhöhung"
                            ToolTip.delay: 300
                        }
                    }

                    Label {
                        id: labelSortP4
                        property string baseText: "-Periode-4(Ges.%)"
                        text: baseText + (currentSortPeriod === 4 ? (currentSortAsc ? "↑" : "↓") : "")
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
                            ToolTip.text: "Sortieren nach prozentueller Erhöhung"
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
                    height: 2
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
                            const itemsPerPage = Math.floor(listView.height / 40); // Zeilenhöhe ist 40

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
                            return Math.max(1, Math.floor(listView.height / 40)); // 40 ist die Zeilenhöhe
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
                            console.log("✅ Alle selektiert: ", selectedSymbols.length + " Items");
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
