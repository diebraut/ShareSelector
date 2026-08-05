#include "databasemanager.h"
#include "databasemanager_internal.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTimer>
#include <QUrl>

using namespace DatabaseManagerInternal;

void DatabaseManager::startMarketstackBatch()
{
    if (m_marketstackBatchActive || m_yahooFundamentalsBatchActive)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol"
        FROM "Stocks"
        WHERE COALESCE("use_marketstack", FALSE) = TRUE
          AND COALESCE("marketplace_sym", '') = ''
        ORDER BY "Symbol"
    )SQL");
    if (!query.exec()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Marketstack-Batch konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            false);
        return;
    }

    m_marketstackBatchSymbols.clear();
    while (query.next())
        m_marketstackBatchSymbols << query.value(0).toString();

    if (m_marketstackBatchSymbols.isEmpty()) {
        setFundamentalDataStatus(QStringLiteral("Marketstack-Batch: Keine vorgemerkten Aktien gefunden."), false);
        return;
    }

    m_marketstackBatchActive = true;
    m_marketstackBatchIndex = 0;
    m_marketstackBatchSuccessCount = 0;
    m_marketstackBatchFailureCount = 0;
    setFundamentalDataStatus(
        QStringLiteral("Marketstack-Batch gestartet: Fuer %1 Aktien wird der umsatzstaerkste Marktplatz gesucht.")
            .arg(m_marketstackBatchSymbols.size()),
        true);
    scheduleNextMarketstackBatchSymbol(MarketstackInitialDelayMs);
}

void DatabaseManager::stopMarketstackBatch()
{
    if (!m_marketstackBatchActive)
        return;

    m_marketstackBatchTimer.stop();
    finishMarketstackBatch(
        QStringLiteral("Marketstack-Batch gestoppt: %1/%2 verarbeitet, %3 erfolgreich, %4 fehlgeschlagen.")
            .arg(marketstackBatchDone())
            .arg(m_marketstackBatchSymbols.size())
            .arg(m_marketstackBatchSuccessCount)
            .arg(m_marketstackBatchFailureCount));
}

void DatabaseManager::loadNextMarketstackBatchSymbol()
{
    if (!m_marketstackBatchActive)
        return;

    if (m_marketstackBatchIndex >= m_marketstackBatchSymbols.size()) {
        finishMarketstackBatch(
            QStringLiteral("Marketstack-Batch abgeschlossen: %1 Aktien, %2 Marktplaetze gespeichert, %3 fehlgeschlagen.")
                .arg(m_marketstackBatchSymbols.size())
                .arg(m_marketstackBatchSuccessCount)
                .arg(m_marketstackBatchFailureCount));
        return;
    }

    const QString symbol = m_marketstackBatchSymbols.at(m_marketstackBatchIndex++).trimmed();
    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol", "ISIN", "Name", "MIC", "IBKRResolvedSymbol", "YahooSymbol", "AlphaVantageSymbol"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec() || !query.next()) {
        ++m_marketstackBatchFailureCount;
        scheduleNextMarketstackBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    m_pendingMarketstackSymbol = query.value(QStringLiteral("Symbol")).toString().trimmed();
    m_pendingMarketstackIsin = query.value(QStringLiteral("ISIN")).toString().trimmed();
    m_pendingMarketstackName = query.value(QStringLiteral("Name")).toString().trimmed();
    m_pendingMarketstackMic = query.value(QStringLiteral("MIC")).toString().trimmed().toUpper();
    m_pendingMarketstackIbkrResolvedSymbol =
        query.value(QStringLiteral("IBKRResolvedSymbol")).toString().trimmed();
    m_pendingMarketstackYahooSymbol = query.value(QStringLiteral("YahooSymbol")).toString().trimmed();
    m_pendingMarketstackAlphaVantageSymbol =
        query.value(QStringLiteral("AlphaVantageSymbol")).toString().trimmed();
    setFundamentalDataStatus(
        QStringLiteral("Marketstack-Batch: %1/%2 %3 - Marktplatzsuche laeuft. OK: %4, Fehler: %5")
            .arg(m_marketstackBatchIndex)
            .arg(m_marketstackBatchSymbols.size())
            .arg(m_pendingMarketstackSymbol)
            .arg(m_marketstackBatchSuccessCount)
            .arg(m_marketstackBatchFailureCount),
        true);
    startMarketstackTickerLookup();
}

void DatabaseManager::scheduleNextMarketstackBatchSymbol(int delayMs)
{
    if (!m_marketstackBatchActive)
        return;
    m_marketstackBatchTimer.start(delayMs);
}

void DatabaseManager::finishMarketstackBatch(const QString &message)
{
    m_marketstackBatchTimer.stop();
    m_marketstackBatchActive = false;
    m_pendingMarketstackSymbol.clear();
    m_pendingMarketstackIsin.clear();
    m_pendingMarketstackName.clear();
    m_pendingMarketstackMic.clear();
    m_pendingMarketstackIbkrResolvedSymbol.clear();
    m_pendingMarketstackYahooSymbol.clear();
    m_pendingMarketstackAlphaVantageSymbol.clear();
    m_pendingMarketstackLookupUrls.clear();
    m_pendingMarketstackLookupIndex = 0;
    m_pendingMarketstackCandidates.clear();
    m_pendingMarketstackCandidateIndex = 0;
    m_pendingMarketstackRateLimitRetries = 0;
    m_pendingMarketstackBestMarketplaceSym.clear();
    m_pendingMarketstackBestExchange.clear();
    m_pendingMarketstackBestTurnover = 0.0;
    m_pendingMarketstackBestHasQuotes = false;
    setFundamentalDataStatus(message, false);
}

void DatabaseManager::startMarketstackTickerLookup()
{
    m_pendingMarketstackLookupUrls.clear();
    m_pendingMarketstackLookupIndex = 0;
    m_pendingMarketstackCandidates.clear();
    m_pendingMarketstackCandidateIndex = 0;
    m_pendingMarketstackRateLimitRetries = 0;
    m_pendingMarketstackBestMarketplaceSym.clear();
    m_pendingMarketstackBestExchange.clear();
    m_pendingMarketstackBestTurnover = 0.0;
    m_pendingMarketstackBestHasQuotes = false;

    QStringList lookupSymbols;
    appendUniqueMarketstackSymbol(lookupSymbols, m_pendingMarketstackIbkrResolvedSymbol);
    appendUniqueMarketstackSymbol(lookupSymbols, m_pendingMarketstackAlphaVantageSymbol);
    appendUniqueMarketstackSymbol(lookupSymbols, m_pendingMarketstackYahooSymbol);
    appendUniqueMarketstackSymbol(lookupSymbols, m_pendingMarketstackSymbol);
    if (!lookupSymbols.isEmpty()) {
        appendMarketstackLookupUrl(
            m_pendingMarketstackLookupUrls,
            marketstackUrl(QStringLiteral("tickers"),
                           {{QStringLiteral("symbols"), lookupSymbols.join(QLatin1Char(','))},
                            {QStringLiteral("limit"), QStringLiteral("10")}}));
    }

    const QStringList nameSearchTerms =
        ibkrSymbolSearchKeywordVariants(m_pendingMarketstackName);
    int appendedNameLookups = 0;
    for (const QString &searchTerm : nameSearchTerms) {
        if (appendedNameLookups++ >= MarketstackMaxNameLookupTerms)
            break;
        appendMarketstackLookupUrl(
            m_pendingMarketstackLookupUrls,
            marketstackUrl(QStringLiteral("tickers"),
                           {{QStringLiteral("search"), searchTerm},
                            {QStringLiteral("limit"), QStringLiteral("20")}}));
    }

    requestNextMarketstackTickerLookup();
}

void DatabaseManager::requestNextMarketstackTickerLookup()
{
    if (!m_marketstackBatchActive)
        return;

    if (m_pendingMarketstackLookupIndex >= m_pendingMarketstackLookupUrls.size()) {
        QTimer::singleShot(MarketstackLookupDelayMs,
                           this,
                           &DatabaseManager::requestNextMarketstackCandidateQuotes);
        return;
    }

    const QUrl url = m_pendingMarketstackLookupUrls.at(m_pendingMarketstackLookupIndex++);
    QNetworkReply *reply = m_marketstackNetworkManager.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMarketstackTickerLookupReply(reply);
    });
}

void DatabaseManager::handleMarketstackTickerLookupReply(QNetworkReply *reply)
{
    const QByteArray responseData = reply->readAll();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus == 429) {
        if (++m_pendingMarketstackRateLimitRetries <= MarketstackMaxRateLimitRetries) {
            --m_pendingMarketstackLookupIndex;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack-Batch: Rate-Limit erreicht, pausiere kurz und versuche erneut (%1/%2).")
                    .arg(m_pendingMarketstackRateLimitRetries)
                    .arg(MarketstackMaxRateLimitRetries),
                true);
            QTimer::singleShot(MarketstackRateLimitDelayMs,
                               this,
                               &DatabaseManager::requestNextMarketstackTickerLookup);
            return;
        }
        finishMarketstackBatch(
            QStringLiteral("Marketstack-Batch wegen Marketstack-Rate-Limit gestoppt. Bitte spaeter fortsetzen."));
        return;
    }

    m_pendingMarketstackRateLimitRetries = 0;

    if (networkOk) {
        const QJsonDocument document = QJsonDocument::fromJson(responseData);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        for (const QJsonValue &value : data) {
            const QJsonObject ticker = value.toObject();
            const QJsonObject exchange = ticker.value(QStringLiteral("stock_exchange")).toObject();
            const QString candidateName = ticker.value(QStringLiteral("name")).toString();
            const QString candidateExchange = exchange.value(QStringLiteral("mic")).toString();
            const QString candidateCountryCode =
                exchange.value(QStringLiteral("country_code")).toString();
            if (!marketstackNameMatchesStock(m_pendingMarketstackName, candidateName))
                continue;
            if (!marketstackCountryMatchesStock(m_pendingMarketstackIsin,
                                               candidateCountryCode,
                                               candidateExchange))
                continue;
            appendMarketstackCandidate(
                m_pendingMarketstackCandidates,
                ticker.value(QStringLiteral("symbol")).toString(),
                candidateExchange,
                ticker.value(QStringLiteral("has_eod")).toBool(),
                candidateName,
                candidateCountryCode);
        }
    }

    QTimer::singleShot(MarketstackLookupDelayMs,
                       this,
                       &DatabaseManager::requestNextMarketstackTickerLookup);
}

void DatabaseManager::requestNextMarketstackCandidateQuotes()
{
    if (!m_marketstackBatchActive)
        return;

    if (m_pendingMarketstackCandidateIndex >= m_pendingMarketstackCandidates.size()) {
        if (!m_pendingMarketstackBestMarketplaceSym.isEmpty()
            && saveMarketstackSelection(m_pendingMarketstackSymbol,
                                        m_pendingMarketstackBestMarketplaceSym,
                                        m_pendingMarketstackBestExchange,
                                        m_pendingMarketstackBestTurnover)) {
            ++m_marketstackBatchSuccessCount;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack-Batch: %1 -> %2 gespeichert, Umsatz %3. OK: %4, Fehler: %5")
                    .arg(m_pendingMarketstackSymbol,
                         m_pendingMarketstackBestMarketplaceSym)
                    .arg(m_pendingMarketstackBestTurnover, 0, 'f', 2)
                    .arg(m_marketstackBatchSuccessCount)
                    .arg(m_marketstackBatchFailureCount),
                true);
        } else {
            const QString noDataSymbol = m_pendingMarketstackSymbol;
            if (deleteMarketstackNoDataStock(noDataSymbol)) {
                ++m_marketstackBatchSuccessCount;
                setFundamentalDataStatus(
                    QStringLiteral("Marketstack-Batch: %1 ohne Kursdaten - aus DB geloescht. OK: %2, Fehler: %3")
                        .arg(noDataSymbol)
                        .arg(m_marketstackBatchSuccessCount)
                        .arg(m_marketstackBatchFailureCount),
                    true);
            } else {
                ++m_marketstackBatchFailureCount;
                const QString error = QStringLiteral("Marketstack lieferte keinen plausiblen Marktplatz mit Kursdaten; Loeschen fehlgeschlagen");
                saveMarketstackSelection(noDataSymbol, QString(), QString(), 0.0, error);
                setFundamentalDataStatus(
                    QStringLiteral("Marketstack-Batch: %1 ohne Kursdaten, Loeschen fehlgeschlagen. OK: %2, Fehler: %3")
                        .arg(noDataSymbol)
                        .arg(m_marketstackBatchSuccessCount)
                        .arg(m_marketstackBatchFailureCount),
                    true);
            }
        }
        scheduleNextMarketstackBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    const QVariantMap candidate =
        m_pendingMarketstackCandidates.at(m_pendingMarketstackCandidateIndex++).toMap();
    const QString symbol = candidate.value(QStringLiteral("symbol")).toString();
    const QString exchange = candidate.value(QStringLiteral("exchange")).toString();
    const QUrl url = marketstackUrl(
        QStringLiteral("eod"),
        {{QStringLiteral("symbols"), symbol},
         {QStringLiteral("exchange"), exchange},
         {QStringLiteral("limit"), QStringLiteral("10")}});

    QNetworkReply *reply = m_marketstackNetworkManager.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMarketstackCandidateQuotesReply(reply);
    });
}

void DatabaseManager::handleMarketstackCandidateQuotesReply(QNetworkReply *reply)
{
    const QByteArray responseData = reply->readAll();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus == 429) {
        if (++m_pendingMarketstackRateLimitRetries <= MarketstackMaxRateLimitRetries) {
            --m_pendingMarketstackCandidateIndex;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack-Batch: Rate-Limit erreicht, pausiere kurz und versuche erneut (%1/%2).")
                    .arg(m_pendingMarketstackRateLimitRetries)
                    .arg(MarketstackMaxRateLimitRetries),
                true);
            QTimer::singleShot(MarketstackRateLimitDelayMs,
                               this,
                               &DatabaseManager::requestNextMarketstackCandidateQuotes);
            return;
        }
        finishMarketstackBatch(
            QStringLiteral("Marketstack-Batch wegen Marketstack-Rate-Limit gestoppt. Bitte spaeter fortsetzen."));
        return;
    }

    m_pendingMarketstackRateLimitRetries = 0;

    const QVariantMap candidate =
        m_pendingMarketstackCandidates.value(m_pendingMarketstackCandidateIndex - 1).toMap();
    const QString marketplaceSym = candidate.value(QStringLiteral("marketplaceSym")).toString();
    const QString exchange = candidate.value(QStringLiteral("exchange")).toString();

    double turnover = 0.0;
    bool hasQuotes = false;
    if (networkOk) {
        const QJsonDocument document = QJsonDocument::fromJson(responseData);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        int bars = 0;
        for (const QJsonValue &value : data) {
            if (bars++ >= 10)
                break;
            hasQuotes = true;
            const QJsonObject bar = value.toObject();
            const double close = bar.value(QStringLiteral("close")).toDouble();
            const QJsonValue volumeValue = bar.value(QStringLiteral("volume"));
            const double volume = volumeValue.isDouble() ? volumeValue.toDouble() : 0.0;
            turnover += close * volume;
        }
    }

    if (hasQuotes
        && (!m_pendingMarketstackBestHasQuotes
            || turnover > m_pendingMarketstackBestTurnover)) {
        m_pendingMarketstackBestHasQuotes = true;
        m_pendingMarketstackBestTurnover = turnover;
        m_pendingMarketstackBestMarketplaceSym = marketplaceSym;
        m_pendingMarketstackBestExchange = exchange;
    }

    QTimer::singleShot(MarketstackCandidateDelayMs,
                       this,
                       &DatabaseManager::requestNextMarketstackCandidateQuotes);
}

bool DatabaseManager::saveMarketstackSelection(const QString &symbol,
                                               const QString &marketplaceSym,
                                               const QString &exchange,
                                               double turnover,
                                               const QString &error)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "marketplace_sym" = CASE
                WHEN NULLIF(:marketplaceSym, '') IS NULL THEN "marketplace_sym"
                ELSE :marketplaceSym
            END,
            "marketplace_exchange" = CASE
                WHEN NULLIF(:marketplaceExchange, '') IS NULL THEN "marketplace_exchange"
                ELSE :marketplaceExchange
            END,
            "marketplace_turnover" = CASE
                WHEN NULLIF(:marketplaceSym, '') IS NULL THEN "marketplace_turnover"
                ELSE :turnover
            END,
            "marketplace_checked_at" = CURRENT_TIMESTAMP,
            "marketplace_last_error" = NULLIF(:error, '')
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    query.bindValue(QStringLiteral(":marketplaceSym"), marketplaceSym.trimmed().toUpper());
    query.bindValue(QStringLiteral(":marketplaceExchange"), exchange.trimmed().toUpper());
    query.bindValue(QStringLiteral(":turnover"), turnover);
    query.bindValue(QStringLiteral(":error"), error.trimmed());
    if (!query.exec()) {
        qWarning() << "Marketstack-Marktplatz konnte nicht gespeichert werden:"
                   << query.lastError().text() << symbol << marketplaceSym;
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool DatabaseManager::deleteMarketstackNoDataStock(const QString &symbol)
{
    const QString normalizedSymbol = symbol.trimmed().toUpper();
    if (normalizedSymbol.isEmpty() || !db.isOpen())
        return false;

    QSqlQuery boughtQuery(db);
    boughtQuery.prepare(R"SQL(
        SELECT 1
        FROM "BoughtStocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    boughtQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!boughtQuery.exec()) {
        qWarning() << "Marketstack-No-Data-Kandidat konnte nicht gegen Portfolio geprueft werden:"
                   << boughtQuery.lastError().text() << normalizedSymbol;
        return false;
    }
    if (boughtQuery.next()) {
        qWarning() << "Marketstack-No-Data-Kandidat ist im Portfolio und wird nicht automatisch geloescht:"
                   << normalizedSymbol;
        return false;
    }

    if (!db.transaction()) {
        qWarning() << "Marketstack-No-Data-Kandidat konnte nicht zum Loeschen gesperrt werden:"
                   << db.lastError().text() << normalizedSymbol;
        return false;
    }

    const QStringList statements = {
        QStringLiteral(R"SQL(DELETE FROM "StockFundamentals" WHERE "Symbol" = :symbol)SQL"),
        QStringLiteral(R"SQL(DELETE FROM "Quotes" WHERE "Symbol" = :symbol)SQL"),
        QStringLiteral(R"SQL(DELETE FROM "Stocks_IBKRConflictBackup" WHERE "Symbol" = :symbol)SQL"),
        QStringLiteral(R"SQL(DELETE FROM "Stocks" WHERE "Symbol" = :symbol)SQL")
    };

    for (const QString &statement : statements) {
        QSqlQuery query(db);
        query.prepare(statement);
        query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        if (!query.exec()) {
            qWarning() << "Marketstack-No-Data-Kandidat konnte nicht geloescht werden:"
                       << query.lastError().text() << normalizedSymbol;
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qWarning() << "Marketstack-No-Data-Loeschung konnte nicht abgeschlossen werden:"
                   << db.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    emit ibkrStockDataUpdated(normalizedSymbol);
    return true;
}

void DatabaseManager::startMarketstackQuotesBatch()
{
    if (m_marketstackQuotesBatchActive || m_marketstackBatchActive || m_yahooFundamentalsBatchActive)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT s."Symbol"
        FROM "Stocks" s
        WHERE COALESCE(s."use_marketstack", FALSE) = TRUE
          AND COALESCE(s."marketplace_sym", '') <> ''
        ORDER BY s."Symbol"
    )SQL");
    if (!query.exec()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Marketstack Get Quotes konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            false);
        return;
    }

    m_marketstackQuotesBatchSymbols.clear();
    while (query.next())
        m_marketstackQuotesBatchSymbols << query.value(0).toString();

    if (m_marketstackQuotesBatchSymbols.isEmpty()) {
        setFundamentalDataStatus(QStringLiteral("Marketstack Get Quotes: Keine offenen Aktien gefunden."), false);
        return;
    }

    m_marketstackQuotesBatchActive = true;
    m_marketstackQuotesBatchIndex = 0;
    m_marketstackQuotesBatchSuccessCount = 0;
    m_marketstackQuotesBatchFailureCount = 0;
    m_marketstackQuotesRateLimitRetries = 0;
    setFundamentalDataStatus(
        QStringLiteral("Marketstack Get Quotes gestartet: Fuer %1 Aktien werden Quotes geladen.")
            .arg(m_marketstackQuotesBatchSymbols.size()),
        true);
    scheduleNextMarketstackQuotesBatchSymbol(MarketstackInitialDelayMs);
}

void DatabaseManager::stopMarketstackQuotesBatch()
{
    if (!m_marketstackQuotesBatchActive)
        return;

    finishMarketstackQuotesBatch(
        QStringLiteral("Marketstack Get Quotes gestoppt: %1/%2 verarbeitet, %3 erfolgreich, %4 fehlgeschlagen.")
            .arg(marketstackQuotesBatchDone())
            .arg(m_marketstackQuotesBatchSymbols.size())
            .arg(m_marketstackQuotesBatchSuccessCount)
            .arg(m_marketstackQuotesBatchFailureCount));
}

void DatabaseManager::loadNextMarketstackQuotesBatchSymbol()
{
    if (!m_marketstackQuotesBatchActive)
        return;

    if (m_marketstackQuotesBatchIndex >= m_marketstackQuotesBatchSymbols.size()) {
        finishMarketstackQuotesBatch(
            QStringLiteral("Marketstack Get Quotes abgeschlossen: %1 Aktien, %2 mit Quotes gespeichert, %3 fehlgeschlagen.")
                .arg(m_marketstackQuotesBatchSymbols.size())
                .arg(m_marketstackQuotesBatchSuccessCount)
                .arg(m_marketstackQuotesBatchFailureCount));
        return;
    }

    const QString symbol = m_marketstackQuotesBatchSymbols.at(m_marketstackQuotesBatchIndex++).trimmed();
    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol", "marketplace_sym", "marketplace_exchange"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec() || !query.next()) {
        ++m_marketstackQuotesBatchFailureCount;
        scheduleNextMarketstackQuotesBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    m_pendingMarketstackQuotesSymbol = query.value(QStringLiteral("Symbol")).toString().trimmed();
    m_pendingMarketstackQuotesMarketplaceSym =
        query.value(QStringLiteral("marketplace_sym")).toString().trimmed().toUpper();
    m_pendingMarketstackQuotesExchange =
        query.value(QStringLiteral("marketplace_exchange")).toString().trimmed().toUpper();

    if (m_pendingMarketstackQuotesExchange.isEmpty())
        m_pendingMarketstackQuotesExchange =
            m_pendingMarketstackQuotesMarketplaceSym.section(QLatin1Char('/'), 1, 1).trimmed().toUpper();

    setFundamentalDataStatus(
        QStringLiteral("Marketstack Get Quotes: %1/%2 %3 - Quotes laden. OK: %4, Fehler: %5")
            .arg(m_marketstackQuotesBatchIndex)
            .arg(m_marketstackQuotesBatchSymbols.size())
            .arg(m_pendingMarketstackQuotesSymbol)
            .arg(m_marketstackQuotesBatchSuccessCount)
            .arg(m_marketstackQuotesBatchFailureCount),
        true);
    requestMarketstackQuotesForPendingSymbol();
}

void DatabaseManager::scheduleNextMarketstackQuotesBatchSymbol(int delayMs)
{
    if (!m_marketstackQuotesBatchActive)
        return;
    m_marketstackQuotesBatchTimer.start(delayMs);
}

void DatabaseManager::finishMarketstackQuotesBatch(const QString &message)
{
    m_marketstackQuotesBatchTimer.stop();
    m_marketstackQuotesBatchActive = false;
    m_marketstackQuotesRateLimitRetries = 0;
    m_pendingMarketstackQuotesSymbol.clear();
    m_pendingMarketstackQuotesMarketplaceSym.clear();
    m_pendingMarketstackQuotesExchange.clear();
    setFundamentalDataStatus(message, false);
}

void DatabaseManager::requestMarketstackQuotesForPendingSymbol()
{
    if (!m_marketstackQuotesBatchActive)
        return;

    const QString requestSymbol =
        m_pendingMarketstackQuotesMarketplaceSym.section(QLatin1Char('/'), 0, 0).trimmed().toUpper();
    const QString requestExchange = m_pendingMarketstackQuotesExchange.trimmed().toUpper();
    if (requestSymbol.isEmpty() || requestExchange.isEmpty()) {
        ++m_marketstackQuotesBatchFailureCount;
        scheduleNextMarketstackQuotesBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    const QUrl url = marketstackUrl(
        QStringLiteral("eod"),
        {{QStringLiteral("symbols"), requestSymbol},
         {QStringLiteral("exchange"), requestExchange},
         {QStringLiteral("limit"), QStringLiteral("100")}});

    QNetworkReply *reply = m_marketstackNetworkManager.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMarketstackQuotesReply(reply);
    });
}

void DatabaseManager::handleMarketstackQuotesReply(QNetworkReply *reply)
{
    if (!m_marketstackQuotesBatchActive) {
        reply->deleteLater();
        return;
    }

    const QByteArray responseData = reply->readAll();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus == 429) {
        if (++m_marketstackQuotesRateLimitRetries <= MarketstackMaxRateLimitRetries) {
            setFundamentalDataStatus(
                QStringLiteral("Marketstack Get Quotes: Rate-Limit erreicht, pausiere kurz und versuche erneut (%1/%2).")
                    .arg(m_marketstackQuotesRateLimitRetries)
                    .arg(MarketstackMaxRateLimitRetries),
                true);
            QTimer::singleShot(MarketstackRateLimitDelayMs,
                               this,
                               &DatabaseManager::requestMarketstackQuotesForPendingSymbol);
            return;
        }
        finishMarketstackQuotesBatch(
            QStringLiteral("Marketstack Get Quotes wegen Marketstack-Rate-Limit gestoppt. Bitte spaeter fortsetzen."));
        return;
    }

    m_marketstackQuotesRateLimitRetries = 0;

    bool saved = false;
    if (networkOk) {
        const QJsonDocument document = QJsonDocument::fromJson(responseData);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        saved = saveMarketstackHistoricalQuotes(m_pendingMarketstackQuotesSymbol, data);
    }

    if (saved) {
        ++m_marketstackQuotesBatchSuccessCount;
        setFundamentalDataStatus(
            QStringLiteral("Marketstack Get Quotes: Quotes fuer %1 gespeichert. OK: %2, Fehler: %3.")
                .arg(m_pendingMarketstackQuotesSymbol)
                .arg(m_marketstackQuotesBatchSuccessCount)
                .arg(m_marketstackQuotesBatchFailureCount),
            true);
    } else {
        const QString noDataSymbol = m_pendingMarketstackQuotesSymbol;
        if (deleteMarketstackNoDataStock(noDataSymbol)) {
            ++m_marketstackQuotesBatchSuccessCount;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack Get Quotes: %1 ohne Quotes - aus DB geloescht. OK: %2, Fehler: %3.")
                    .arg(noDataSymbol)
                    .arg(m_marketstackQuotesBatchSuccessCount)
                    .arg(m_marketstackQuotesBatchFailureCount),
                true);
        } else {
            ++m_marketstackQuotesBatchFailureCount;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack Get Quotes: Quotes fuer %1 konnten nicht gespeichert werden. OK: %2, Fehler: %3.")
                    .arg(noDataSymbol)
                    .arg(m_marketstackQuotesBatchSuccessCount)
                    .arg(m_marketstackQuotesBatchFailureCount),
                true);
        }
    }

    scheduleNextMarketstackQuotesBatchSymbol(MarketstackNextSymbolDelayMs);
}

bool DatabaseManager::saveMarketstackHistoricalQuotes(const QString &symbol, const QJsonArray &bars)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || bars.isEmpty())
        return false;

    if (!db.transaction()) {
        qCritical() << "Marketstack-Quotes konnten keine Transaktion starten:" << db.lastError().text();
        return false;
    }

    QSqlQuery deleteQuery(db);
    deleteQuery.prepare(R"SQL(
        DELETE FROM "Quotes"
        WHERE "Symbol" = :symbol
    )SQL");
    deleteQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!deleteQuery.exec()) {
        qCritical() << "Bestehende Marketstack-Quotes konnten nicht geloescht werden:"
                    << deleteQuery.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(R"SQL(
        INSERT INTO "Quotes" (
            "Symbol", "CloseDate", "ClosePrice", "OpenPrice",
            "HighestPrice", "LowestPrice", "Volume"
        )
        VALUES (
            :symbol, :closeDate, :closePrice, :openPrice,
            :highestPrice, :lowestPrice, :volume
        )
        ON CONFLICT ("Symbol", "CloseDate") DO UPDATE
        SET
            "ClosePrice" = EXCLUDED."ClosePrice",
            "OpenPrice" = EXCLUDED."OpenPrice",
            "HighestPrice" = EXCLUDED."HighestPrice",
            "LowestPrice" = EXCLUDED."LowestPrice",
            "Volume" = EXCLUDED."Volume"
    )SQL");

    int inserted = 0;
    for (const QJsonValue &value : bars) {
        const QJsonObject bar = value.toObject();
        const QString dateText = bar.value(QStringLiteral("date")).toString();
        QDate closeDate = QDate::fromString(dateText.left(10), QStringLiteral("yyyy-MM-dd"));
        if (!closeDate.isValid())
            continue;

        insertQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        insertQuery.bindValue(QStringLiteral(":closeDate"), closeDate);
        insertQuery.bindValue(QStringLiteral(":closePrice"), bar.value(QStringLiteral("close")).toDouble());
        insertQuery.bindValue(QStringLiteral(":openPrice"), bar.value(QStringLiteral("open")).toDouble());
        insertQuery.bindValue(QStringLiteral(":highestPrice"), bar.value(QStringLiteral("high")).toDouble());
        insertQuery.bindValue(QStringLiteral(":lowestPrice"), bar.value(QStringLiteral("low")).toDouble());
        const QJsonValue volumeValue = bar.value(QStringLiteral("volume"));
        insertQuery.bindValue(QStringLiteral(":volume"),
                              volumeValue.isDouble() ? volumeValue.toDouble() : 0.0);
        if (!insertQuery.exec()) {
            qCritical() << "Marketstack-Quote konnte nicht gespeichert werden:"
                        << insertQuery.lastError().text() << normalizedSymbol << closeDate;
            db.rollback();
            return false;
        }
        ++inserted;
    }

    if (inserted == 0) {
        qCritical() << "Marketstack-Quotes enthielten keine gueltigen Tagesdaten:" << normalizedSymbol;
        db.rollback();
        return false;
    }

    const QString marketstackCurrency =
        marketstackCurrencyForMic(m_pendingMarketstackQuotesExchange);

    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"SQL(
        UPDATE "Stocks"
        SET "LastUpdateDate" = CURRENT_DATE,
            "Currency" = CASE
                WHEN NULLIF(:currency, '') IS NULL THEN "Currency"
                ELSE :currency
            END,
            "marketplace_last_error" = NULL
        WHERE "Symbol" = :symbol
    )SQL");
    updateQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    updateQuery.bindValue(QStringLiteral(":currency"), marketstackCurrency);
    if (!updateQuery.exec()) {
        qCritical() << "Stock-Update nach Marketstack-Quotes fehlgeschlagen:"
                    << updateQuery.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Marketstack-Quotes konnten nicht abgeschlossen werden:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

void DatabaseManager::startMarketstackValidationBatch()
{
    if (m_marketstackValidationBatchActive || m_marketstackBatchActive
        || m_marketstackQuotesBatchActive || m_yahooFundamentalsBatchActive)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol"
        FROM "Stocks"
        WHERE COALESCE("use_marketstack", FALSE) = TRUE
          AND COALESCE("marketplace_sym", '') <> ''
        ORDER BY "Symbol"
    )SQL");
    if (!query.exec()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Marketstack Validate konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            false);
        return;
    }

    m_marketstackValidationBatchSymbols.clear();
    while (query.next())
        m_marketstackValidationBatchSymbols << query.value(0).toString();

    if (m_marketstackValidationBatchSymbols.isEmpty()) {
        setFundamentalDataStatus(QStringLiteral("Marketstack Validate: Keine gespeicherten Mappings gefunden."), false);
        return;
    }

    m_marketstackValidationBatchActive = true;
    m_marketstackValidationBatchIndex = 0;
    m_marketstackValidationBatchSuccessCount = 0;
    m_marketstackValidationBatchFailureCount = 0;
    m_marketstackValidationRateLimitRetries = 0;
    setFundamentalDataStatus(
        QStringLiteral("Marketstack Validate gestartet: %1 Mappings werden geprueft.")
            .arg(m_marketstackValidationBatchSymbols.size()),
        true);
    scheduleNextMarketstackValidationBatchSymbol(MarketstackInitialDelayMs);
}

void DatabaseManager::stopMarketstackValidationBatch()
{
    if (!m_marketstackValidationBatchActive)
        return;

    finishMarketstackValidationBatch(
        QStringLiteral("Marketstack Validate gestoppt: %1/%2 verarbeitet, %3 OK, %4 zu pruefen.")
            .arg(marketstackValidationBatchDone())
            .arg(m_marketstackValidationBatchSymbols.size())
            .arg(m_marketstackValidationBatchSuccessCount)
            .arg(m_marketstackValidationBatchFailureCount));
}

void DatabaseManager::loadNextMarketstackValidationBatchSymbol()
{
    if (!m_marketstackValidationBatchActive)
        return;

    if (m_marketstackValidationBatchIndex >= m_marketstackValidationBatchSymbols.size()) {
        finishMarketstackValidationBatch(
            QStringLiteral("Marketstack Validate abgeschlossen: %1 Mappings, %2 OK, %3 zu pruefen.")
                .arg(m_marketstackValidationBatchSymbols.size())
                .arg(m_marketstackValidationBatchSuccessCount)
                .arg(m_marketstackValidationBatchFailureCount));
        return;
    }

    const QString symbol = m_marketstackValidationBatchSymbols.at(m_marketstackValidationBatchIndex++).trimmed();
    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol", "ISIN", "Name", "marketplace_sym", "marketplace_exchange"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec() || !query.next()) {
        ++m_marketstackValidationBatchFailureCount;
        scheduleNextMarketstackValidationBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    m_pendingMarketstackValidationSymbol = query.value(QStringLiteral("Symbol")).toString().trimmed();
    m_pendingMarketstackValidationIsin = query.value(QStringLiteral("ISIN")).toString().trimmed();
    m_pendingMarketstackValidationName = query.value(QStringLiteral("Name")).toString().trimmed();
    m_pendingMarketstackValidationMarketplaceSym =
        query.value(QStringLiteral("marketplace_sym")).toString().trimmed().toUpper();
    m_pendingMarketstackValidationExchange =
        query.value(QStringLiteral("marketplace_exchange")).toString().trimmed().toUpper();
    if (m_pendingMarketstackValidationExchange.isEmpty())
        m_pendingMarketstackValidationExchange =
            m_pendingMarketstackValidationMarketplaceSym.section(QLatin1Char('/'), 1, 1).trimmed().toUpper();

    setFundamentalDataStatus(
        QStringLiteral("Marketstack Validate: %1/%2 %3 pruefen. OK: %4, Zu pruefen: %5")
            .arg(m_marketstackValidationBatchIndex)
            .arg(m_marketstackValidationBatchSymbols.size())
            .arg(m_pendingMarketstackValidationSymbol)
            .arg(m_marketstackValidationBatchSuccessCount)
            .arg(m_marketstackValidationBatchFailureCount),
        true);
    requestMarketstackValidationForPendingSymbol();
}

void DatabaseManager::scheduleNextMarketstackValidationBatchSymbol(int delayMs)
{
    if (!m_marketstackValidationBatchActive)
        return;
    m_marketstackValidationBatchTimer.start(delayMs);
}

void DatabaseManager::finishMarketstackValidationBatch(const QString &message)
{
    m_marketstackValidationBatchTimer.stop();
    m_marketstackValidationBatchActive = false;
    m_marketstackValidationRateLimitRetries = 0;
    m_pendingMarketstackValidationSymbol.clear();
    m_pendingMarketstackValidationIsin.clear();
    m_pendingMarketstackValidationName.clear();
    m_pendingMarketstackValidationMarketplaceSym.clear();
    m_pendingMarketstackValidationExchange.clear();
    setFundamentalDataStatus(message, false);
}

void DatabaseManager::requestMarketstackValidationForPendingSymbol()
{
    if (!m_marketstackValidationBatchActive)
        return;

    const QString requestSymbol =
        m_pendingMarketstackValidationMarketplaceSym.section(QLatin1Char('/'), 0, 0).trimmed().toUpper();
    QString searchTerm = requestSymbol;
    if (searchTerm.contains(QLatin1Char('.')))
        searchTerm = searchTerm.section(QLatin1Char('.'), 0, 0);
    if (searchTerm.isEmpty()) {
        ++m_marketstackValidationBatchFailureCount;
        scheduleNextMarketstackValidationBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    QUrl url(QStringLiteral("https://marketstack.com/stock_api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("exchange"), m_pendingMarketstackValidationExchange.trimmed().toUpper());
    query.addQueryItem(QStringLiteral("search"), searchTerm);
    url.setQuery(query);

    QNetworkReply *reply = m_marketstackNetworkManager.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMarketstackValidationReply(reply);
    });
}

void DatabaseManager::handleMarketstackValidationReply(QNetworkReply *reply)
{
    if (!m_marketstackValidationBatchActive) {
        reply->deleteLater();
        return;
    }

    const QByteArray responseData = reply->readAll();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus == 429) {
        if (++m_marketstackValidationRateLimitRetries <= MarketstackMaxRateLimitRetries) {
            setFundamentalDataStatus(
                QStringLiteral("Marketstack Validate: Rate-Limit erreicht, pausiere kurz und versuche erneut (%1/%2).")
                    .arg(m_marketstackValidationRateLimitRetries)
                    .arg(MarketstackMaxRateLimitRetries),
                true);
            QTimer::singleShot(MarketstackRateLimitDelayMs,
                               this,
                               &DatabaseManager::requestMarketstackValidationForPendingSymbol);
            return;
        }
        finishMarketstackValidationBatch(
            QStringLiteral("Marketstack Validate wegen Marketstack-Rate-Limit gestoppt. Bitte spaeter fortsetzen."));
        return;
    }

    m_marketstackValidationRateLimitRetries = 0;

    QString error;
    if (!networkOk) {
        error = QStringLiteral("Marketstack-Validierung: Netzwerkfehler");
    } else {
        const QString expectedSymbol =
            m_pendingMarketstackValidationMarketplaceSym.section(QLatin1Char('/'), 0, 0).trimmed().toUpper();
        const QString expectedExchange = m_pendingMarketstackValidationExchange.trimmed().toUpper();
        const QJsonDocument document = QJsonDocument::fromJson(responseData);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        QJsonObject selectedTicker;
        for (const QJsonValue &value : data) {
            const QJsonObject ticker = value.toObject();
            const QJsonObject exchange = ticker.value(QStringLiteral("stock_exchange")).toObject();
            const QString tickerSymbol = ticker.value(QStringLiteral("ticker")).toString().trimmed().isEmpty()
                ? ticker.value(QStringLiteral("symbol")).toString().trimmed()
                : ticker.value(QStringLiteral("ticker")).toString().trimmed();
            if (marketstackWebsiteTickerMatches(tickerSymbol, expectedSymbol, expectedExchange)
                && exchange.value(QStringLiteral("mic")).toString().trimmed().compare(expectedExchange, Qt::CaseInsensitive) == 0) {
                selectedTicker = ticker;
                break;
            }
        }

        if (selectedTicker.isEmpty()) {
            error = QStringLiteral("Marketstack-Validierung: Symbol/MIC nicht gefunden");
        } else {
            const QJsonObject exchange = selectedTicker.value(QStringLiteral("stock_exchange")).toObject();
            const QString candidateName = selectedTicker.value(QStringLiteral("name")).toString();
            const QString candidateCountryCode = exchange.value(QStringLiteral("country_code")).toString();
            if (!marketstackNameMatchesStock(m_pendingMarketstackValidationName, candidateName)) {
                error = QStringLiteral("Marketstack-Validierung: Name passt nicht (%1)")
                            .arg(candidateName);
            } else if (!marketstackCountryMatchesStock(m_pendingMarketstackValidationIsin,
                                                       candidateCountryCode,
                                                       expectedExchange)) {
                error = QStringLiteral("Marketstack-Validierung: Land passt nicht (%1)")
                            .arg(candidateCountryCode);
            }
        }
    }

    if (error.isEmpty()) {
        ++m_marketstackValidationBatchSuccessCount;
        setFundamentalDataStatus(
            QStringLiteral("Marketstack Validate: %1 OK. OK: %2, Zu pruefen: %3")
                .arg(m_pendingMarketstackValidationSymbol)
                .arg(m_marketstackValidationBatchSuccessCount)
                .arg(m_marketstackValidationBatchFailureCount),
            true);
    } else {
        resetMarketstackMapping(m_pendingMarketstackValidationSymbol, error);
        ++m_marketstackValidationBatchFailureCount;
        setFundamentalDataStatus(
            QStringLiteral("Marketstack Validate: %1 zu pruefen (%2). OK: %3, Zu pruefen: %4")
                .arg(m_pendingMarketstackValidationSymbol,
                     error)
                .arg(m_marketstackValidationBatchSuccessCount)
                .arg(m_marketstackValidationBatchFailureCount),
            true);
    }

    scheduleNextMarketstackValidationBatchSymbol(MarketstackLookupDelayMs);
}

bool DatabaseManager::resetMarketstackMapping(const QString &symbol, const QString &error)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty())
        return false;

    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"SQL(
        UPDATE "Stocks"
        SET "marketplace_last_error" = LEFT(:error, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    updateQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    updateQuery.bindValue(QStringLiteral(":error"), error.trimmed());
    if (!updateQuery.exec())
        return false;

    emit ibkrStockDataUpdated(normalizedSymbol);
    return true;
}
