#include "databasemanager.h"
#include <QSqlRecord>
#include <QDate>
#include <QtConcurrent>

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent)
{
    db = QSqlDatabase::addDatabase("QPSQL"); // PostgreSQL-Treiber
    db.setHostName("localhost");
    db.setDatabaseName("TotalStocks");
    db.setUserName("postgres");
    db.setPassword("castell");

    if (!db.open()) {
        qDebug() << "Fehler bei der Verbindung zur Datenbank:" << db.lastError().text();
    } else {
        qDebug() << "Erfolgreich mit der Datenbank verbunden!";
        qDebug() << "Tables:" << db.tables(QSql::Tables);
    }
}

DatabaseManager::~DatabaseManager()
{
    if (db.isOpen()) {
        db.close();
    }
}

bool DatabaseManager::connectToDatabase(const QString &host, const QString &dbName, const QString &user, const QString &password)
{
    db.setHostName(host);
    db.setDatabaseName(dbName);
    db.setUserName(user);
    db.setPassword(password);

    if (!db.open()) {
        qDebug() << "Fehler bei der Verbindung zur Datenbank:" << db.lastError().text();
        return false;
    }
    qDebug() << "Erfolgreich mit der Datenbank verbunden!";
    return true;
}

QVariantList DatabaseManager::getBoughtStocks()
{
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return results;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT
            "Symbol",
            "Name",
            TO_CHAR("BuyDate", 'YYYY-MM-DD') AS "BuyDate",
            TO_CHAR("SellDate", 'YYYY-MM-DD') AS "SellDate",
            "CurrentValue",
            "EntryValue",
            "ValueIncreasePercent",
            "Status"
        FROM "BoughtStocks"
        ORDER BY "BuyDate" DESC, "Symbol" ASC
    )SQL");

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden der gekauften Aktien:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.insert(query.record().fieldName(i), query.value(i));
        }
        results << row;
    }

    return results;
}

bool DatabaseManager::isBoughtStock(const QString &symbol)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("SELECT 1 FROM \"BoughtStocks\" WHERE \"Symbol\" = :symbol LIMIT 1");
    query.bindValue(":symbol", symbol.trimmed());

    if (!query.exec()) {
        qCritical() << "Fehler beim Prüfen der gekauften Aktie:" << query.lastError().text();
        return false;
    }

    return query.next();
}

bool DatabaseManager::deleteBoughtStock(const QString &symbol)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM \"BoughtStocks\" WHERE \"Symbol\" = :symbol");
    query.bindValue(":symbol", symbol.trimmed());

    if (!query.exec()) {
        qCritical() << "Fehler beim Löschen der gekauften Aktie:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool DatabaseManager::saveBoughtStock(
    const QString &symbol,
    const QString &name,
    const QString &buyDate,
    const QString &sellDate,
    double currentValue,
    double entryValue,
    double valueIncreasePercent,
    int status)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    if (symbol.trimmed().isEmpty() || name.trimmed().isEmpty() || buyDate.trimmed().isEmpty()) {
        qWarning() << "Pflichtfelder für gekaufte Aktie fehlen.";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "BoughtStocks" (
            "Symbol",
            "Name",
            "BuyDate",
            "SellDate",
            "CurrentValue",
            "EntryValue",
            "ValueIncreasePercent",
            "Status"
        )
        VALUES (
            :symbol,
            :name,
            :buyDate,
            NULLIF(:sellDate, '')::date,
            :currentValue,
            :entryValue,
            :valueIncreasePercent,
            :status
        )
        ON CONFLICT ("Symbol") DO UPDATE SET
            "Name" = EXCLUDED."Name",
            "BuyDate" = EXCLUDED."BuyDate",
            "SellDate" = EXCLUDED."SellDate",
            "CurrentValue" = EXCLUDED."CurrentValue",
            "EntryValue" = EXCLUDED."EntryValue",
            "ValueIncreasePercent" = EXCLUDED."ValueIncreasePercent",
            "Status" = EXCLUDED."Status"
    )SQL");
    query.bindValue(":symbol", symbol.trimmed());
    query.bindValue(":name", name.trimmed());
    query.bindValue(":buyDate", buyDate.trimmed());
    query.bindValue(":sellDate", sellDate.trimmed());
    query.bindValue(":currentValue", currentValue);
    query.bindValue(":entryValue", entryValue);
    query.bindValue(":valueIncreasePercent", valueIncreasePercent);
    query.bindValue(":status", status);

    if (!query.exec()) {
        qCritical() << "Fehler beim Speichern der gekauften Aktie:" << query.lastError().text();
        return false;
    }

    return true;
}

QVariantList DatabaseManager::searchByTickerAndExchange(const QString &symbol, const QString &exchange) {
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return results;
    }

    QSqlQuery query(db);
    query.prepare("SELECT * FROM \"Stocks\" WHERE \"Symbol\" = :symbol ");
    query.bindValue(":symbol",symbol);

    if (!query.exec()) {
        qCritical() << "Query error:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap stock;
        stock["symbol"] = query.value("Symbol").toString();
        stock["name"] = query.value("Name").toString();
        stock["exchange"] = query.value("MIC").toString();
        stock["lastQuoteDate"] = query.value("LastQuoteDate").toString();
        stock["days5Success"] = query.value("5DaysSuccess");
        stock["days10Success"] = query.value("10DaysSuccess");
        stock["days20Success"] = query.value("20DaysSuccess");
        stock["days40Success"] = query.value("40DaysSuccess");
        stock["days5ValueInc"] = query.value("5DaysValueInc");
        stock["days10ValueInc"] = query.value("10DaysValueInc");
        stock["days20ValueInc"] = query.value("20DaysValueInc");
        stock["days40ValueInc"] = query.value("40DaysValueInc");
        stock["days5Volumen"] = query.value("5DaysVolumen");
        stock["days10Volumen"] = query.value("10DaysVolumen");
        stock["days20Volumen"] = query.value("20DaysVolumen");
        stock["days40Volumen"] = query.value("40DaysVolumen");
        results.append(stock);
        break;
    }

    return results;
}

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

void DatabaseManager::saveShares(const QList<ShareData>& shares) {
    for (const ShareData &share : shares) {
        saveShare(share);
    }
}

void DatabaseManager::updateShares(const QList<ShareData>& shares) {
    for (const ShareData &share : shares) {
        updateShare(share);
    }
}



// databasemanager.cpp
void DatabaseManager::saveShare(const ShareData &share) {
    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return;
    }

    QSqlQuery query(db);
    if (!query.prepare(
            "INSERT INTO \"Stocks\" (\"MIC\", \"ISIN\", \"Name\", \"Symbol\", \"Exchange\", \"CountryCode\", \"City\") "
            "VALUES (:mic, :name, :symbol, :exchange, :country_code, :city) "
            "ON CONFLICT (\"Symbol\") DO UPDATE "
            "SET \"Name\" = EXCLUDED.\"Name\", "
            "    \"Exchange\" = EXCLUDED.\"Exchange\", "
            "    \"CountryCode\" = EXCLUDED.\"CountryCode\", "
            "    \"City\" = EXCLUDED.\"City\";")) {
        qCritical() << "SQL-Fehler beim Vorbereiten:" << query.lastError().text();
        return;
    }
    query.bindValue(":mic", share.mic);
    query.bindValue(":isin", share.isin);
    query.bindValue(":symbol", share.symbol);
    query.bindValue(":name", share.name);
    query.bindValue(":exchange", share.exchange);
    query.bindValue(":country_code", share.countryCode);
    query.bindValue(":city", share.city);

    if (!query.exec()) {
        qCritical() << "❌ Fehler beim Speichern der Aktie:" << query.lastError().text();
        qCritical() << "Fehlerhafte Aktie:" << share.symbol << share.name;
        return;
    }
    qDebug() << "✅ Aktie gespeichert:" << share.symbol;
    emit saveComplete(share.symbol);  // jetzt eindeutig!
}

#include <QNetworkRequest>

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

QString DatabaseManager::convertToEodTicker(const QString& symbol) {
    QString ticker = symbol;

    static const QMap<QString, QString> exchangeMap = {
        {"XFRA", "F"},
        {"XETR", "DE"},
        {"XNAS", "US"},
        {"XNYS", "US"},
        // weitere falls nötig
    };

    QString exch = symbol.section('.', 1, 1);
    if (exchangeMap.contains(exch)) {
        ticker = symbol.section('.', 0, 0) + "." + exchangeMap.value(exch);
    }

    return ticker;
}



void DatabaseManager::updateShare(const ShareData &share) {
    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return;
    }

    QSqlQuery query(db);
    if (!query.prepare(
            "UPDATE \"Stocks\" "
            "SET \"ISIN\" = :isin "
            "WHERE \"Symbol\" = :symbol")) {
        qCritical() << "SQL-Fehler beim Vorbereiten:" << query.lastError().text();
        return;
    }

    query.bindValue(":isin", share.isin);
    query.bindValue(":symbol", share.symbol);

    if (!query.exec()) {
        qCritical() << "❌ Fehler beim Aktualisieren der ISIN:" << query.lastError().text();
        qCritical() << "Symbol:" << share.symbol << " | ISIN:" << share.isin;
        return;
    }

    if (query.numRowsAffected() == 0) {
        qWarning() << "⚠️ Kein Eintrag aktualisiert – Symbol nicht gefunden:" << share.symbol;
    } else {
        qDebug() << "✅ ISIN aktualisiert für Symbol:" << share.symbol;
    }
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

#include <QEventLoop>

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

QVariantList DatabaseManager::getQuoteDetails(const QString &symbol, int fromDay, int toDay)
{
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return results;
    }

    if (symbol.trimmed().isEmpty() || fromDay < 1 || toDay < fromDay) {
        qWarning() << "Ungültige Parameter für Kursdetails:" << symbol << fromDay << toDay;
        return results;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        WITH ordered_quotes AS (
            SELECT
                "Symbol",
                "CloseDate",
                "OpenPrice",
                "ClosePrice",
                "HighestPrice",
                "LowestPrice",
                "Volume",
                ROW_NUMBER() OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" DESC) AS dayIndex,
                LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS previousClosePrice
            FROM "Quotes"
            WHERE "Symbol" = :symbol
        )
        SELECT
            dayIndex,
            TO_CHAR("CloseDate", 'DD.MM.YYYY') AS closeDate,
            "OpenPrice" AS openPrice,
            "ClosePrice" AS closePrice,
            "HighestPrice" AS highestPrice,
            "LowestPrice" AS lowestPrice,
            "Volume" AS volume,
            ROUND((("ClosePrice" - previousClosePrice) / NULLIF(previousClosePrice, 0) * 100)::numeric, 2) AS changePercent
        FROM ordered_quotes
        WHERE dayIndex BETWEEN :fromDay AND :toDay
        ORDER BY dayIndex ASC
    )SQL");
    query.bindValue(":symbol", symbol);
    query.bindValue(":fromDay", fromDay);
    query.bindValue(":toDay", toDay);

    if (!query.exec()) {
        qCritical() << "SQL-Fehler bei Kursdetails:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.insert(query.record().fieldName(i), query.value(i));
        }
        results << row;
    }

    return results;
}

QVariantList DatabaseManager::runShareQuery(const QString& sql)
{
    QVariantList results;

    QSqlQuery query(db);
    if (!query.exec(sql)) {
        qCritical() << "❌ SQL-Fehler:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.insert(query.record().fieldName(i), query.value(i));
        }
        results << row;
    }

    return results;
}


QVariantList DatabaseManager::getShares(
    int firstTo, int firstThreshold, bool firstGreaterThan,
    int secondTo, int secondThreshold, bool secondGreaterThan,
    int thirdTo, int thirdThreshold, bool thirdGreaterThan,
    int fourthTo, int fourthThreshold, bool fourthGreaterThan,
    int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol)
{
    QString sql = buildShareQuery(
        firstTo, firstThreshold, firstGreaterThan,
        secondTo, secondThreshold, secondGreaterThan,
        thirdTo, thirdThreshold, thirdGreaterThan,
        fourthTo, fourthThreshold, fourthGreaterThan,
        greaterThanSalesPrice, sortPeriod, sortAsc, symbol
        );
    return runShareQuery(sql);
}

void DatabaseManager::getSharesAsync(
    int firstTo, int firstThreshold, bool firstGreaterThan,
    int secondTo, int secondThreshold, bool secondGreaterThan,
    int thirdTo, int thirdThreshold, bool thirdGreaterThan,
    int fourthTo, int fourthThreshold, bool fourthGreaterThan,
    int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol)
{
    (void) QtConcurrent::run([=]() {
        QString sql = buildShareQuery(
            firstTo, firstThreshold, firstGreaterThan,
            secondTo, secondThreshold, secondGreaterThan,
            thirdTo, thirdThreshold, thirdGreaterThan,
            fourthTo, fourthThreshold, fourthGreaterThan,
            greaterThanSalesPrice, sortPeriod, sortAsc, symbol
            );

        QVariantList results = runShareQuery(sql);

        QMetaObject::invokeMethod(this, [=]() {
                emit getSharesComplete(results);
            }, Qt::QueuedConnection);
    });
}

void DatabaseManager::getSharesByNameAsync(
    int firstTo, int firstThreshold, bool firstGreaterThan,
    int secondTo, int secondThreshold, bool secondGreaterThan,
    int thirdTo, int thirdThreshold, bool thirdGreaterThan,
    int fourthTo, int fourthThreshold, bool fourthGreaterThan,
    int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& name)
{
    (void) QtConcurrent::run([=]() {
        QString sql = buildShareQuery(
            firstTo, firstThreshold, firstGreaterThan,
            secondTo, secondThreshold, secondGreaterThan,
            thirdTo, thirdThreshold, thirdGreaterThan,
            fourthTo, fourthThreshold, fourthGreaterThan,
            greaterThanSalesPrice, sortPeriod, sortAsc, "", name
            );

        QVariantList results = runShareQuery(sql);

        QMetaObject::invokeMethod(this, [=]() {
                emit getSharesComplete(results);
            }, Qt::QueuedConnection);
    });
}



QString DatabaseManager::buildShareQuery(
    int firstTo, int firstThreshold, bool firstGreaterThan,
    int secondTo, int secondThreshold, bool secondGreaterThan,
    int thirdTo, int thirdThreshold, bool thirdGreaterThan,
    int fourthTo, int fourthThreshold, bool fourthGreaterThan,
    int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol, const QString& name)
{
    bool isSymbolMode = !symbol.isNull() && !symbol.trimmed().isEmpty();
    bool isNameMode = !isSymbolMode && !name.isNull() && !name.trimmed().isEmpty();

    QString sqlTemplate = R"SQL(
            WITH
            ordered_quotes AS (
                SELECT *, ROW_NUMBER() OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" DESC) AS rn_desc
                FROM "Quotes"
                WHERE 1=1
                %13
            ),
            quotes_q1 AS (SELECT * FROM ordered_quotes WHERE rn_desc <= %1),
            quotes_q2 AS (SELECT * FROM ordered_quotes WHERE rn_desc > %2 AND rn_desc <= %3),
            quotes_q3 AS (SELECT * FROM ordered_quotes WHERE rn_desc > %4 AND rn_desc <= %5),
            quotes_q4 AS (SELECT * FROM ordered_quotes WHERE rn_desc > %6 AND rn_desc <= %7),

            quotes_q1_lag AS (
                SELECT *, LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS prev_close
                FROM quotes_q1
            ),
            quotes_q2_lag AS (
                SELECT *, LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS prev_close
                FROM quotes_q2
            ),
            quotes_q3_lag AS (
                SELECT *, LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS prev_close
                FROM quotes_q3
            ),
            quotes_q4_lag AS (
                SELECT *, LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS prev_close
                FROM quotes_q4
            ),

            q1 AS (
                SELECT "Symbol",
                    COUNT(*) FILTER (WHERE prev_close IS NOT NULL AND "ClosePrice" > prev_close) AS daysSuccess,
                    (MAX("ClosePrice") - MIN("ClosePrice")) / NULLIF(MIN("ClosePrice"), 0) * 100 AS valueInc,
                    SUM("Volume") AS volumeSum,
                    SUM("ClosePrice") AS closeSum,
                    SUM("Volume") * SUM("ClosePrice") AS volumePrice
                FROM quotes_q1_lag
                WHERE rn_desc <= %8
                GROUP BY "Symbol"
            ),
            q2 AS (
                SELECT "Symbol",
                    COUNT(*) FILTER (WHERE prev_close IS NOT NULL AND "ClosePrice" > prev_close) AS daysSuccess,
                    (MAX("ClosePrice") - MIN("ClosePrice")) / NULLIF(MIN("ClosePrice"), 0) * 100 AS valueInc,
                    SUM("Volume") AS volumeSum,
                    SUM("ClosePrice") AS closeSum,
                    SUM("Volume") * SUM("ClosePrice") AS volumePrice
                FROM quotes_q2_lag
                WHERE rn_desc > %2 AND rn_desc <= %9
                GROUP BY "Symbol"
            ),
            q3 AS (
                SELECT "Symbol",
                    COUNT(*) FILTER (WHERE prev_close IS NOT NULL AND "ClosePrice" > prev_close) AS daysSuccess,
                    (MAX("ClosePrice") - MIN("ClosePrice")) / NULLIF(MIN("ClosePrice"), 0) * 100 AS valueInc,
                    SUM("Volume") AS volumeSum,
                    SUM("ClosePrice") AS closeSum,
                    SUM("Volume") * SUM("ClosePrice") AS volumePrice
                FROM quotes_q3_lag
                WHERE rn_desc > %4 AND rn_desc <= %10
                GROUP BY "Symbol"
            ),
            q4 AS (
                SELECT "Symbol",
                    COUNT(*) FILTER (WHERE prev_close IS NOT NULL AND "ClosePrice" > prev_close) AS daysSuccess,
                    (MAX("ClosePrice") - MIN("ClosePrice")) / NULLIF(MIN("ClosePrice"), 0) * 100 AS valueInc,
                    SUM("Volume") AS volumeSum,
                    SUM("ClosePrice") AS closeSum,
                    SUM("Volume") * SUM("ClosePrice") AS volumePrice
                FROM quotes_q4_lag
                WHERE rn_desc > %6 AND rn_desc <= %11
                GROUP BY "Symbol"
            ),

            "last_close" AS (
                SELECT q."Symbol", q."ClosePrice" AS lastClosePrice,  q."CloseDate" AS lastClosePriceDate
                FROM "Quotes" q
                INNER JOIN (
                    SELECT "Symbol", MAX("CloseDate") AS maxDate
                    FROM "Quotes"
                    GROUP BY "Symbol"
                ) latest ON q."Symbol" = latest."Symbol" AND q."CloseDate" = latest.maxDate
            )

            SELECT
                s."Symbol", s."MIC", s."Name",
                TO_CHAR(s."LastUpdateDate", 'DD.MM.YYYY') AS "LastUpdateDate",

                q1.daysSuccess AS daysFirstPeriodSuccess,
                ROUND(q1.valueInc, 2) AS firstPeriodValueInc,
                q1.volumeSum AS firstPeriodVolume,
                q1.volumePrice AS firstPeriodVolumePrice,

                q2.daysSuccess AS daysSecondPeriodSuccess,
                ROUND(q2.valueInc, 2) AS secondPeriodValueInc,
                q2.volumeSum AS secondPeriodVolume,
                q2.volumePrice AS secondPeriodVolumePrice,

                q3.daysSuccess AS daysThirdPeriodSuccess,
                ROUND(q3.valueInc, 2) AS thirdPeriodValueInc,
                q3.volumeSum AS thirdPeriodVolume,
                q3.volumePrice AS thirdPeriodVolumePrice,

                q4.daysSuccess AS daysFourthPeriodSuccess,
                ROUND(q4.valueInc, 2) AS fourthPeriodValueInc,
                q4.volumeSum AS fourthPeriodVolume,
                q4.volumePrice AS fourthPeriodVolumePrice,

                "last_close".lastClosePrice     AS lastClosePrice,
                TO_CHAR("last_close".lastClosePriceDate, 'DD.MM.YYYY') AS lastClosePriceDate

            FROM "Stocks" s
            LEFT JOIN q1 ON s."Symbol" = q1."Symbol"
            LEFT JOIN q2 ON s."Symbol" = q2."Symbol"
            LEFT JOIN q3 ON s."Symbol" = q3."Symbol"
            LEFT JOIN q4 ON s."Symbol" = q4."Symbol"
            LEFT JOIN "last_close" ON s."Symbol" = "last_close"."Symbol"
            WHERE 1=1
            %12
        )SQL";

    QString filterClause;
    QString quotesFilterClause;
    if (isSymbolMode) {
        QString escapedSymbol = symbol;
        escapedSymbol.replace("'", "''");
        filterClause = QString(" AND s.\"Symbol\" = '%1' LIMIT 1").arg(escapedSymbol);
        quotesFilterClause = QString(" AND \"Symbol\" = '%1'").arg(escapedSymbol);

    } else {
        if (isNameMode) {
            QString escapedName = name.trimmed();
            escapedName.replace("'", "''");
            filterClause = QString(" AND s.\"Name\" ILIKE '%%' || '%1' || '%%'").arg(escapedName);
            quotesFilterClause = QString(R"SQL(
                AND "Symbol" IN (
                    SELECT "Symbol"
                    FROM "Stocks"
                    WHERE "Name" ILIKE '%%' || '%1' || '%%'
                )
            )SQL").arg(escapedName);
            filterClause += " ORDER BY s.\"Name\" ASC";
        } else {

            //filterClause = QString(" AND (s.\"Symbol\" = 'R9GA.XFRA' OR s.\"Symbol\" = 'H6F.XFRA') AND q1.volumePrice > %1").arg(greaterThanSalesPrice);
            filterClause += QString(" AND q1.volumePrice > %1").arg(greaterThanSalesPrice);
            if (firstThreshold > 0)
                filterClause += QString(" AND q1.daysSuccess %1 %2")
                                    .arg(firstGreaterThan ? ">" : "<")
                                    .arg(firstThreshold);
            if (secondThreshold > 0)
                filterClause += QString(" AND q2.daysSuccess %1 %2")
                                    .arg(secondGreaterThan ? ">" : "<")
                                    .arg(secondThreshold);
            if (thirdThreshold > 0)
                filterClause += QString(" AND q3.daysSuccess %1 %2")
                                    .arg(thirdGreaterThan ? ">" : "<")
                                    .arg(thirdThreshold);
            if (fourthThreshold > 0)
                filterClause += QString(" AND q4.daysSuccess %1 %2")
                                    .arg(fourthGreaterThan ? ">" : "<")
                                    .arg(fourthThreshold);

            QString orderDirection = sortAsc ? "ASC" : "DESC";

            switch (sortPeriod) {
            case 1:
                filterClause += QString(" ORDER BY q1.daysSuccess %1").arg(orderDirection);
                break;
            case 2:
                filterClause += QString(" ORDER BY q2.daysSuccess %1").arg(orderDirection);
                break;
            case 3:
                filterClause += QString(" ORDER BY q3.daysSuccess %1").arg(orderDirection);
                break;
            case 4:
                filterClause += QString(" ORDER BY q4.daysSuccess %1").arg(orderDirection);
                break;
            default:
                filterClause += ""; // keine Sortierung
                break;
            }
        }
    }

    QString sql = sqlTemplate
                      .arg(firstTo + 1)
                      .arg(firstTo)
                      .arg(secondTo + 1)
                      .arg(secondTo)
                      .arg(thirdTo + 1)
                      .arg(thirdTo)
                      .arg(fourthTo + 1)
                      .arg(firstTo)
                      .arg(secondTo)
                      .arg(thirdTo)
                      .arg(fourthTo)
                      .arg(filterClause)
                      .arg(quotesFilterClause);

    qDebug().noquote() << "\n[DEBUG] Generiertes SQL:\n" << sql;
    return sql;
}

