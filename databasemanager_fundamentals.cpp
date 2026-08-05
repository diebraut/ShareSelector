#include "databasemanager.h"
#include "databasemanager_internal.h"

#include <QDate>
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>

using namespace DatabaseManagerInternal;

void DatabaseManager::getAlphaVantageFundamentals(const QString &symbol)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || m_fundamentalDataLoading)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        resetFundamentalRequestState();
        return;
    }

    resetFundamentalRequestState();
    m_pendingFundamentalSymbol = normalizedSymbol;
    fetchYahooFundamentalsFallback(normalizedSymbol);
}

void DatabaseManager::startYahooFundamentalsBatch()
{
    if (m_fundamentalDataLoading || m_yahooFundamentalsBatchActive)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT s."Symbol"
        FROM "Stocks" s
        WHERE COALESCE(s."Symbol", '') <> ''
          AND COALESCE(s."IBKRLastError", '') = ''
          AND COALESCE(s."IBKRValidationStatus", '') NOT IN (
              'duplicate_isin',
              'ambiguous_isin',
              'review_required',
              'name_mismatch'
          )
          AND (
              s."YahooFundamentalsLastSuccessAt" IS NULL
              OR s."YahooFundamentalsLastSuccessAt" < CURRENT_TIMESTAMP - INTERVAL '30 days'
          )
          AND (
              COALESCE(s."YahooFundamentalsFailureCount", 0) < 3
              OR s."YahooFundamentalsLastAttemptAt" IS NULL
              OR s."YahooFundamentalsLastAttemptAt" < CURRENT_TIMESTAMP - INTERVAL '1 day'
          )
        ORDER BY
          CASE WHEN s."YahooFundamentalsLastSuccessAt" IS NULL THEN 0 ELSE 1 END,
          s."YahooFundamentalsLastAttemptAt" NULLS FIRST,
          s."Symbol"
    )SQL");

    if (!query.exec()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Yahoo-Batch konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            false);
        return;
    }

    m_yahooFundamentalsBatchSymbols.clear();
    while (query.next())
        m_yahooFundamentalsBatchSymbols << query.value(0).toString();

    if (m_yahooFundamentalsBatchSymbols.isEmpty()) {
        setFundamentalDataStatus(QStringLiteral("Yahoo-Batch: Keine faelligen Aktien gefunden."), false);
        return;
    }

    resetFundamentalRequestState();
    m_yahooFundamentalsBatchActive = true;
    m_yahooFundamentalsBatchIndex = 0;
    m_yahooFundamentalsBatchSuccessCount = 0;
    m_yahooFundamentalsBatchFailureCount = 0;
    setFundamentalDataStatus(
        QStringLiteral("Yahoo-Batch gestartet: %1 Aktien werden seriell aktualisiert.")
            .arg(m_yahooFundamentalsBatchSymbols.size()),
        true);
    scheduleNextYahooFundamentalsBatchSymbol(100);
}

void DatabaseManager::stopYahooFundamentalsBatch()
{
    if (!m_yahooFundamentalsBatchActive)
        return;

    m_yahooFundamentalsBatchTimer.stop();
    const int processed = qMax(0, m_yahooFundamentalsBatchIndex - 1);
    finishYahooFundamentalsBatch(
        QStringLiteral("Yahoo-Batch gestoppt: %1/%2 verarbeitet, %3 erfolgreich, %4 fehlgeschlagen.")
            .arg(processed)
            .arg(m_yahooFundamentalsBatchSymbols.size())
            .arg(m_yahooFundamentalsBatchSuccessCount)
            .arg(m_yahooFundamentalsBatchFailureCount));
}

bool DatabaseManager::reserveAlphaVantageRequest()
{
    if (!db.transaction()) {
        qCritical() << "Alpha-Vantage-Limit konnte keine Transaktion starten:"
                    << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "ApiDailyUsage" ("Provider", "UsageDate", "RequestCount")
        VALUES ('AlphaVantage', CURRENT_DATE, 1)
        ON CONFLICT ("Provider", "UsageDate") DO UPDATE SET
            "RequestCount" = "ApiDailyUsage"."RequestCount" + 1,
            "UpdatedAt" = CURRENT_TIMESTAMP
        WHERE "ApiDailyUsage"."RequestCount" < 25
        RETURNING "RequestCount"
    )SQL");

    if (!query.exec() || !query.next()) {
        if (query.lastError().isValid())
            qCritical() << "Alpha-Vantage-Limit konnte nicht aktualisiert werden:"
                        << query.lastError().text();
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Alpha-Vantage-Limit konnte nicht abgeschlossen werden:"
                    << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool DatabaseManager::cacheAlphaVantageSymbol(const QString &symbol, const QString &alphaVantageSymbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "AlphaVantageSymbol" = :alphaVantageSymbol
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":alphaVantageSymbol"), alphaVantageSymbol.trimmed());
    if (!query.exec()) {
        qCritical() << "Alpha-Vantage-Symbol konnte nicht gespeichert werden:"
                    << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool DatabaseManager::cacheYahooSymbol(const QString &symbol, const QString &yahooSymbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "YahooSymbol" = :yahooSymbol
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":yahooSymbol"), yahooSymbol.trimmed());
    if (!query.exec()) {
        qCritical() << "Yahoo-Symbol konnte nicht gespeichert werden:"
                    << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

void DatabaseManager::updateYahooFundamentalAttempt(const QString &symbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "YahooFundamentalsLastAttemptAt" = CURRENT_TIMESTAMP
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec()) {
        qWarning() << "Yahoo-Batch-Versuch konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::updateYahooFundamentalSuccess(const QString &symbol, int qualityScore)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "YahooFundamentalsLastSuccessAt" = CURRENT_TIMESTAMP,
            "YahooFundamentalsLastAttemptAt" = CURRENT_TIMESTAMP,
            "YahooFundamentalsFailureCount" = 0,
            "YahooFundamentalsLastError" = NULL,
            "YahooFundamentalsQualityScore" = :qualityScore
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":qualityScore"), qualityScore);
    if (!query.exec()) {
        qWarning() << "Yahoo-Batch-Erfolg konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::updateYahooFundamentalFailure(const QString &symbol, const QString &error)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "YahooFundamentalsLastAttemptAt" = CURRENT_TIMESTAMP,
            "YahooFundamentalsFailureCount" = COALESCE("YahooFundamentalsFailureCount", 0) + 1,
            "YahooFundamentalsLastError" = LEFT(:error, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":error"), error);
    if (!query.exec()) {
        qWarning() << "Yahoo-Batch-Fehler konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::fetchAlphaVantageFundamentalOverview(const QString &symbol, const QString &apiSymbol)
{
    if (!reserveAlphaVantageRequest()) {
        setFundamentalDataStatus(
            QStringLiteral("Alpha-Vantage-Tageslimit erreicht: 25 Abrufe wurden heute bereits verwendet."),
            false);
        resetFundamentalRequestState();
        return;
    }

    m_pendingFundamentalSymbol = symbol;
    setFundamentalDataStatus(
        QStringLiteral("Alpha-Vantage-Fundamentaldaten fuer %1 (%2) werden abgerufen ... Noch %3 freie Abrufe heute.")
            .arg(symbol)
            .arg(apiSymbol)
            .arg(alphaVantageRequestsRemaining()),
        true);
    alphaVantageClient.fetchFundamentalOverview(apiSymbol);
}

void DatabaseManager::tryNextAlphaVantageCandidate()
{
    if (m_pendingFundamentalSymbol.isEmpty())
        return;

    if (m_pendingAlphaVantageCandidateIndex >= m_pendingAlphaVantageCandidates.size()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Kein Alpha-Vantage-Kandidat hat Fundamentaldaten geliefert."),
            false);
        resetFundamentalRequestState();
        return;
    }

    const QString symbol = m_pendingFundamentalSymbol;
    const QString apiSymbol = m_pendingAlphaVantageCandidates.at(m_pendingAlphaVantageCandidateIndex++);
    m_pendingResolvedAlphaVantageSymbol = apiSymbol;
    fetchAlphaVantageFundamentalOverview(symbol, apiSymbol);
}

void DatabaseManager::fetchYahooFundamentalsFallback(const QString &symbol)
{
    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "YahooSymbol", "ISIN", "Name", "MIC", "PrimaryExchange",
               "IBKRResolvedSymbol", "LocalSymbol", "TradingClass", "ValidExchanges"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), symbol);
    if (!stockQuery.exec() || !stockQuery.next()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Die ausgewaehlte Aktie wurde nicht in der Datenbank gefunden."),
            false);
        resetFundamentalRequestState();
        return;
    }

    const QString name = stockQuery.value(QStringLiteral("Name")).toString();
    QStringList exchangeValues;
    appendUniqueSymbol(exchangeValues, stockQuery.value(QStringLiteral("MIC")).toString());
    appendUniqueSymbol(exchangeValues, stockQuery.value(QStringLiteral("PrimaryExchange")).toString());
    const QStringList validExchanges = stockQuery.value(QStringLiteral("ValidExchanges"))
                                           .toString()
                                           .split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &exchange : validExchanges)
        appendUniqueSymbol(exchangeValues, exchange);

    QStringList ibkrSymbols;
    appendUniqueSymbol(ibkrSymbols, stockQuery.value(QStringLiteral("IBKRResolvedSymbol")).toString());
    appendUniqueSymbol(ibkrSymbols, stockQuery.value(QStringLiteral("LocalSymbol")).toString());
    const QString tradingClass = stockQuery.value(QStringLiteral("TradingClass")).toString();
    if (!isGenericIbkrTradingClass(tradingClass))
        appendUniqueSymbol(ibkrSymbols, tradingClass);
    appendUniqueSymbol(ibkrSymbols, symbol.section(QLatin1Char('.'), 0, 0));

    m_pendingFundamentalSymbol = symbol;
    m_pendingFundamentalSearchKeywords = alphaVantageSearchKeywords(name, symbol);
    m_pendingPreferredYahooSuffix = preferredYahooSuffix(exchangeValues);
    m_pendingYahooCandidates = yahooCandidateSymbols(
        symbol,
        stockQuery.value(QStringLiteral("YahooSymbol")).toString(),
        m_pendingPreferredYahooSuffix,
        ibkrSymbols);
    m_pendingYahooCandidateIndex = 0;
    m_pendingYahooSearchStarted = false;
    m_pendingYahooLastError.clear();
    m_pendingYahooBestSymbol.clear();
    m_pendingYahooBestData.clear();
    m_pendingYahooBestScore = 0;
    m_pendingYahooSymbol.clear();
    m_pendingResolvedAlphaVantageSymbol.clear();
    m_pendingAlphaVantageCandidates.clear();
    m_pendingAlphaVantageCandidateIndex = 0;
    tryNextYahooCandidate();
}

void DatabaseManager::tryNextYahooCandidate()
{
    if (m_pendingFundamentalSymbol.isEmpty())
        return;

    while (m_pendingYahooCandidateIndex < m_pendingYahooCandidates.size()) {
        const QString yahooSymbol = m_pendingYahooCandidates.at(m_pendingYahooCandidateIndex++).trimmed();
        if (yahooSymbol.isEmpty())
            continue;

        m_pendingYahooSymbol = yahooSymbol;
        setFundamentalDataStatus(
            QStringLiteral("Yahoo-Fundamentaldaten fuer %1 (%2) werden abgerufen ...")
                .arg(m_pendingFundamentalSymbol)
                .arg(yahooSymbol),
            true);
        yahooFinanceClient.fetchFundamentals(m_pendingFundamentalSymbol, yahooSymbol);
        return;
    }

    if (!m_pendingYahooSearchStarted && !m_pendingFundamentalSearchKeywords.isEmpty()) {
        m_pendingYahooSearchStarted = true;
        setFundamentalDataStatus(
            QStringLiteral("Yahoo-Symbolsuche fuer %1 mit \"%2\" laeuft ...")
                .arg(m_pendingFundamentalSymbol)
                .arg(m_pendingFundamentalSearchKeywords),
            true);
        yahooFinanceClient.resolveSymbol(m_pendingFundamentalSymbol, m_pendingFundamentalSearchKeywords);
        return;
    }

    const QString checkedSymbols = m_pendingYahooCandidates.isEmpty()
        ? QStringLiteral("keine")
        : m_pendingYahooCandidates.join(QStringLiteral(", "));
    if (!m_pendingYahooBestSymbol.isEmpty() && !m_pendingYahooBestData.isEmpty()) {
        const QString symbol = m_pendingFundamentalSymbol;
        const QString yahooSymbol = m_pendingYahooBestSymbol;
        const int score = m_pendingYahooBestScore;
        const QVariantMap data = m_pendingYahooBestData;
        if (saveYahooFundamentals(symbol, yahooSymbol, data)) {
            updateYahooFundamentalSuccess(symbol, score);
            if (m_yahooFundamentalsBatchActive) {
                ++m_yahooFundamentalsBatchSuccessCount;
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Batch: %1 gespeichert (%2, nur %3 Kennzahlen). Erfolgreich: %4, Fehler: %5.")
                        .arg(symbol)
                        .arg(yahooSymbol)
                        .arg(score)
                        .arg(m_yahooFundamentalsBatchSuccessCount)
                        .arg(m_yahooFundamentalsBatchFailureCount),
                    true);
                emit fundamentalDataUpdated(symbol);
                resetFundamentalRequestState();
                scheduleNextYahooFundamentalsBatchSymbol(1500);
                return;
            }
            setFundamentalDataStatus(
                QStringLiteral("Yahoo-Fundamentaldaten fuer %1 (%2) wurden gespeichert, aber Yahoo lieferte nur %3 Kennzahlen. Das Symbol wurde deshalb nicht als Standard gespeichert. Gepruefte Kandidaten: %4")
                    .arg(symbol)
                    .arg(yahooSymbol)
                    .arg(score)
                    .arg(checkedSymbols),
                false);
            emit fundamentalDataUpdated(symbol);
        } else {
            if (m_yahooFundamentalsBatchActive) {
                ++m_yahooFundamentalsBatchFailureCount;
                updateYahooFundamentalFailure(symbol, QStringLiteral("Yahoo-Fundamentaldaten konnten nicht gespeichert werden."));
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Batch: %1 konnte nicht gespeichert werden. Erfolgreich: %2, Fehler: %3.")
                        .arg(symbol)
                        .arg(m_yahooFundamentalsBatchSuccessCount)
                        .arg(m_yahooFundamentalsBatchFailureCount),
                    true);
                resetFundamentalRequestState();
                scheduleNextYahooFundamentalsBatchSymbol(3000);
                return;
            }
            setFundamentalDataStatus(
                QStringLiteral("Fehler: Yahoo-Fundamentaldaten konnten nicht gespeichert werden."),
                false);
        }
        resetFundamentalRequestState();
        return;
    }

    const QString lastError = m_pendingYahooLastError.isEmpty()
        ? QStringLiteral("keine nutzbaren Kennzahlen gefunden")
        : m_pendingYahooLastError;
    if (m_yahooFundamentalsBatchActive) {
        const QString failedSymbol = m_pendingFundamentalSymbol;
        ++m_yahooFundamentalsBatchFailureCount;
        updateYahooFundamentalFailure(
            failedSymbol,
            QStringLiteral("Gepruefte Kandidaten: %1. Letzter Fehler: %2")
                .arg(checkedSymbols)
                .arg(lastError));
        setFundamentalDataStatus(
            QStringLiteral("Yahoo-Batch: %1 ohne Fundamentaldaten. Erfolgreich: %2, Fehler: %3.")
                .arg(failedSymbol)
                .arg(m_yahooFundamentalsBatchSuccessCount)
                .arg(m_yahooFundamentalsBatchFailureCount),
            true);
        resetFundamentalRequestState();
        scheduleNextYahooFundamentalsBatchSymbol(3000);
        return;
    }
    setFundamentalDataStatus(
        QStringLiteral("Fehler: Auch Yahoo lieferte fuer %1 keine Fundamentaldaten. Gepruefte Kandidaten: %2. Letzter Fehler: %3")
            .arg(m_pendingFundamentalSymbol)
            .arg(checkedSymbols)
            .arg(lastError),
        false);
    resetFundamentalRequestState();
}

void DatabaseManager::loadNextYahooFundamentalsBatchSymbol()
{
    if (!m_yahooFundamentalsBatchActive)
        return;

    if (m_yahooFundamentalsBatchIndex >= m_yahooFundamentalsBatchSymbols.size()) {
        finishYahooFundamentalsBatch(
            QStringLiteral("Yahoo-Batch abgeschlossen: %1 Aktien, %2 erfolgreich, %3 fehlgeschlagen.")
                .arg(m_yahooFundamentalsBatchSymbols.size())
                .arg(m_yahooFundamentalsBatchSuccessCount)
                .arg(m_yahooFundamentalsBatchFailureCount));
        return;
    }

    const QString symbol = m_yahooFundamentalsBatchSymbols.at(m_yahooFundamentalsBatchIndex++).trimmed();
    if (symbol.isEmpty()) {
        scheduleNextYahooFundamentalsBatchSymbol(100);
        return;
    }

    updateYahooFundamentalAttempt(symbol);
    setFundamentalDataStatus(
        QStringLiteral("Yahoo-Batch: %1/%2 %3 wird aktualisiert ... Erfolgreich: %4, Fehler: %5")
            .arg(m_yahooFundamentalsBatchIndex)
            .arg(m_yahooFundamentalsBatchSymbols.size())
            .arg(symbol)
            .arg(m_yahooFundamentalsBatchSuccessCount)
            .arg(m_yahooFundamentalsBatchFailureCount),
        true);
    fetchYahooFundamentalsFallback(symbol);
}

void DatabaseManager::scheduleNextYahooFundamentalsBatchSymbol(int delayMs)
{
    if (!m_yahooFundamentalsBatchActive)
        return;
    m_yahooFundamentalsBatchTimer.start(delayMs);
}

void DatabaseManager::finishYahooFundamentalsBatch(const QString &message)
{
    m_yahooFundamentalsBatchTimer.stop();
    m_yahooFundamentalsBatchActive = false;
    resetFundamentalRequestState();
    setFundamentalDataStatus(message, false);
    emit fundamentalDataChanged();
}

void DatabaseManager::resetFundamentalRequestState()
{
    m_pendingFundamentalSymbol.clear();
    m_pendingFundamentalSearchKeywords.clear();
    m_pendingResolvedAlphaVantageSymbol.clear();
    m_pendingAlphaVantageCandidates.clear();
    m_pendingAlphaVantageCandidateIndex = 0;
    m_pendingYahooSymbol.clear();
    m_pendingPreferredYahooSuffix.clear();
    m_pendingYahooCandidates.clear();
    m_pendingYahooCandidateIndex = 0;
    m_pendingYahooSearchStarted = false;
    m_pendingYahooLastError.clear();
    m_pendingYahooBestSymbol.clear();
    m_pendingYahooBestData.clear();
    m_pendingYahooBestScore = 0;
}

bool DatabaseManager::saveAlphaVantageFundamentals(const QString &symbol, const QVariantMap &overview)
{
    if (!db.transaction()) {
        qCritical() << "Alpha-Vantage-Fundamentaldaten konnten keine Transaktion starten:"
                    << db.lastError().text();
        return false;
    }

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "IBKRConId", "Currency"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), symbol);
    if (!stockQuery.exec() || !stockQuery.next()) {
        qCritical() << "Aktie fuer Alpha-Vantage-Fundamentaldaten nicht gefunden:" << symbol;
        db.rollback();
        return false;
    }

    const QVariant revenue = alphaNumber(overview, QStringLiteral("RevenueTTM"));
    const QVariant profitMargin = alphaPercent(overview, QStringLiteral("ProfitMargin"));
    const QVariant evToRevenue = alphaNumber(overview, QStringLiteral("EVToRevenue"));
    const QVariant dividendYield = alphaPercent(overview, QStringLiteral("DividendYield"));

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "StockFundamentals" (
            "Symbol", "IBKRConId", "AsOfDate", "Currency",
            "MarketCapitalization", "EnterpriseValue", "PERatio", "ForwardPERatio",
            "PriceToBookRatio", "PriceToSalesRatio", "EPS", "ForwardEPS",
            "DividendPerShare", "DividendYield", "PayoutRatio", "Beta",
            "Revenue", "NetIncome", "EBITDA", "ReturnOnEquity", "ReturnOnAssets",
            "SharesOutstanding", "Week52High", "Week52Low", "Source", "RawData",
            "UpdatedAt"
        )
        VALUES (
            :symbol, :ibkrConId, CURRENT_DATE, :currency,
            :marketCapitalization, :enterpriseValue, :peRatio, :forwardPeRatio,
            :priceToBookRatio, :priceToSalesRatio, :eps, :forwardEps,
            :dividendPerShare, :dividendYield, :payoutRatio, :beta,
            :revenue, :netIncome, :ebitda, :returnOnEquity, :returnOnAssets,
            :sharesOutstanding, :week52High, :week52Low, 'AlphaVantage', CAST(:rawData AS jsonb),
            CURRENT_TIMESTAMP
        )
        ON CONFLICT ("Symbol", "AsOfDate", "Source") DO UPDATE SET
            "IBKRConId" = EXCLUDED."IBKRConId",
            "Currency" = EXCLUDED."Currency",
            "MarketCapitalization" = EXCLUDED."MarketCapitalization",
            "EnterpriseValue" = EXCLUDED."EnterpriseValue",
            "PERatio" = EXCLUDED."PERatio",
            "ForwardPERatio" = EXCLUDED."ForwardPERatio",
            "PriceToBookRatio" = EXCLUDED."PriceToBookRatio",
            "PriceToSalesRatio" = EXCLUDED."PriceToSalesRatio",
            "EPS" = EXCLUDED."EPS",
            "ForwardEPS" = EXCLUDED."ForwardEPS",
            "DividendPerShare" = EXCLUDED."DividendPerShare",
            "DividendYield" = EXCLUDED."DividendYield",
            "PayoutRatio" = EXCLUDED."PayoutRatio",
            "Beta" = EXCLUDED."Beta",
            "Revenue" = EXCLUDED."Revenue",
            "NetIncome" = EXCLUDED."NetIncome",
            "EBITDA" = EXCLUDED."EBITDA",
            "ReturnOnEquity" = EXCLUDED."ReturnOnEquity",
            "ReturnOnAssets" = EXCLUDED."ReturnOnAssets",
            "SharesOutstanding" = EXCLUDED."SharesOutstanding",
            "Week52High" = EXCLUDED."Week52High",
            "Week52Low" = EXCLUDED."Week52Low",
            "RawData" = EXCLUDED."RawData",
            "UpdatedAt" = CURRENT_TIMESTAMP
    )SQL");

    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":ibkrConId"), stockQuery.value(QStringLiteral("IBKRConId")));
    query.bindValue(QStringLiteral(":currency"),
                    alphaText(overview, QStringLiteral("Currency")).isValid()
                        ? alphaText(overview, QStringLiteral("Currency"))
                        : stockQuery.value(QStringLiteral("Currency")));
    query.bindValue(QStringLiteral(":marketCapitalization"), alphaNumber(overview, QStringLiteral("MarketCapitalization")));
    query.bindValue(QStringLiteral(":enterpriseValue"), computedProduct(revenue, evToRevenue));
    query.bindValue(QStringLiteral(":peRatio"), alphaNumber(overview, QStringLiteral("PERatio")));
    query.bindValue(QStringLiteral(":forwardPeRatio"), alphaNumber(overview, QStringLiteral("ForwardPE")));
    query.bindValue(QStringLiteral(":priceToBookRatio"), alphaNumber(overview, QStringLiteral("PriceToBookRatio")));
    query.bindValue(QStringLiteral(":priceToSalesRatio"), alphaNumber(overview, QStringLiteral("PriceToSalesRatioTTM")));
    query.bindValue(QStringLiteral(":eps"), alphaNumber(overview, QStringLiteral("EPS")));
    query.bindValue(QStringLiteral(":forwardEps"), alphaNumber(overview, QStringLiteral("ForwardEPS")));
    query.bindValue(QStringLiteral(":dividendPerShare"), alphaNumber(overview, QStringLiteral("DividendPerShare")));
    query.bindValue(QStringLiteral(":dividendYield"), dividendYield);
    query.bindValue(QStringLiteral(":payoutRatio"), alphaPercent(overview, QStringLiteral("PayoutRatio")));
    query.bindValue(QStringLiteral(":beta"), alphaNumber(overview, QStringLiteral("Beta")));
    query.bindValue(QStringLiteral(":revenue"), revenue);
    query.bindValue(QStringLiteral(":netIncome"),
                    profitMargin.isValid() && revenue.isValid()
                        ? QVariant(revenue.toDouble() * profitMargin.toDouble() / 100.0)
                        : QVariant());
    query.bindValue(QStringLiteral(":ebitda"), alphaNumber(overview, QStringLiteral("EBITDA")));
    query.bindValue(QStringLiteral(":returnOnEquity"), alphaPercent(overview, QStringLiteral("ReturnOnEquityTTM")));
    query.bindValue(QStringLiteral(":returnOnAssets"), alphaPercent(overview, QStringLiteral("ReturnOnAssetsTTM")));
    query.bindValue(QStringLiteral(":sharesOutstanding"), alphaNumber(overview, QStringLiteral("SharesOutstanding")));
    query.bindValue(QStringLiteral(":week52High"), alphaNumber(overview, QStringLiteral("52WeekHigh")));
    query.bindValue(QStringLiteral(":week52Low"), alphaNumber(overview, QStringLiteral("52WeekLow")));
    query.bindValue(QStringLiteral(":rawData"),
                    QString::fromUtf8(QJsonDocument::fromVariant(overview).toJson(QJsonDocument::Compact)));

    if (!query.exec()) {
        qCritical() << "Alpha-Vantage-Fundamentaldaten konnten nicht gespeichert werden:"
                    << query.lastError().text();
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Alpha-Vantage-Fundamentaldaten konnten nicht abgeschlossen werden:"
                    << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool DatabaseManager::saveYahooFundamentals(const QString &symbol,
                                            const QString &yahooSymbol,
                                            const QVariantMap &data)
{
    if (!db.transaction()) {
        qCritical() << "Yahoo-Fundamentaldaten konnten keine Transaktion starten:"
                    << db.lastError().text();
        return false;
    }

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "IBKRConId", "Currency"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), symbol);
    if (!stockQuery.exec() || !stockQuery.next()) {
        qCritical() << "Aktie fuer Yahoo-Fundamentaldaten nicht gefunden:" << symbol;
        db.rollback();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "StockFundamentals" (
            "Symbol", "IBKRConId", "AsOfDate", "Currency",
            "MarketCapitalization", "EnterpriseValue", "PERatio", "ForwardPERatio",
            "PriceToBookRatio", "PriceToSalesRatio", "EPS", "ForwardEPS",
            "DividendPerShare", "DividendYield", "PayoutRatio", "Beta",
            "Revenue", "NetIncome", "EBITDA", "ReturnOnEquity", "ReturnOnAssets",
            "DebtToEquity", "SharesOutstanding", "Week52High", "Week52Low",
            "Source", "RawData", "UpdatedAt"
        )
        VALUES (
            :symbol, :ibkrConId, CURRENT_DATE, :currency,
            :marketCapitalization, :enterpriseValue, :peRatio, :forwardPeRatio,
            :priceToBookRatio, :priceToSalesRatio, :eps, :forwardEps,
            :dividendPerShare, :dividendYield, :payoutRatio, :beta,
            :revenue, :netIncome, :ebitda, :returnOnEquity, :returnOnAssets,
            :debtToEquity, :sharesOutstanding, :week52High, :week52Low,
            'Yahoo', CAST(:rawData AS jsonb), CURRENT_TIMESTAMP
        )
        ON CONFLICT ("Symbol", "AsOfDate", "Source") DO UPDATE SET
            "IBKRConId" = EXCLUDED."IBKRConId",
            "Currency" = EXCLUDED."Currency",
            "MarketCapitalization" = EXCLUDED."MarketCapitalization",
            "EnterpriseValue" = EXCLUDED."EnterpriseValue",
            "PERatio" = EXCLUDED."PERatio",
            "ForwardPERatio" = EXCLUDED."ForwardPERatio",
            "PriceToBookRatio" = EXCLUDED."PriceToBookRatio",
            "PriceToSalesRatio" = EXCLUDED."PriceToSalesRatio",
            "EPS" = EXCLUDED."EPS",
            "ForwardEPS" = EXCLUDED."ForwardEPS",
            "DividendPerShare" = EXCLUDED."DividendPerShare",
            "DividendYield" = EXCLUDED."DividendYield",
            "PayoutRatio" = EXCLUDED."PayoutRatio",
            "Beta" = EXCLUDED."Beta",
            "Revenue" = EXCLUDED."Revenue",
            "NetIncome" = EXCLUDED."NetIncome",
            "EBITDA" = EXCLUDED."EBITDA",
            "ReturnOnEquity" = EXCLUDED."ReturnOnEquity",
            "ReturnOnAssets" = EXCLUDED."ReturnOnAssets",
            "DebtToEquity" = EXCLUDED."DebtToEquity",
            "SharesOutstanding" = EXCLUDED."SharesOutstanding",
            "Week52High" = EXCLUDED."Week52High",
            "Week52Low" = EXCLUDED."Week52Low",
            "RawData" = EXCLUDED."RawData",
            "UpdatedAt" = CURRENT_TIMESTAMP
    )SQL");

    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":ibkrConId"), stockQuery.value(QStringLiteral("IBKRConId")));
    query.bindValue(QStringLiteral(":currency"),
                    hasValue(data.value(QStringLiteral("currency")))
                        ? data.value(QStringLiteral("currency"))
                        : stockQuery.value(QStringLiteral("Currency")));
    query.bindValue(QStringLiteral(":marketCapitalization"), yahooNumber(data, QStringLiteral("marketCapitalization")));
    query.bindValue(QStringLiteral(":enterpriseValue"), yahooNumber(data, QStringLiteral("enterpriseValue")));
    query.bindValue(QStringLiteral(":peRatio"), yahooNumber(data, QStringLiteral("peRatio")));
    query.bindValue(QStringLiteral(":forwardPeRatio"), yahooNumber(data, QStringLiteral("forwardPeRatio")));
    query.bindValue(QStringLiteral(":priceToBookRatio"), yahooNumber(data, QStringLiteral("priceToBookRatio")));
    query.bindValue(QStringLiteral(":priceToSalesRatio"), yahooNumber(data, QStringLiteral("priceToSalesRatio")));
    query.bindValue(QStringLiteral(":eps"), yahooNumber(data, QStringLiteral("eps")));
    query.bindValue(QStringLiteral(":forwardEps"), yahooNumber(data, QStringLiteral("forwardEps")));
    query.bindValue(QStringLiteral(":dividendPerShare"), yahooNumber(data, QStringLiteral("dividendPerShare")));
    query.bindValue(QStringLiteral(":dividendYield"), yahooNumber(data, QStringLiteral("dividendYield")));
    query.bindValue(QStringLiteral(":payoutRatio"), yahooNumber(data, QStringLiteral("payoutRatio")));
    query.bindValue(QStringLiteral(":beta"), yahooNumber(data, QStringLiteral("beta")));
    query.bindValue(QStringLiteral(":revenue"), yahooNumber(data, QStringLiteral("revenue")));
    query.bindValue(QStringLiteral(":netIncome"), yahooNumber(data, QStringLiteral("netIncome")));
    query.bindValue(QStringLiteral(":ebitda"), yahooNumber(data, QStringLiteral("ebitda")));
    query.bindValue(QStringLiteral(":returnOnEquity"), yahooNumber(data, QStringLiteral("returnOnEquity")));
    query.bindValue(QStringLiteral(":returnOnAssets"), yahooNumber(data, QStringLiteral("returnOnAssets")));
    query.bindValue(QStringLiteral(":debtToEquity"), yahooNumber(data, QStringLiteral("debtToEquity")));
    query.bindValue(QStringLiteral(":sharesOutstanding"), yahooNumber(data, QStringLiteral("sharesOutstanding")));
    query.bindValue(QStringLiteral(":week52High"), yahooNumber(data, QStringLiteral("week52High")));
    query.bindValue(QStringLiteral(":week52Low"), yahooNumber(data, QStringLiteral("week52Low")));
    const QString rawData = hasValue(data.value(QStringLiteral("rawData")))
        ? data.value(QStringLiteral("rawData")).toString()
        : QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));
    query.bindValue(QStringLiteral(":rawData"), rawData);

    if (!query.exec()) {
        qCritical() << "Yahoo-Fundamentaldaten konnten nicht gespeichert werden:"
                    << query.lastError().text() << yahooSymbol;
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Yahoo-Fundamentaldaten konnten nicht abgeschlossen werden:"
                    << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}
