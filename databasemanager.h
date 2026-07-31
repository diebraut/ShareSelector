#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#define FIRST_PERIOD    6
#define SECOND_PERIOD   11
#define THIRD_PERIOD    21
#define FOURTH_PERIOD   41

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QHash>
#include <QTcpSocket>
#include <QTimer>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

#include "sharedata.h"
#include "marketstackclient.h"
#include "alphavantageclient.h"
#include "yahoofinanceclient.h"

//J2H71H3K5SQLU2PS Alpha Vantage
//2c7445a74b7f5ed6371d655f39ab4f4f marketstack

class DatabaseManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString ibkrConnectionStatus READ ibkrConnectionStatus NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(bool ibkrConnected READ ibkrConnected NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(bool ibkrConnecting READ ibkrConnecting NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(bool ibkrDataLoading READ ibkrDataLoading NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(QString fundamentalDataStatus READ fundamentalDataStatus NOTIFY fundamentalDataChanged)
    Q_PROPERTY(bool fundamentalDataLoading READ fundamentalDataLoading NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int alphaVantageRequestsRemaining READ alphaVantageRequestsRemaining NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int yahooFundamentalsBatchTotal READ yahooFundamentalsBatchTotal NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int yahooFundamentalsBatchDone READ yahooFundamentalsBatchDone NOTIFY fundamentalDataChanged)
    Q_PROPERTY(bool yahooFundamentalsBatchActive READ yahooFundamentalsBatchActive NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int ibkrBatchTotal READ ibkrBatchTotal NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(int ibkrBatchDone READ ibkrBatchDone NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(bool ibkrBatchActive READ ibkrBatchActive NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(bool ibkrGetStocksActive READ ibkrGetStocksActive NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(int ibkrGetStocksTotal READ ibkrGetStocksTotal NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(int ibkrGetStocksDone READ ibkrGetStocksDone NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(int marketstackBatchTotal READ marketstackBatchTotal NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int marketstackBatchDone READ marketstackBatchDone NOTIFY fundamentalDataChanged)
    Q_PROPERTY(bool marketstackBatchActive READ marketstackBatchActive NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int marketstackQuotesBatchTotal READ marketstackQuotesBatchTotal NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int marketstackQuotesBatchDone READ marketstackQuotesBatchDone NOTIFY fundamentalDataChanged)
    Q_PROPERTY(bool marketstackQuotesBatchActive READ marketstackQuotesBatchActive NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int marketstackValidationBatchTotal READ marketstackValidationBatchTotal NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int marketstackValidationBatchDone READ marketstackValidationBatchDone NOTIFY fundamentalDataChanged)
    Q_PROPERTY(bool marketstackValidationBatchActive READ marketstackValidationBatchActive NOTIFY fundamentalDataChanged)
    Q_PROPERTY(int ibkrNameCheckBatchTotal READ ibkrNameCheckBatchTotal NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(int ibkrNameCheckBatchDone READ ibkrNameCheckBatchDone NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(bool ibkrNameCheckBatchActive READ ibkrNameCheckBatchActive NOTIFY ibkrConnectionChanged)
public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();

    bool connectToDatabase(const QString &host, const QString &dbName, const QString &user, const QString &password);
    Q_INVOKABLE QVariantList searchByTickerAndExchange(const QString &ticker, const QString &exchange);
    Q_INVOKABLE void saveShares(const QList<ShareData>& shares);
    Q_INVOKABLE void updateShares(const QList<ShareData>& shares);
    Q_INVOKABLE void createQuotesForStock(const QString symbol, const QString exchange);
    Q_INVOKABLE void generateQuotesForAllStocks(); // Neue Methode für alle Stocks
    Q_INVOKABLE void generateQuoteForStock(const QString symbol, const QString exchange);
    Q_INVOKABLE QVariantList getQuoteDetails(const QString &symbol, int fromDay, int toDay);
    Q_INVOKABLE QVariantList getStockAnalysisResults(double minIncreasePercent);
    Q_INVOKABLE QVariantList getStockAnalysisIbkrSymbols();
    Q_INVOKABLE QVariantMap getStockAnalysisCandidate(const QString &symbol, double minIncreasePercent);
    Q_INVOKABLE QVariantList getBoughtStocks();
    Q_INVOKABLE QVariantList getTestPortfolio();
    Q_INVOKABLE void connectToIbkr();
    Q_INVOKABLE void getIbkrData(const QString &symbol);
    Q_INVOKABLE void getAlphaVantageFundamentals(const QString &symbol);
    Q_INVOKABLE void startYahooFundamentalsBatch();
    Q_INVOKABLE void stopYahooFundamentalsBatch();
    Q_INVOKABLE void startIbkrBatch();
    Q_INVOKABLE void stopIbkrBatch();
    Q_INVOKABLE void startIbkrGetStocks();
    Q_INVOKABLE void stopIbkrGetStocks();
    Q_INVOKABLE void startMarketstackBatch();
    Q_INVOKABLE void stopMarketstackBatch();
    Q_INVOKABLE void startMarketstackQuotesBatch();
    Q_INVOKABLE void stopMarketstackQuotesBatch();
    Q_INVOKABLE void startMarketstackValidationBatch();
    Q_INVOKABLE void stopMarketstackValidationBatch();
    Q_INVOKABLE void startIbkrNameCheckBatch();
    Q_INVOKABLE void stopIbkrNameCheckBatch();
    QString ibkrConnectionStatus() const;
    bool ibkrConnected() const;
    bool ibkrConnecting() const;
    bool ibkrDataLoading() const;
    QString fundamentalDataStatus() const;
    bool fundamentalDataLoading() const;
    int alphaVantageRequestsRemaining() const;
    int yahooFundamentalsBatchTotal() const;
    int yahooFundamentalsBatchDone() const;
    bool yahooFundamentalsBatchActive() const;
    int ibkrBatchTotal() const;
    int ibkrBatchDone() const;
    bool ibkrBatchActive() const;
    bool ibkrGetStocksActive() const;
    int ibkrGetStocksTotal() const;
    int ibkrGetStocksDone() const;
    int marketstackBatchTotal() const;
    int marketstackBatchDone() const;
    bool marketstackBatchActive() const;
    int marketstackQuotesBatchTotal() const;
    int marketstackQuotesBatchDone() const;
    bool marketstackQuotesBatchActive() const;
    int marketstackValidationBatchTotal() const;
    int marketstackValidationBatchDone() const;
    bool marketstackValidationBatchActive() const;
    int ibkrNameCheckBatchTotal() const;
    int ibkrNameCheckBatchDone() const;
    bool ibkrNameCheckBatchActive() const;
    Q_INVOKABLE bool isBoughtStock(const QString &symbol);
    Q_INVOKABLE bool deleteBoughtStock(const QString &symbol);
    Q_INVOKABLE bool saveBoughtStock(
        const QString &symbol,
        const QString &name,
        const QString &buyDate,
        const QString &sellDate,
        double currentValue,
        double entryValue,
        double valueIncreasePercent,
        int status);

    Q_INVOKABLE QVariantList getShares(
        int firstTo, int firstThreshold, bool firstGreaterThan,
        int secondTo, int secondThreshold, bool secondGreaterThan,
        int thirdTo, int thirdThreshold, bool thirdGreaterThan,
        int fourthTo, int fourthThreshold, bool fourthGreaterThan,
        int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol);

    Q_INVOKABLE void getSharesAsync(
        int firstTo, int firstThreshold, bool firstGreaterThan,
        int secondTo, int secondThreshold, bool secondGreaterThan,
        int thirdTo, int thirdThreshold, bool thirdGreaterThan,
        int fourthTo, int fourthThreshold, bool fourthGreaterThan,
        int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol);
    Q_INVOKABLE void getSharesByNameAsync(
        int firstTo, int firstThreshold, bool firstGreaterThan,
        int secondTo, int secondThreshold, bool secondGreaterThan,
        int thirdTo, int thirdThreshold, bool thirdGreaterThan,
        int fourthTo, int fourthThreshold, bool fourthGreaterThan,
        int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& name);

    Q_INVOKABLE void updateAllISINs();


    // In DatabaseManager.h, add this signal:
    Q_SIGNAL void getSharesComplete(const QVariantList &shares);

signals:
    Q_SIGNAL void saveComplete(QString symbol);
    Q_SIGNAL void ibkrConnectionChanged();
    Q_SIGNAL void ibkrStockDataUpdated(QString symbol);
    Q_SIGNAL void fundamentalDataChanged();
    Q_SIGNAL void fundamentalDataUpdated(QString symbol);

private:
    bool ensureSchema();
    void tryNextIbkrPort();
    void setIbkrConnectionState(const QString &status, bool connected, bool connecting);
    void finishIbkrDataRequest(int exitCode, QProcess::ExitStatus exitStatus);
    void startIbkrHelperRequest(const QString &candidateSymbol);
    void startIbkrNameSearchRequest();
    bool tryNextIbkrCandidate(const QString &lastError = QString());
    bool tryNextIbkrAmbiguousIsin();
    void finalizeIbkrDataFailure(const QString &message);
    void loadNextIbkrBatchSymbol();
    void scheduleNextIbkrBatchSymbol(int delayMs);
    void finishIbkrBatch(const QString &message);
    void loadNextIbkrGetStocksSymbol();
    void scheduleNextIbkrGetStocksSymbol(int delayMs);
    void finishIbkrGetStocksBatch(const QString &message);
    bool startIbkrQuoteExchangeProbeForSymbol(const QString &symbol);
    void startIbkrQuotesRequestForIsin(const QString &isin, int days);
    void startIbkrQuoteHelperRequest(bool probeExchange);
    void finishIbkrQuotesRequest(const QJsonObject &result);
    void finishIbkrQuoteExchangeProbe(const QJsonObject &result);
    bool saveIbkrHistoricalQuotes(const QString &symbol, const QJsonArray &bars);
    bool saveIbkrQuoteExchange(const QString &symbol,
                               const QString &quoteExchange,
                               double turnover,
                               const QString &bestDirectExchange = QString(),
                               double bestDirectTurnover = 0.0);
    void updateIbkrQuoteExchangeAttempt(const QString &symbol);
    void updateIbkrQuoteExchangeFailure(const QString &symbol, const QString &error);
    void updateIbkrQuoteExchangeSuccess(const QString &symbol);
    void markStockUseMarketstack(const QString &symbol, bool useMarketstack);
    void loadNextMarketstackBatchSymbol();
    void scheduleNextMarketstackBatchSymbol(int delayMs);
    void finishMarketstackBatch(const QString &message);
    void startMarketstackTickerLookup();
    void requestNextMarketstackTickerLookup();
    void handleMarketstackTickerLookupReply(QNetworkReply *reply);
    void requestNextMarketstackCandidateQuotes();
    void handleMarketstackCandidateQuotesReply(QNetworkReply *reply);
    bool saveMarketstackSelection(const QString &symbol,
                                  const QString &marketplaceSym,
                                  const QString &exchange,
                                  double turnover,
                                  const QString &error = QString());
    bool deleteMarketstackNoDataStock(const QString &symbol);
    void loadNextMarketstackQuotesBatchSymbol();
    void scheduleNextMarketstackQuotesBatchSymbol(int delayMs);
    void finishMarketstackQuotesBatch(const QString &message);
    void requestMarketstackQuotesForPendingSymbol();
    void handleMarketstackQuotesReply(QNetworkReply *reply);
    bool saveMarketstackHistoricalQuotes(const QString &symbol, const QJsonArray &bars);
    void loadNextMarketstackValidationBatchSymbol();
    void scheduleNextMarketstackValidationBatchSymbol(int delayMs);
    void finishMarketstackValidationBatch(const QString &message);
    void requestMarketstackValidationForPendingSymbol();
    void handleMarketstackValidationReply(QNetworkReply *reply);
    bool resetMarketstackMapping(const QString &symbol, const QString &error);
    void loadNextIbkrNameCheckBatchSymbol();
    void scheduleNextIbkrNameCheckBatchSymbol(int delayMs);
    void finishIbkrNameCheckBatch(const QString &message);
    void startIbkrNameCheckRequest(const QString &requestSymbol,
                                   const QString &ibkrSymbol,
                                   qint64 conId,
                                   const QString &currency,
                                   const QString &exchange,
                                   bool useIsin = false,
                                   bool directExchange = true);
    void prepareIbkrNameCheckCandidates(const QString &symbol,
                                        const QString &ibkrSymbol,
                                        const QString &localSymbol,
                                        const QString &currency,
                                        const QString &exchange);
    bool startNextIbkrNameCheckCandidate(const QString &lastError = QString());
    void finishIbkrNameCheckRequest(const QJsonObject &result);
    void updateIbkrBatchFailure(const QString &symbol, const QString &error);
    void updateIbkrBatchSuccess(const QString &symbol);
    bool markIbkrValidationIssue(const QString &symbol, const QString &status, const QString &message);
    bool saveIbkrContractDetails(const QString &symbol, const QVariantMap &details);
    bool deleteStockWithReferencedData(const QString &symbol);
    bool reserveAlphaVantageRequest();
    bool cacheAlphaVantageSymbol(const QString &symbol, const QString &alphaVantageSymbol);
    bool cacheYahooSymbol(const QString &symbol, const QString &yahooSymbol);
    bool saveAlphaVantageFundamentals(const QString &symbol, const QVariantMap &overview);
    bool saveYahooFundamentals(const QString &symbol, const QString &yahooSymbol, const QVariantMap &data);
    void updateYahooFundamentalAttempt(const QString &symbol);
    void updateYahooFundamentalSuccess(const QString &symbol, int qualityScore);
    void updateYahooFundamentalFailure(const QString &symbol, const QString &error);
    void fetchAlphaVantageFundamentalOverview(const QString &symbol, const QString &apiSymbol);
    void tryNextAlphaVantageCandidate();
    void fetchYahooFundamentalsFallback(const QString &symbol);
    void tryNextYahooCandidate();
    void loadNextYahooFundamentalsBatchSymbol();
    void scheduleNextYahooFundamentalsBatchSymbol(int delayMs);
    void finishYahooFundamentalsBatch(const QString &message);
    void resetFundamentalRequestState();
    void setFundamentalDataStatus(const QString &status, bool loading);
    QVariantMap extractStock(const QSqlQuery &query);
    QSqlDatabase db;
    AlphaVantageClient alphaVantageClient;
    YahooFinanceClient yahooFinanceClient;
    MarketStackClient marketStackClient; // Mitgliedsvariable hinzufügen
    QTcpSocket m_ibkrSocket;
    QTimer m_ibkrConnectTimeout;
    QList<quint16> m_ibkrPorts;
    qsizetype m_ibkrPortIndex = 0;
    QString m_ibkrConnectionStatus = QStringLiteral("IBKR-Verbindung wurde noch nicht geprüft.");
    bool m_ibkrConnected = false;
    bool m_ibkrConnecting = false;
    bool m_ibkrPortAdvanceInProgress = false;
    bool m_ibkrProbeDisconnectInProgress = false;
    bool m_ibkrDataLoading = false;
    quint16 m_ibkrConnectedPort = 0;
    QProcess m_ibkrProcess;
    QTimer m_ibkrDataTimeout;
    QTimer m_ibkrBatchTimer;
    QTimer m_ibkrGetStocksTimer;
    QTimer m_ibkrNameCheckBatchTimer;
    QTimer m_yahooFundamentalsBatchTimer;
    QTimer m_marketstackBatchTimer;
    QTimer m_marketstackQuotesBatchTimer;
    QTimer m_marketstackValidationBatchTimer;
    QNetworkAccessManager m_marketstackNetworkManager;
    QString m_ibkrPendingSymbol;
    QString m_pendingIbkrCurrency;
    QString m_pendingIbkrExchange;
    QString m_pendingIbkrIsin;
    QString m_pendingIbkrSearchKeywords;
    QStringList m_pendingIbkrNameSearchTerms;
    int m_pendingIbkrNameSearchIndex = 0;
    QString m_pendingIbkrLastError;
    QStringList m_pendingIbkrCandidateSymbols;
    QHash<QString, QString> m_pendingIbkrCandidateCurrencies;
    QHash<QString, QString> m_pendingIbkrCandidateExchanges;
    QString m_pendingIbkrCurrentCandidateSymbol;
    QStringList m_pendingIbkrAmbiguousIsinCandidates;
    QStringList m_pendingIbkrTriedAmbiguousIsins;
    int m_pendingIbkrCandidateIndex = 0;
    bool m_pendingIbkrSearchStarted = false;
    bool m_pendingIbkrNameSearchStarted = false;
    bool m_pendingIbkrProcessIsNameSearch = false;
    bool m_pendingIbkrProcessIsNameCheck = false;
    bool m_pendingIbkrProcessIsHistoricalQuotes = false;
    bool m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    bool m_pendingIbkrDataForNameCheckRecovery = false;
    bool m_pendingIbkrReviewRequired = false;
    QString m_pendingIbkrReviewReason;
    bool m_pendingIbkrTryWithoutIsin = false;
    bool m_pendingIbkrDirectExchange = false;
    QStringList m_pendingIbkrDirectExchanges;
    int m_pendingIbkrDirectExchangeIndex = 0;
    QString m_pendingIbkrCurrentDirectExchange;
    QString m_fundamentalDataStatus = QStringLiteral("Fundamentaldaten wurden noch nicht abgerufen.");
    bool m_fundamentalDataLoading = false;
    QString m_pendingFundamentalSymbol;
    QString m_pendingFundamentalSearchKeywords;
    QString m_pendingResolvedAlphaVantageSymbol;
    QStringList m_pendingAlphaVantageCandidates;
    int m_pendingAlphaVantageCandidateIndex = 0;
    QString m_pendingYahooSymbol;
    QString m_pendingPreferredYahooSuffix;
    QStringList m_pendingYahooCandidates;
    int m_pendingYahooCandidateIndex = 0;
    bool m_pendingYahooSearchStarted = false;
    QString m_pendingYahooLastError;
    QString m_pendingYahooBestSymbol;
    QVariantMap m_pendingYahooBestData;
    int m_pendingYahooBestScore = 0;
    bool m_yahooFundamentalsBatchActive = false;
    QStringList m_yahooFundamentalsBatchSymbols;
    int m_yahooFundamentalsBatchIndex = 0;
    int m_yahooFundamentalsBatchSuccessCount = 0;
    int m_yahooFundamentalsBatchFailureCount = 0;
    bool m_ibkrBatchActive = false;
    QStringList m_ibkrBatchSymbols;
    int m_ibkrBatchIndex = 0;
    int m_ibkrBatchSuccessCount = 0;
    int m_ibkrBatchFailureCount = 0;
    bool m_ibkrGetStocksBatchActive = false;
    QStringList m_ibkrGetStocksSymbols;
    int m_ibkrGetStocksIndex = 0;
    int m_ibkrGetStocksSuccessCount = 0;
    int m_ibkrGetStocksFailureCount = 0;
    bool m_marketstackBatchActive = false;
    QStringList m_marketstackBatchSymbols;
    int m_marketstackBatchIndex = 0;
    int m_marketstackBatchSuccessCount = 0;
    int m_marketstackBatchFailureCount = 0;
    bool m_marketstackQuotesBatchActive = false;
    QStringList m_marketstackQuotesBatchSymbols;
    int m_marketstackQuotesBatchIndex = 0;
    int m_marketstackQuotesBatchSuccessCount = 0;
    int m_marketstackQuotesBatchFailureCount = 0;
    int m_marketstackQuotesRateLimitRetries = 0;
    QString m_pendingMarketstackQuotesSymbol;
    QString m_pendingMarketstackQuotesMarketplaceSym;
    QString m_pendingMarketstackQuotesExchange;
    bool m_marketstackValidationBatchActive = false;
    QStringList m_marketstackValidationBatchSymbols;
    int m_marketstackValidationBatchIndex = 0;
    int m_marketstackValidationBatchSuccessCount = 0;
    int m_marketstackValidationBatchFailureCount = 0;
    int m_marketstackValidationRateLimitRetries = 0;
    QString m_pendingMarketstackValidationSymbol;
    QString m_pendingMarketstackValidationIsin;
    QString m_pendingMarketstackValidationName;
    QString m_pendingMarketstackValidationMarketplaceSym;
    QString m_pendingMarketstackValidationExchange;
    QString m_pendingMarketstackSymbol;
    QString m_pendingMarketstackIsin;
    QString m_pendingMarketstackName;
    QString m_pendingMarketstackMic;
    QString m_pendingMarketstackIbkrResolvedSymbol;
    QString m_pendingMarketstackYahooSymbol;
    QString m_pendingMarketstackAlphaVantageSymbol;
    QList<QUrl> m_pendingMarketstackLookupUrls;
    int m_pendingMarketstackLookupIndex = 0;
    QVariantList m_pendingMarketstackCandidates;
    int m_pendingMarketstackCandidateIndex = 0;
    int m_pendingMarketstackRateLimitRetries = 0;
    QString m_pendingMarketstackBestMarketplaceSym;
    QString m_pendingMarketstackBestExchange;
    double m_pendingMarketstackBestTurnover = 0.0;
    bool m_pendingMarketstackBestHasQuotes = false;
    bool m_ibkrNameCheckBatchActive = false;
    QStringList m_ibkrNameCheckBatchSymbols;
    int m_ibkrNameCheckBatchIndex = 0;
    int m_ibkrNameCheckBatchSuccessCount = 0;
    int m_ibkrNameCheckBatchFailureCount = 0;
    QString m_pendingIbkrNameCheckSymbol;
    QString m_pendingIbkrNameCheckName;
    QString m_pendingIbkrNameCheckIsin;
    QString m_pendingIbkrQuotesSymbol;
    QString m_pendingIbkrQuotesIsin;
    QString m_pendingIbkrQuotesIbkrSymbol;
    QString m_pendingIbkrQuotesCurrency;
    QString m_pendingIbkrQuotesExchange;
    QString m_pendingIbkrQuotesPrimaryExchange;
    QStringList m_pendingIbkrQuotesProbeExchanges;
    qint64 m_pendingIbkrQuotesConId = 0;
    int m_pendingIbkrQuotesDays = 0;
    int m_pendingIbkrQuotesFallbackIndex = 0;
    bool m_pendingIbkrQuotesSupportsSmart = false;
    bool m_pendingIbkrQuotesForceDirectProbeResult = false;
    bool m_pendingIbkrNameCheckHasConId = false;
    bool m_pendingIbkrNameCheckRequestUsesIsin = false;
    QStringList m_pendingIbkrNameCheckCandidates;
    int m_pendingIbkrNameCheckCandidateIndex = 0;
    QString getISINFromOpenFIGI(const QString &apiKey, const QString &ticker, const QString &exchangeCode);

    void updateShare(const ShareData &share);
    void saveShare(const ShareData &share);
    QVariantList runShareQuery(const QString& sql);
    QString convertToEodTicker(const QString& symbol);

    QString buildShareQuery(
        int firstTo, int firstThreshold, bool firstGreaterThan,
        int secondTo, int secondThreshold, bool secondGreaterThan,
        int thirdTo, int thirdThreshold, bool thirdGreaterThan,
        int fourthTo, int fourthThreshold, bool fourthGreaterThan,
        int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol,
        const QString& name = QString());


};

#endif // DATABASEMANAGER_H
