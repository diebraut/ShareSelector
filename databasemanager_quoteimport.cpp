#include "databasemanager.h"

#include <QDate>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

QVariantMap DatabaseManager::extractStock(const QSqlQuery &query) {
    return {
            {"ticker", query.value("ticker")},
            {"name", query.value("name")},
            {"exchange", query.value("exchange")},
            {"currency", query.value("currency")},
            {"market_cap", query.value("market_cap")},
            {"sector", query.value("sector")},
            {"industry", query.value("industry")},
            };
}


QString DatabaseManager::getISINFromOpenFIGI(const QString &apiKey, const QString &ticker, const QString &exchangeCode) {
    QNetworkAccessManager manager;
    QUrl url("https://api.openfigi.com/v3/mapping");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-OPENFIGI-APIKEY", apiKey.toUtf8());

    // Erstelle den JSON Body
    QJsonArray jsonArray;
    QJsonObject query;
    query["idType"] = "TICKER";
    //query["idValue"] = ticker;
    //query["exchCode"] = exchangeCode;
    query["idValue"] = "APC";
    query["exchCode"] = "XETR";
    jsonArray.append(query);

    QJsonDocument doc(jsonArray);
    QByteArray payload = doc.toJson();

    // Senden der Anfrage synchron mit QEventLoop (Achtung: nicht im GUI-Thread)
    QNetworkReply *reply = manager.post(request, payload);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QString isin;
    if(reply->error() == QNetworkReply::NoError) {
        QByteArray response = reply->readAll();
        QJsonDocument responseDoc = QJsonDocument::fromJson(response);
        QJsonArray responseArray = responseDoc.array();
        if (!responseArray.isEmpty()) {
            QJsonObject obj = responseArray.first().toObject();
            if (obj.contains("data")) {
                QJsonArray dataArray = obj["data"].toArray();
                if (!dataArray.isEmpty()) {
                    QJsonObject dataObj = dataArray.first().toObject();
                    isin = dataObj.value("isin").toString();
                    qDebug() << "ISIN gefunden:" << isin;
                }
            }
        }
    } else {
        qDebug() << "Fehler bei OpenFIGI Anfrage:" << reply->errorString();
    }
    reply->deleteLater();
    return isin;
}

void DatabaseManager::updateAllISINs() {
    if (!db.isOpen()) {
        qWarning() << "⚠️ Datenbank nicht verbunden!";
        return;
    }

    const QString openFigiApiKey = "4b5256ce-2580-4e19-b746-d599b0dcffd0"; // 👉 Ersetze durch deinen API-Key

    QSqlQuery query(db);
    if (!query.exec(R"SQL(
        SELECT "Symbol", "MIC"
        FROM "Stocks"
        WHERE "ISIN" IS NULL OR "ISIN" = ''
    )SQL")) {
        qCritical() << "❌ Fehler beim Abrufen leerer ISINs:" << query.lastError().text();
        return;
    }

    int updatedCount = 0;
    int totalCount = 0;

    while (query.next()) {
        totalCount++;
        QString symbol = query.value("Symbol").toString();
        QString mic = query.value("MIC").toString();

        // OpenFIGI verwendet z. B. "XETR", "XFRA" usw.
        QString isin = getISINFromOpenFIGI(openFigiApiKey, symbol.section('.', 0, 0), mic); // Symbol ohne ".MIC"

        if (!isin.isEmpty()) {
            QSqlQuery updateQuery(db);
            updateQuery.prepare(R"SQL(
                UPDATE "Stocks"
                SET "ISIN" = :isin
                WHERE "Symbol" = :symbol
            )SQL");
            updateQuery.bindValue(":isin", isin);
            updateQuery.bindValue(":symbol", symbol);

            if (!updateQuery.exec()) {
                qWarning() << "❌ Fehler beim Aktualisieren der ISIN für" << symbol << ":" << updateQuery.lastError().text();
            } else {
                updatedCount++;
                qDebug() << "✅ ISIN aktualisiert für" << symbol << ":" << isin;
            }
        } else {
            qDebug() << "⚠️ Keine ISIN gefunden für" << symbol;
        }
    }

    qDebug() << QString("🏁 ISIN-Update abgeschlossen: %1 von %2 Aktien aktualisiert.").arg(updatedCount).arg(totalCount);
}

void DatabaseManager::createQuotesForStock(const QString symbol, const QString exchange) {
    qDebug() << "🟢 Starte Verarbeitung für Stock:" << symbol << "| Exchange:" << exchange;

    // Trenne alle bestehenden Verbindungen für historicalDataReceived
    disconnect(&marketStackClient, &MarketStackClient::historicalDataReceived, this, nullptr);

    connect(&marketStackClient, &MarketStackClient::errorOccurred, this, [](const QString error) {
        qDebug() << "❌ Fehler beim Abrufen der historischen Daten:" << error;
    });

    // Letztes gespeichertes Datum abrufen
    QSqlQuery query(db);
    query.prepare("SELECT Max(\"CloseDate\") AS LastCloseDate FROM \"Quotes\" WHERE \"Symbol\" = :symbol");
    query.bindValue(":symbol", symbol);

    if (!query.exec()) {
        qDebug() << "❌ Fehler beim Abrufen des LastUpdateDate:" << query.lastError().text();
        emit saveComplete(symbol); // Signal auslösen, um die Verarbeitung abzuschließen
        return;
    }

    int limit = 1000; // Standardwert, wenn 1 Jahr zurück
    QDate fromDate = QDate::currentDate().addYears(-1); // Standardwert: 1 Jahr zurück
    if (query.next()) {
        QVariant lastQuoteDate = query.value("LastCloseDate");
        if (!lastQuoteDate.isNull()) {
            fromDate = lastQuoteDate.toDate();
            qDebug() << "🟠 Letztes gespeichertes Datum gefunden. Limit auf" << limit << "gesetzt.";
        }
    }

    // Verbinde das Signal mit einer Lambda-Funktion, die den aktuellen Stock verarbeitet
    connect(&marketStackClient, &MarketStackClient::historicalDataReceived, this, [this, symbol, exchange, dbCopy = QSqlDatabase::database()](QMap<QString, QVariantMap> data) mutable {
        qDebug() << "🔵 Empfangene historische Daten für Stock:" << symbol << "| Anzahl der Datensätze:" << data.size();

        if (data.isEmpty()) {
            qDebug() << "⚠️ Keine historischen Daten verfügbar für Stock:" << symbol;
            emit saveComplete(symbol); // Signal auslösen, um die Verarbeitung abzuschließen
            return;
        }

        QList<QDate> closeDates;
        QList<double> closePrices;
        QList<double> volumes;
        QDate latestDate;

        QSqlQuery insertQuery(dbCopy);
        insertQuery.prepare("INSERT INTO \"Quotes\" (\"Symbol\", \"CloseDate\", \"ClosePrice\", \"OpenPrice\", \"HighestPrice\", \"LowestPrice\", \"Volume\") "
                            "VALUES (:symbol, :closeDate, :closePrice, :openPrice, :highestPrice, :lowestPrice, :volume) "
                            "ON CONFLICT (\"Symbol\", \"CloseDate\") DO NOTHING");

        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            QString dateOnly = it.key().split("T").first();
            QDate closeDate = QDate::fromString(dateOnly, "yyyy-MM-dd");
            if (!closeDate.isValid()) {
                qDebug() << "⚠️ Ungültiges Datum gefunden:" << dateOnly;
                continue;
            }

            insertQuery.bindValue(":symbol", symbol);
            insertQuery.bindValue(":closeDate", closeDate);
            insertQuery.bindValue(":closePrice", it.value()["close"]);
            insertQuery.bindValue(":openPrice", it.value()["open"]);
            insertQuery.bindValue(":highestPrice", it.value()["high"]);
            insertQuery.bindValue(":lowestPrice", it.value()["low"]);
            insertQuery.bindValue(":volume", it.value()["volume"]);
            latestDate = closeDate;
            if (!insertQuery.exec()) {
                qDebug() << "❌ Fehler beim Einfügen des Quotes:" << insertQuery.lastError().text();
            }
            closeDates.append(closeDate);
            closePrices.append(it.value()["close"].toDouble());
            volumes.append(it.value()["volume"].toDouble());
        }
        QSqlQuery updateQuery(db);
        if (!updateQuery.prepare(
                "UPDATE \"Stocks\" "
                "SET \"LastUpdateDate\" = :lastUpdateDate "
                "WHERE \"Symbol\" = :symbol")) {
            qCritical() << "SQL-Fehler beim Vorbereiten:" << updateQuery.lastError().text();
            return;
        }

        updateQuery.bindValue(":lastUpdateDate", QDate::currentDate());
        updateQuery.bindValue(":symbol", symbol);

        if (!updateQuery.exec()) {
            qCritical() << "❌ Fehler beim Aktualisieren des aktualisierungs Datum:" << updateQuery.lastError().text();
            return;
        }

        emit saveComplete(symbol); // Signal auslösen, um die Verarbeitung abzuschließen
    });

    qDebug() << "API-Anfrage für Stock:" << symbol << "| Exchange:" << exchange << "| Von:" << fromDate.toString("yyyy-MM-dd") << "| Limit:" << limit;
    marketStackClient.fetchHistoricalData(symbol, exchange, fromDate, limit);
}


void DatabaseManager::generateQuoteForStock(const QString symbol, const QString exchange) {

    // **Erstelle eine EventLoop, um auf den Abschluss der Verarbeitung zu warten**
    QEventLoop loop;
    connect(this, &DatabaseManager::saveComplete, &loop, &QEventLoop::quit);

    // **Rufe die Methode auf**
    createQuotesForStock(symbol, exchange);
}

void DatabaseManager::generateQuotesForAllStocks() {
    if (!db.isOpen()) {
        qCritical() << "❌ Fehler: Datenbank ist nicht verbunden!";
        return;
    }

    QSqlQuery query(db);
    query.prepare("SELECT \"Symbol\", \"MIC\" FROM \"Stocks\"  where \"LastUpdateDate\" !=  CURRENT_DATE;" );

    if (!query.exec()) {
        qCritical() << "❌ Fehler beim Abrufen der Stocks-Liste: " << query.lastError().text();
        return;
    }

    int stockCounter = 0;

    while (query.next() ) {
        QString symbol = query.value("Symbol").toString();
        QString mic = query.value("MIC").toString();

        qDebug() << "📈 [" << ++stockCounter << "] Verarbeite Stock:" << symbol << " | MIC:" << mic;

        // **Erstelle eine EventLoop, um auf den Abschluss der Verarbeitung zu warten**
        QEventLoop loop;
        connect(this, &DatabaseManager::saveComplete, &loop, &QEventLoop::quit);

        // **Rufe die Methode auf**
        createQuotesForStock(symbol, mic);

        // **Warte, bis `saveComplete()` gesendet wird**
        loop.exec();

    }

    qDebug() << "✅ Verarbeitung aller Stocks abgeschlossen. Gesamtzahl: " << stockCounter;
}
