#include "databasemanager.h"
#include "databasemanager_internal.h"

using namespace DatabaseManagerInternal;

QString DatabaseManager::ibkrConnectionStatus() const
{
    return m_ibkrConnectionStatus;
}

bool DatabaseManager::ibkrConnected() const
{
    return m_ibkrConnected;
}

bool DatabaseManager::ibkrConnecting() const
{
    return m_ibkrConnecting;
}

bool DatabaseManager::ibkrDataLoading() const
{
    return m_ibkrDataLoading;
}

QString DatabaseManager::fundamentalDataStatus() const
{
    return m_fundamentalDataStatus;
}

bool DatabaseManager::fundamentalDataLoading() const
{
    return m_fundamentalDataLoading;
}

int DatabaseManager::alphaVantageRequestsRemaining() const
{
    if (!db.isOpen())
        return 0;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT COALESCE("RequestCount", 0)
        FROM "ApiDailyUsage"
        WHERE "Provider" = 'AlphaVantage'
          AND "UsageDate" = CURRENT_DATE
    )SQL");
    if (!query.exec() || !query.next())
        return 25;

    return qMax(0, 25 - query.value(0).toInt());
}

int DatabaseManager::yahooFundamentalsBatchTotal() const
{
    return m_yahooFundamentalsBatchSymbols.size();
}

int DatabaseManager::yahooFundamentalsBatchDone() const
{
    return m_yahooFundamentalsBatchSuccessCount + m_yahooFundamentalsBatchFailureCount;
}

bool DatabaseManager::yahooFundamentalsBatchActive() const
{
    return m_yahooFundamentalsBatchActive;
}

int DatabaseManager::ibkrBatchTotal() const
{
    return m_ibkrBatchSymbols.size();
}

int DatabaseManager::ibkrBatchDone() const
{
    return m_ibkrBatchSuccessCount + m_ibkrBatchFailureCount;
}

bool DatabaseManager::ibkrBatchActive() const
{
    return m_ibkrBatchActive;
}

bool DatabaseManager::ibkrGetStocksActive() const
{
    return m_ibkrGetStocksBatchActive
        || m_pendingIbkrProcessIsHistoricalQuotes
        || m_pendingIbkrProcessIsQuoteExchangeProbe
        || m_pendingIbkrProcessIsMarketSnapshot;
}

QString DatabaseManager::ibkrGetStocksBatchName() const
{
    return m_ibkrGetStocksBatchName;
}

int DatabaseManager::ibkrGetStocksTotal() const
{
    return m_ibkrGetStocksSymbols.size();
}

int DatabaseManager::ibkrGetStocksDone() const
{
    return m_ibkrGetStocksSuccessCount + m_ibkrGetStocksFailureCount;
}

int DatabaseManager::marketstackBatchTotal() const
{
    return m_marketstackBatchSymbols.size();
}

int DatabaseManager::marketstackBatchDone() const
{
    return m_marketstackBatchSuccessCount + m_marketstackBatchFailureCount;
}

bool DatabaseManager::marketstackBatchActive() const
{
    return m_marketstackBatchActive;
}

int DatabaseManager::marketstackQuotesBatchTotal() const
{
    return m_marketstackQuotesBatchSymbols.size();
}

int DatabaseManager::marketstackQuotesBatchDone() const
{
    return m_marketstackQuotesBatchSuccessCount + m_marketstackQuotesBatchFailureCount;
}

bool DatabaseManager::marketstackQuotesBatchActive() const
{
    return m_marketstackQuotesBatchActive;
}

int DatabaseManager::marketstackValidationBatchTotal() const
{
    return m_marketstackValidationBatchSymbols.size();
}

int DatabaseManager::marketstackValidationBatchDone() const
{
    return m_marketstackValidationBatchSuccessCount + m_marketstackValidationBatchFailureCount;
}

bool DatabaseManager::marketstackValidationBatchActive() const
{
    return m_marketstackValidationBatchActive;
}

int DatabaseManager::ibkrNameCheckBatchTotal() const
{
    return m_ibkrNameCheckBatchSymbols.size();
}

int DatabaseManager::ibkrNameCheckBatchDone() const
{
    return m_ibkrNameCheckBatchSuccessCount + m_ibkrNameCheckBatchFailureCount;
}

bool DatabaseManager::ibkrNameCheckBatchActive() const
{
    return m_ibkrNameCheckBatchActive;
}

void DatabaseManager::setIbkrConnectionState(const QString &status,
                                             bool connected,
                                             bool connecting)
{
    if (m_ibkrConnectionStatus == status
        && m_ibkrConnected == connected
        && m_ibkrConnecting == connecting) {
        return;
    }

    m_ibkrConnectionStatus = status;
    m_ibkrConnected = connected;
    m_ibkrConnecting = connecting;
    emit ibkrConnectionChanged();
}

void DatabaseManager::setFundamentalDataStatus(const QString &status, bool loading)
{
    if (m_fundamentalDataStatus == status && m_fundamentalDataLoading == loading)
        return;

    m_fundamentalDataStatus = status;
    m_fundamentalDataLoading = loading;
    emit fundamentalDataChanged();
}
