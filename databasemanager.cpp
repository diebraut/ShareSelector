#include "databasemanager.h"
#include "databasemanager_internal.h"
#include <QSqlRecord>
#include <QDate>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrlQuery>

#include <utility>

using namespace DatabaseManagerInternal;

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent)
{
    m_ibkrPorts = {7497, 7496, 4002, 4001};
    m_ibkrConnectTimeout.setSingleShot(true);
    m_ibkrConnectTimeout.setInterval(1200);
    connect(&m_ibkrConnectTimeout, &QTimer::timeout, this, [this]() {
        m_ibkrPortAdvanceInProgress = true;
        m_ibkrSocket.abort();
        ++m_ibkrPortIndex;
        m_ibkrPortAdvanceInProgress = false;
        tryNextIbkrPort();
    });
    connect(&m_ibkrSocket, &QTcpSocket::connected, this, [this]() {
        m_ibkrConnectTimeout.stop();
        m_ibkrConnectedPort = m_ibkrSocket.peerPort();
        setIbkrConnectionState(
            QStringLiteral("IBKR TWS/IB Gateway ist auf 127.0.0.1:%1 erreichbar.")
                .arg(m_ibkrSocket.peerPort()),
            true,
            false);
        m_ibkrProbeDisconnectInProgress = true;
        m_ibkrSocket.disconnectFromHost();
    });
    connect(&m_ibkrSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_ibkrConnecting || m_ibkrPortAdvanceInProgress)
            return;
        m_ibkrConnectTimeout.stop();
        ++m_ibkrPortIndex;
        QTimer::singleShot(0, this, &DatabaseManager::tryNextIbkrPort);
    });
    connect(&m_ibkrSocket, &QTcpSocket::disconnected, this, [this]() {
        if (m_ibkrProbeDisconnectInProgress) {
            m_ibkrProbeDisconnectInProgress = false;
            return;
        }
        if (m_ibkrConnecting || m_ibkrDataLoading)
            return;
        if (m_ibkrConnected) {
            setIbkrConnectionState(
                QStringLiteral("Die Verbindung zu IBKR TWS/IB Gateway wurde getrennt."),
                false,
                false);
        }
    });

    m_ibkrDataTimeout.setSingleShot(true);
    m_ibkrDataTimeout.setInterval(25000);
    connect(&m_ibkrDataTimeout, &QTimer::timeout, this, [this]() {
        const QString timedOutSymbol = m_ibkrPendingSymbol;
        const QString timedOutNameCheckSymbol = m_pendingIbkrNameCheckSymbol;
        m_ibkrDataLoading = false;
        if (m_ibkrProcess.state() != QProcess::NotRunning) {
            m_ibkrProcess.kill();
            m_ibkrProcess.waitForFinished(2000);
        }
        setIbkrConnectionState(
            QStringLiteral("Fehler: Zeitüberschreitung beim Abruf der IBKR-Daten."),
            m_ibkrConnected,
            false);
        if (m_pendingIbkrProcessIsHistoricalQuotes || m_pendingIbkrProcessIsQuoteExchangeProbe) {
            m_pendingIbkrProcessIsHistoricalQuotes = false;
            m_pendingIbkrProcessIsQuoteExchangeProbe = false;
            updateIbkrQuoteExchangeFailure(timedOutSymbol, QStringLiteral("Timeout"));
            if (m_ibkrGetStocksBatchActive) {
                ++m_ibkrGetStocksFailureCount;
                setIbkrConnectionState(
                    QStringLiteral("IBKR Get Quotes: %1 Timeout. OK: %2, Fehler: %3.")
                        .arg(timedOutSymbol)
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
            m_ibkrPendingSymbol.clear();
            m_ibkrDataTimeout.setInterval(25000);
            if (m_ibkrGetStocksBatchActive)
                scheduleNextIbkrGetStocksSymbol(1000);
            emit ibkrConnectionChanged();
        } else if (m_pendingIbkrDataForNameCheckRecovery && m_ibkrNameCheckBatchActive) {
            m_pendingIbkrDataForNameCheckRecovery = false;
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(timedOutSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenssuche Timeout"));
            m_ibkrPendingSymbol.clear();
            scheduleNextIbkrNameCheckBatchSymbol(1000);
        } else if (m_ibkrBatchActive) {
            ++m_ibkrBatchFailureCount;
            updateIbkrBatchFailure(timedOutSymbol, QStringLiteral("Timeout"));
            m_ibkrPendingSymbol.clear();
            setIbkrConnectionState(
                QStringLiteral("IBKR-Batch: %1 Timeout. Erfolgreich: %2, Fehler: %3.")
                    .arg(timedOutSymbol)
                    .arg(m_ibkrBatchSuccessCount)
                    .arg(m_ibkrBatchFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrBatchSymbol(1000);
        } else if (m_ibkrNameCheckBatchActive && m_pendingIbkrProcessIsNameCheck) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(timedOutNameCheckSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung Timeout"));
            m_pendingIbkrProcessIsNameCheck = false;
            m_pendingIbkrNameCheckSymbol.clear();
            m_pendingIbkrNameCheckName.clear();
            m_pendingIbkrNameCheckIsin.clear();
            m_pendingIbkrNameCheckHasConId = false;
            m_pendingIbkrNameCheckRequestUsesIsin = false;
            m_pendingIbkrNameCheckCandidates.clear();
            m_pendingIbkrNameCheckCandidateIndex = 0;
            scheduleNextIbkrNameCheckBatchSymbol(1000);
        }
    });
    connect(&m_ibkrProcess,
            &QProcess::errorOccurred,
            this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_ibkrDataTimeout.stop();
        m_ibkrDataLoading = false;
        setIbkrConnectionState(
            QStringLiteral("Fehler: Der IBKR-Helfer konnte nicht gestartet werden."),
            m_ibkrConnected,
            false);
        if (m_pendingIbkrProcessIsHistoricalQuotes || m_pendingIbkrProcessIsQuoteExchangeProbe) {
            m_pendingIbkrProcessIsHistoricalQuotes = false;
            m_pendingIbkrProcessIsQuoteExchangeProbe = false;
            updateIbkrQuoteExchangeFailure(m_ibkrPendingSymbol, QStringLiteral("IBKR-Helfer konnte nicht gestartet werden"));
            if (m_ibkrGetStocksBatchActive) {
                ++m_ibkrGetStocksFailureCount;
                setIbkrConnectionState(
                    QStringLiteral("IBKR Get Quotes: %1 Helper konnte nicht gestartet werden. OK: %2, Fehler: %3.")
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
            m_ibkrPendingSymbol.clear();
            m_ibkrDataTimeout.setInterval(25000);
            if (m_ibkrGetStocksBatchActive)
                scheduleNextIbkrGetStocksSymbol(1000);
            emit ibkrConnectionChanged();
        } else if (m_pendingIbkrDataForNameCheckRecovery && m_ibkrNameCheckBatchActive) {
            m_pendingIbkrDataForNameCheckRecovery = false;
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(m_ibkrPendingSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Helfer konnte nicht gestartet werden"));
            m_ibkrPendingSymbol.clear();
            scheduleNextIbkrNameCheckBatchSymbol(1000);
        } else if (m_ibkrBatchActive) {
            ++m_ibkrBatchFailureCount;
            updateIbkrBatchFailure(m_ibkrPendingSymbol, QStringLiteral("IBKR-Helfer konnte nicht gestartet werden"));
            m_ibkrPendingSymbol.clear();
            scheduleNextIbkrBatchSymbol(1000);
        } else if (m_ibkrNameCheckBatchActive && m_pendingIbkrProcessIsNameCheck) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(m_pendingIbkrNameCheckSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Helfer konnte nicht gestartet werden"));
            m_pendingIbkrProcessIsNameCheck = false;
            m_pendingIbkrNameCheckSymbol.clear();
            m_pendingIbkrNameCheckName.clear();
            m_pendingIbkrNameCheckIsin.clear();
            m_pendingIbkrNameCheckHasConId = false;
            m_pendingIbkrNameCheckRequestUsesIsin = false;
            m_pendingIbkrNameCheckCandidates.clear();
            m_pendingIbkrNameCheckCandidateIndex = 0;
            scheduleNextIbkrNameCheckBatchSymbol(1000);
        }
    });
    connect(&m_ibkrProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &DatabaseManager::finishIbkrDataRequest);
    m_ibkrBatchTimer.setSingleShot(true);
    connect(&m_ibkrBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextIbkrBatchSymbol);
    m_ibkrGetStocksTimer.setSingleShot(true);
    connect(&m_ibkrGetStocksTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextIbkrGetStocksSymbol);
    m_ibkrNameCheckBatchTimer.setSingleShot(true);
    connect(&m_ibkrNameCheckBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextIbkrNameCheckBatchSymbol);
    m_yahooFundamentalsBatchTimer.setSingleShot(true);
    connect(&m_yahooFundamentalsBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextYahooFundamentalsBatchSymbol);
    m_marketstackBatchTimer.setSingleShot(true);
    connect(&m_marketstackBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextMarketstackBatchSymbol);
    m_marketstackQuotesBatchTimer.setSingleShot(true);
    connect(&m_marketstackQuotesBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextMarketstackQuotesBatchSymbol);
    m_marketstackValidationBatchTimer.setSingleShot(true);
    connect(&m_marketstackValidationBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextMarketstackValidationBatchSymbol);
    connect(&alphaVantageClient,
            &AlphaVantageClient::fundamentalOverviewReceived,
            this,
            [this](const QString &requestSymbol, const QVariantMap &data) {
        const QString symbol = m_pendingFundamentalSymbol.isEmpty()
            ? requestSymbol
            : m_pendingFundamentalSymbol;
        if (saveAlphaVantageFundamentals(symbol, data)) {
            if (!m_pendingResolvedAlphaVantageSymbol.isEmpty())
                cacheAlphaVantageSymbol(symbol, m_pendingResolvedAlphaVantageSymbol);
            setFundamentalDataStatus(
                QStringLiteral("Alpha-Vantage-Fundamentaldaten fuer %1 wurden gespeichert. Noch %2 freie Abrufe heute.")
                    .arg(symbol)
                    .arg(alphaVantageRequestsRemaining()),
                false);
            emit fundamentalDataUpdated(symbol);
        } else {
            setFundamentalDataStatus(
                QStringLiteral("Fehler: Alpha-Vantage-Fundamentaldaten konnten nicht gespeichert werden."),
                false);
        }
        resetFundamentalRequestState();
    });
    connect(&alphaVantageClient,
            &AlphaVantageClient::fundamentalOverviewNotFound,
            this,
            [this](const QString &apiSymbol) {
        const QString symbol = m_pendingFundamentalSymbol.isEmpty()
            ? apiSymbol
            : m_pendingFundamentalSymbol;
        if (m_pendingAlphaVantageCandidateIndex < m_pendingAlphaVantageCandidates.size()) {
            setFundamentalDataStatus(
                QStringLiteral("Alpha Vantage hat fuer %1 (%2) keine Fundamentaldaten geliefert. Naechster Kandidat startet gleich ...")
                    .arg(symbol)
                    .arg(apiSymbol),
                true);
            QTimer::singleShot(1200, this, [this, symbol]() {
                if (m_pendingFundamentalSymbol == symbol)
                    tryNextAlphaVantageCandidate();
            });
        } else {
            const QString checkedSymbols = m_pendingAlphaVantageCandidates.isEmpty()
                ? apiSymbol
                : m_pendingAlphaVantageCandidates.join(QStringLiteral(", "));
            setFundamentalDataStatus(
                QStringLiteral("Alpha Vantage hat fuer %1 keine Fundamentaldaten geliefert. Gepruefte Kandidaten: %2. Yahoo wird versucht ...")
                    .arg(symbol)
                    .arg(checkedSymbols),
                true);
            QTimer::singleShot(1200, this, [this, symbol]() {
                if (m_pendingFundamentalSymbol == symbol)
                    fetchYahooFundamentalsFallback(symbol);
            });
        }
    });
    connect(&alphaVantageClient,
            &AlphaVantageClient::fundamentalSymbolResolved,
            this,
            [this](const QString &requestSymbol, const QStringList &resolvedSymbols) {
        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        m_pendingAlphaVantageCandidates = resolvedSymbols;
        m_pendingAlphaVantageCandidateIndex = 0;
        m_pendingResolvedAlphaVantageSymbol.clear();
        setFundamentalDataStatus(
            QStringLiteral("Alpha-Vantage-Symbole fuer %1 gefunden: %2. Fundamentaldaten starten gleich ...")
                .arg(requestSymbol)
                .arg(resolvedSymbols.join(QStringLiteral(", "))),
            true);
        QTimer::singleShot(1200, this, [this, requestSymbol]() {
            if (m_pendingFundamentalSymbol != requestSymbol) {
                return;
            }
            tryNextAlphaVantageCandidate();
        });
    });
    connect(&alphaVantageClient,
            &AlphaVantageClient::fundamentalSymbolResolveFailed,
            this,
            [this](const QString &requestSymbol, const QString &message) {
        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        setFundamentalDataStatus(
            QStringLiteral("Alpha-Vantage-Symbolsuche fuer %1 fehlgeschlagen: %2. Yahoo wird versucht ...")
                .arg(requestSymbol)
                .arg(message),
            true);
        QTimer::singleShot(1200, this, [this, requestSymbol]() {
            if (m_pendingFundamentalSymbol == requestSymbol)
                fetchYahooFundamentalsFallback(requestSymbol);
        });
    });
    connect(&alphaVantageClient,
            &AlphaVantageClient::errorOccurred,
            this,
            [this](const QString &error) {
        if (!m_fundamentalDataLoading)
            return;
        resetFundamentalRequestState();
        setFundamentalDataStatus(QStringLiteral("Fehler: %1").arg(error), false);
    });
    connect(&yahooFinanceClient,
            &YahooFinanceClient::fundamentalsReceived,
            this,
            [this](const QString &requestSymbol, const QString &yahooSymbol, const QVariantMap &data) {
        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        const int score = yahooFundamentalScore(data);
        if (isYahooNoClassicFundamentals(data)) {
            if (saveYahooFundamentals(requestSymbol, yahooSymbol, data)) {
                cacheYahooSymbol(requestSymbol, yahooSymbol);
                updateYahooFundamentalSuccess(requestSymbol, 0);
                if (m_yahooFundamentalsBatchActive) {
                    ++m_yahooFundamentalsBatchSuccessCount;
                    setFundamentalDataStatus(
                        QStringLiteral("Yahoo-Batch: %1 (%2) gefunden, aber keine klassischen Fundamentaldaten fuer ETF/ETN/Fonds. Erfolgreich: %3, Fehler: %4.")
                            .arg(requestSymbol)
                            .arg(yahooSymbol)
                            .arg(m_yahooFundamentalsBatchSuccessCount)
                            .arg(m_yahooFundamentalsBatchFailureCount),
                        true);
                    emit fundamentalDataUpdated(requestSymbol);
                    resetFundamentalRequestState();
                    scheduleNextYahooFundamentalsBatchSymbol(1500);
                    return;
                }
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Symbol fuer %1 (%2) gefunden, aber Yahoo liefert keine klassischen Fundamentaldaten fuer ETF/ETN/Fonds.")
                        .arg(requestSymbol)
                        .arg(yahooSymbol),
                    false);
                emit fundamentalDataUpdated(requestSymbol);
            } else {
                if (m_yahooFundamentalsBatchActive) {
                    ++m_yahooFundamentalsBatchFailureCount;
                    updateYahooFundamentalFailure(requestSymbol, QStringLiteral("Yahoo-ETF/Fonds-Hinweis konnte nicht gespeichert werden."));
                    setFundamentalDataStatus(
                        QStringLiteral("Yahoo-Batch: %1 konnte nicht gespeichert werden. Erfolgreich: %2, Fehler: %3.")
                            .arg(requestSymbol)
                            .arg(m_yahooFundamentalsBatchSuccessCount)
                            .arg(m_yahooFundamentalsBatchFailureCount),
                        true);
                    resetFundamentalRequestState();
                    scheduleNextYahooFundamentalsBatchSymbol(3000);
                    return;
                }
                setFundamentalDataStatus(
                    QStringLiteral("Fehler: Yahoo-ETF/Fonds-Hinweis konnte nicht gespeichert werden."),
                    false);
            }
            resetFundamentalRequestState();
            return;
        }
        if (score > m_pendingYahooBestScore) {
            m_pendingYahooBestScore = score;
            m_pendingYahooBestSymbol = yahooSymbol;
            m_pendingYahooBestData = data;
        }

        if (score < 5) {
            m_pendingYahooLastError = QStringLiteral("%1 lieferte nur %2 Kennzahlen")
                                          .arg(yahooSymbol)
                                          .arg(score);
            tryNextYahooCandidate();
            return;
        }

        if (saveYahooFundamentals(requestSymbol, yahooSymbol, data)) {
            cacheYahooSymbol(requestSymbol, yahooSymbol);
            updateYahooFundamentalSuccess(requestSymbol, score);
            if (m_yahooFundamentalsBatchActive) {
                ++m_yahooFundamentalsBatchSuccessCount;
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Batch: %1 gespeichert (%2). Erfolgreich: %3, Fehler: %4.")
                        .arg(requestSymbol)
                        .arg(yahooSymbol)
                        .arg(m_yahooFundamentalsBatchSuccessCount)
                        .arg(m_yahooFundamentalsBatchFailureCount),
                    true);
                emit fundamentalDataUpdated(requestSymbol);
                resetFundamentalRequestState();
                scheduleNextYahooFundamentalsBatchSymbol(1500);
                return;
            }
            setFundamentalDataStatus(
                QStringLiteral("Yahoo-Fundamentaldaten fuer %1 (%2) wurden gespeichert.")
                    .arg(requestSymbol)
                    .arg(yahooSymbol),
                false);
            emit fundamentalDataUpdated(requestSymbol);
        } else {
            if (m_yahooFundamentalsBatchActive) {
                ++m_yahooFundamentalsBatchFailureCount;
                updateYahooFundamentalFailure(requestSymbol, QStringLiteral("Yahoo-Fundamentaldaten konnten nicht gespeichert werden."));
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Batch: %1 konnte nicht gespeichert werden. Erfolgreich: %2, Fehler: %3.")
                        .arg(requestSymbol)
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
    });
    connect(&yahooFinanceClient,
            &YahooFinanceClient::fundamentalsFailed,
            this,
            [this](const QString &requestSymbol, const QString &yahooSymbol, const QString &message) {
        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        m_pendingYahooLastError = QStringLiteral("%1: %2").arg(yahooSymbol, message);
        tryNextYahooCandidate();
    });
    connect(&yahooFinanceClient,
            &YahooFinanceClient::symbolResolved,
            this,
            [this](const QString &requestSymbol, const QStringList &yahooSymbols) {
        if (m_ibkrPendingSymbol == requestSymbol && m_pendingIbkrSearchStarted) {
            for (const QString &symbol : yahooSymbols)
                appendIbkrSymbolVariants(m_pendingIbkrCandidateSymbols, symbol);
            if (!tryNextIbkrCandidate())
                finalizeIbkrDataFailure(m_pendingIbkrLastError);
            return;
        }

        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        const QStringList orderedSymbols = preferYahooSuffix(yahooSymbols, m_pendingPreferredYahooSuffix);
        for (const QString &symbol : orderedSymbols)
            appendUniqueSymbol(m_pendingYahooCandidates, symbol);
        tryNextYahooCandidate();
    });
    connect(&yahooFinanceClient,
            &YahooFinanceClient::symbolResolveFailed,
            this,
            [this](const QString &requestSymbol, const QString &message) {
        if (m_ibkrPendingSymbol == requestSymbol && m_pendingIbkrSearchStarted) {
            m_pendingIbkrLastError = message;
            if (!tryNextIbkrCandidate(message))
                finalizeIbkrDataFailure(message);
            return;
        }

        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        m_pendingYahooLastError = message;
        tryNextYahooCandidate();
    });

    db = QSqlDatabase::addDatabase("QPSQL"); // PostgreSQL-Treiber
    db.setHostName("localhost");
    db.setDatabaseName("TotalStocks");
    db.setUserName("postgres");
    db.setPassword("castell");

    if (!db.open()) {
        qDebug() << "Fehler bei der Verbindung zur Datenbank:" << db.lastError().text();
    } else {
        qDebug() << "Erfolgreich mit der Datenbank verbunden!";
        if (!ensureSchema())
            qCritical() << "Datenbankschema konnte nicht aktualisiert werden.";
        qDebug() << "Tables:" << db.tables(QSql::Tables);
    }
}

DatabaseManager::~DatabaseManager()
{
    if (m_ibkrProcess.state() != QProcess::NotRunning) {
        m_ibkrProcess.kill();
        m_ibkrProcess.waitForFinished(2000);
    }
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
    return ensureSchema();
}

bool DatabaseManager::ensureSchema()
{
    if (!db.isOpen())
        return false;

    const QStringList statements = {
        QStringLiteral(R"SQL(
            ALTER TABLE "Stocks"
                ADD COLUMN IF NOT EXISTS "IBKRConId" BIGINT,
                ADD COLUMN IF NOT EXISTS "IBKRResolvedSymbol" VARCHAR(64),
                ADD COLUMN IF NOT EXISTS "Currency" VARCHAR(8),
                ADD COLUMN IF NOT EXISTS "PrimaryExchange" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "LocalSymbol" VARCHAR(64),
                ADD COLUMN IF NOT EXISTS "SecurityType" VARCHAR(16),
                ADD COLUMN IF NOT EXISTS "TradingClass" VARCHAR(64),
                ADD COLUMN IF NOT EXISTS "StockType" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "Industry" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "Category" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "Subcategory" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "TimeZoneId" VARCHAR(64),
                ADD COLUMN IF NOT EXISTS "TradingHours" TEXT,
                ADD COLUMN IF NOT EXISTS "LiquidHours" TEXT,
                ADD COLUMN IF NOT EXISTS "MinTick" NUMERIC(20, 10),
                ADD COLUMN IF NOT EXISTS "MarketRuleIds" TEXT,
                ADD COLUMN IF NOT EXISTS "ValidExchanges" TEXT,
                ADD COLUMN IF NOT EXISTS "OrderTypes" TEXT,
                ADD COLUMN IF NOT EXISTS "MarketName" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "CUSIP" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "AlphaVantageSymbol" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "YahooSymbol" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsLastAttemptAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsLastSuccessAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsFailureCount" INTEGER NOT NULL DEFAULT 0,
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsLastError" TEXT,
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsQualityScore" INTEGER,
                ADD COLUMN IF NOT EXISTS "IBKRLastSyncAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRLastAttemptAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRFailureCount" INTEGER NOT NULL DEFAULT 0,
                ADD COLUMN IF NOT EXISTS "IBKRLastError" TEXT,
                ADD COLUMN IF NOT EXISTS "IBKRValidationStatus" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "IBKRValidationMessage" TEXT,
                ADD COLUMN IF NOT EXISTS "IBKRValidationAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchange" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeTurnover" NUMERIC(28, 4),
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeCheckedAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeLastAttemptAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeLastSuccessAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeFailureCount" INTEGER NOT NULL DEFAULT 0,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeLastError" TEXT,
                ADD COLUMN IF NOT EXISTS "IBKRBestDirectExchange" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "IBKRBestDirectExchangeTurnover" NUMERIC(28, 4),
                ADD COLUMN IF NOT EXISTS "IBKRBestDirectExchangeCheckedAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "use_marketstack" BOOLEAN NOT NULL DEFAULT FALSE,
                ADD COLUMN IF NOT EXISTS "marketplace_sym" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "marketplace_exchange" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "marketplace_turnover" NUMERIC(28, 4),
                ADD COLUMN IF NOT EXISTS "marketplace_checked_at" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "marketplace_last_error" TEXT
        )SQL"),
        QStringLiteral(R"SQL(
            UPDATE "Stocks"
            SET "AlphaVantageSymbol" = 'NVDA'
            WHERE "ISIN" = 'US67066G1040'
              AND COALESCE("AlphaVantageSymbol", '') = ''
        )SQL"),
        QStringLiteral(R"SQL(
            UPDATE "Stocks"
            SET "AlphaVantageSymbol" = 'STX'
            WHERE "ISIN" = 'IE00BKVD2N49'
              AND COALESCE("AlphaVantageSymbol", '') = ''
        )SQL"),
        QStringLiteral(R"SQL(
            DROP INDEX IF EXISTS "Stocks_IBKRConId_uidx"
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE INDEX IF NOT EXISTS "Stocks_IBKRConId_idx"
                ON "Stocks" ("IBKRConId")
                WHERE "IBKRConId" IS NOT NULL
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS "StockFundamentals" (
                "Id" BIGSERIAL PRIMARY KEY,
                "Symbol" VARCHAR NOT NULL,
                "IBKRConId" BIGINT,
                "AsOfDate" DATE NOT NULL,
                "Currency" VARCHAR(8),
                "MarketCapitalization" NUMERIC(24, 4),
                "EnterpriseValue" NUMERIC(24, 4),
                "PERatio" NUMERIC(20, 8),
                "ForwardPERatio" NUMERIC(20, 8),
                "PriceToBookRatio" NUMERIC(20, 8),
                "PriceToSalesRatio" NUMERIC(20, 8),
                "PriceToCashFlowRatio" NUMERIC(20, 8),
                "PriceToDividendRatio" NUMERIC(20, 8),
                "EPS" NUMERIC(20, 8),
                "ForwardEPS" NUMERIC(20, 8),
                "DividendPerShare" NUMERIC(20, 8),
                "DividendYield" NUMERIC(20, 8),
                "PayoutRatio" NUMERIC(20, 8),
                "Beta" NUMERIC(20, 8),
                "Revenue" NUMERIC(24, 4),
                "NetIncome" NUMERIC(24, 4),
                "EBITDA" NUMERIC(24, 4),
                "ReturnOnEquity" NUMERIC(20, 8),
                "ReturnOnAssets" NUMERIC(20, 8),
                "DebtToEquity" NUMERIC(20, 8),
                "SharesOutstanding" NUMERIC(24, 4),
                "Week52High" NUMERIC(20, 8),
                "Week52Low" NUMERIC(20, 8),
                "Source" VARCHAR(32) NOT NULL DEFAULT 'IBKR',
                "RawData" JSONB,
                "UpdatedAt" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
                CONSTRAINT "StockFundamentals_symbol_date_source_key"
                    UNIQUE ("Symbol", "AsOfDate", "Source")
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE INDEX IF NOT EXISTS "StockFundamentals_IBKRConId_idx"
                ON "StockFundamentals" ("IBKRConId")
                WHERE "IBKRConId" IS NOT NULL
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE INDEX IF NOT EXISTS "StockFundamentals_Symbol_AsOfDate_idx"
                ON "StockFundamentals" ("Symbol", "AsOfDate" DESC)
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS "StockAnalysisConfigs" (
                "Name" TEXT PRIMARY KEY,
                "IncreasePercent" NUMERIC(10, 4) NOT NULL,
                "CorridorPercent" NUMERIC(10, 4) NOT NULL DEFAULT 10,
                "CorridorRequiredPercent" NUMERIC(10, 4) NOT NULL DEFAULT 0,
                "MaxDrawdownPercent" NUMERIC(10, 4) NOT NULL DEFAULT 10,
                "QuoteCount" INTEGER NOT NULL DEFAULT 90,
                "CreatedAt" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
                "UpdatedAt" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
                CONSTRAINT "StockAnalysisConfigs_QuoteCount_chk"
                    CHECK ("QuoteCount" BETWEEN 10 AND 90 AND "QuoteCount" % 10 = 0)
            )
        )SQL"),
        QStringLiteral(R"SQL(
            INSERT INTO "StockAnalysisConfigs" (
                "Name", "IncreasePercent", "CorridorPercent",
                "CorridorRequiredPercent", "MaxDrawdownPercent", "QuoteCount"
            ) VALUES
                ('25_Stocks', 25, 10, 92, 10, 90),
                ('30_Stocks', 30, 10, 86, 10, 90),
                ('35_Stocks', 35, 36, 92, 10, 90),
                ('40_Stocks', 40, 10, 92, 10, 90)
            ON CONFLICT ("Name") DO NOTHING
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS "AppSettings" (
                "Key" TEXT PRIMARY KEY,
                "Value" TEXT NOT NULL,
                "UpdatedAt" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS "BoughtStocks" (
                "Symbol" VARCHAR PRIMARY KEY,
                "Name" TEXT NOT NULL,
                "BuyDate" DATE NOT NULL,
                "SellDate" DATE,
                "CurrentValue" NUMERIC(20, 8) NOT NULL DEFAULT 0,
                "EntryValue" NUMERIC(20, 8) NOT NULL DEFAULT 0,
                "ValueIncreasePercent" NUMERIC(20, 8) NOT NULL DEFAULT 0,
                "Status" INTEGER NOT NULL DEFAULT 0,
                "AnalysisConfigName" TEXT,
                "CreatedAt" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
                "UpdatedAt" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
            )
        )SQL"),
        QStringLiteral(R"SQL(
            ALTER TABLE "BoughtStocks"
                ADD COLUMN IF NOT EXISTS "Quantity" NUMERIC(24, 8) NOT NULL DEFAULT 1,
                ADD COLUMN IF NOT EXISTS "AnalysisConfigName" TEXT
        )SQL"),
        QStringLiteral(R"SQL(
            INSERT INTO "StockAnalysisConfigs" ("Name", "IncreasePercent")
            SELECT DISTINCT b."AnalysisConfigName", 0
            FROM "BoughtStocks" b
            WHERE COALESCE(b."AnalysisConfigName", '') <> ''
            ON CONFLICT ("Name") DO NOTHING
        )SQL"),
        QStringLiteral(R"SQL(
            DO $$
            BEGIN
                IF NOT EXISTS (
                    SELECT 1
                    FROM pg_constraint
                    WHERE conname = 'BoughtStocks_AnalysisConfigName_fkey'
                ) THEN
                    ALTER TABLE "BoughtStocks"
                        ADD CONSTRAINT "BoughtStocks_AnalysisConfigName_fkey"
                        FOREIGN KEY ("AnalysisConfigName")
                        REFERENCES "StockAnalysisConfigs" ("Name")
                        ON UPDATE CASCADE
                        ON DELETE SET NULL;
                END IF;
            END $$
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS "ApiDailyUsage" (
                "Provider" VARCHAR(64) NOT NULL,
                "UsageDate" DATE NOT NULL,
                "RequestCount" INTEGER NOT NULL DEFAULT 0,
                "UpdatedAt" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
                CONSTRAINT "ApiDailyUsage_provider_date_key"
                    PRIMARY KEY ("Provider", "UsageDate")
            )
        )SQL")
    };

    if (!db.transaction()) {
        qCritical() << "Schema-Migration konnte keine Transaktion starten:"
                    << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SET LOCAL client_min_messages TO warning"))) {
        qCritical() << "Schema-Migration konnte PostgreSQL-Hinweise nicht filtern:"
                    << query.lastError().text();
        db.rollback();
        return false;
    }

    for (const QString &statement : statements) {
        if (query.exec(statement))
            continue;

        qCritical() << "Schema-Migration fehlgeschlagen:" << query.lastError().text();
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Schema-Migration konnte nicht abgeschlossen werden:"
                    << db.lastError().text();
        db.rollback();
        return false;
    }

    return true;
}


QVariantList DatabaseManager::getStockAnalysisConfigs()
{
    QVariantList results;
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return results;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT
            "Name",
            "IncreasePercent",
            "CorridorPercent",
            "CorridorRequiredPercent",
            "MaxDrawdownPercent",
            "QuoteCount"
        FROM "StockAnalysisConfigs"
        ORDER BY "Name"
    )SQL");

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden der Stockanalyse-Konfigurationen:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        row["name"] = query.value("Name");
        row["increasePercent"] = query.value("IncreasePercent");
        row["corridorPercent"] = query.value("CorridorPercent");
        row["corridorRequiredPercent"] = query.value("CorridorRequiredPercent");
        row["maxDrawdownPercent"] = query.value("MaxDrawdownPercent");
        row["quoteCount"] = query.value("QuoteCount");
        results << row;
    }

    return results;
}

bool DatabaseManager::saveStockAnalysisConfig(
    const QString &name,
    double increasePercent,
    double corridorPercent,
    double corridorRequiredPercent,
    double maxDrawdownPercent,
    int quoteCount)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty())
        return false;

    const int boundedQuoteCount = qBound(10, quoteCount, 90);
    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "StockAnalysisConfigs" (
            "Name", "IncreasePercent", "CorridorPercent",
            "CorridorRequiredPercent", "MaxDrawdownPercent", "QuoteCount"
        ) VALUES (
            :name, :increasePercent, :corridorPercent,
            :corridorRequiredPercent, :maxDrawdownPercent, :quoteCount
        )
        ON CONFLICT ("Name") DO UPDATE SET
            "IncreasePercent" = EXCLUDED."IncreasePercent",
            "CorridorPercent" = EXCLUDED."CorridorPercent",
            "CorridorRequiredPercent" = EXCLUDED."CorridorRequiredPercent",
            "MaxDrawdownPercent" = EXCLUDED."MaxDrawdownPercent",
            "QuoteCount" = EXCLUDED."QuoteCount",
            "UpdatedAt" = CURRENT_TIMESTAMP
    )SQL");
    query.bindValue(":name", trimmedName);
    query.bindValue(":increasePercent", increasePercent);
    query.bindValue(":corridorPercent", corridorPercent);
    query.bindValue(":corridorRequiredPercent", corridorRequiredPercent);
    query.bindValue(":maxDrawdownPercent", maxDrawdownPercent);
    query.bindValue(":quoteCount", qRound(boundedQuoteCount / 10.0) * 10);

    if (!query.exec()) {
        qCritical() << "Fehler beim Speichern der Stockanalyse-Konfiguration:" << query.lastError().text();
        return false;
    }

    return true;
}

QString DatabaseManager::lastStockAnalysisConfigName()
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return QString();
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Value"
        FROM "AppSettings"
        WHERE "Key" = 'lastStockAnalysisConfigName'
        LIMIT 1
    )SQL");

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden der letzten Stockanalyse-Konfiguration:" << query.lastError().text();
        return QString();
    }

    return query.next() ? query.value(0).toString() : QString();
}

bool DatabaseManager::saveLastStockAnalysisConfigName(const QString &name)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    if (name.trimmed().isEmpty())
        return false;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "AppSettings" ("Key", "Value", "UpdatedAt")
        VALUES ('lastStockAnalysisConfigName', :name, CURRENT_TIMESTAMP)
        ON CONFLICT ("Key") DO UPDATE SET
            "Value" = EXCLUDED."Value",
            "UpdatedAt" = CURRENT_TIMESTAMP
    )SQL");
    query.bindValue(":name", name.trimmed());

    if (!query.exec()) {
        qCritical() << "Fehler beim Speichern der letzten Stockanalyse-Konfiguration:" << query.lastError().text();
        return false;
    }

    return true;
}

QString DatabaseManager::appSetting(const QString &key)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return QString();
    }

    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty())
        return QString();

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Value"
        FROM "AppSettings"
        WHERE "Key" = :key
        LIMIT 1
    )SQL");
    query.bindValue(":key", trimmedKey);

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden der App-Einstellung:" << query.lastError().text();
        return QString();
    }

    return query.next() ? query.value(0).toString() : QString();
}

bool DatabaseManager::saveAppSetting(const QString &key, const QString &value)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty())
        return false;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "AppSettings" ("Key", "Value", "UpdatedAt")
        VALUES (:key, :value, CURRENT_TIMESTAMP)
        ON CONFLICT ("Key") DO UPDATE SET
            "Value" = EXCLUDED."Value",
            "UpdatedAt" = CURRENT_TIMESTAMP
    )SQL");
    query.bindValue(":key", trimmedKey);
    query.bindValue(":value", value);

    if (!query.exec()) {
        qCritical() << "Fehler beim Speichern der App-Einstellung:" << query.lastError().text();
        return false;
    }

    return true;
}
