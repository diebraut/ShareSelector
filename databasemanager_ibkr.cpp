#include "databasemanager.h"
#include "databasemanager_internal.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QTimer>

using namespace DatabaseManagerInternal;

namespace {
QString csvField(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

bool updateActiveBoughtStockCurrentValue(QSqlDatabase &db, const QString &symbol, double currentValue)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || currentValue <= 0.0)
        return true;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "BoughtStocks"
        SET
            "CurrentValue" = :currentValue,
            "ValueIncreasePercent" = CASE
                WHEN NULLIF("EntryValue", 0) IS NULL THEN "ValueIncreasePercent"
                ELSE ROUND(((CAST(:currentValue AS numeric) - "EntryValue") / NULLIF("EntryValue", 0) * 100)::numeric, 2)
            END,
            "UpdatedAt" = CURRENT_TIMESTAMP
        WHERE "DepotId" = 1
          AND "Symbol" = :symbol
          AND "SellDate" IS NULL
          AND COALESCE("Status", 0) <> 10
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    query.bindValue(QStringLiteral(":currentValue"), currentValue);
    return query.exec();
}

void appendIbkrQuoteTimingLog(const QString &symbol,
                              const QString &phase,
                              qint64 elapsedMs,
                              QProcess::ExitStatus exitStatus,
                              bool parseOk,
                              bool success,
                              const QString &message)
{
    const QString logPath = QDir(QCoreApplication::applicationDirPath())
    .filePath(QStringLiteral("ibkr_quote_timings.csv"));
    const bool writeHeader = !QFileInfo::exists(logPath) || QFileInfo(logPath).size() == 0;
    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "IBKR-Timing-Log konnte nicht geschrieben werden:" << logPath << file.errorString();
        return;
    }

    QTextStream stream(&file);
    if (writeHeader)
        stream << "timestamp,symbol,phase,elapsed_ms,elapsed_s,exit_status,parse_ok,success,message\n";
    stream << csvField(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)) << ','
           << csvField(symbol) << ','
           << csvField(phase) << ','
           << elapsedMs << ','
           << QString::number(elapsedMs / 1000.0, 'f', 3) << ','
           << csvField(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash")) << ','
           << (parseOk ? QStringLiteral("true") : QStringLiteral("false")) << ','
           << (success ? QStringLiteral("true") : QStringLiteral("false")) << ','
           << csvField(message.left(500)) << '\n';
}

QDate mostRecentWeekday(const QDate &date)
{
    QDate result = date;
    while (result.dayOfWeek() > 5)
        result = result.addDays(-1);
    return result;
}

}
void DatabaseManager::startIbkrBatch()
{
    if (m_ibkrBatchActive || m_ibkrDataLoading || m_ibkrNameCheckBatchActive)
        return;

    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Stammdaten-Batch")))
        return;

    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT s."Symbol"
        FROM "Stocks" s
        LEFT JOIN "BoughtStocks" b ON b."Symbol" = s."Symbol"
        WHERE COALESCE(s."Symbol", '') <> ''
          AND COALESCE(s."ISIN", '') = ''
          AND COALESCE(s."IBKRValidationStatus", '') NOT IN (
              'duplicate_isin',
              'ambiguous_isin',
              'review_required',
              'name_mismatch',
              'verified_name',
              'verified_isin',
              'verified_symbol'
          )
          AND (
              s."IBKRLastSyncAt" IS NULL
              OR s."IBKRLastSyncAt" < CURRENT_TIMESTAMP - INTERVAL '30 days'
          )
          AND (
              COALESCE(s."IBKRFailureCount", 0) < 3
              OR s."IBKRLastAttemptAt" IS NULL
              OR s."IBKRLastAttemptAt" < CURRENT_TIMESTAMP - INTERVAL '1 day'
          )
        ORDER BY
          CASE
            WHEN s."IBKRConId" IS NULL AND COALESCE(s."IBKRLastError", '') <> '' THEN 0
            ELSE 1
          END,
          s."IBKRLastAttemptAt" DESC NULLS LAST,
          CASE WHEN b."Symbol" IS NOT NULL THEN 0 ELSE 1 END,
          CASE WHEN COALESCE(s."ISIN", '') <> '' THEN 0 ELSE 1 END,
          s."IBKRFailureCount" NULLS FIRST,
          s."IBKRLastSyncAt" NULLS FIRST,
          s."IBKRLastAttemptAt" NULLS FIRST,
          s."Symbol"
    )SQL");

    if (!query.exec()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: IBKR-Batch konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            m_ibkrConnected,
            false);
        return;
    }

    m_ibkrBatchSymbols.clear();
    while (query.next())
        m_ibkrBatchSymbols << query.value(0).toString();

    if (m_ibkrBatchSymbols.isEmpty()) {
        setIbkrConnectionState(QStringLiteral("IBKR-Batch: Keine faelligen Aktien gefunden."), m_ibkrConnected, false);
        return;
    }

    m_ibkrBatchActive = true;
    m_ibkrBatchIndex = 0;
    m_ibkrBatchSuccessCount = 0;
    m_ibkrBatchFailureCount = 0;
    setIbkrConnectionState(
        QStringLiteral("IBKR-Batch gestartet: %1 Aktien werden seriell aktualisiert. Reihenfolge: Depot, dann ISIN, dann uebrige Symbole.")
            .arg(m_ibkrBatchSymbols.size()),
        m_ibkrConnected,
        false);
    scheduleNextIbkrBatchSymbol(100);
}

void DatabaseManager::stopIbkrBatch()
{
    if (!m_ibkrBatchActive)
        return;

    m_ibkrBatchTimer.stop();
    if (m_ibkrDataLoading && m_ibkrProcess.state() != QProcess::NotRunning)
        m_ibkrProcess.kill();

    finishIbkrBatch(
        QStringLiteral("IBKR-Batch gestoppt: %1/%2 verarbeitet, %3 erfolgreich, %4 fehlgeschlagen.")
            .arg(ibkrBatchDone())
            .arg(m_ibkrBatchSymbols.size())
            .arg(m_ibkrBatchSuccessCount)
            .arg(m_ibkrBatchFailureCount));
}

void DatabaseManager::startIbkrGetStocks()
{
    startIbkrGetStocksBatch(true);
}

void DatabaseManager::startIbkrGetAllStocks()
{
    startIbkrGetStocksBatch(false);
}

void DatabaseManager::getIbkrQuotesForSingleStock(const QString &symbol)
{
    const QString normalizedSymbol = symbol.trimmed();

    if (normalizedSymbol.isEmpty()) {
        setIbkrConnectionState(QStringLiteral("Get Quotes: Keine Aktie ausgewaehlt."), m_ibkrConnected, false);
        return;
    }

    if (m_ibkrGetStocksBatchActive || m_ibkrDataLoading || m_ibkrNameCheckBatchActive)
        return;

    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Quote-Abruf")))
        return;
    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    m_ibkrGetStocksBatchName = QStringLiteral("Get Quotes");
    setIbkrConnectionState(
        QStringLiteral("Get Quotes: %1 wird aktualisiert.").arg(normalizedSymbol),
        m_ibkrConnected,
        false);

    if (!startIbkrQuoteExchangeProbeForSymbol(normalizedSymbol))
        emit ibkrConnectionChanged();
}

void DatabaseManager::startIbkrGetStocksForSymbols(const QVariantList &symbols)
{
    if (m_ibkrGetStocksBatchActive || m_ibkrDataLoading || m_ibkrNameCheckBatchActive)
        return;

    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Quote-Abruf")))
        return;
    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    QStringList uniqueSymbols;
    for (const QVariant &value : symbols) {
        const QString symbol = value.toString().trimmed();
        if (!symbol.isEmpty() && !uniqueSymbols.contains(symbol, Qt::CaseInsensitive))
            uniqueSymbols << symbol;
    }

    if (uniqueSymbols.isEmpty()) {
        setIbkrConnectionState(QStringLiteral("Get Quotes: Keine Aktien ausgewaehlt."), m_ibkrConnected, false);
        return;
    }

    m_ibkrGetStocksBatchName = QStringLiteral("Get Quotes");
    m_ibkrGetStocksSymbols = uniqueSymbols;
    m_ibkrGetStocksIndex = 0;
    m_ibkrGetStocksSuccessCount = 0;
    m_ibkrGetStocksFailureCount = 0;
    m_ibkrGetStocksChangedQuoteCount = 0;
    m_ibkrGetStocksHistoricalOnlyBatch = false;
    m_ibkrGetStocksBatchActive = true;

    setIbkrConnectionState(
        QStringLiteral("%1 gestartet: Fuer %2 ausgewaehlte Aktien werden Quotes aktualisiert.")
            .arg(m_ibkrGetStocksBatchName)
            .arg(m_ibkrGetStocksSymbols.size()),
        m_ibkrConnected,
        false);
    scheduleNextIbkrGetStocksSymbol(100);
}

void DatabaseManager::startIbkrGetStocksBatch(bool depotOnly)
{
    if (m_ibkrGetStocksBatchActive || m_ibkrDataLoading || m_ibkrNameCheckBatchActive)
        return;

    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Quote-Batch")))
        return;
    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    m_ibkrGetStocksBatchName = depotOnly
                                   ? QStringLiteral("Get New Quotes for Depot")
                                   : QStringLiteral("Get new Quotes for IBKR Data");

    QSqlQuery query(db);
    if (depotOnly) {
        query.prepare(R"SQL(
            SELECT DISTINCT s."Symbol"
            FROM "Stocks" s
            JOIN "BoughtStocks" b ON b."Symbol" = s."Symbol"
            WHERE COALESCE(s."Symbol", '') <> ''
              AND s."IBKRConId" IS NOT NULL
              AND COALESCE(s."from_IBKR", TRUE) = TRUE
              AND b."DepotId" = 1
              AND b."SellDate" IS NULL
              AND COALESCE(b."Status", 0) <> 10
            ORDER BY s."Symbol"
        )SQL");
    } else {
        query.prepare(R"SQL(
            SELECT DISTINCT s."Symbol"
            FROM "Stocks" s
            WHERE COALESCE(s."Symbol", '') <> ''
              AND s."IBKRConId" IS NOT NULL
              AND COALESCE(s."from_IBKR", TRUE) = TRUE
            ORDER BY s."Symbol"
        )SQL");
    }
    if (!query.exec()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: %1 konnte die Aktienliste nicht laden: %2")
                .arg(m_ibkrGetStocksBatchName)
                .arg(query.lastError().text()),
            m_ibkrConnected,
            false);
        return;
    }

    m_ibkrGetStocksSymbols.clear();
    while (query.next())
        m_ibkrGetStocksSymbols << query.value(0).toString();

    if (m_ibkrGetStocksSymbols.isEmpty()) {
        setIbkrConnectionState(
            QStringLiteral("%1: Keine Aktien mit IBKRConId gefunden.").arg(m_ibkrGetStocksBatchName),
            m_ibkrConnected,
            false);
        return;
    }

    m_ibkrGetStocksBatchActive = true;
    m_ibkrGetStocksIndex = 0;
    m_ibkrGetStocksSuccessCount = 0;
    m_ibkrGetStocksFailureCount = 0;
    m_ibkrGetStocksChangedQuoteCount = 0;
    m_ibkrGetStocksHistoricalOnlyBatch = !depotOnly;
    setIbkrConnectionState(
        QStringLiteral("%1 gestartet: Fuer %2 Aktien werden Quotes aktualisiert.")
            .arg(m_ibkrGetStocksBatchName)
            .arg(m_ibkrGetStocksSymbols.size()),
        m_ibkrConnected,
        false);
    scheduleNextIbkrGetStocksSymbol(100);
}

void DatabaseManager::stopIbkrGetStocks()
{
    if (!m_ibkrGetStocksBatchActive
        && !m_pendingIbkrQuoteAfterMetadata
        && !m_pendingIbkrProcessIsHistoricalQuotes
        && !m_pendingIbkrProcessIsQuoteExchangeProbe
        && !m_pendingIbkrProcessIsMarketSnapshot) {
        return;
    }

    m_ibkrGetStocksTimer.stop();
    m_ibkrDataTimeout.stop();
    if (m_ibkrProcess.state() != QProcess::NotRunning)
        m_ibkrProcess.kill();
    m_ibkrGetStocksBatchActive = false;
    m_ibkrGetStocksHistoricalOnlyBatch = false;
    m_ibkrDataLoading = false;
    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_pendingIbkrProcessIsMarketSnapshot = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrQuoteAfterMetadata = false;
    const QString symbol = m_pendingIbkrQuotesSymbol;
    m_pendingIbkrQuotesSymbol.clear();
    m_pendingIbkrQuotesIsin.clear();
    m_pendingIbkrQuotesIbkrSymbol.clear();
    m_pendingIbkrQuotesCurrency.clear();
    m_pendingIbkrQuotesExchange.clear();
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesProbeExchanges.clear();
    m_pendingIbkrQuotesConId = 0;
    m_pendingIbkrQuotesDays = 0;
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = false;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_pendingIbkrQuotesSmartHistoricalRetry = false;
    m_ibkrPendingSymbol.clear();
    m_ibkrDataTimeout.setInterval(25000);
    setIbkrConnectionState(
        QStringLiteral("%1 gestoppt%2.")
            .arg(m_ibkrGetStocksBatchName)
            .arg(symbol.isEmpty() ? QString() : QStringLiteral(": %1").arg(symbol)),
        m_ibkrConnected,
        false);
    emit ibkrConnectionChanged();
}

void DatabaseManager::loadNextIbkrGetStocksSymbol()
{
    if (!m_ibkrGetStocksBatchActive)
        return;

    if (m_ibkrDataLoading) {
        scheduleNextIbkrGetStocksSymbol(200);
        return;
    }

    if (m_ibkrGetStocksIndex >= m_ibkrGetStocksSymbols.size()) {
        finishIbkrGetStocksBatch(
            QStringLiteral("%1 abgeschlossen: %2 Aktien, %3 OK, %4 Quote-Zeilen neu/geaendert, %5 fehlgeschlagen.")
                .arg(m_ibkrGetStocksBatchName)
                .arg(m_ibkrGetStocksSymbols.size())
                .arg(m_ibkrGetStocksSuccessCount)
                .arg(m_ibkrGetStocksChangedQuoteCount)
                .arg(m_ibkrGetStocksFailureCount));
        return;
    }

    const QString symbol = m_ibkrGetStocksSymbols.at(m_ibkrGetStocksIndex++).trimmed();
    if (symbol.isEmpty()) {
        scheduleNextIbkrGetStocksSymbol(50);
        return;
    }

    setIbkrConnectionState(
        (m_ibkrGetStocksHistoricalOnlyBatch
             ? QStringLiteral("%1: %2/%3 %4 - neue Quotes per Historical Data laden. Neue Quotes: %5, Fehler: %6")
             : QStringLiteral("%1: %2/%3 %4 - Stammdaten aktualisieren und neue Quotes laden. Neue Quotes: %5, Fehler: %6"))
            .arg(m_ibkrGetStocksBatchName)
            .arg(m_ibkrGetStocksIndex)
            .arg(m_ibkrGetStocksSymbols.size())
            .arg(symbol)
            .arg(m_ibkrGetStocksSuccessCount)
            .arg(m_ibkrGetStocksFailureCount),
        m_ibkrConnected,
        false);
    bool started = false;
    if (m_ibkrGetStocksHistoricalOnlyBatch) {
        started = startIbkrHistoricalQuotesOnlyForSymbol(symbol);
    } else {
        m_pendingIbkrQuoteAfterMetadata = true;
        getIbkrData(symbol);
        started = true;
    }
    if (!started) {
        m_pendingIbkrQuoteAfterMetadata = false;
        ++m_ibkrGetStocksFailureCount;
        scheduleNextIbkrGetStocksSymbol(500);
    }
}

void DatabaseManager::scheduleNextIbkrGetStocksSymbol(int delayMs)
{
    if (!m_ibkrGetStocksBatchActive)
        return;
    m_ibkrGetStocksTimer.start(delayMs);
}

void DatabaseManager::finishIbkrGetStocksBatch(const QString &message)
{
    m_ibkrGetStocksTimer.stop();
    m_ibkrGetStocksBatchActive = false;
    m_ibkrGetStocksHistoricalOnlyBatch = false;
    m_ibkrDataLoading = false;
    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_pendingIbkrProcessIsMarketSnapshot = false;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrQuotesSymbol.clear();
    m_pendingIbkrQuotesIsin.clear();
    m_pendingIbkrQuotesIbkrSymbol.clear();
    m_pendingIbkrQuotesCurrency.clear();
    m_pendingIbkrQuotesExchange.clear();
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesProbeExchanges.clear();
    m_pendingIbkrQuotesConId = 0;
    m_pendingIbkrQuotesDays = 0;
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = false;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_pendingIbkrQuotesSmartHistoricalRetry = false;
    m_pendingIbkrQuoteAfterMetadata = false;
    m_ibkrDataTimeout.setInterval(25000);
    setIbkrConnectionState(message, m_ibkrConnected, false);
    emit ibkrConnectionChanged();
    emit ibkrStockDataUpdated(QString());
}

int DatabaseManager::ibkrMissingQuoteDays(const QString &symbol, int fallbackDays)
{
    const QString normalizedSymbol = symbol.trimmed();
    const int calendarDaysForTradingWindow = qMax(1, ((fallbackDays * 7 + 4) / 5) + 21);
    if (normalizedSymbol.isEmpty() || !db.isOpen())
        return calendarDaysForTradingWindow;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT
            MAX("CloseDate") AS last_quote_date,
            COUNT(*) FILTER (
                WHERE COALESCE("ClosePrice", 0) > 0
            ) AS valid_quote_count
        FROM "Quotes"
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!query.exec() || !query.next() || query.value(QStringLiteral("last_quote_date")).isNull())
        return calendarDaysForTradingWindow;

    const QDate lastQuoteDate = query.value(QStringLiteral("last_quote_date")).toDate();
    if (!lastQuoteDate.isValid())
        return calendarDaysForTradingWindow;

    const int validQuoteCount = query.value(QStringLiteral("valid_quote_count")).toInt();
    const int missingCalendarDays = qMax(1, lastQuoteDate.daysTo(QDate::currentDate()) + 2);
    if (validQuoteCount < fallbackDays)
        return qMax(missingCalendarDays, calendarDaysForTradingWindow);
    return missingCalendarDays;
}

bool DatabaseManager::ibkrHasQuoteForExpectedDate(const QString &symbol) const
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || !db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT 1
        FROM "Quotes"
        WHERE "Symbol" = :symbol
          AND "CloseDate" = :closeDate
          AND COALESCE("ClosePrice", 0) > 0
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    query.bindValue(QStringLiteral(":closeDate"), mostRecentWeekday(QDate::currentDate()));
    return query.exec() && query.next();
}

bool DatabaseManager::startIbkrHistoricalQuotesOnlyForSymbol(const QString &symbol)
{
    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "Symbol", "IBKRConId", "IBKRResolvedSymbol", "LocalSymbol",
               "Currency", "CountryCode", "PrimaryExchange", "MIC", "ISIN",
               "IBKRQuoteExchange", "IBKRBestDirectExchange"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    if (!stockQuery.exec() || !stockQuery.next()) {
        updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Stock nicht in der Datenbank gefunden"));
        setIbkrConnectionState(
            QStringLiteral("%1: %2 nicht in der Datenbank gefunden.")
                .arg(m_ibkrGetStocksBatchName)
                .arg(symbol),
            m_ibkrConnected,
            false);
        return false;
    }

    const qint64 conId = stockQuery.value(QStringLiteral("IBKRConId")).toLongLong();
    if (conId <= 0) {
        updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Keine gueltige IBKRConId"));
        setIbkrConnectionState(
            QStringLiteral("%1: %2 hat keine gueltige IBKRConId.")
                .arg(m_ibkrGetStocksBatchName)
                .arg(symbol),
            m_ibkrConnected,
            false);
        return false;
    }

    QString ibkrSymbol = stockQuery.value(QStringLiteral("IBKRResolvedSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = stockQuery.value(QStringLiteral("LocalSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();

    QString currency = stockQuery.value(QStringLiteral("Currency")).toString().trimmed();
    if (currency.isEmpty())
        currency = currencyForCountry(stockQuery.value(QStringLiteral("CountryCode")).toString());

    const QString cachedQuoteExchange =
        stockQuery.value(QStringLiteral("IBKRQuoteExchange")).toString().trimmed().toUpper();
    const QString cachedPrimaryExchange =
        stockQuery.value(QStringLiteral("IBKRBestDirectExchange")).toString().trimmed().toUpper();
    const QString primaryExchange =
        stockQuery.value(QStringLiteral("PrimaryExchange")).toString().trimmed().toUpper();
    const QString mic = stockQuery.value(QStringLiteral("MIC")).toString().trimmed().toUpper();

    QString quoteExchange = cachedQuoteExchange;
    if (quoteExchange.isEmpty())
        quoteExchange = cachedPrimaryExchange;
    if (quoteExchange.isEmpty())
        quoteExchange = primaryExchange;
    if (quoteExchange.isEmpty())
        quoteExchange = mic;

    m_ibkrSocket.abort();
    m_ibkrPendingSymbol = symbol.trimmed();
    m_pendingIbkrQuotesSymbol = symbol.trimmed();
    m_pendingIbkrQuotesIsin = stockQuery.value(QStringLiteral("ISIN")).toString().trimmed().toUpper();
    m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
    m_pendingIbkrQuotesCurrency = currency;
    m_pendingIbkrQuotesExchange = quoteExchange;
    m_pendingIbkrQuotesPrimaryExchange.clear();
    if (quoteExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) == 0)
        m_pendingIbkrQuotesPrimaryExchange = cachedPrimaryExchange.isEmpty() ? primaryExchange : cachedPrimaryExchange;
    m_pendingIbkrQuotesProbeExchanges.clear();
    m_pendingIbkrQuotesConId = conId;
    m_pendingIbkrQuotesDays = ibkrMissingQuoteDays(m_pendingIbkrQuotesSymbol, 90);
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = false;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_pendingIbkrQuotesSmartHistoricalRetry = false;
    m_pendingIbkrProcessIsHistoricalQuotes = true;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_pendingIbkrProcessIsMarketSnapshot = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrProcessIsNameCheck = false;
    m_ibkrDataLoading = true;
    startIbkrQuoteHelperRequest(false);
    return true;
}

bool DatabaseManager::startIbkrQuoteExchangeProbeForSymbol(const QString &symbol)
{
    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "Symbol", "IBKRConId", "IBKRResolvedSymbol", "LocalSymbol",
               "Currency", "CountryCode", "PrimaryExchange", "MIC", "ISIN",
               "ValidExchanges", "IBKRQuoteExchange", "IBKRBestDirectExchange"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    if (!stockQuery.exec() || !stockQuery.next()) {
        updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Stock nicht in der Datenbank gefunden"));
        setIbkrConnectionState(
            QStringLiteral("%1: %2 nicht in der Datenbank gefunden.")
                .arg(m_ibkrGetStocksBatchName)
                .arg(symbol),
            m_ibkrConnected,
            false);
        return false;
    }

    const qint64 conId = stockQuery.value(QStringLiteral("IBKRConId")).toLongLong();
    if (conId <= 0) {
        updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Keine gueltige IBKRConId"));
        setIbkrConnectionState(
            QStringLiteral("%1: %2 hat keine gueltige IBKRConId.")
                .arg(m_ibkrGetStocksBatchName)
                .arg(symbol),
            m_ibkrConnected,
            false);
        return false;
    }

    QString ibkrSymbol = stockQuery.value(QStringLiteral("IBKRResolvedSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = stockQuery.value(QStringLiteral("LocalSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();

    QString currency = stockQuery.value(QStringLiteral("Currency")).toString().trimmed();
    if (currency.isEmpty())
        currency = currencyForCountry(stockQuery.value(QStringLiteral("CountryCode")).toString());
    const QString cachedQuoteExchange =
        stockQuery.value(QStringLiteral("IBKRQuoteExchange")).toString().trimmed().toUpper();
    const QString cachedPrimaryExchange =
        stockQuery.value(QStringLiteral("IBKRBestDirectExchange")).toString().trimmed().toUpper();
    const QString validExchanges = stockQuery.value(QStringLiteral("ValidExchanges")).toString();
    const bool supportsSmart = ibkrValidExchangesContainSmart(validExchanges);
    const QStringList probeExchanges = ibkrQuoteExchangeCandidates(
        validExchanges,
        stockQuery.value(QStringLiteral("PrimaryExchange")).toString(),
        stockQuery.value(QStringLiteral("MIC")).toString());
    const bool hasEuroProbeExchange = ibkrQuoteExchangesContainEuro(probeExchanges);
    const bool cachedExchangeIsEuro =
        ibkrIsEuroQuoteExchange(cachedQuoteExchange)
        || ibkrIsEuroQuoteExchange(cachedPrimaryExchange);

    if (!cachedQuoteExchange.isEmpty() && (!hasEuroProbeExchange || cachedExchangeIsEuro)) {
        m_ibkrSocket.abort();
        m_ibkrPendingSymbol = symbol.trimmed();
        m_pendingIbkrQuotesSymbol = symbol.trimmed();
        m_pendingIbkrQuotesIsin = stockQuery.value(QStringLiteral("ISIN")).toString().trimmed().toUpper();
        m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
        m_pendingIbkrQuotesCurrency = currency;
        m_pendingIbkrQuotesExchange = cachedQuoteExchange;
        m_pendingIbkrQuotesPrimaryExchange = cachedPrimaryExchange;
        if (!cachedPrimaryExchange.isEmpty()
            && m_pendingIbkrQuotesExchange.compare(cachedPrimaryExchange, Qt::CaseInsensitive) != 0) {
            m_pendingIbkrQuotesExchange = cachedPrimaryExchange;
            m_pendingIbkrQuotesPrimaryExchange.clear();
            saveIbkrQuoteExchange(symbol.trimmed(), cachedPrimaryExchange, 0.0, cachedPrimaryExchange, 0.0);
        } else if (m_pendingIbkrQuotesExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0) {
            m_pendingIbkrQuotesPrimaryExchange.clear();
        }
        m_pendingIbkrQuotesProbeExchanges = ibkrQuoteFallbackExchanges(cachedPrimaryExchange, probeExchanges);
        m_pendingIbkrQuotesConId = conId;
        m_pendingIbkrQuotesDays = ibkrMissingQuoteDays(m_pendingIbkrQuotesSymbol, 90);
        m_pendingIbkrQuotesFallbackIndex = 0;
        m_pendingIbkrQuotesSupportsSmart = supportsSmart;
        m_pendingIbkrQuotesForceDirectProbeResult = false;
        m_pendingIbkrQuotesSmartHistoricalRetry = false;
        const bool startWithSnapshot = !m_ibkrGetStocksBatchActive
                                       && ibkrHasQuoteForExpectedDate(m_pendingIbkrQuotesSymbol)
                                       && m_pendingIbkrQuotesExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0;
        m_pendingIbkrProcessIsHistoricalQuotes = !startWithSnapshot;
        m_pendingIbkrProcessIsQuoteExchangeProbe = false;
        m_pendingIbkrProcessIsMarketSnapshot = startWithSnapshot;
        m_pendingIbkrProcessIsNameSearch = false;
        m_pendingIbkrProcessIsNameCheck = false;
        m_ibkrDataLoading = true;
        if (startWithSnapshot) {
            startIbkrQuoteSnapshotFallback(
                QStringLiteral("Quote fuer aktuellen Handelstag vorhanden, aktualisiere Live-Snapshot"));
            return true;
        }
        startIbkrQuoteHelperRequest(false);
        return true;
    }

    updateIbkrQuoteExchangeAttempt(symbol.trimmed());
    if (probeExchanges.isEmpty()) {
        updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Keine pruefbaren Boersen-Kandidaten"));
        setIbkrConnectionState(
            QStringLiteral("%1: %2 hat keine pruefbaren Boersen-Kandidaten.")
                .arg(m_ibkrGetStocksBatchName)
                .arg(symbol),
            m_ibkrConnected,
            false);
        return false;
    }

    m_ibkrSocket.abort();
    m_ibkrPendingSymbol = symbol.trimmed();
    m_pendingIbkrQuotesSymbol = symbol.trimmed();
    m_pendingIbkrQuotesIsin = stockQuery.value(QStringLiteral("ISIN")).toString().trimmed().toUpper();
    m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
    m_pendingIbkrQuotesCurrency = currency;
    m_pendingIbkrQuotesExchange = QStringLiteral("SMART");
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesProbeExchanges = probeExchanges;
    m_pendingIbkrQuotesConId = conId;
    m_pendingIbkrQuotesDays = ibkrMissingQuoteDays(m_pendingIbkrQuotesSymbol, 90);
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = supportsSmart;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_pendingIbkrQuotesSmartHistoricalRetry = false;

    if (probeExchanges.size() == 1) {
        const QString onlyExchange = probeExchanges.first().trimmed().toUpper();
        const QString quoteExchange = onlyExchange;
        if (!saveIbkrQuoteExchange(symbol.trimmed(), quoteExchange, 0.0, onlyExchange, 0.0)) {
            updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Quote-Boerse konnte nicht gespeichert werden"));
            setIbkrConnectionState(
                QStringLiteral("%1: Quote-Boerse fuer %2 konnte nicht gespeichert werden.")
                    .arg(m_ibkrGetStocksBatchName)
                    .arg(symbol),
                m_ibkrConnected,
                false);
            return false;
        }
        updateIbkrQuoteExchangeSuccess(symbol.trimmed());
        m_pendingIbkrQuotesExchange = quoteExchange;
        m_pendingIbkrQuotesPrimaryExchange.clear();
        if (m_ibkrGetStocksBatchActive) {
            const bool startWithSnapshot = false;
            setIbkrConnectionState(
                QStringLiteral("%1: %2 -> %3, beste Direktboerse %4. %5 ... OK: %6, Fehler: %7.")
                    .arg(m_ibkrGetStocksBatchName)
                    .arg(symbol, quoteExchange, onlyExchange)
                    .arg(startWithSnapshot
                             ? QStringLiteral("Live-Snapshot wird aktualisiert")
                             : QStringLiteral("Neue Quotes werden geladen"))
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            m_pendingIbkrProcessIsHistoricalQuotes = !startWithSnapshot;
            m_pendingIbkrProcessIsQuoteExchangeProbe = false;
            m_pendingIbkrProcessIsMarketSnapshot = startWithSnapshot;
            if (startWithSnapshot)
                startIbkrQuoteSnapshotFallback(
                    QStringLiteral("Quote fuer aktuellen Handelstag vorhanden, aktualisiere Live-Snapshot"));
            else
                startIbkrQuoteHelperRequest(false);
        }
        return true;
    }

    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrProcessIsQuoteExchangeProbe = true;
    m_pendingIbkrProcessIsMarketSnapshot = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrProcessIsNameCheck = false;
    m_ibkrDataLoading = true;
    startIbkrQuoteHelperRequest(true);
    return true;
}

void DatabaseManager::startIbkrQuotesRequestForIsin(const QString &isin, int days)
{
    if (m_ibkrDataLoading || m_ibkrBatchActive || m_ibkrNameCheckBatchActive)
        return;

    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Quote-Abruf")))
        return;
    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "Symbol", "IBKRConId", "IBKRResolvedSymbol", "LocalSymbol",
               "Currency", "CountryCode", "PrimaryExchange", "MIC", "ISIN",
               "ValidExchanges", "IBKRQuoteExchange", "IBKRBestDirectExchange"
        FROM "Stocks"
        WHERE "ISIN" = :isin
        ORDER BY "Symbol"
        LIMIT 1
    )SQL");
    stockQuery.bindValue(QStringLiteral(":isin"), isin.trimmed().toUpper());
    if (!stockQuery.exec() || !stockQuery.next()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Keine Aktie mit ISIN %1 gefunden.").arg(isin),
            m_ibkrConnected,
            false);
        return;
    }

    const QString symbol = stockQuery.value(QStringLiteral("Symbol")).toString().trimmed();
    const qint64 conId = stockQuery.value(QStringLiteral("IBKRConId")).toLongLong();
    QString ibkrSymbol = stockQuery.value(QStringLiteral("IBKRResolvedSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = stockQuery.value(QStringLiteral("LocalSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();

    QString currency = stockQuery.value(QStringLiteral("Currency")).toString().trimmed();
    if (currency.isEmpty())
        currency = currencyForCountry(stockQuery.value(QStringLiteral("CountryCode")).toString());

    const QString cachedQuoteExchange =
        stockQuery.value(QStringLiteral("IBKRQuoteExchange")).toString().trimmed().toUpper();
    const QString cachedPrimaryExchange =
        stockQuery.value(QStringLiteral("IBKRBestDirectExchange")).toString().trimmed().toUpper();
    const QString validExchanges = stockQuery.value(QStringLiteral("ValidExchanges")).toString();
    const bool supportsSmart = ibkrValidExchangesContainSmart(validExchanges);
    const QStringList probeExchanges = ibkrQuoteExchangeCandidates(
        validExchanges,
        stockQuery.value(QStringLiteral("PrimaryExchange")).toString(),
        stockQuery.value(QStringLiteral("MIC")).toString());
    const bool hasEuroProbeExchange = ibkrQuoteExchangesContainEuro(probeExchanges);
    const bool cachedExchangeIsEuro =
        ibkrIsEuroQuoteExchange(cachedQuoteExchange)
        || ibkrIsEuroQuoteExchange(cachedPrimaryExchange);
    const bool useCachedQuoteExchange =
        !cachedQuoteExchange.isEmpty() && (!hasEuroProbeExchange || cachedExchangeIsEuro);

    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        setIbkrConnectionState(QStringLiteral("Fehler: Der IBKR-Helfer fehlt im Build-Verzeichnis."), m_ibkrConnected, false);
        return;
    }

    m_ibkrSocket.abort();
    m_ibkrPendingSymbol = symbol;
    m_pendingIbkrQuotesSymbol = symbol;
    m_pendingIbkrQuotesIsin = isin.trimmed().toUpper();
    m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
    m_pendingIbkrQuotesCurrency = currency;
    m_pendingIbkrQuotesExchange = useCachedQuoteExchange ? cachedQuoteExchange : QStringLiteral("SMART");
    m_pendingIbkrQuotesPrimaryExchange = cachedPrimaryExchange;
    if (useCachedQuoteExchange
        && !cachedPrimaryExchange.isEmpty()
        && m_pendingIbkrQuotesExchange.compare(cachedPrimaryExchange, Qt::CaseInsensitive) != 0) {
        m_pendingIbkrQuotesExchange = cachedPrimaryExchange;
        m_pendingIbkrQuotesPrimaryExchange.clear();
        saveIbkrQuoteExchange(symbol, cachedPrimaryExchange, 0.0, cachedPrimaryExchange, 0.0);
    } else if (m_pendingIbkrQuotesExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0) {
        m_pendingIbkrQuotesPrimaryExchange.clear();
    }
    m_pendingIbkrQuotesProbeExchanges = ibkrQuoteFallbackExchanges(cachedPrimaryExchange, probeExchanges);
    m_pendingIbkrQuotesConId = conId;
    m_pendingIbkrQuotesDays = qMax(1, days);
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = supportsSmart;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_pendingIbkrQuotesSmartHistoricalRetry = false;
    const bool startWithSnapshot = !m_ibkrGetStocksBatchActive
                                   && ibkrHasQuoteForExpectedDate(m_pendingIbkrQuotesSymbol)
                                   && m_pendingIbkrQuotesExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0;
    m_pendingIbkrProcessIsHistoricalQuotes = !startWithSnapshot;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_pendingIbkrProcessIsMarketSnapshot = startWithSnapshot;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrProcessIsNameCheck = false;
    m_ibkrDataLoading = true;

    if (startWithSnapshot) {
        startIbkrQuoteSnapshotFallback(
            QStringLiteral("Quote fuer aktuellen Handelstag vorhanden, aktualisiere Live-Snapshot"));
    } else if (!useCachedQuoteExchange && !probeExchanges.isEmpty()) {
        m_pendingIbkrProcessIsHistoricalQuotes = false;
        m_pendingIbkrProcessIsQuoteExchangeProbe = true;
        m_pendingIbkrProcessIsMarketSnapshot = false;
        startIbkrQuoteHelperRequest(true);
    } else {
        startIbkrQuoteHelperRequest(false);
    }
}

void DatabaseManager::startIbkrQuoteHelperRequest(bool probeExchange)
{
    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
    .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        setIbkrConnectionState(QStringLiteral("Fehler: Der IBKR-Helfer fehlt im Build-Verzeichnis."), m_ibkrConnected, false);
        return;
    }

    const bool snapshotRequest = !probeExchange && m_pendingIbkrProcessIsMarketSnapshot;
    const QString requestSymbol = m_pendingIbkrQuotesSymbol.isEmpty()
                                      ? m_ibkrPendingSymbol
                                      : m_pendingIbkrQuotesSymbol;
    if (!refreshIbkrConnectionState(
            snapshotRequest
                ? QStringLiteral("IBKR-Snapshot")
                : (probeExchange ? QStringLiteral("IBKR-Boersenprobe") : QStringLiteral("IBKR-Quote-Abruf")))) {
        m_ibkrDataLoading = false;
        updateIbkrQuoteExchangeFailure(requestSymbol, QStringLiteral("Keine IBKR-API erreichbar"));
        if (m_ibkrGetStocksBatchActive) {
            ++m_ibkrGetStocksFailureCount;
            setIbkrConnectionState(
                QStringLiteral("%1: %2 keine IBKR-API erreichbar. OK: %3, Fehler: %4.")
                    .arg(m_ibkrGetStocksBatchName)
                    .arg(requestSymbol)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                false,
                false);
            scheduleNextIbkrGetStocksSymbol(1000);
        }
        m_pendingIbkrProcessIsHistoricalQuotes = false;
        m_pendingIbkrProcessIsQuoteExchangeProbe = false;
        m_pendingIbkrProcessIsMarketSnapshot = false;
        m_pendingIbkrQuotesSymbol.clear();
        m_pendingIbkrQuotesIsin.clear();
        m_pendingIbkrQuotesIbkrSymbol.clear();
        m_pendingIbkrQuotesCurrency.clear();
        m_pendingIbkrQuotesExchange.clear();
        m_pendingIbkrQuotesPrimaryExchange.clear();
        m_pendingIbkrQuotesProbeExchanges.clear();
        m_pendingIbkrQuotesConId = 0;
        m_pendingIbkrQuotesDays = 0;
        m_pendingIbkrQuotesFallbackIndex = 0;
        m_pendingIbkrQuotesSupportsSmart = false;
        m_pendingIbkrQuotesForceDirectProbeResult = false;
        m_pendingIbkrQuotesSmartHistoricalRetry = false;
        m_ibkrPendingSymbol.clear();
        emit ibkrConnectionChanged();
        return;
    }

    QStringList arguments = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
        QStringLiteral("--client-id"), probeExchange ? QStringLiteral("25") : (snapshotRequest ? QStringLiteral("26") : QStringLiteral("24")),
        QStringLiteral("--symbol"), m_pendingIbkrQuotesIbkrSymbol,
        probeExchange
            ? QStringLiteral("--probe-quote-exchanges")
            : (snapshotRequest ? QStringLiteral("--market-snapshot") : QStringLiteral("--historical-quotes")),
        QStringLiteral("--days"), QString::number(probeExchange ? 20 : m_pendingIbkrQuotesDays)
    };
    arguments << QStringLiteral("--timeout-seconds")
              << QString::number(snapshotRequest ? 5 : 45);
    if (m_pendingIbkrQuotesConId > 0)
        arguments << QStringLiteral("--con-id") << QString::number(m_pendingIbkrQuotesConId);
    if (!m_pendingIbkrQuotesCurrency.isEmpty())
        arguments << QStringLiteral("--currency") << m_pendingIbkrQuotesCurrency;
    if (probeExchange) {
        arguments << QStringLiteral("--exchanges")
        << m_pendingIbkrQuotesProbeExchanges.join(QLatin1Char(','));
    } else if (!m_pendingIbkrQuotesExchange.isEmpty()
               && m_pendingIbkrQuotesExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0) {
        arguments << QStringLiteral("--exchange") << m_pendingIbkrQuotesExchange
                  << QStringLiteral("--direct-exchange");
    }
    if (!probeExchange && !m_pendingIbkrQuotesPrimaryExchange.isEmpty())
        arguments << QStringLiteral("--primary-exchange") << m_pendingIbkrQuotesPrimaryExchange;
    if (m_pendingIbkrQuotesConId <= 0 && !m_pendingIbkrQuotesIsin.isEmpty())
        arguments << QStringLiteral("--isin") << m_pendingIbkrQuotesIsin;

    m_ibkrProcess.setProgram(helperPath);
    m_ibkrProcess.setArguments(arguments);
    m_ibkrProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrDataLoading = true;
    m_lastIbkrHelperElapsedMs = -1;
    m_ibkrHelperTimer.restart();
    setIbkrConnectionState(
        probeExchange
            ? QStringLiteral("%1: staerkste Umsatzboerse fuer %2 wird ermittelt ...")
                  .arg(m_ibkrGetStocksBatchName, m_pendingIbkrQuotesSymbol)
            : snapshotRequest
                  ? QStringLiteral("%1: Snapshot fuer %2 (%3) wird von %4 abgerufen ...")
                        .arg(m_ibkrGetStocksBatchName, m_pendingIbkrQuotesSymbol, m_pendingIbkrQuotesIsin,
                             m_pendingIbkrQuotesExchange)
                  : QStringLiteral("%1: Quotes fuer %2 (%3) werden fuer %4 Tage von %5 abgerufen ...")
                        .arg(m_ibkrGetStocksBatchName, m_pendingIbkrQuotesSymbol, m_pendingIbkrQuotesIsin)
                        .arg(m_pendingIbkrQuotesDays)
                        .arg(m_pendingIbkrQuotesExchange),
        true,
        false);
    m_ibkrProcess.start();
    m_ibkrDataTimeout.setInterval(snapshotRequest ? 12000 : 55000);
    m_ibkrDataTimeout.start();
    emit ibkrConnectionChanged();
}

void DatabaseManager::startIbkrSmartHistoricalRetry(const QString &symbol,
                                                    const QString &isin,
                                                    const QString &ibkrSymbol,
                                                    const QString &currency,
                                                    qint64 conId,
                                                    int days,
                                                    const QStringList &fallbackExchanges,
                                                    const QString &reason)
{
    m_ibkrPendingSymbol = symbol;
    m_pendingIbkrQuotesSymbol = symbol;
    m_pendingIbkrQuotesIsin = isin;
    m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
    m_pendingIbkrQuotesCurrency = currency;
    m_pendingIbkrQuotesExchange = QStringLiteral("SMART");
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesProbeExchanges = fallbackExchanges;
    m_pendingIbkrQuotesConId = conId;
    m_pendingIbkrQuotesDays = days <= 0 ? ibkrMissingQuoteDays(symbol, 90) : days;
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = true;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_pendingIbkrQuotesSmartHistoricalRetry = true;
    m_pendingIbkrProcessIsHistoricalQuotes = true;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_pendingIbkrProcessIsMarketSnapshot = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrProcessIsNameCheck = false;
    m_ibkrDataLoading = true;
    setIbkrConnectionState(
        QStringLiteral("%1: %2, retry per SMART Historical Data fuer %3 ...")
            .arg(m_ibkrGetStocksBatchName, reason, symbol),
        m_ibkrConnected,
        false);
    startIbkrQuoteHelperRequest(false);
}

void DatabaseManager::startIbkrQuoteSnapshotFallback(const QString &reason)
{
    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_pendingIbkrProcessIsMarketSnapshot = true;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrProcessIsNameCheck = false;
    if (m_pendingIbkrQuotesFallbackIndex <= 0)
        m_pendingIbkrQuotesFallbackIndex = 1;
    m_ibkrDataLoading = true;
    setIbkrConnectionState(
        QStringLiteral("%1: %2, Snapshot fuer %3 wird abgerufen ...")
            .arg(m_ibkrGetStocksBatchName, reason, m_pendingIbkrQuotesSymbol),
        m_ibkrConnected,
        false);
    startIbkrQuoteHelperRequest(false);
}

bool DatabaseManager::retryIbkrQuoteSnapshotSmartFallback(const QString &reason)
{
    if (!m_pendingIbkrProcessIsMarketSnapshot
        || m_pendingIbkrQuotesFallbackIndex != 1
        || m_pendingIbkrQuotesExchange.isEmpty()
        || m_pendingIbkrQuotesExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) == 0) {
        return false;
    }

    m_pendingIbkrQuotesExchange = QStringLiteral("SMART");
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesFallbackIndex = 2;
    m_ibkrDataLoading = true;
    setIbkrConnectionState(
        QStringLiteral("%1: Snapshot per Direktboerse fehlgeschlagen (%2), versuche SMART fuer %3 ...")
            .arg(m_ibkrGetStocksBatchName, reason, m_pendingIbkrQuotesSymbol),
        m_ibkrConnected,
        false);
    startIbkrQuoteHelperRequest(false);
    return true;
}

void DatabaseManager::startIbkrNameCheckBatch()
{
    if (m_ibkrNameCheckBatchActive || m_ibkrBatchActive || m_ibkrDataLoading)
        return;

    if (m_yahooFundamentalsBatchActive)
        stopYahooFundamentalsBatch();

    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Namenspruefung"))) {
        return;
    }
    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    m_ibkrNameCheckBatchSymbols.clear();
    const QStringList listSymbols = ibkrNameCheckIsinOverrides().keys();
    for (const QString &symbol : listSymbols) {
        const QString listIsin = ibkrNameCheckIsinOverride(symbol);
        QSqlQuery query(db);
        if (listIsin.isEmpty()) {
            query.prepare(R"SQL(
                SELECT 1
                FROM "Stocks"
                WHERE "Symbol" = :symbol
                LIMIT 1
            )SQL");
        } else {
            query.prepare(R"SQL(
                SELECT 1
                WHERE NOT EXISTS (
                    SELECT 1
                    FROM "Stocks"
                    WHERE "Symbol" = :symbol
                      AND "IBKRConId" IS NOT NULL
                )
                LIMIT 1
            )SQL");
        }
        query.bindValue(QStringLiteral(":symbol"), symbol);
        if (!query.exec()) {
            setIbkrConnectionState(
                QStringLiteral("Fehler: IBKR-Namenspruefung konnte %1 nicht pruefen: %2")
                    .arg(symbol, query.lastError().text()),
                m_ibkrConnected,
                false);
            return;
        }
        if (query.next())
            m_ibkrNameCheckBatchSymbols << symbol;
    }
    m_ibkrNameCheckBatchSymbols.sort(Qt::CaseInsensitive);

    if (m_ibkrNameCheckBatchSymbols.isEmpty()) {
        setIbkrConnectionState(QStringLiteral("IBKR-Namenspruefung: Keine offenen Datensaetze aus der ISIN-Liste gefunden."), m_ibkrConnected, false);
        return;
    }

    m_ibkrNameCheckBatchActive = true;
    m_ibkrNameCheckBatchIndex = 0;
    m_ibkrNameCheckBatchSuccessCount = 0;
    m_ibkrNameCheckBatchFailureCount = 0;
    setIbkrConnectionState(
        QStringLiteral("IBKR-Namenspruefung gestartet: %1 Datensaetze werden geprueft.")
            .arg(m_ibkrNameCheckBatchSymbols.size()),
        m_ibkrConnected,
        false);
    scheduleNextIbkrNameCheckBatchSymbol(100);
}

void DatabaseManager::stopIbkrNameCheckBatch()
{
    if (!m_ibkrNameCheckBatchActive)
        return;

    m_ibkrNameCheckBatchTimer.stop();
    if (m_ibkrDataLoading && m_pendingIbkrProcessIsNameCheck
        && m_ibkrProcess.state() != QProcess::NotRunning) {
        m_ibkrProcess.kill();
    }

    finishIbkrNameCheckBatch(
        QStringLiteral("IBKR-Namenspruefung gestoppt: %1/%2 verarbeitet, %3 OK, %4 Fehler/Pruefen.")
            .arg(ibkrNameCheckBatchDone())
            .arg(m_ibkrNameCheckBatchSymbols.size())
            .arg(m_ibkrNameCheckBatchSuccessCount)
            .arg(m_ibkrNameCheckBatchFailureCount));
}

bool DatabaseManager::startIbkrTradingApp(const QString &programPath)
{
    const QString trimmedPath = programPath.trimmed();
    if (trimmedPath.isEmpty()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Pfad zu TWS/IB Gateway ist leer."), m_ibkrConnected, false);
        return false;
    }

    const QFileInfo fileInfo(trimmedPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: TWS/IB Gateway wurde nicht gefunden: %1").arg(trimmedPath),
            m_ibkrConnected,
            false);
        return false;
    }

    const bool started = QProcess::startDetached(
        fileInfo.absoluteFilePath(),
        QStringList(),
        fileInfo.absolutePath());
    if (!started) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: TWS/IB Gateway konnte nicht gestartet werden: %1").arg(trimmedPath),
            m_ibkrConnected,
            false);
        return false;
    }

    setIbkrConnectionState(
        QStringLiteral("TWS/IB Gateway wurde gestartet. Warte auf Anmeldung und API-Verbindung ..."),
        m_ibkrConnected,
        false);
    return true;
}

bool DatabaseManager::startIbkrQuoteWorkerAll()
{
    if (!refreshIbkrConnectionState(QStringLiteral("IBKR Gesamtbatch")))
        return false;

    const QString projectDir = QStringLiteral("K:/QT-Projekte/ShareSelector");
    QString scriptPath = QDir(projectDir).filePath(QStringLiteral("scripts/run_ibkr_quote_job.py"));
    if (!QFileInfo::exists(scriptPath)) {
        const QString appDirScript = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("scripts/run_ibkr_quote_job.py"));
        if (QFileInfo::exists(appDirScript))
            scriptPath = appDirScript;
    }

    const QFileInfo scriptInfo(scriptPath);
    if (!scriptInfo.exists() || !scriptInfo.isFile()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: IBKR Quote Worker Script wurde nicht gefunden: %1").arg(scriptPath),
            m_ibkrConnected,
            false);
        return false;
    }

    QStringList arguments;
    arguments << QStringLiteral("/c")
              << QStringLiteral("start")
              << QStringLiteral("IBKR Gesamtbatch")
              << QStringLiteral("cmd.exe")
              << QStringLiteral("/k")
              << QStringLiteral("python")
              << scriptInfo.absoluteFilePath()
              << QStringLiteral("--all-ibkr")
              << QStringLiteral("--snapshot-timeout-seconds")
              << QStringLiteral("5")
              << QStringLiteral("--job-name")
              << QStringLiteral("IBKR Gesamtbatch");

    qint64 processId = 0;
    const bool started = QProcess::startDetached(
        QStringLiteral("cmd.exe"),
        arguments,
        scriptInfo.absolutePath(),
        &processId);
    if (!started) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Externer IBKR Quote Worker konnte nicht gestartet werden."),
            m_ibkrConnected,
            false);
        return false;
    }

    setIbkrConnectionState(
        QStringLiteral("Externer IBKR Quote Worker fuer alle IBKR-Stocks wurde gestartet. ShareSelector bleibt bedienbar."),
        m_ibkrConnected,
        false);
    return true;
}

void DatabaseManager::connectToIbkr()
{
    if (m_ibkrSocket.state() == QAbstractSocket::ConnectedState) {
        setIbkrConnectionState(
            QStringLiteral("IBKR TWS/IB Gateway ist auf 127.0.0.1:%1 erreichbar.")
                .arg(m_ibkrSocket.peerPort()),
            true,
            false);
        return;
    }

    m_ibkrConnectTimeout.stop();
    m_ibkrSocket.abort();
    m_ibkrPortIndex = 0;
    setIbkrConnectionState(QStringLiteral("IBKR-Verbindung wird geprüft ..."), false, true);
    tryNextIbkrPort();
}

void DatabaseManager::tryNextIbkrPort()
{
    if (!m_ibkrConnecting)
        return;

    if (m_ibkrPortIndex >= m_ibkrPorts.size()) {
        setIbkrConnectionState(
            QStringLiteral("Keine IBKR-API erreichbar. TWS oder IB Gateway starten und Socket Clients aktivieren."),
            false,
            false);
        return;
    }

    const quint16 port = m_ibkrPorts.at(m_ibkrPortIndex);
    m_ibkrSocket.connectToHost(QHostAddress::LocalHost, port);
    m_ibkrConnectTimeout.start();
}

bool DatabaseManager::probeIbkrConnection(int timeoutMs, quint16 *connectedPort)
{
    QList<quint16> portsToCheck;
    if (m_ibkrConnectedPort != 0)
        portsToCheck << m_ibkrConnectedPort;
    for (const quint16 port : m_ibkrPorts) {
        if (!portsToCheck.contains(port))
            portsToCheck << port;
    }

    for (const quint16 port : portsToCheck) {
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port);
        if (probe.waitForConnected(timeoutMs)) {
            probe.disconnectFromHost();
            if (probe.state() != QAbstractSocket::UnconnectedState)
                probe.waitForDisconnected(100);
            if (connectedPort)
                *connectedPort = port;
            return true;
        }
    }
    return false;
}

void DatabaseManager::pollIbkrConnectionState()
{
    if (m_ibkrConnecting || m_ibkrDataLoading)
        return;

    quint16 reachablePort = 0;
    if (probeIbkrConnection(150, &reachablePort)) {
        m_ibkrConnectedPort = reachablePort;
        if (!m_ibkrConnected) {
            setIbkrConnectionState(
                QStringLiteral("IBKR TWS/IB Gateway ist auf 127.0.0.1:%1 erreichbar.")
                    .arg(reachablePort),
                true,
                false);
        }
        return;
    }

    if (m_ibkrConnected) {
        m_ibkrConnectedPort = 0;
        setIbkrConnectionState(
            QStringLiteral("Die Verbindung zu IBKR TWS/IB Gateway ist nicht mehr erreichbar."),
            false,
            false);
    }
}

bool DatabaseManager::refreshIbkrConnectionState(const QString &action)
{
    quint16 reachablePort = 0;
    if (probeIbkrConnection(300, &reachablePort)) {
        m_ibkrConnectedPort = reachablePort;
        if (!m_ibkrConnected) {
            setIbkrConnectionState(
                QStringLiteral("IBKR TWS/IB Gateway ist auf 127.0.0.1:%1 erreichbar.")
                    .arg(reachablePort),
                true,
                false);
        }
        return true;
    }

    const QString prefix = action.trimmed().isEmpty()
                               ? QStringLiteral("IBKR-Aktion")
                               : action.trimmed();
    setIbkrConnectionState(
        QStringLiteral("Fehler: %1 nicht gestartet. Keine IBKR-API erreichbar. TWS oder IB Gateway starten und Socket Clients aktivieren.")
            .arg(prefix),
        false,
        false);
    m_ibkrConnectedPort = 0;
    return false;
}

void DatabaseManager::getIbkrData(const QString &symbol)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || m_ibkrDataLoading)
        return;

    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Datenabruf")))
        return;

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "ISIN", "Currency", "CountryCode", "MIC", "PrimaryExchange",
               "IBKRResolvedSymbol", "YahooSymbol", "Name"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!stockQuery.exec() || !stockQuery.next()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Die ausgewählte Aktie wurde nicht in der Datenbank gefunden."),
            m_ibkrConnected,
            false);
        return;
    }

    QString currency = stockQuery.value(QStringLiteral("Currency")).toString().trimmed();
    if (currency.isEmpty())
        currency = currencyForCountry(stockQuery.value(QStringLiteral("CountryCode")).toString());

    m_ibkrPendingSymbol = normalizedSymbol;
    m_pendingIbkrCurrency = currency;
    m_pendingIbkrExchange = stockQuery.value(QStringLiteral("PrimaryExchange")).toString().trimmed().isEmpty()
                                ? stockQuery.value(QStringLiteral("MIC")).toString().trimmed()
                                : stockQuery.value(QStringLiteral("PrimaryExchange")).toString().trimmed();
    m_pendingIbkrIsin = stockQuery.value(QStringLiteral("ISIN")).toString().trimmed();
    m_pendingIbkrNameSearchTerms = ibkrSymbolSearchKeywordVariants(
        stockQuery.value(QStringLiteral("Name")).toString());
    m_pendingIbkrNameSearchIndex = 0;
    m_pendingIbkrSearchKeywords = m_pendingIbkrNameSearchTerms.isEmpty()
                                      ? QString()
                                      : m_pendingIbkrNameSearchTerms.first();
    m_pendingIbkrLastError.clear();
    m_pendingIbkrCandidateSymbols.clear();
    m_pendingIbkrCandidateCurrencies.clear();
    m_pendingIbkrCandidateExchanges.clear();
    m_pendingIbkrCurrentCandidateSymbol.clear();
    m_pendingIbkrAmbiguousIsinCandidates.clear();
    m_pendingIbkrTriedAmbiguousIsins.clear();
    m_pendingIbkrCandidateIndex = 0;
    m_pendingIbkrSearchStarted = false;
    m_pendingIbkrNameSearchStarted = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrProcessIsHistoricalQuotes = false;
    if (!m_ibkrGetStocksBatchActive)
        m_pendingIbkrQuoteAfterMetadata = false;
    m_pendingIbkrReviewRequired = false;
    m_pendingIbkrReviewReason.clear();
    m_pendingIbkrTryWithoutIsin = false;
    m_pendingIbkrDirectExchange = false;
    m_pendingIbkrDirectExchanges = ibkrDirectExchanges(m_pendingIbkrExchange);
    m_pendingIbkrDirectExchangeIndex = 0;
    m_pendingIbkrCurrentDirectExchange.clear();
    m_pendingIbkrDirectExchange = false;

    appendIbkrSymbolVariants(m_pendingIbkrCandidateSymbols,
                             stockQuery.value(QStringLiteral("IBKRResolvedSymbol")).toString());
    appendIbkrSymbolVariants(m_pendingIbkrCandidateSymbols,
                             stockQuery.value(QStringLiteral("YahooSymbol")).toString());
    appendIbkrSymbolVariants(m_pendingIbkrCandidateSymbols, normalizedSymbol);
    if (m_ibkrBatchActive) {
        QSqlQuery attemptQuery(db);
        attemptQuery.prepare(R"SQL(
            UPDATE "Stocks"
            SET "IBKRLastAttemptAt" = CURRENT_TIMESTAMP
            WHERE "Symbol" = :symbol
        )SQL");
        attemptQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        if (!attemptQuery.exec())
            qWarning() << "IBKR-Batch-Versuch konnte nicht protokolliert werden:"
                       << attemptQuery.lastError().text() << normalizedSymbol;
    }
    m_ibkrSocket.abort();
    setIbkrConnectionState(
        QStringLiteral("IBKR-Daten für %1 werden abgerufen ...").arg(normalizedSymbol),
        true,
        false);

    tryNextIbkrCandidate();
}

void DatabaseManager::startIbkrHelperRequest(const QString &candidateSymbol)
{
    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
    .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        finalizeIbkrDataFailure(QStringLiteral("Der IBKR-Helfer fehlt im Build-Verzeichnis."));
        return;
    }

    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Stammdatenabruf"))) {
        finalizeIbkrDataFailure(QStringLiteral("Keine IBKR-API erreichbar"));
        return;
    }

    m_ibkrDataLoading = true;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrCurrentCandidateSymbol = candidateSymbol.trimmed();
    QStringList arguments = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
        QStringLiteral("--client-id"), QStringLiteral("23"),
        QStringLiteral("--symbol"), candidateSymbol,
        QStringLiteral("--timeout-seconds"), QStringLiteral("30")
    };
    const QString candidateKey = ibkrCandidateKey(candidateSymbol);
    const QString hintedCurrency =
        m_pendingIbkrCandidateCurrencies.value(candidateKey).trimmed();
    const QString hintedExchange =
        m_pendingIbkrCandidateExchanges.value(candidateKey).trimmed();
    const QString currency = hintedCurrency.isEmpty()
                                 ? m_pendingIbkrCurrency
                                 : hintedCurrency;
    if (!currency.isEmpty())
        arguments << QStringLiteral("--currency") << currency;
    const bool useHintedDirectExchange =
        !hintedExchange.isEmpty() && m_pendingIbkrDataForNameCheckRecovery;
    const QString exchange = m_pendingIbkrDirectExchange
                                 ? m_pendingIbkrCurrentDirectExchange
                                 : (hintedExchange.isEmpty() ? m_pendingIbkrExchange : hintedExchange);
    if (!exchange.isEmpty())
        arguments << QStringLiteral("--exchange") << exchange;
    if ((m_pendingIbkrDirectExchange || useHintedDirectExchange) && !exchange.isEmpty())
        arguments << QStringLiteral("--direct-exchange");
    if (!m_pendingIbkrTryWithoutIsin && !m_pendingIbkrIsin.isEmpty())
        arguments << QStringLiteral("--isin") << m_pendingIbkrIsin;

    m_ibkrProcess.setProgram(helperPath);
    m_ibkrProcess.setArguments(arguments);
    m_ibkrProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrProcess.start();
    m_ibkrDataTimeout.setInterval(40000);
    m_ibkrDataTimeout.start();
    emit ibkrConnectionChanged();
}

bool DatabaseManager::tryNextIbkrAmbiguousIsin()
{
    while (!m_pendingIbkrAmbiguousIsinCandidates.isEmpty()) {
        const QString isin = m_pendingIbkrAmbiguousIsinCandidates.takeFirst().trimmed().toUpper();
        if (isin.isEmpty())
            continue;
        if (m_pendingIbkrCurrentCandidateSymbol.trimmed().isEmpty())
            return false;

        m_pendingIbkrIsin = isin;
        m_pendingIbkrTriedAmbiguousIsins << isin;
        m_pendingIbkrTryWithoutIsin = false;
        setIbkrConnectionState(
            QStringLiteral("IBKR: %1 wird mit freier ISIN %2 aus mehrdeutigem Treffer geprueft ...")
                .arg(m_ibkrPendingSymbol, isin),
            true,
            false);
        startIbkrHelperRequest(m_pendingIbkrCurrentCandidateSymbol);
        return true;
    }
    return false;
}

void DatabaseManager::startIbkrNameSearchRequest()
{
    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
    .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        finalizeIbkrDataFailure(QStringLiteral("Der IBKR-Helfer fehlt im Build-Verzeichnis."));
        return;
    }

    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Namenssuche"))) {
        finalizeIbkrDataFailure(QStringLiteral("Keine IBKR-API erreichbar"));
        return;
    }

    m_ibkrDataLoading = true;
    m_pendingIbkrProcessIsNameSearch = true;
    QStringList arguments = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
        QStringLiteral("--client-id"), QStringLiteral("23"),
        QStringLiteral("--symbol"), m_pendingIbkrSearchKeywords,
        QStringLiteral("--match-symbols"),
        QStringLiteral("--timeout-seconds"), QStringLiteral("20")
    };

    m_ibkrProcess.setProgram(helperPath);
    m_ibkrProcess.setArguments(arguments);
    m_ibkrProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrProcess.start();
    m_ibkrDataTimeout.setInterval(30000);
    m_ibkrDataTimeout.start();
    emit ibkrConnectionChanged();
}

bool DatabaseManager::tryNextIbkrCandidate(const QString &lastError)
{
    if (!lastError.trimmed().isEmpty())
        m_pendingIbkrLastError = lastError.trimmed();

    while (m_pendingIbkrCandidateIndex < m_pendingIbkrCandidateSymbols.size()) {
        const QString candidate = m_pendingIbkrCandidateSymbols.at(m_pendingIbkrCandidateIndex++).trimmed();
        if (candidate.isEmpty())
            continue;

        setIbkrConnectionState(
            QStringLiteral("IBKR-Daten fuer %1 werden mit Symbol %2%3%4 abgerufen ...")
                .arg(m_ibkrPendingSymbol,
                     candidate,
                     m_pendingIbkrTryWithoutIsin ? QStringLiteral(" ohne ISIN") : QString(),
                     m_pendingIbkrDirectExchange ? QStringLiteral(" direkt an FWB") : QString()),
            true,
            false);
        startIbkrHelperRequest(candidate);
        return true;
    }

    if (m_pendingIbkrIsin.isEmpty()
        && m_pendingIbkrNameSearchIndex < m_pendingIbkrNameSearchTerms.size()) {
        m_pendingIbkrSearchKeywords =
            m_pendingIbkrNameSearchTerms.at(m_pendingIbkrNameSearchIndex++).trimmed();
        m_pendingIbkrNameSearchStarted = true;
        setIbkrConnectionState(
            QStringLiteral("IBKR-Namenssuche fuer %1 mit \"%2\" laeuft ...")
                .arg(m_ibkrPendingSymbol, m_pendingIbkrSearchKeywords),
            true,
            false);
        startIbkrNameSearchRequest();
        return true;
    }

    if (!m_pendingIbkrSearchStarted && !m_pendingIbkrSearchKeywords.isEmpty()) {
        m_pendingIbkrSearchStarted = true;
        setIbkrConnectionState(
            QStringLiteral("IBKR-Symbolsuche fuer %1 ueber Yahoo mit \"%2\" laeuft ...")
                .arg(m_ibkrPendingSymbol, m_pendingIbkrSearchKeywords),
            true,
            false);
        yahooFinanceClient.resolveSymbol(m_ibkrPendingSymbol, m_pendingIbkrSearchKeywords);
        return true;
    }

    if (m_pendingIbkrDirectExchangeIndex < m_pendingIbkrDirectExchanges.size()) {
        m_pendingIbkrDirectExchange = true;
        m_pendingIbkrCurrentDirectExchange =
            m_pendingIbkrDirectExchanges.at(m_pendingIbkrDirectExchangeIndex++);
        m_pendingIbkrCandidateIndex = 0;
        setIbkrConnectionState(
            QStringLiteral("IBKR: %1 wird direkt an %2 getestet.")
                .arg(m_ibkrPendingSymbol, m_pendingIbkrCurrentDirectExchange),
            true,
            false);
        return tryNextIbkrCandidate();
    }

    if (!m_pendingIbkrTryWithoutIsin && !m_pendingIbkrIsin.isEmpty()) {
        m_pendingIbkrTryWithoutIsin = true;
        m_pendingIbkrDirectExchange = false;
        m_pendingIbkrDirectExchangeIndex = 0;
        m_pendingIbkrCurrentDirectExchange.clear();
        m_pendingIbkrCandidateIndex = 0;
        setIbkrConnectionState(
            QStringLiteral("IBKR: %1 wird erneut ohne ISIN getestet, weil die ISIN keinen Treffer geliefert hat.")
                .arg(m_ibkrPendingSymbol),
            true,
            false);
        return tryNextIbkrCandidate();
    }

    return false;
}

void DatabaseManager::finalizeIbkrDataFailure(const QString &message)
{
    const QString requestedSymbol = m_ibkrPendingSymbol;
    const QString finalMessage = message.trimmed().isEmpty()
                                     ? (m_pendingIbkrLastError.isEmpty()
                                            ? QStringLiteral("kein passender IBKR-Kontrakt gefunden")
                                            : m_pendingIbkrLastError)
                                     : message.trimmed();
    const bool quoteAfterMetadata = m_pendingIbkrQuoteAfterMetadata;

    m_ibkrDataLoading = false;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrCurrency.clear();
    m_pendingIbkrExchange.clear();
    m_pendingIbkrIsin.clear();
    m_pendingIbkrSearchKeywords.clear();
    m_pendingIbkrNameSearchTerms.clear();
    m_pendingIbkrNameSearchIndex = 0;
    m_pendingIbkrLastError.clear();
    m_pendingIbkrCandidateSymbols.clear();
    m_pendingIbkrCandidateCurrencies.clear();
    m_pendingIbkrCandidateExchanges.clear();
    m_pendingIbkrCurrentCandidateSymbol.clear();
    m_pendingIbkrAmbiguousIsinCandidates.clear();
    m_pendingIbkrTriedAmbiguousIsins.clear();
    m_pendingIbkrCandidateIndex = 0;
    m_pendingIbkrSearchStarted = false;
    m_pendingIbkrNameSearchStarted = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrQuoteAfterMetadata = false;
    m_pendingIbkrReviewRequired = false;
    m_pendingIbkrReviewReason.clear();
    m_pendingIbkrTryWithoutIsin = false;
    m_pendingIbkrDirectExchange = false;
    m_pendingIbkrDirectExchanges.clear();
    m_pendingIbkrDirectExchangeIndex = 0;
    m_pendingIbkrCurrentDirectExchange.clear();

    if (m_pendingIbkrDataForNameCheckRecovery) {
        m_pendingIbkrDataForNameCheckRecovery = false;
        if (m_ibkrNameCheckBatchActive) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(requestedSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenssuche fehlgeschlagen: %1").arg(finalMessage));
            setIbkrConnectionState(
                QStringLiteral("IBKR-Namenspruefung: %1 fehlgeschlagen (%2). OK: %3, Fehler/Pruefen: %4.")
                    .arg(requestedSymbol)
                    .arg(finalMessage)
                    .arg(m_ibkrNameCheckBatchSuccessCount)
                    .arg(m_ibkrNameCheckBatchFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrNameCheckBatchSymbol(1000);
            return;
        }
    }

    if (quoteAfterMetadata && m_ibkrGetStocksBatchActive) {
        ++m_ibkrGetStocksFailureCount;
        updateIbkrBatchFailure(requestedSymbol, finalMessage);
        setIbkrConnectionState(
            QStringLiteral("%1: %2 Stammdaten fehlgeschlagen (%3). OK: %4, Fehler: %5.")
                .arg(m_ibkrGetStocksBatchName)
                .arg(requestedSymbol)
                .arg(finalMessage)
                .arg(m_ibkrGetStocksSuccessCount)
                .arg(m_ibkrGetStocksFailureCount),
            m_ibkrConnected,
            false);
        scheduleNextIbkrGetStocksSymbol(1000);
        return;
    }

    if (m_ibkrBatchActive) {
        ++m_ibkrBatchFailureCount;
        updateIbkrBatchFailure(requestedSymbol, finalMessage);
        setIbkrConnectionState(
            QStringLiteral("IBKR-Batch: %1 fehlgeschlagen (%2). Erfolgreich: %3, Fehler: %4.")
                .arg(requestedSymbol)
                .arg(finalMessage)
                .arg(m_ibkrBatchSuccessCount)
                .arg(m_ibkrBatchFailureCount),
            m_ibkrConnected,
            false);
        scheduleNextIbkrBatchSymbol(1000);
        return;
    }

    setIbkrConnectionState(QStringLiteral("Fehler: %1").arg(finalMessage), m_ibkrConnected, false);
}

void DatabaseManager::finishIbkrQuotesRequest(const QJsonObject &result)
{
    const QString symbol = m_pendingIbkrQuotesSymbol;
    const QString isin = m_pendingIbkrQuotesIsin;
    const QString message = result.value(QStringLiteral("message")).toString();
    const QString apiDurationText = m_lastIbkrHelperElapsedMs >= 0
                                        ? QStringLiteral("%1 s").arg(m_lastIbkrHelperElapsedMs / 1000.0, 0, 'f', 2)
                                        : QStringLiteral("-");
    const bool getStocksBatchActive = m_ibkrGetStocksBatchActive;
    const QString ibkrSymbol = m_pendingIbkrQuotesIbkrSymbol;
    const QString currency = m_pendingIbkrQuotesCurrency;
    const QString quoteExchange = m_pendingIbkrQuotesExchange;
    const QStringList fallbackExchanges = m_pendingIbkrQuotesProbeExchanges;
    const qint64 conId = m_pendingIbkrQuotesConId;
    const int days = m_pendingIbkrQuotesDays;
    const bool supportsSmart = m_pendingIbkrQuotesSupportsSmart;
    const bool forceDirectProbeResult = m_pendingIbkrQuotesForceDirectProbeResult;
    const bool smartHistoricalRetry = m_pendingIbkrQuotesSmartHistoricalRetry;
    const bool historicalOnlyBatch = m_ibkrGetStocksHistoricalOnlyBatch;

    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_pendingIbkrProcessIsMarketSnapshot = false;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrQuotesSymbol.clear();
    m_pendingIbkrQuotesIsin.clear();
    m_pendingIbkrQuotesIbkrSymbol.clear();
    m_pendingIbkrQuotesCurrency.clear();
    m_pendingIbkrQuotesExchange.clear();
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesProbeExchanges.clear();
    m_pendingIbkrQuotesConId = 0;
    m_pendingIbkrQuotesDays = 0;
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = false;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_pendingIbkrQuotesSmartHistoricalRetry = false;
    m_ibkrDataTimeout.setInterval(25000);

    if (!result.value(QStringLiteral("success")).toBool()) {
        const QString finalError = message.trimmed().isEmpty()
        ? QStringLiteral("IBKR lieferte keine Quotes")
        : message;
        if (!historicalOnlyBatch
            && !quoteExchange.isEmpty()
            && quoteExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0) {
            m_ibkrPendingSymbol = symbol;
            m_pendingIbkrQuotesSymbol = symbol;
            m_pendingIbkrQuotesIsin = isin;
            m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
            m_pendingIbkrQuotesCurrency = currency;
            m_pendingIbkrQuotesExchange = quoteExchange;
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrQuotesProbeExchanges = fallbackExchanges;
            m_pendingIbkrQuotesConId = conId;
            m_pendingIbkrQuotesDays = days <= 0 ? ibkrMissingQuoteDays(symbol, 90) : days;
            m_pendingIbkrQuotesFallbackIndex = 0;
            m_pendingIbkrQuotesSupportsSmart = supportsSmart;
            m_pendingIbkrQuotesForceDirectProbeResult = forceDirectProbeResult;
            m_pendingIbkrQuotesSmartHistoricalRetry = smartHistoricalRetry;
            startIbkrQuoteSnapshotFallback(
                QStringLiteral("Historical Data fehlgeschlagen (%1), versuche Live-Snapshot").arg(finalError));
            return;
        }
        if (!historicalOnlyBatch
            && !smartHistoricalRetry
            && supportsSmart
            && quoteExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0) {
            startIbkrSmartHistoricalRetry(
                symbol,
                isin,
                ibkrSymbol,
                currency,
                conId,
                days,
                fallbackExchanges,
                QStringLiteral("%1 fehlgeschlagen (%2)").arg(quoteExchange, finalError));
            return;
        }
        if (!historicalOnlyBatch && !forceDirectProbeResult && !fallbackExchanges.isEmpty()) {
            m_ibkrPendingSymbol = symbol;
            m_pendingIbkrQuotesSymbol = symbol;
            m_pendingIbkrQuotesIsin = isin;
            m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
            m_pendingIbkrQuotesCurrency = currency;
            m_pendingIbkrQuotesExchange = quoteExchange;
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrQuotesProbeExchanges = fallbackExchanges;
            m_pendingIbkrQuotesConId = conId;
            m_pendingIbkrQuotesDays = days <= 0 ? ibkrMissingQuoteDays(symbol, 90) : days;
            m_pendingIbkrQuotesFallbackIndex = 0;
            m_pendingIbkrQuotesSupportsSmart = supportsSmart;
            m_pendingIbkrQuotesForceDirectProbeResult = true;
            m_pendingIbkrQuotesSmartHistoricalRetry = smartHistoricalRetry;
            m_pendingIbkrProcessIsHistoricalQuotes = false;
            m_pendingIbkrProcessIsQuoteExchangeProbe = true;
            m_pendingIbkrProcessIsMarketSnapshot = false;
            m_ibkrDataLoading = true;
            setIbkrConnectionState(
                QStringLiteral("%1: %2 %3 fehlgeschlagen, pruefe direkte Boersen nach Umsatz ...")
                    .arg(m_ibkrGetStocksBatchName, symbol, quoteExchange.isEmpty() ? QStringLiteral("Quote-Abruf") : quoteExchange),
                m_ibkrConnected,
                false);
            startIbkrQuoteHelperRequest(true);
            return;
        }
        if (!historicalOnlyBatch) {
            m_ibkrPendingSymbol = symbol;
            m_pendingIbkrQuotesSymbol = symbol;
            m_pendingIbkrQuotesIsin = isin;
            m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
            m_pendingIbkrQuotesCurrency = currency;
            m_pendingIbkrQuotesExchange = smartHistoricalRetry ? QStringLiteral("SMART") : quoteExchange;
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrQuotesProbeExchanges = fallbackExchanges;
            m_pendingIbkrQuotesConId = conId;
            m_pendingIbkrQuotesDays = days <= 0 ? ibkrMissingQuoteDays(symbol, 90) : days;
            m_pendingIbkrQuotesFallbackIndex = 0;
            m_pendingIbkrQuotesSupportsSmart = supportsSmart;
            m_pendingIbkrQuotesForceDirectProbeResult = forceDirectProbeResult;
            m_pendingIbkrQuotesSmartHistoricalRetry = smartHistoricalRetry;
            startIbkrQuoteSnapshotFallback(
                QStringLiteral("Historical Data fehlgeschlagen (%1)").arg(finalError));
            return;
        }
        if (getStocksBatchActive) {
            ++m_ibkrGetStocksFailureCount;
            updateIbkrQuoteExchangeFailure(
                symbol,
                finalError);
            setIbkrConnectionState(
                QStringLiteral("%1: %2 Quote-Abruf fehlgeschlagen (%3). API: %4. OK: %5, Fehler: %6.")
                    .arg(m_ibkrGetStocksBatchName,
                         symbol,
                         finalError)
                    .arg(apiDurationText)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrGetStocksSymbol(200);
            emit ibkrConnectionChanged();
            return;
        }
        setIbkrConnectionState(
            QStringLiteral("Fehler: %1").arg(message.trimmed().isEmpty()
                                                 ? QStringLiteral("IBKR lieferte keine Quotes.")
                                                 : message),
            m_ibkrConnected,
            false);
        emit ibkrConnectionChanged();
        return;
    }

    const QJsonArray bars = result.value(QStringLiteral("data")).toArray();
    int changedQuoteCount = 0;
    QDate latestQuoteDate;
    if (!saveIbkrHistoricalQuotes(symbol, bars, &changedQuoteCount, &latestQuoteDate)) {
        if (getStocksBatchActive) {
            ++m_ibkrGetStocksFailureCount;
            updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("IBKR-Quotes konnten nicht gespeichert werden"));
            setIbkrConnectionState(
                QStringLiteral("%1: Quotes fuer %2 konnten nicht gespeichert werden. OK: %3, Fehler: %4.")
                    .arg(m_ibkrGetStocksBatchName)
                    .arg(symbol)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrGetStocksSymbol(200);
            emit ibkrConnectionChanged();
            return;
        }
        setIbkrConnectionState(
            QStringLiteral("Fehler: IBKR-Quotes fuer %1 konnten nicht gespeichert werden.").arg(symbol),
            m_ibkrConnected,
            false);
        emit ibkrConnectionChanged();
        return;
    }

    const QDate expectedQuoteDate = mostRecentWeekday(QDate::currentDate());
    if (!historicalOnlyBatch && latestQuoteDate.isValid() && latestQuoteDate < expectedQuoteDate) {
        const QString staleMessage =
            QStringLiteral("IBKR lieferte fuer %1 nur Quotes bis %2, erwartet mindestens %3.")
                .arg(symbol,
                     latestQuoteDate.toString(QStringLiteral("yyyy-MM-dd")),
                     expectedQuoteDate.toString(QStringLiteral("yyyy-MM-dd")));
        m_ibkrPendingSymbol = symbol;
        m_pendingIbkrQuotesSymbol = symbol;
        m_pendingIbkrQuotesIsin = isin;
        m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
        m_pendingIbkrQuotesCurrency = currency;
        m_pendingIbkrQuotesExchange = quoteExchange;
        m_pendingIbkrQuotesPrimaryExchange.clear();
        m_pendingIbkrQuotesProbeExchanges = fallbackExchanges;
        m_pendingIbkrQuotesConId = conId;
        m_pendingIbkrQuotesDays = days;
        m_pendingIbkrQuotesFallbackIndex = 0;
        m_pendingIbkrQuotesSupportsSmart = supportsSmart;
        m_pendingIbkrQuotesForceDirectProbeResult = forceDirectProbeResult;
        m_pendingIbkrQuotesSmartHistoricalRetry = smartHistoricalRetry;
        startIbkrQuoteSnapshotFallback(staleMessage);
        return;
    }

    if (!historicalOnlyBatch
        && !getStocksBatchActive
        && latestQuoteDate.isValid()
        && latestQuoteDate >= expectedQuoteDate
        && expectedQuoteDate == QDate::currentDate()
        && !quoteExchange.isEmpty()
        && quoteExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0) {
        m_ibkrPendingSymbol = symbol;
        m_pendingIbkrQuotesSymbol = symbol;
        m_pendingIbkrQuotesIsin = isin;
        m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
        m_pendingIbkrQuotesCurrency = currency;
        m_pendingIbkrQuotesExchange = quoteExchange;
        m_pendingIbkrQuotesPrimaryExchange.clear();
        m_pendingIbkrQuotesProbeExchanges = fallbackExchanges;
        m_pendingIbkrQuotesConId = conId;
        m_pendingIbkrQuotesDays = days;
        m_pendingIbkrQuotesFallbackIndex = 0;
        m_pendingIbkrQuotesSupportsSmart = supportsSmart;
        m_pendingIbkrQuotesForceDirectProbeResult = forceDirectProbeResult;
        m_pendingIbkrQuotesSmartHistoricalRetry = smartHistoricalRetry;
        startIbkrQuoteSnapshotFallback(
            QStringLiteral("Historical Data bis %1 gespeichert, aktualisiere heutigen Live-Snapshot")
                .arg(latestQuoteDate.toString(QStringLiteral("yyyy-MM-dd"))));
        return;
    }

    if (smartHistoricalRetry)
        saveIbkrQuoteExchange(symbol, QStringLiteral("SMART"), 0.0);
    updateIbkrQuoteExchangeSuccess(symbol);

    setIbkrConnectionState(
        QStringLiteral("%1: %2 empfangen, %3 neu/geaendert fuer %4 (%5) gespeichert.")
            .arg(m_ibkrGetStocksBatchName)
            .arg(bars.size())
            .arg(changedQuoteCount)
            .arg(symbol, isin),
        m_ibkrConnected,
        false);
    emit saveComplete(symbol);
    emit ibkrStockDataUpdated(symbol);
    if (getStocksBatchActive) {
        ++m_ibkrGetStocksSuccessCount;
        m_ibkrGetStocksChangedQuoteCount += changedQuoteCount;
        setIbkrConnectionState(
            QStringLiteral("%1: %2 Quotes fuer %3 empfangen, %4 neu/geaendert. API: %5. OK: %6, neu/geaendert gesamt: %7, Fehler: %8.")
                .arg(m_ibkrGetStocksBatchName)
                .arg(bars.size())
                .arg(symbol)
                .arg(changedQuoteCount)
                .arg(apiDurationText)
                .arg(m_ibkrGetStocksSuccessCount)
                .arg(m_ibkrGetStocksChangedQuoteCount)
                .arg(m_ibkrGetStocksFailureCount),
            m_ibkrConnected,
            false);
        scheduleNextIbkrGetStocksSymbol(200);
    }
    emit ibkrConnectionChanged();
}

void DatabaseManager::finishIbkrQuoteSnapshotRequest(const QJsonObject &result)
{
    const QString symbol = m_pendingIbkrQuotesSymbol.isEmpty()
    ? m_ibkrPendingSymbol
    : m_pendingIbkrQuotesSymbol;
    const QString isin = m_pendingIbkrQuotesIsin;
    const QString message = result.value(QStringLiteral("message")).toString();
    const QString quoteExchange = m_pendingIbkrQuotesExchange;
    const bool smartHistoricalRetry = m_pendingIbkrQuotesSmartHistoricalRetry;
    const bool getStocksBatchActive = m_ibkrGetStocksBatchActive;
    const QString apiDurationText = m_lastIbkrHelperElapsedMs >= 0
                                        ? QStringLiteral("%1 s").arg(m_lastIbkrHelperElapsedMs / 1000.0, 0, 'f', 2)
                                        : QStringLiteral("-");

    const auto resetPendingIbkrQuoteRequest = [this]() {
        m_pendingIbkrProcessIsHistoricalQuotes = false;
        m_pendingIbkrProcessIsQuoteExchangeProbe = false;
        m_pendingIbkrProcessIsMarketSnapshot = false;
        m_ibkrPendingSymbol.clear();
        m_pendingIbkrQuotesSymbol.clear();
        m_pendingIbkrQuotesIsin.clear();
        m_pendingIbkrQuotesIbkrSymbol.clear();
        m_pendingIbkrQuotesCurrency.clear();
        m_pendingIbkrQuotesExchange.clear();
        m_pendingIbkrQuotesPrimaryExchange.clear();
        m_pendingIbkrQuotesProbeExchanges.clear();
        m_pendingIbkrQuotesConId = 0;
        m_pendingIbkrQuotesDays = 0;
        m_pendingIbkrQuotesFallbackIndex = 0;
        m_pendingIbkrQuotesSupportsSmart = false;
        m_pendingIbkrQuotesForceDirectProbeResult = false;
        m_pendingIbkrQuotesSmartHistoricalRetry = false;
        m_ibkrDataTimeout.setInterval(25000);
    };

    const QJsonObject data = result.value(QStringLiteral("data")).toObject();
    double snapshotPrice = 0.0;
    if (!result.value(QStringLiteral("success")).toBool()
        || !saveIbkrQuoteSnapshot(symbol, data, &snapshotPrice)) {
        const QString finalError = message.trimmed().isEmpty()
        ? QStringLiteral("IBKR-Snapshot lieferte keinen verwertbaren Kurs")
        : message;
        if (retryIbkrQuoteSnapshotSmartFallback(finalError))
            return;
        resetPendingIbkrQuoteRequest();
        updateIbkrQuoteExchangeFailure(symbol, finalError);
        if (getStocksBatchActive) {
            ++m_ibkrGetStocksFailureCount;
            setIbkrConnectionState(
                QStringLiteral("%1: Snapshot fuer %2 fehlgeschlagen (%3). API: %4. OK: %5, Fehler: %6.")
                    .arg(m_ibkrGetStocksBatchName, symbol, finalError, apiDurationText)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            emit ibkrStockDataUpdated(symbol);
            scheduleNextIbkrGetStocksSymbol(200);
            emit ibkrConnectionChanged();
            return;
        }
        setIbkrConnectionState(QStringLiteral("Fehler: %1").arg(finalError), m_ibkrConnected, false);
        emit ibkrStockDataUpdated(symbol);
        emit ibkrConnectionChanged();
        return;
    }

    resetPendingIbkrQuoteRequest();
    if (smartHistoricalRetry
        && quoteExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) == 0) {
        saveIbkrQuoteExchange(symbol, QStringLiteral("SMART"), 0.0);
    }
    updateIbkrQuoteExchangeSuccess(symbol);
    setIbkrConnectionState(
        QStringLiteral("%1: Snapshot fuer %2 (%3) mit %4 als heutiger Quote gespeichert.")
            .arg(m_ibkrGetStocksBatchName, symbol, isin)
            .arg(snapshotPrice, 0, 'f', 2),
        m_ibkrConnected,
        false);
    emit saveComplete(symbol);
    emit ibkrStockDataUpdated(symbol);
    if (getStocksBatchActive) {
        ++m_ibkrGetStocksSuccessCount;
        ++m_ibkrGetStocksChangedQuoteCount;
        setIbkrConnectionState(
            QStringLiteral("%1: Snapshot fuer %2 gespeichert (%3). API: %4. OK: %5, neu/geaendert gesamt: %6, Fehler: %7.")
                .arg(m_ibkrGetStocksBatchName, symbol)
                .arg(snapshotPrice, 0, 'f', 2)
                .arg(apiDurationText)
                .arg(m_ibkrGetStocksSuccessCount)
                .arg(m_ibkrGetStocksChangedQuoteCount)
                .arg(m_ibkrGetStocksFailureCount),
            m_ibkrConnected,
            false);
        scheduleNextIbkrGetStocksSymbol(200);
    }
    emit ibkrConnectionChanged();
}

void DatabaseManager::finishIbkrQuoteExchangeProbe(const QJsonObject &result)
{
    const QString message = result.value(QStringLiteral("message")).toString();
    const bool forceDirectProbeResult = m_pendingIbkrQuotesForceDirectProbeResult;
    if (!result.value(QStringLiteral("success")).toBool()) {
        m_pendingIbkrProcessIsQuoteExchangeProbe = false;
        m_ibkrDataTimeout.setInterval(25000);
        const QString fallbackExchange = m_pendingIbkrQuotesProbeExchanges.isEmpty()
                                             ? QString()
                                             : m_pendingIbkrQuotesProbeExchanges.first().trimmed().toUpper();
        const QString quoteExchange = fallbackExchange;
        if (!forceDirectProbeResult
            && !fallbackExchange.isEmpty()
            && saveIbkrQuoteExchange(m_pendingIbkrQuotesSymbol,
                                     quoteExchange,
                                     0.0,
                                     fallbackExchange,
                                     0.0)) {
            updateIbkrQuoteExchangeSuccess(m_pendingIbkrQuotesSymbol);
            if (m_ibkrGetStocksBatchActive) {
                m_pendingIbkrQuotesExchange = quoteExchange;
                m_pendingIbkrQuotesPrimaryExchange.clear();
                m_pendingIbkrQuotesDays = ibkrMissingQuoteDays(m_pendingIbkrQuotesSymbol, 90);
                m_pendingIbkrProcessIsHistoricalQuotes = true;
                m_pendingIbkrProcessIsMarketSnapshot = false;
                setIbkrConnectionState(
                    QStringLiteral("%1: %2 -> %3, beste Direktboerse %4 als Fallback. Neue Quotes werden geladen ... OK: %5, Fehler: %6.")
                        .arg(m_ibkrGetStocksBatchName)
                        .arg(m_pendingIbkrQuotesSymbol, quoteExchange, fallbackExchange)
                        .arg(m_ibkrGetStocksSuccessCount)
                        .arg(m_ibkrGetStocksFailureCount),
                    m_ibkrConnected,
                    false);
                startIbkrQuoteHelperRequest(false);
                return;
            }
            m_pendingIbkrQuotesExchange = quoteExchange;
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrProcessIsHistoricalQuotes = true;
            m_pendingIbkrProcessIsMarketSnapshot = false;
            startIbkrQuoteHelperRequest(false);
            return;
        }
        const QString finalError = message.trimmed().isEmpty()
                                       ? QStringLiteral("Keine Umsatzboerse ermittelt")
                                       : message;
        if (m_ibkrGetStocksBatchActive) {
            if (m_pendingIbkrQuotesExchange.isEmpty() && !fallbackExchange.isEmpty())
                m_pendingIbkrQuotesExchange = fallbackExchange;
            m_pendingIbkrQuotesPrimaryExchange.clear();
            startIbkrQuoteSnapshotFallback(
                QStringLiteral("Boersen-Probe fehlgeschlagen (%1)").arg(finalError));
            return;
        }
        updateIbkrQuoteExchangeFailure(m_pendingIbkrQuotesSymbol, finalError);
        setIbkrConnectionState(
            QStringLiteral("Fehler: %1").arg(message.trimmed().isEmpty()
                                                 ? QStringLiteral("IBKR konnte keine Umsatzboerse ermitteln.")
                                                 : message),
            m_ibkrConnected,
            false);
        emit ibkrConnectionChanged();
        return;
    }

    const QJsonObject data = result.value(QStringLiteral("data")).toObject();
    const QString exchange = data.value(QStringLiteral("exchange")).toString().trimmed().toUpper();
    const double turnover = data.value(QStringLiteral("turnover")).toDouble();
    const QString quoteExchange = exchange;
    if (exchange.isEmpty()
        || !saveIbkrQuoteExchange(m_pendingIbkrQuotesSymbol,
                                  quoteExchange,
                                  turnover,
                                  exchange,
                                  turnover)) {
        m_pendingIbkrProcessIsQuoteExchangeProbe = false;
        m_ibkrDataTimeout.setInterval(25000);
        updateIbkrQuoteExchangeFailure(
            m_pendingIbkrQuotesSymbol,
            exchange.isEmpty()
                ? QStringLiteral("IBKR lieferte keine Quote-Boerse")
                : QStringLiteral("Quote-Boerse konnte nicht gespeichert werden"));
        if (m_ibkrGetStocksBatchActive) {
            ++m_ibkrGetStocksFailureCount;
            setIbkrConnectionState(
                QStringLiteral("%1: Quote-Boerse fuer %2 konnte nicht gespeichert werden. OK: %3, Fehler: %4.")
                    .arg(m_ibkrGetStocksBatchName)
                    .arg(m_pendingIbkrQuotesSymbol)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrGetStocksSymbol(200);
            emit ibkrConnectionChanged();
            return;
        }
        setIbkrConnectionState(
            QStringLiteral("Fehler: Ermittelte IBKR-Quote-Boerse konnte nicht gespeichert werden."),
            m_ibkrConnected,
            false);
        emit ibkrConnectionChanged();
        return;
    }

    m_pendingIbkrQuotesExchange = quoteExchange;
    m_pendingIbkrQuotesPrimaryExchange.clear();
    updateIbkrQuoteExchangeSuccess(m_pendingIbkrQuotesSymbol);
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    if (m_ibkrGetStocksBatchActive) {
        m_pendingIbkrQuotesDays = ibkrMissingQuoteDays(m_pendingIbkrQuotesSymbol, 90);
        m_pendingIbkrProcessIsHistoricalQuotes = true;
        m_pendingIbkrProcessIsMarketSnapshot = false;
        setIbkrConnectionState(
            QStringLiteral("%1: %2 -> %3, beste Direktboerse %4 gespeichert. Neue Quotes werden geladen ... OK: %5, Fehler: %6.")
                .arg(m_ibkrGetStocksBatchName)
                .arg(m_pendingIbkrQuotesSymbol, quoteExchange, exchange)
                .arg(m_ibkrGetStocksSuccessCount)
                .arg(m_ibkrGetStocksFailureCount),
            m_ibkrConnected,
            false);
        startIbkrQuoteHelperRequest(false);
        return;
    }
    m_pendingIbkrProcessIsHistoricalQuotes = true;
    m_pendingIbkrProcessIsMarketSnapshot = false;
    startIbkrQuoteHelperRequest(false);
}

bool DatabaseManager::saveIbkrHistoricalQuotes(const QString &symbol,
                                               const QJsonArray &bars,
                                               int *changedQuoteCount,
                                               QDate *latestQuoteDate)
{
    if (changedQuoteCount)
        *changedQuoteCount = 0;
    if (latestQuoteDate)
        *latestQuoteDate = QDate();

    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || bars.isEmpty())
        return false;

    if (!db.transaction()) {
        qCritical() << "IBKR-Quotes konnten keine Transaktion starten:" << db.lastError().text();
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

    QSqlQuery existingQuery(db);
    existingQuery.prepare(R"SQL(
        SELECT 1
        FROM "Quotes"
        WHERE "Symbol" = :symbol
          AND "CloseDate" = :closeDate
          AND "ClosePrice" IS NOT DISTINCT FROM :closePrice
          AND "OpenPrice" IS NOT DISTINCT FROM :openPrice
          AND "HighestPrice" IS NOT DISTINCT FROM :highestPrice
          AND "LowestPrice" IS NOT DISTINCT FROM :lowestPrice
          AND "Volume" IS NOT DISTINCT FROM :volume
        LIMIT 1
    )SQL");

    int inserted = 0;
    int changed = 0;
    QDate newestQuoteDate;
    double newestClosePrice = 0.0;
    for (const QJsonValue &value : bars) {
        const QJsonObject bar = value.toObject();
        const QDate closeDate = QDate::fromString(
            bar.value(QStringLiteral("date")).toString(),
            QStringLiteral("yyyy-MM-dd"));
        if (!closeDate.isValid())
            continue;
        const double closePrice = bar.value(QStringLiteral("close")).toDouble();
        if (!newestQuoteDate.isValid() || closeDate > newestQuoteDate) {
            newestQuoteDate = closeDate;
            newestClosePrice = closePrice;
        }

        const double openPrice = bar.value(QStringLiteral("open")).toDouble();
        const double highestPrice = bar.value(QStringLiteral("high")).toDouble();
        const double lowestPrice = bar.value(QStringLiteral("low")).toDouble();
        const double volume = bar.value(QStringLiteral("volume")).toDouble();

        existingQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        existingQuery.bindValue(QStringLiteral(":closeDate"), closeDate);
        existingQuery.bindValue(QStringLiteral(":closePrice"), closePrice);
        existingQuery.bindValue(QStringLiteral(":openPrice"), openPrice);
        existingQuery.bindValue(QStringLiteral(":highestPrice"), highestPrice);
        existingQuery.bindValue(QStringLiteral(":lowestPrice"), lowestPrice);
        existingQuery.bindValue(QStringLiteral(":volume"), volume);
        if (!existingQuery.exec()) {
            qCritical() << "IBKR-Quote-Aenderung konnte nicht geprueft werden:"
                        << existingQuery.lastError().text() << normalizedSymbol << closeDate;
            db.rollback();
            return false;
        }
        if (!existingQuery.next())
            ++changed;

        insertQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        insertQuery.bindValue(QStringLiteral(":closeDate"), closeDate);
        insertQuery.bindValue(QStringLiteral(":closePrice"), closePrice);
        insertQuery.bindValue(QStringLiteral(":openPrice"), openPrice);
        insertQuery.bindValue(QStringLiteral(":highestPrice"), highestPrice);
        insertQuery.bindValue(QStringLiteral(":lowestPrice"), lowestPrice);
        insertQuery.bindValue(QStringLiteral(":volume"), volume);
        if (!insertQuery.exec()) {
            qCritical() << "IBKR-Quote konnte nicht gespeichert werden:"
                        << insertQuery.lastError().text() << normalizedSymbol << closeDate;
            db.rollback();
            return false;
        }
        ++inserted;
    }

    if (inserted == 0) {
        qCritical() << "IBKR-Quotes enthielten keine gueltigen Tagesdaten:" << normalizedSymbol;
        db.rollback();
        return false;
    }

    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"SQL(
        UPDATE "Stocks"
        SET "LastUpdateDate" = CURRENT_DATE
        WHERE "Symbol" = :symbol
    )SQL");
    updateQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!updateQuery.exec()) {
        qCritical() << "Stock-Update nach IBKR-Quotes fehlgeschlagen:"
                    << updateQuery.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    if (!updateActiveBoughtStockCurrentValue(db, normalizedSymbol, newestClosePrice)) {
        qCritical() << "Depotwert konnte nach IBKR-Quotes nicht aktualisiert werden:"
                    << normalizedSymbol;
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "IBKR-Quotes konnten nicht abgeschlossen werden:" << db.lastError().text();
        db.rollback();
        return false;
    }
    if (changedQuoteCount)
        *changedQuoteCount = changed;
    if (latestQuoteDate)
        *latestQuoteDate = newestQuoteDate;
    return true;
}

bool DatabaseManager::saveIbkrQuoteSnapshot(const QString &symbol,
                                            const QJsonObject &snapshotData,
                                            double *savedPrice)
{
    if (savedPrice)
        *savedPrice = 0.0;

    const QString normalizedSymbol = symbol.trimmed();
    const double selectedPrice = snapshotData.value(QStringLiteral("selected")).toDouble();
    if (normalizedSymbol.isEmpty() || selectedPrice <= 0.0)
        return false;

    const QJsonObject prices = snapshotData.value(QStringLiteral("prices")).toObject();
    const QJsonObject sizes = snapshotData.value(QStringLiteral("sizes")).toObject();
    double openPrice = prices.value(QStringLiteral("OPEN")).toDouble();
    double highestPrice = prices.value(QStringLiteral("HIGH")).toDouble();
    double lowestPrice = prices.value(QStringLiteral("LOW")).toDouble();
    const double closePrice = selectedPrice;
    double volume = sizes.value(QStringLiteral("VOLUME")).toDouble();
    if (volume <= 0.0)
        volume = prices.value(QStringLiteral("VOLUME")).toDouble();

    if (openPrice <= 0.0)
        openPrice = closePrice;
    if (highestPrice <= 0.0 || highestPrice < closePrice)
        highestPrice = closePrice;
    if (lowestPrice <= 0.0 || lowestPrice > closePrice)
        lowestPrice = closePrice;

    if (!db.transaction()) {
        qCritical() << "IBKR-Snapshot konnte keine Transaktion starten:" << db.lastError().text();
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(R"SQL(
        INSERT INTO "Quotes" (
            "Symbol", "CloseDate", "ClosePrice", "OpenPrice",
            "HighestPrice", "LowestPrice", "Volume"
        )
        VALUES (
            :symbol, CURRENT_DATE, :closePrice, :openPrice,
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
    insertQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    insertQuery.bindValue(QStringLiteral(":closePrice"), closePrice);
    insertQuery.bindValue(QStringLiteral(":openPrice"), openPrice);
    insertQuery.bindValue(QStringLiteral(":highestPrice"), highestPrice);
    insertQuery.bindValue(QStringLiteral(":lowestPrice"), lowestPrice);
    insertQuery.bindValue(QStringLiteral(":volume"), volume);
    if (!insertQuery.exec()) {
        qCritical() << "IBKR-Snapshot konnte nicht gespeichert werden:"
                    << insertQuery.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"SQL(
        UPDATE "Stocks"
        SET "LastUpdateDate" = CURRENT_DATE
        WHERE "Symbol" = :symbol
    )SQL");
    updateQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!updateQuery.exec()) {
        qCritical() << "Stock-Update nach IBKR-Snapshot fehlgeschlagen:"
                    << updateQuery.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    if (!updateActiveBoughtStockCurrentValue(db, normalizedSymbol, closePrice)) {
        qCritical() << "Depotwert konnte nach IBKR-Snapshot nicht aktualisiert werden:"
                    << normalizedSymbol;
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "IBKR-Snapshot konnte nicht abgeschlossen werden:" << db.lastError().text();
        db.rollback();
        return false;
    }

    if (savedPrice)
        *savedPrice = closePrice;
    return true;
}

bool DatabaseManager::saveIbkrQuoteExchange(const QString &symbol,
                                            const QString &quoteExchange,
                                            double turnover,
                                            const QString &bestDirectExchange,
                                            double bestDirectTurnover)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRQuoteExchange" = :quoteExchange,
            "IBKRQuoteExchangeTurnover" = :turnover,
            "IBKRQuoteExchangeCheckedAt" = CURRENT_TIMESTAMP,
            "IBKRBestDirectExchange" = COALESCE(NULLIF(:bestDirectExchange, ''), "IBKRBestDirectExchange"),
            "IBKRBestDirectExchangeTurnover" = CASE
                WHEN NULLIF(:bestDirectExchange, '') IS NULL THEN "IBKRBestDirectExchangeTurnover"
                ELSE :bestDirectTurnover
            END,
            "IBKRBestDirectExchangeCheckedAt" = CASE
                WHEN NULLIF(:bestDirectExchange, '') IS NULL THEN "IBKRBestDirectExchangeCheckedAt"
                ELSE CURRENT_TIMESTAMP
            END,
            "IBKRQuoteExchangeLastSuccessAt" = CURRENT_TIMESTAMP,
            "IBKRQuoteExchangeFailureCount" = 0,
            "IBKRQuoteExchangeLastError" = NULL,
            "from_IBKR" = TRUE
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    query.bindValue(QStringLiteral(":quoteExchange"), quoteExchange.trimmed().toUpper());
    query.bindValue(QStringLiteral(":turnover"), turnover);
    query.bindValue(QStringLiteral(":bestDirectExchange"), bestDirectExchange.trimmed().toUpper());
    query.bindValue(QStringLiteral(":bestDirectTurnover"), bestDirectTurnover);
    if (!query.exec()) {
        qCritical() << "IBKR-Quote-Boerse konnte nicht gespeichert werden:"
                    << query.lastError().text() << symbol << quoteExchange << bestDirectExchange;
        return false;
    }
    return query.numRowsAffected() > 0;
}

void DatabaseManager::updateIbkrQuoteExchangeAttempt(const QString &symbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRQuoteExchangeLastAttemptAt" = CURRENT_TIMESTAMP
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    if (!query.exec()) {
        qWarning() << "IBKR-Quote-Boersen-Versuch konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::updateIbkrQuoteExchangeFailure(const QString &symbol, const QString &error)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRQuoteExchangeLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRQuoteExchangeFailureCount" = COALESCE("IBKRQuoteExchangeFailureCount", 0) + 1,
            "IBKRQuoteExchangeLastError" = LEFT(:error, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    query.bindValue(QStringLiteral(":error"), error.trimmed());
    if (!query.exec()) {
        qWarning() << "IBKR-Quote-Boersen-Fehler konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol << error;
    }
}

void DatabaseManager::updateIbkrQuoteExchangeSuccess(const QString &symbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRQuoteExchangeLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRQuoteExchangeLastSuccessAt" = CURRENT_TIMESTAMP,
            "IBKRQuoteExchangeFailureCount" = 0,
            "IBKRQuoteExchangeLastError" = NULL,
            "from_IBKR" = TRUE
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    if (!query.exec()) {
        qWarning() << "IBKR-Quote-Boersen-Erfolg konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::markStockFromIbkr(const QString &symbol, bool fromIbkr)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "from_IBKR" = :fromIbkr,
            "IBKRQuoteExchangeFailureCount" = CASE
                WHEN :fromIbkr = FALSE THEN 0
                ELSE "IBKRQuoteExchangeFailureCount"
            END,
            "IBKRQuoteExchangeLastError" = CASE
                WHEN :fromIbkr = FALSE THEN NULL
                ELSE "IBKRQuoteExchangeLastError"
            END
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    query.bindValue(QStringLiteral(":fromIbkr"), fromIbkr);
    if (!query.exec()) {
        qWarning() << "Stock-Datenquelle konnte nicht aktualisiert werden:"
                   << query.lastError().text() << symbol << fromIbkr;
    }
}

void DatabaseManager::finishIbkrDataRequest(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    m_ibkrDataTimeout.stop();
    if (!m_ibkrDataLoading)
        return;

    m_ibkrDataLoading = false;

    m_lastIbkrHelperElapsedMs = m_ibkrHelperTimer.isValid() ? m_ibkrHelperTimer.elapsed() : -1;
    if (m_lastIbkrHelperElapsedMs >= 0)
        qDebug().noquote() << "IBKR-Helfer API-Dauer:" << m_lastIbkrHelperElapsedMs << "ms";
    const QString stderrText = QString::fromUtf8(m_ibkrProcess.readAllStandardError()).trimmed();
    if (!stderrText.isEmpty())
        qDebug().noquote() << "IBKR-Helfer:" << stderrText;

    const QByteArray output = m_ibkrProcess.readAllStandardOutput().trimmed();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    const bool parseOk = parseError.error == QJsonParseError::NoError && document.isObject();
    if (m_pendingIbkrProcessIsHistoricalQuotes
        || m_pendingIbkrProcessIsQuoteExchangeProbe
        || m_pendingIbkrProcessIsMarketSnapshot) {
        const QJsonObject timingResult = parseOk ? document.object() : QJsonObject();
        appendIbkrQuoteTimingLog(
            m_pendingIbkrQuotesSymbol.isEmpty() ? m_ibkrPendingSymbol : m_pendingIbkrQuotesSymbol,
            m_pendingIbkrProcessIsQuoteExchangeProbe
                ? QStringLiteral("probe")
                : (m_pendingIbkrProcessIsMarketSnapshot ? QStringLiteral("snapshot") : QStringLiteral("historical")),
            m_lastIbkrHelperElapsedMs,
            exitStatus,
            parseOk,
            timingResult.value(QStringLiteral("success")).toBool(false),
            parseOk ? timingResult.value(QStringLiteral("message")).toString() : QString::fromUtf8(output.left(500)));
    }
    if (exitStatus != QProcess::NormalExit
        || !parseOk) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Ungültige Antwort vom IBKR-Helfer."),
            m_ibkrConnected,
            false);
        if (m_pendingIbkrProcessIsHistoricalQuotes
            || m_pendingIbkrProcessIsQuoteExchangeProbe
            || m_pendingIbkrProcessIsMarketSnapshot) {
            m_pendingIbkrProcessIsHistoricalQuotes = false;
            m_pendingIbkrProcessIsQuoteExchangeProbe = false;
            m_pendingIbkrProcessIsMarketSnapshot = false;
            updateIbkrQuoteExchangeFailure(m_ibkrPendingSymbol, QStringLiteral("Ungueltige Helper-Antwort"));
            if (m_ibkrGetStocksBatchActive) {
                ++m_ibkrGetStocksFailureCount;
                setIbkrConnectionState(
                    QStringLiteral("%1: %2 ungueltige Helper-Antwort. OK: %3, Fehler: %4.")
                        .arg(m_ibkrGetStocksBatchName)
                        .arg(m_ibkrPendingSymbol)
                        .arg(m_ibkrGetStocksSuccessCount)
                        .arg(m_ibkrGetStocksFailureCount),
                    m_ibkrConnected,
                    false);
            }
            m_pendingIbkrQuotesSymbol.clear();
            m_pendingIbkrQuotesIsin.clear();
            m_pendingIbkrQuotesIbkrSymbol.clear();
            m_pendingIbkrQuotesCurrency.clear();
            m_pendingIbkrQuotesExchange.clear();
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrQuotesProbeExchanges.clear();
            m_pendingIbkrQuotesConId = 0;
            m_pendingIbkrQuotesDays = 0;
            m_pendingIbkrQuotesFallbackIndex = 0;
            m_pendingIbkrQuotesSupportsSmart = false;
            m_pendingIbkrQuotesForceDirectProbeResult = false;
            m_pendingIbkrQuotesSmartHistoricalRetry = false;
            m_ibkrPendingSymbol.clear();
            m_ibkrDataTimeout.setInterval(25000);
            if (m_ibkrGetStocksBatchActive)
                scheduleNextIbkrGetStocksSymbol(200);
            emit ibkrConnectionChanged();
            return;
        }
        if (m_pendingIbkrProcessIsNameCheck) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(m_pendingIbkrNameCheckSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung: ungueltige Helper-Antwort"));
            m_pendingIbkrProcessIsNameCheck = false;
            m_pendingIbkrNameCheckSymbol.clear();
            m_pendingIbkrNameCheckName.clear();
            m_pendingIbkrNameCheckIsin.clear();
            m_pendingIbkrNameCheckHasConId = false;
            m_pendingIbkrNameCheckCandidates.clear();
            m_pendingIbkrNameCheckCandidateIndex = 0;
            scheduleNextIbkrNameCheckBatchSymbol(1000);
            return;
        }
        if (m_pendingIbkrProcessIsNameSearch)
            m_pendingIbkrProcessIsNameSearch = false;
        if (tryNextIbkrCandidate(QStringLiteral("ungueltige Helper-Antwort")))
            return;
        finalizeIbkrDataFailure(QStringLiteral("ungueltige Helper-Antwort"));
        return;
    }

    const QJsonObject result = document.object();
    const QString message = result.value(QStringLiteral("message")).toString();
    if (m_pendingIbkrProcessIsHistoricalQuotes) {
        finishIbkrQuotesRequest(result);
        return;
    }
    if (m_pendingIbkrProcessIsQuoteExchangeProbe) {
        finishIbkrQuoteExchangeProbe(result);
        return;
    }
    if (m_pendingIbkrProcessIsMarketSnapshot) {
        finishIbkrQuoteSnapshotRequest(result);
        return;
    }
    if (m_pendingIbkrProcessIsNameCheck) {
        finishIbkrNameCheckRequest(result);
        return;
    }

    const QString reason = result.value(QStringLiteral("reason")).toString().trimmed();
    if (reason == QStringLiteral("ambiguous_isin")) {
        const QJsonObject data = result.value(QStringLiteral("data")).toObject();
        QStringList isins;
        for (const QJsonValue &value : data.value(QStringLiteral("isins")).toArray()) {
            const QString isin = value.toString().trimmed();
            if (!isin.isEmpty())
                isins << isin;
        }
        QStringList existingIsins;
        QStringList freeIsins;
        for (const QString &isin : std::as_const(isins)) {
            QSqlQuery isinQuery(db);
            isinQuery.prepare(R"SQL(
                SELECT 1
                FROM "Stocks"
                WHERE "ISIN" = :isin
                  AND "Symbol" <> :symbol
                LIMIT 1
            )SQL");
            isinQuery.bindValue(QStringLiteral(":isin"), isin);
            isinQuery.bindValue(QStringLiteral(":symbol"), m_ibkrPendingSymbol);
            if (!isinQuery.exec()) {
                qWarning() << "IBKR ambiguous_isin Dublettenpruefung fehlgeschlagen:"
                           << isinQuery.lastError().text() << m_ibkrPendingSymbol << isin;
                existingIsins << isin;
                continue;
            }
            if (isinQuery.next())
                existingIsins << isin;
            else if (!m_pendingIbkrAmbiguousIsinCandidates.contains(isin, Qt::CaseInsensitive)
                     && !m_pendingIbkrTriedAmbiguousIsins.contains(isin, Qt::CaseInsensitive))
                freeIsins << isin;
        }

        if (!freeIsins.isEmpty()) {
            m_pendingIbkrAmbiguousIsinCandidates << freeIsins;
            if (tryNextIbkrAmbiguousIsin())
                return;
        }

        const QString validationMessage = isins.isEmpty()
                                              ? message
                                              : QStringLiteral("%1 ISINs: %2%3")
                                                    .arg(message,
                                                         isins.join(QStringLiteral(", ")),
                                                         existingIsins.isEmpty()
                                                             ? QString()
                                                             : QStringLiteral(" Bereits vorhanden: %1")
                                                                   .arg(existingIsins.join(QStringLiteral(", "))));
        markIbkrValidationIssue(m_ibkrPendingSymbol,
                                QStringLiteral("ambiguous_isin"),
                                validationMessage);
        finalizeIbkrDataFailure(validationMessage);
        return;
    }

    if (m_pendingIbkrProcessIsNameSearch) {
        m_pendingIbkrProcessIsNameSearch = false;
        if (!result.value(QStringLiteral("success")).toBool()) {
            if (tryNextIbkrCandidate(message))
                return;
            finalizeIbkrDataFailure(message);
            return;
        }

        const QJsonArray samples = result.value(QStringLiteral("data")).toArray();
        QStringList labels;
        QStringList preferredSymbols;
        QStringList fallbackSymbols;
        for (const QJsonValue &value : samples) {
            const QJsonObject sample = value.toObject();
            const QString securityType = sample.value(QStringLiteral("securityType")).toString().trimmed();
            if (!securityType.isEmpty()
                && securityType.compare(QStringLiteral("STK"), Qt::CaseInsensitive) != 0) {
                continue;
            }

            const QString candidateSymbol = sample.value(QStringLiteral("symbol")).toString().trimmed();
            if (candidateSymbol.isEmpty())
                continue;

            const QString primaryExchange =
                sample.value(QStringLiteral("primaryExchange")).toString().trimmed();
            const QString currency = sample.value(QStringLiteral("currency")).toString().trimmed();
            const bool preferredByCurrency = !m_pendingIbkrCurrency.isEmpty()
                                             && currency.compare(m_pendingIbkrCurrency, Qt::CaseInsensitive) == 0;
            const bool preferredByExchange = !primaryExchange.isEmpty()
                                             && (primaryExchange.compare(m_pendingIbkrExchange, Qt::CaseInsensitive) == 0
                                                 || m_pendingIbkrDirectExchanges.contains(primaryExchange, Qt::CaseInsensitive));
            const QString candidateKey = ibkrCandidateKey(candidateSymbol);
            if (!candidateKey.isEmpty()) {
                if (!currency.isEmpty())
                    m_pendingIbkrCandidateCurrencies.insert(candidateKey, currency);
                if (!primaryExchange.isEmpty())
                    m_pendingIbkrCandidateExchanges.insert(candidateKey, primaryExchange);

                static const QRegularExpression helsinkiSeriesSuffix(
                    QStringLiteral("^([A-Z]{2,})(1[HV])$"));
                const QRegularExpressionMatch match = helsinkiSeriesSuffix.match(candidateKey);
                if (match.hasMatch()) {
                    if (!currency.isEmpty())
                        m_pendingIbkrCandidateCurrencies.insert(match.captured(1), currency);
                    if (!primaryExchange.isEmpty())
                        m_pendingIbkrCandidateExchanges.insert(match.captured(1), primaryExchange);
                }
            }
            appendIbkrMatchedSymbolVariants(preferredByCurrency || preferredByExchange
                                                ? preferredSymbols
                                                : fallbackSymbols,
                                            candidateSymbol);
            labels << QStringLiteral("%1/%2/%3").arg(candidateSymbol, primaryExchange, currency);
        }

        int added = 0;
        for (const QString &candidateSymbol : preferredSymbols) {
            const int before = m_pendingIbkrCandidateSymbols.size();
            appendIbkrMatchedSymbolVariants(m_pendingIbkrCandidateSymbols, candidateSymbol);
            if (m_pendingIbkrCandidateSymbols.size() > before)
                ++added;
        }
        for (const QString &candidateSymbol : fallbackSymbols) {
            const int before = m_pendingIbkrCandidateSymbols.size();
            appendIbkrMatchedSymbolVariants(m_pendingIbkrCandidateSymbols, candidateSymbol);
            if (m_pendingIbkrCandidateSymbols.size() > before)
                ++added;
        }

        if (added > 0) {
            if (m_pendingIbkrDataForNameCheckRecovery) {
                m_pendingIbkrCurrency.clear();
                m_pendingIbkrExchange.clear();
                m_pendingIbkrDirectExchange = false;
                m_pendingIbkrDirectExchanges.clear();
                m_pendingIbkrDirectExchangeIndex = 0;
                m_pendingIbkrCurrentDirectExchange.clear();
            }
            m_pendingIbkrReviewRequired = true;
            m_pendingIbkrReviewReason = QStringLiteral(
                                            "IBKR-Namenssuche nach \"%1\": %2 Kandidaten, uebernommenen Treffer bitte pruefen (%3).")
                                            .arg(m_pendingIbkrSearchKeywords)
                                            .arg(added)
                                            .arg(labels.mid(0, 5).join(QStringLiteral(", ")));
            if (tryNextIbkrCandidate())
                return;
        }

        const QString failure = message.trimmed().isEmpty()
                                    ? QStringLiteral("IBKR-Namenssuche fand keinen brauchbaren Aktienkandidaten.")
                                    : message;
        if (tryNextIbkrCandidate(failure))
            return;
        finalizeIbkrDataFailure(failure);
        return;
    }

    if (!result.value(QStringLiteral("success")).toBool()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: %1").arg(message),
            m_ibkrConnected,
            false);
        if (tryNextIbkrAmbiguousIsin())
            return;
        if (tryNextIbkrCandidate(message))
            return;
        finalizeIbkrDataFailure(message);
        return;
    }

    QVariantMap details = result.value(QStringLiteral("data")).toObject().toVariantMap();
    const QString returnedIsin = details.value(QStringLiteral("isin")).toString().trimmed().toUpper();
    if (!returnedIsin.isEmpty()) {
        QSqlQuery duplicateQuery(db);
        duplicateQuery.prepare(R"SQL(
            SELECT "Symbol"
            FROM "Stocks"
            WHERE "ISIN" = :isin
              AND "Symbol" <> :symbol
            ORDER BY "Symbol"
            LIMIT 5
        )SQL");
        duplicateQuery.bindValue(QStringLiteral(":isin"), returnedIsin);
        duplicateQuery.bindValue(QStringLiteral(":symbol"), m_ibkrPendingSymbol);
        if (!duplicateQuery.exec()) {
            qWarning() << "IBKR-ISIN-Dublettenpruefung fehlgeschlagen:"
                       << duplicateQuery.lastError().text() << m_ibkrPendingSymbol << returnedIsin;
        } else {
            QStringList duplicateSymbols;
            while (duplicateQuery.next())
                duplicateSymbols << duplicateQuery.value(0).toString();
            if (!duplicateSymbols.isEmpty()) {
                const QString validationMessage =
                    QStringLiteral("IBKR lieferte ISIN %1, diese existiert bereits bei %2. Daten wurden nicht uebernommen.")
                        .arg(returnedIsin, duplicateSymbols.join(QStringLiteral(", ")));
                markIbkrValidationIssue(m_ibkrPendingSymbol,
                                        QStringLiteral("duplicate_isin"),
                                        validationMessage);
                finalizeIbkrDataFailure(validationMessage);
                return;
            }
        }
    }

    if (m_pendingIbkrReviewRequired && !returnedIsin.isEmpty()) {
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("verified_name"));
        details.insert(QStringLiteral("validationMessage"), QString());
    } else if (m_pendingIbkrReviewRequired) {
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("review_required"));
        details.insert(QStringLiteral("validationMessage"), m_pendingIbkrReviewReason);
    } else if (!m_pendingIbkrIsin.isEmpty() && !m_pendingIbkrTryWithoutIsin) {
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("verified_isin"));
        details.insert(QStringLiteral("validationMessage"), QString());
    } else {
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("verified_symbol"));
        details.insert(QStringLiteral("validationMessage"), QString());
    }

    if (!saveIbkrContractDetails(m_ibkrPendingSymbol, details)) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: IBKR-Daten konnten nicht in der Datenbank gespeichert werden."),
            true,
            false);
        finalizeIbkrDataFailure(QStringLiteral("Datenbankfehler"));
        return;
    }

    const QString completedSymbol = m_ibkrPendingSymbol;
    const bool quoteAfterMetadata = m_pendingIbkrQuoteAfterMetadata;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrCurrency.clear();
    m_pendingIbkrExchange.clear();
    m_pendingIbkrIsin.clear();
    m_pendingIbkrSearchKeywords.clear();
    m_pendingIbkrNameSearchTerms.clear();
    m_pendingIbkrNameSearchIndex = 0;
    m_pendingIbkrLastError.clear();
    m_pendingIbkrCandidateSymbols.clear();
    m_pendingIbkrCandidateCurrencies.clear();
    m_pendingIbkrCandidateExchanges.clear();
    m_pendingIbkrCurrentCandidateSymbol.clear();
    m_pendingIbkrAmbiguousIsinCandidates.clear();
    m_pendingIbkrTriedAmbiguousIsins.clear();
    m_pendingIbkrCandidateIndex = 0;
    m_pendingIbkrSearchStarted = false;
    m_pendingIbkrNameSearchStarted = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrReviewRequired = false;
    m_pendingIbkrReviewReason.clear();
    m_pendingIbkrQuoteAfterMetadata = false;
    m_pendingIbkrTryWithoutIsin = false;
    m_pendingIbkrDirectExchange = false;
    m_pendingIbkrDirectExchanges.clear();
    m_pendingIbkrDirectExchangeIndex = 0;
    m_pendingIbkrCurrentDirectExchange.clear();
    if (quoteAfterMetadata && m_ibkrGetStocksBatchActive) {
        setIbkrConnectionState(
            QStringLiteral("%1: IBKR-Stammdaten fuer %2 aktualisiert, lade Quotes ...")
                .arg(m_ibkrGetStocksBatchName, completedSymbol),
            true,
            false);
        const bool quoteStarted = startIbkrQuoteExchangeProbeForSymbol(completedSymbol);
        if (!quoteStarted) {
            ++m_ibkrGetStocksFailureCount;
            setIbkrConnectionState(
                QStringLiteral("%1: %2 Stammdaten aktualisiert, Quote-Abruf konnte nicht gestartet werden. OK: %3, Fehler: %4.")
                    .arg(m_ibkrGetStocksBatchName)
                    .arg(completedSymbol)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrGetStocksSymbol(500);
        }
        return;
    }
    if (m_pendingIbkrDataForNameCheckRecovery) {
        m_pendingIbkrDataForNameCheckRecovery = false;
        if (m_ibkrNameCheckBatchActive) {
            ++m_ibkrNameCheckBatchSuccessCount;
            updateIbkrBatchSuccess(completedSymbol);
            setIbkrConnectionState(
                QStringLiteral("IBKR-Namenspruefung: %1 per Namenssuche uebernommen. OK: %2, Fehler/Pruefen: %3.")
                    .arg(completedSymbol)
                    .arg(m_ibkrNameCheckBatchSuccessCount)
                    .arg(m_ibkrNameCheckBatchFailureCount),
                true,
                false);
            emit ibkrStockDataUpdated(completedSymbol);
            scheduleNextIbkrNameCheckBatchSymbol(1000);
            return;
        }
    }
    if (m_ibkrBatchActive) {
        ++m_ibkrBatchSuccessCount;
        updateIbkrBatchSuccess(completedSymbol);
        setIbkrConnectionState(
            QStringLiteral("IBKR-Batch: %1 gespeichert. Erfolgreich: %2, Fehler: %3.")
                .arg(completedSymbol)
                .arg(m_ibkrBatchSuccessCount)
                .arg(m_ibkrBatchFailureCount),
            true,
            false);
        emit ibkrStockDataUpdated(completedSymbol);
        scheduleNextIbkrBatchSymbol(1000);
        return;
    }

    setIbkrConnectionState(
        QStringLiteral("%1 Datenbank und Anzeige wurden aktualisiert.").arg(message),
        true,
        false);
    emit ibkrStockDataUpdated(completedSymbol);
}

void DatabaseManager::loadNextIbkrBatchSymbol()
{
    if (!m_ibkrBatchActive)
        return;

    if (m_ibkrDataLoading) {
        scheduleNextIbkrBatchSymbol(200);
        return;
    }

    if (m_ibkrBatchIndex >= m_ibkrBatchSymbols.size()) {
        finishIbkrBatch(
            QStringLiteral("IBKR-Batch abgeschlossen: %1 Aktien, %2 erfolgreich, %3 fehlgeschlagen.")
                .arg(m_ibkrBatchSymbols.size())
                .arg(m_ibkrBatchSuccessCount)
                .arg(m_ibkrBatchFailureCount));
        return;
    }

    const QString symbol = m_ibkrBatchSymbols.at(m_ibkrBatchIndex++).trimmed();
    if (symbol.isEmpty()) {
        scheduleNextIbkrBatchSymbol(100);
        return;
    }

    setIbkrConnectionState(
        QStringLiteral("IBKR-Batch: %1/%2 %3 wird aktualisiert ... Erfolgreich: %4, Fehler: %5")
            .arg(m_ibkrBatchIndex)
            .arg(m_ibkrBatchSymbols.size())
            .arg(symbol)
            .arg(m_ibkrBatchSuccessCount)
            .arg(m_ibkrBatchFailureCount),
        m_ibkrConnected,
        false);
    getIbkrData(symbol);
}

void DatabaseManager::scheduleNextIbkrBatchSymbol(int delayMs)
{
    if (!m_ibkrBatchActive)
        return;
    m_ibkrBatchTimer.start(delayMs);
}

void DatabaseManager::finishIbkrBatch(const QString &message)
{
    m_ibkrBatchTimer.stop();
    m_ibkrBatchActive = false;
    m_ibkrDataLoading = false;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrCurrency.clear();
    m_pendingIbkrExchange.clear();
    m_pendingIbkrIsin.clear();
    m_pendingIbkrSearchKeywords.clear();
    m_pendingIbkrNameSearchTerms.clear();
    m_pendingIbkrNameSearchIndex = 0;
    m_pendingIbkrLastError.clear();
    m_pendingIbkrCandidateSymbols.clear();
    m_pendingIbkrCandidateCurrencies.clear();
    m_pendingIbkrCandidateExchanges.clear();
    m_pendingIbkrCurrentCandidateSymbol.clear();
    m_pendingIbkrAmbiguousIsinCandidates.clear();
    m_pendingIbkrTriedAmbiguousIsins.clear();
    m_pendingIbkrCandidateIndex = 0;
    m_pendingIbkrSearchStarted = false;
    m_pendingIbkrNameSearchStarted = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrReviewRequired = false;
    m_pendingIbkrReviewReason.clear();
    m_pendingIbkrTryWithoutIsin = false;
    m_pendingIbkrDirectExchange = false;
    m_pendingIbkrDirectExchanges.clear();
    m_pendingIbkrDirectExchangeIndex = 0;
    m_pendingIbkrCurrentDirectExchange.clear();
    setIbkrConnectionState(message, m_ibkrConnected, false);
    emit ibkrConnectionChanged();
    emit ibkrStockDataUpdated(QString());
}

void DatabaseManager::loadNextIbkrNameCheckBatchSymbol()
{
    if (!m_ibkrNameCheckBatchActive)
        return;

    if (m_ibkrNameCheckBatchIndex >= m_ibkrNameCheckBatchSymbols.size()) {
        finishIbkrNameCheckBatch(
            QStringLiteral("IBKR-Namenspruefung abgeschlossen: %1 Datensaetze, %2 OK, %3 Fehler/Pruefen.")
                .arg(m_ibkrNameCheckBatchSymbols.size())
                .arg(m_ibkrNameCheckBatchSuccessCount)
                .arg(m_ibkrNameCheckBatchFailureCount));
        return;
    }

    const QString symbol = m_ibkrNameCheckBatchSymbols.at(m_ibkrNameCheckBatchIndex++).trimmed();
    if (symbol.isEmpty()) {
        scheduleNextIbkrNameCheckBatchSymbol(50);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Name", "ISIN", "IBKRConId", "IBKRResolvedSymbol", "LocalSymbol",
               "Currency", "PrimaryExchange", "CountryCode", "MIC"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec()) {
        ++m_ibkrNameCheckBatchFailureCount;
        scheduleNextIbkrNameCheckBatchSymbol(100);
        return;
    }
    const bool stockExists = query.next();

    m_pendingIbkrNameCheckSymbol = symbol;
    m_pendingIbkrNameCheckName = stockExists
                                     ? query.value(QStringLiteral("Name")).toString()
                                     : symbol.section(QLatin1Char('.'), 0, 0).trimmed();
    m_pendingIbkrNameCheckIsin = ibkrNameCheckIsinOverride(symbol);
    m_pendingIbkrNameCheckHasConId = stockExists
                                     && !query.value(QStringLiteral("IBKRConId")).isNull()
                                     && query.value(QStringLiteral("IBKRConId")).toLongLong() > 0;
    m_pendingIbkrNameCheckRequestUsesIsin = false;
    if (m_pendingIbkrNameCheckIsin.isEmpty()) {
        ++m_ibkrNameCheckBatchFailureCount;
        if (stockExists && deleteStockWithReferencedData(symbol)) {
            setIbkrConnectionState(
                QStringLiteral("IBKR-Namenspruefung: %1 ohne Listen-ISIN geloescht.")
                    .arg(symbol),
                m_ibkrConnected,
                false);
        }
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    }

    QSqlQuery duplicateQuery(db);
    duplicateQuery.prepare(R"SQL(
        SELECT "Symbol"
        FROM "Stocks"
        WHERE "ISIN" = :isin
          AND "Symbol" <> :symbol
        LIMIT 1
    )SQL");
    duplicateQuery.bindValue(QStringLiteral(":isin"), m_pendingIbkrNameCheckIsin);
    duplicateQuery.bindValue(QStringLiteral(":symbol"), symbol);
    if (!duplicateQuery.exec() || duplicateQuery.next()) {
        ++m_ibkrNameCheckBatchFailureCount;
        const QString duplicateSymbol = duplicateQuery.isActive()
                                            ? duplicateQuery.value(0).toString()
                                            : QString();
        if (stockExists && deleteStockWithReferencedData(symbol)) {
            setIbkrConnectionState(
                duplicateSymbol.isEmpty()
                    ? QStringLiteral("IBKR-Namenspruefung: %1 wegen ISIN-Dublettenpruefung geloescht.")
                          .arg(symbol)
                    : QStringLiteral("IBKR-Namenspruefung: %1 geloescht, ISIN %2 existiert bereits bei %3.")
                          .arg(symbol, m_pendingIbkrNameCheckIsin, duplicateSymbol),
                m_ibkrConnected,
                false);
        }
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    }
    QString ibkrSymbol = stockExists
                             ? query.value(QStringLiteral("IBKRResolvedSymbol")).toString().trimmed()
                             : QString();
    const QString localSymbol = stockExists
                                    ? query.value(QStringLiteral("LocalSymbol")).toString().trimmed()
                                    : QString();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = localSymbol;
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();
    QString currency = stockExists
                           ? query.value(QStringLiteral("Currency")).toString().trimmed()
                           : QString();
    if (currency.isEmpty() && stockExists)
        currency = currencyForCountry(query.value(QStringLiteral("CountryCode")).toString());
    if (currency.isEmpty())
        currency = currencyForCountry(m_pendingIbkrNameCheckIsin.left(2));
    if (currency.isEmpty()) {
        const QString suffix = symbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();
        if (suffix == QStringLiteral("XFRA") || suffix == QStringLiteral("FRA")
            || suffix == QStringLiteral("XETR")) {
            currency = QStringLiteral("EUR");
        }
    }
    QString exchange = stockExists
                           ? query.value(QStringLiteral("PrimaryExchange")).toString().trimmed()
                           : QString();
    if (exchange.isEmpty())
        exchange = stockExists
                       ? query.value(QStringLiteral("MIC")).toString().trimmed()
                       : symbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();

    setIbkrConnectionState(
        QStringLiteral("IBKR-Namenspruefung: %1/%2 %3 wird geprueft ... OK: %4, Fehler/Pruefen: %5")
            .arg(m_ibkrNameCheckBatchIndex)
            .arg(m_ibkrNameCheckBatchSymbols.size())
            .arg(symbol)
            .arg(m_ibkrNameCheckBatchSuccessCount)
            .arg(m_ibkrNameCheckBatchFailureCount),
        m_ibkrConnected,
        false);
    prepareIbkrNameCheckCandidates(symbol, ibkrSymbol, localSymbol, currency, exchange);
    if (!startNextIbkrNameCheckCandidate()) {
        ++m_ibkrNameCheckBatchFailureCount;
        if (!ibkrNameCheckIsinOverride(symbol).isEmpty()) {
            deleteStockWithReferencedData(symbol);
        } else {
            markIbkrValidationIssue(symbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung: keine pruefbaren Kandidaten erzeugt"));
        }
        scheduleNextIbkrNameCheckBatchSymbol(800);
    }
}

void DatabaseManager::scheduleNextIbkrNameCheckBatchSymbol(int delayMs)
{
    if (!m_ibkrNameCheckBatchActive)
        return;
    m_ibkrNameCheckBatchTimer.start(delayMs);
}

void DatabaseManager::finishIbkrNameCheckBatch(const QString &message)
{
    m_ibkrNameCheckBatchTimer.stop();
    m_ibkrNameCheckBatchActive = false;
    m_ibkrDataLoading = false;
    m_pendingIbkrProcessIsNameCheck = false;
    m_pendingIbkrNameCheckSymbol.clear();
    m_pendingIbkrNameCheckName.clear();
    m_pendingIbkrNameCheckIsin.clear();
    m_pendingIbkrNameCheckHasConId = false;
    m_pendingIbkrNameCheckCandidates.clear();
    m_pendingIbkrNameCheckCandidateIndex = 0;
    setIbkrConnectionState(message, m_ibkrConnected, false);
    emit ibkrConnectionChanged();
    emit ibkrStockDataUpdated(QString());
}

void DatabaseManager::prepareIbkrNameCheckCandidates(const QString &symbol,
                                                     const QString &ibkrSymbol,
                                                     const QString &localSymbol,
                                                     const QString &currency,
                                                     const QString &exchange)
{
    m_pendingIbkrNameCheckCandidates.clear();
    m_pendingIbkrNameCheckCandidateIndex = 0;

    const QString baseSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();
    const QString preferredSymbol = ibkrSymbol.trimmed().isEmpty() ? baseSymbol : ibkrSymbol.trimmed();
    const QString preferredLocalSymbol = localSymbol.trimmed().isEmpty() ? baseSymbol : localSymbol.trimmed();
    auto appendCandidate = [this](const QString &candidateSymbol,
                                  const QString &candidateCurrency,
                                  const QString &candidateExchange,
                                  bool useIsin,
                                  bool directExchange) {
        const QString cleanSymbol = candidateSymbol.trimmed();
        if (cleanSymbol.isEmpty())
            return;
        const QString entry = QStringList{
            cleanSymbol,
            candidateCurrency.trimmed(),
            candidateExchange.trimmed(),
            useIsin ? QStringLiteral("1") : QStringLiteral("0"),
            directExchange ? QStringLiteral("1") : QStringLiteral("0")
        }.join(QLatin1Char('\t'));
        if (!m_pendingIbkrNameCheckCandidates.contains(entry))
            m_pendingIbkrNameCheckCandidates << entry;
    };

    if (!ibkrNameCheckIsinOverride(symbol).isEmpty()) {
        appendCandidate(preferredSymbol, currency, exchange, true, false);
        appendCandidate(preferredSymbol, currency, QString(), true, false);
        return;
    }

    if (!m_pendingIbkrNameCheckHasConId && !m_pendingIbkrNameCheckIsin.isEmpty())
        appendCandidate(preferredSymbol, QString(), QString(), true, false);
    appendCandidate(preferredSymbol, currency, exchange, false, !exchange.trimmed().isEmpty());
    if (m_pendingIbkrNameCheckHasConId && !m_pendingIbkrNameCheckIsin.isEmpty())
        appendCandidate(preferredSymbol, QString(), QString(), true, false);
    appendCandidate(preferredSymbol, currency, QString(), false, false);

    const QString suffix = symbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();
    if (suffix == QStringLiteral("XFRA") || suffix == QStringLiteral("FRA")
        || suffix == QStringLiteral("XETR")) {
        const QStringList germanExchanges = {
            QStringLiteral("FWB2"), QStringLiteral("FWB"), QStringLiteral("IBIS"),
            QStringLiteral("GETTEX"), QStringLiteral("SWB")
        };
        for (const QString &germanExchange : germanExchanges) {
            appendCandidate(preferredLocalSymbol, currency, germanExchange, false, true);
            if (preferredSymbol.compare(preferredLocalSymbol, Qt::CaseInsensitive) != 0)
                appendCandidate(preferredSymbol, currency, germanExchange, false, true);
        }
        appendCandidate(preferredLocalSymbol, currency, QString(), false, false);
    }
}

bool DatabaseManager::startNextIbkrNameCheckCandidate(const QString &lastError)
{
    if (m_pendingIbkrNameCheckCandidateIndex >= m_pendingIbkrNameCheckCandidates.size())
        return false;

    const QString entry = m_pendingIbkrNameCheckCandidates.at(m_pendingIbkrNameCheckCandidateIndex++);
    const QStringList parts = entry.split(QLatin1Char('\t'));
    if (parts.size() < 5)
        return startNextIbkrNameCheckCandidate(lastError);

    if (!lastError.trimmed().isEmpty()) {
        setIbkrConnectionState(
            QStringLiteral("IBKR-Namenspruefung: %1 Fallback %2/%3 nach: %4")
                .arg(m_pendingIbkrNameCheckSymbol)
                .arg(m_pendingIbkrNameCheckCandidateIndex)
                .arg(m_pendingIbkrNameCheckCandidates.size())
                .arg(lastError.left(120)),
            m_ibkrConnected,
            false);
    }

    startIbkrNameCheckRequest(m_pendingIbkrNameCheckSymbol,
                              parts.value(0),
                              0,
                              parts.value(1),
                              parts.value(2),
                              parts.value(3) == QStringLiteral("1"),
                              parts.value(4) == QStringLiteral("1"));
    return true;
}

void DatabaseManager::startIbkrNameCheckRequest(const QString &requestSymbol,
                                                const QString &ibkrSymbol,
                                                qint64 conId,
                                                const QString &currency,
                                                const QString &exchange,
                                                bool useIsin,
                                                bool directExchange)
{
    Q_UNUSED(conId)
    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        finishIbkrNameCheckBatch(QStringLiteral("Fehler: Der IBKR-Helfer fehlt im Build-Verzeichnis."));
        return;
    }

    m_ibkrDataLoading = true;
    m_pendingIbkrProcessIsNameCheck = true;
    m_pendingIbkrNameCheckRequestUsesIsin = useIsin && !m_pendingIbkrNameCheckIsin.isEmpty();
    const qint64 clientIdSeed = (QDateTime::currentMSecsSinceEpoch() / 1000)
                                + m_ibkrNameCheckBatchIndex;
    const int clientId = 60000 + int(clientIdSeed % 5000);
    QStringList arguments = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
        QStringLiteral("--client-id"), QString::number(clientId),
        QStringLiteral("--symbol"), ibkrSymbol.isEmpty() ? requestSymbol : ibkrSymbol
    };
    if (useIsin && !m_pendingIbkrNameCheckIsin.isEmpty())
        arguments << QStringLiteral("--isin") << m_pendingIbkrNameCheckIsin;
    if (m_pendingIbkrNameCheckRequestUsesIsin
        && !ibkrNameCheckIsinOverride(requestSymbol).isEmpty()) {
        arguments << QStringLiteral("--isin-only");
    }
    if (!currency.isEmpty())
        arguments << QStringLiteral("--currency") << currency;
    if (!exchange.isEmpty())
        arguments << QStringLiteral("--exchange") << exchange;
    if (directExchange && !exchange.isEmpty())
        arguments << QStringLiteral("--direct-exchange");

    m_ibkrProcess.setProgram(helperPath);
    m_ibkrProcess.setArguments(arguments);
    m_ibkrProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrProcess.start();
    m_ibkrDataTimeout.start();
    emit ibkrConnectionChanged();
}

void DatabaseManager::finishIbkrNameCheckRequest(const QJsonObject &result)
{
    const QString symbol = m_pendingIbkrNameCheckSymbol;
    const QString databaseName = m_pendingIbkrNameCheckName;
    const QString databaseIsin = m_pendingIbkrNameCheckIsin;
    const bool usesListIsin = m_pendingIbkrNameCheckRequestUsesIsin
                              && !ibkrNameCheckIsinOverride(symbol).isEmpty();

    const QString message = result.value(QStringLiteral("message")).toString();
    if (!result.value(QStringLiteral("success")).toBool()) {
        const bool missingContract =
            message.contains(QStringLiteral("Fehler 200"), Qt::CaseInsensitive)
            || message.contains(QStringLiteral("keine Wertpapierdefinition"), Qt::CaseInsensitive);
        if (missingContract && startNextIbkrNameCheckCandidate(message))
            return;

        m_pendingIbkrProcessIsNameCheck = false;
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        m_pendingIbkrNameCheckCandidates.clear();
        m_pendingIbkrNameCheckCandidateIndex = 0;
        ++m_ibkrNameCheckBatchFailureCount;
        if (usesListIsin) {
            deleteStockWithReferencedData(symbol);
        } else {
            markIbkrValidationIssue(symbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung fehlgeschlagen: %1").arg(message));
        }
        scheduleNextIbkrNameCheckBatchSymbol(800);
        return;
    }

    const QJsonObject data = result.value(QStringLiteral("data")).toObject();
    QString ibkrName = data.value(QStringLiteral("longName")).toString().trimmed();
    if (ibkrName.isEmpty())
        ibkrName = data.value(QStringLiteral("marketName")).toString().trimmed();
    if (ibkrName.isEmpty())
        ibkrName = data.value(QStringLiteral("symbol")).toString().trimmed();
    const QString ibkrIsin = data.value(QStringLiteral("isin")).toString().trimmed().toUpper();

    if (usesListIsin && ibkrIsin != databaseIsin) {
        ++m_ibkrNameCheckBatchFailureCount;
        deleteStockWithReferencedData(symbol);
        m_pendingIbkrProcessIsNameCheck = false;
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        m_pendingIbkrNameCheckCandidates.clear();
        m_pendingIbkrNameCheckCandidateIndex = 0;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    }

    QSqlQuery query(db);
    if (!ibkrIsin.isEmpty()) {
        QSqlQuery duplicateQuery(db);
        duplicateQuery.prepare(R"SQL(
            SELECT "Symbol"
            FROM "Stocks"
            WHERE "ISIN" = :isin
              AND "Symbol" <> :symbol
            ORDER BY "Symbol"
            LIMIT 5
        )SQL");
        duplicateQuery.bindValue(QStringLiteral(":isin"), ibkrIsin);
        duplicateQuery.bindValue(QStringLiteral(":symbol"), symbol);
        if (!duplicateQuery.exec()) {
            qWarning() << "IBKR-Namenspruefung ISIN-Dublettenpruefung fehlgeschlagen:"
                       << duplicateQuery.lastError().text() << symbol << ibkrIsin;
        } else {
            QStringList duplicateSymbols;
            while (duplicateQuery.next())
                duplicateSymbols << duplicateQuery.value(0).toString();
            if (!duplicateSymbols.isEmpty()) {
                const QString validationMessage =
                    QStringLiteral("IBKR lieferte ISIN %1, diese existiert bereits bei %2. Daten wurden nicht uebernommen.")
                        .arg(ibkrIsin, duplicateSymbols.join(QStringLiteral(", ")));
                ++m_ibkrNameCheckBatchFailureCount;
                if (usesListIsin) {
                    deleteStockWithReferencedData(symbol);
                } else {
                    markIbkrValidationIssue(symbol,
                                            QStringLiteral("duplicate_isin"),
                                            validationMessage);
                }
                m_pendingIbkrProcessIsNameCheck = false;
                m_pendingIbkrNameCheckSymbol.clear();
                m_pendingIbkrNameCheckName.clear();
                m_pendingIbkrNameCheckIsin.clear();
                m_pendingIbkrNameCheckHasConId = false;
                m_pendingIbkrNameCheckRequestUsesIsin = false;
                m_pendingIbkrNameCheckCandidates.clear();
                m_pendingIbkrNameCheckCandidateIndex = 0;
                scheduleNextIbkrNameCheckBatchSymbol(600);
                return;
            }
        }

        QVariantMap details = data.toVariantMap();
        details.insert(QStringLiteral("name"), ibkrName);
        details.insert(QStringLiteral("validationStatus"),
                       usesListIsin ? QStringLiteral("verified_isin") : QStringLiteral("verified_name"));
        details.insert(QStringLiteral("validationMessage"),
                       usesListIsin
                           ? QStringLiteral("IBKR-Stammdaten per Listen-ISIN %1 uebernommen.")
                                 .arg(databaseIsin)
                           : QString());
        if (!saveIbkrContractDetails(symbol, details)) {
            ++m_ibkrNameCheckBatchFailureCount;
            if (usesListIsin) {
                deleteStockWithReferencedData(symbol);
            } else {
                markIbkrValidationIssue(symbol, QStringLiteral("name_mismatch"),
                                        QStringLiteral("IBKR-Namenspruefung: Datenbankfehler beim Uebernehmen der ISIN"));
            }
        } else {
            ++m_ibkrNameCheckBatchSuccessCount;
            emit ibkrStockDataUpdated(symbol);
        }
        m_pendingIbkrProcessIsNameCheck = false;
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        m_pendingIbkrNameCheckCandidates.clear();
        m_pendingIbkrNameCheckCandidateIndex = 0;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    }

    if (!databaseIsin.isEmpty() && !ibkrIsin.isEmpty() && databaseIsin != ibkrIsin) {
        query.prepare(R"SQL(
            UPDATE "Stocks"
            SET "IBKRValidationStatus" = 'name_mismatch',
                "IBKRValidationMessage" = :message,
                "IBKRValidationAt" = CURRENT_TIMESTAMP,
                "IBKRLastError" = LEFT(:message, 500),
                "IBKRFailureCount" = 0
            WHERE "Symbol" = :symbol
        )SQL");
        query.bindValue(QStringLiteral(":message"),
                        QStringLiteral("ISIN-Abweichung: DB=\"%1\", IBKR=\"%2\", DB-Name=\"%3\", IBKR=\"%4\"")
                            .arg(databaseIsin, ibkrIsin, databaseName, ibkrName));
        ++m_ibkrNameCheckBatchFailureCount;
    } else if (!databaseIsin.isEmpty() && databaseIsin == ibkrIsin) {
        QVariantMap details = data.toVariantMap();
        details.insert(QStringLiteral("name"), ibkrName);
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("verified_name"));
        details.insert(QStringLiteral("validationMessage"),
                       databaseName.compare(ibkrName, Qt::CaseInsensitive) == 0
                           ? QStringLiteral("ISIN und IBKR-Stammdaten bestaetigt: %1, Name=\"%2\"")
                                 .arg(databaseIsin, ibkrName)
                           : QStringLiteral("ISIN gleich, IBKR-Stammdaten uebernommen: %1, Name \"%2\" -> \"%3\"")
                                 .arg(databaseIsin, databaseName, ibkrName));
        if (!saveIbkrContractDetails(symbol, details)) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(symbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung: Datenbankfehler beim Uebernehmen gleicher ISIN"));
        } else {
            ++m_ibkrNameCheckBatchSuccessCount;
            emit ibkrStockDataUpdated(symbol);
        }
        m_pendingIbkrProcessIsNameCheck = false;
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckCandidates.clear();
        m_pendingIbkrNameCheckCandidateIndex = 0;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    } else if (companyNamesLookCompatible(databaseName, ibkrName)) {
        query.prepare(R"SQL(
            UPDATE "Stocks"
            SET "IBKRValidationStatus" = 'verified_name',
                "IBKRValidationMessage" = :message,
                "IBKRValidationAt" = CURRENT_TIMESTAMP,
                "IBKRLastError" = NULL,
                "IBKRFailureCount" = 0
            WHERE "Symbol" = :symbol
        )SQL");
        const QString okMessage = (!databaseIsin.isEmpty() && databaseIsin == ibkrIsin)
                                      ? QStringLiteral("ISIN und Name OK: %1, DB-Name=\"%2\", IBKR=\"%3\"")
                                            .arg(databaseIsin, databaseName, ibkrName)
                                      : QStringLiteral("Namenspruefung OK: DB=\"%1\", IBKR=\"%2\"")
                                            .arg(databaseName, ibkrName);
        query.bindValue(QStringLiteral(":message"), okMessage);
        ++m_ibkrNameCheckBatchSuccessCount;
    } else {
        query.prepare(R"SQL(
            UPDATE "Stocks"
            SET "IBKRValidationStatus" = 'name_mismatch',
                "IBKRValidationMessage" = :message,
                "IBKRValidationAt" = CURRENT_TIMESTAMP,
                "IBKRLastError" = LEFT(:message, 500),
                "IBKRFailureCount" = 0
            WHERE "Symbol" = :symbol
        )SQL");
        const QString mismatchMessage = (!databaseIsin.isEmpty() && databaseIsin == ibkrIsin)
                                            ? QStringLiteral("Name trotz gleicher ISIN pruefen: %1, DB=\"%2\", IBKR=\"%3\"")
                                                  .arg(databaseIsin, databaseName, ibkrName)
                                            : QStringLiteral("Namensabweichung: DB=\"%1\", IBKR=\"%2\"")
                                                  .arg(databaseName, ibkrName);
        query.bindValue(QStringLiteral(":message"), mismatchMessage);
        ++m_ibkrNameCheckBatchFailureCount;
    }
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec())
        qWarning() << "IBKR-Namenspruefung konnte nicht gespeichert werden:" << query.lastError().text() << symbol;

    m_pendingIbkrProcessIsNameCheck = false;
    m_pendingIbkrNameCheckSymbol.clear();
    m_pendingIbkrNameCheckName.clear();
    m_pendingIbkrNameCheckIsin.clear();
    m_pendingIbkrNameCheckHasConId = false;
    m_pendingIbkrNameCheckCandidates.clear();
    m_pendingIbkrNameCheckCandidateIndex = 0;
    scheduleNextIbkrNameCheckBatchSymbol(600);
}

bool DatabaseManager::markIbkrValidationIssue(const QString &symbol,
                                              const QString &status,
                                              const QString &message)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRValidationStatus" = :status,
            "IBKRValidationMessage" = :message,
            "IBKRValidationAt" = CURRENT_TIMESTAMP,
            "IBKRLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRLastError" = LEFT(:message, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":status"), status);
    query.bindValue(QStringLiteral(":message"), message);
    if (!query.exec()) {
        qWarning() << "IBKR-Pruefhinweis konnte nicht gespeichert werden:"
                   << query.lastError().text() << symbol;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteStockWithReferencedData(const QString &symbol)
{
    const QString normalizedSymbol = symbol.trimmed().toUpper();
    if (normalizedSymbol.isEmpty())
        return false;

    if (!db.transaction()) {
        qWarning() << "Aktie konnte nicht zum Loeschen gesperrt werden:"
                   << db.lastError().text() << normalizedSymbol;
        return false;
    }

    auto tableExists = [this](const QString &tableName) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT to_regclass(:table_name) IS NOT NULL"));
        query.bindValue(QStringLiteral(":table_name"),
                        QStringLiteral("\"%1\"").arg(tableName));
        if (!query.exec() || !query.next())
            return true;
        return query.value(0).toBool();
    };

    auto deleteFrom = [this, &normalizedSymbol](const QString &sql) {
        QSqlQuery query(db);
        query.prepare(sql);
        query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        if (!query.exec()) {
            qWarning() << "Referenzdaten konnten nicht geloescht werden:"
                       << query.lastError().text() << normalizedSymbol;
            return false;
        }
        return true;
    };

    struct DeleteStatement {
        QString tableName;
        QString sql;
    };
    const DeleteStatement deleteStatements[] = {
        {QStringLiteral("StockFundamentals"), QStringLiteral(R"SQL(DELETE FROM "StockFundamentals" WHERE "Symbol" = :symbol)SQL")},
        {QStringLiteral("BoughtStocks"), QStringLiteral(R"SQL(DELETE FROM "BoughtStocks" WHERE "Symbol" = :symbol)SQL")},
        {QStringLiteral("Stocks_IBKRConflictBackup"), QStringLiteral(R"SQL(DELETE FROM "Stocks_IBKRConflictBackup" WHERE "Symbol" = :symbol)SQL")},
        {QStringLiteral("Quotes"), QStringLiteral(R"SQL(DELETE FROM "Quotes" WHERE "Symbol" = :symbol)SQL")},
        {QStringLiteral("Stocks"), QStringLiteral(R"SQL(DELETE FROM "Stocks" WHERE "Symbol" = :symbol)SQL")}
    };

    for (const DeleteStatement &statement : deleteStatements) {
        if (!tableExists(statement.tableName))
            continue;
        if (!deleteFrom(statement.sql)) {
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qWarning() << "Aktie konnte nicht geloescht werden:"
                   << db.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    emit ibkrStockDataUpdated(normalizedSymbol);
    return true;
}

bool DatabaseManager::saveIbkrContractDetails(const QString &symbol, const QVariantMap &details)
{
    if (!db.transaction()) {
        qCritical() << "IBKR-Update konnte keine Transaktion starten:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "Stocks" (
            "Symbol", "MIC", "Name", "Exchange", "CountryCode", "LastUpdateDate",
            "ISIN", "IBKRConId", "IBKRResolvedSymbol", "Currency", "PrimaryExchange",
            "LocalSymbol", "SecurityType", "TradingClass", "StockType", "Industry",
            "Category", "Subcategory", "TimeZoneId", "TradingHours", "LiquidHours",
            "MinTick", "MarketRuleIds", "ValidExchanges", "OrderTypes", "MarketName",
            "CUSIP", "IBKRValidationStatus", "IBKRValidationMessage", "IBKRValidationAt",
            "IBKRLastAttemptAt", "IBKRFailureCount", "IBKRLastError", "IBKRLastSyncAt"
        )
        VALUES (
            :symbol, :mic, COALESCE(NULLIF(:name, ''), :symbol),
            :exchange, NULLIF(LEFT(:isin, 2), ''),
            CURRENT_DATE, NULLIF(:isin, ''), :ibkrConId, NULLIF(:ibkrResolvedSymbol, ''),
            NULLIF(:currency, ''), NULLIF(:primaryExchange, ''), NULLIF(:localSymbol, ''),
            NULLIF(:securityType, ''), NULLIF(:tradingClass, ''), NULLIF(:stockType, ''),
            NULLIF(:industry, ''), NULLIF(:category, ''), NULLIF(:subcategory, ''),
            NULLIF(:timeZoneId, ''), NULLIF(:tradingHours, ''), NULLIF(:liquidHours, ''),
            :minTick, NULLIF(:marketRuleIds, ''), NULLIF(:validExchanges, ''),
            NULLIF(:orderTypes, ''), NULLIF(:marketName, ''), NULLIF(:cusip, ''),
            NULLIF(:validationStatus, ''), :validationMessage, CURRENT_TIMESTAMP,
            CURRENT_TIMESTAMP, 0, NULL, CURRENT_TIMESTAMP
        )
        ON CONFLICT ("Symbol") DO UPDATE
        SET
            "Name" = CASE
                WHEN NULLIF(:name, '') IS NOT NULL THEN EXCLUDED."Name"
                ELSE "Stocks"."Name"
            END,
            "IBKRConId" = EXCLUDED."IBKRConId",
            "IBKRResolvedSymbol" = COALESCE(EXCLUDED."IBKRResolvedSymbol", "Stocks"."IBKRResolvedSymbol"),
            "Currency" = COALESCE(EXCLUDED."Currency", "Stocks"."Currency"),
            "PrimaryExchange" = COALESCE(EXCLUDED."PrimaryExchange", "Stocks"."PrimaryExchange"),
            "LocalSymbol" = COALESCE(EXCLUDED."LocalSymbol", "Stocks"."LocalSymbol"),
            "SecurityType" = COALESCE(EXCLUDED."SecurityType", "Stocks"."SecurityType"),
            "TradingClass" = COALESCE(EXCLUDED."TradingClass", "Stocks"."TradingClass"),
            "StockType" = COALESCE(EXCLUDED."StockType", "Stocks"."StockType"),
            "Industry" = COALESCE(EXCLUDED."Industry", "Stocks"."Industry"),
            "Category" = COALESCE(EXCLUDED."Category", "Stocks"."Category"),
            "Subcategory" = COALESCE(EXCLUDED."Subcategory", "Stocks"."Subcategory"),
            "TimeZoneId" = COALESCE(EXCLUDED."TimeZoneId", "Stocks"."TimeZoneId"),
            "TradingHours" = COALESCE(EXCLUDED."TradingHours", "Stocks"."TradingHours"),
            "LiquidHours" = COALESCE(EXCLUDED."LiquidHours", "Stocks"."LiquidHours"),
            "MinTick" = COALESCE(EXCLUDED."MinTick", "Stocks"."MinTick"),
            "MarketRuleIds" = COALESCE(EXCLUDED."MarketRuleIds", "Stocks"."MarketRuleIds"),
            "ValidExchanges" = COALESCE(EXCLUDED."ValidExchanges", "Stocks"."ValidExchanges"),
            "OrderTypes" = COALESCE(EXCLUDED."OrderTypes", "Stocks"."OrderTypes"),
            "MarketName" = COALESCE(EXCLUDED."MarketName", "Stocks"."MarketName"),
            "CUSIP" = COALESCE(EXCLUDED."CUSIP", "Stocks"."CUSIP"),
            "ISIN" = COALESCE(EXCLUDED."ISIN", "Stocks"."ISIN"),
            "IBKRValidationStatus" = COALESCE(EXCLUDED."IBKRValidationStatus", "Stocks"."IBKRValidationStatus"),
            "IBKRValidationMessage" = EXCLUDED."IBKRValidationMessage",
            "IBKRValidationAt" = CURRENT_TIMESTAMP,
            "IBKRLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRFailureCount" = 0,
            "IBKRLastError" = NULL,
            "IBKRLastSyncAt" = CURRENT_TIMESTAMP
    )SQL");

    const QStringList textFields = {
        QStringLiteral("name"),
        QStringLiteral("currency"), QStringLiteral("primaryExchange"),
        QStringLiteral("localSymbol"), QStringLiteral("securityType"),
        QStringLiteral("tradingClass"), QStringLiteral("stockType"),
        QStringLiteral("industry"), QStringLiteral("category"),
        QStringLiteral("subcategory"), QStringLiteral("timeZoneId"),
        QStringLiteral("tradingHours"), QStringLiteral("liquidHours"),
        QStringLiteral("marketRuleIds"), QStringLiteral("validExchanges"),
        QStringLiteral("orderTypes"), QStringLiteral("marketName"),
        QStringLiteral("cusip"), QStringLiteral("isin"),
        QStringLiteral("validationStatus"), QStringLiteral("validationMessage")
    };
    query.bindValue(QStringLiteral(":symbol"), symbol);
    const QString marketCode = symbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();
    const QString fallbackMarket = marketCode.isEmpty() ? QStringLiteral("IBKR") : marketCode;
    query.bindValue(QStringLiteral(":mic"), fallbackMarket);
    query.bindValue(QStringLiteral(":exchange"), fallbackMarket);
    query.bindValue(QStringLiteral(":ibkrConId"), details.value(QStringLiteral("ibkrConId")));
    const QString ibkrResolvedSymbol = details.value(QStringLiteral("symbol")).toString().trimmed();
    query.bindValue(QStringLiteral(":ibkrResolvedSymbol"), ibkrResolvedSymbol);
    for (const QString &field : textFields)
        query.bindValue(QLatin1Char(':') + field, details.value(field).toString());
    const double minTick = details.value(QStringLiteral("minTick")).toDouble();
    query.bindValue(QStringLiteral(":minTick"), minTick > 0.0 ? QVariant(minTick) : QVariant());

    if (!query.exec() || query.numRowsAffected() != 1) {
        qCritical() << "IBKR-Stammdaten konnten nicht gespeichert werden:" << query.lastError().text();
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        qCritical() << "IBKR-Update konnte nicht abgeschlossen werden:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

void DatabaseManager::updateIbkrBatchFailure(const QString &symbol, const QString &error)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRFailureCount" = COALESCE("IBKRFailureCount", 0) + 1,
            "IBKRLastError" = LEFT(:error, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":error"), error);
    if (!query.exec()) {
        qWarning() << "IBKR-Batch-Fehler konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::updateIbkrBatchSuccess(const QString &symbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRFailureCount" = 0,
            "IBKRLastError" = NULL
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec()) {
        qWarning() << "IBKR-Batch-Erfolg konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}
