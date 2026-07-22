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
#include <QTcpSocket>
#include <QTimer>
#include <QProcess>

#include "sharedata.h"
#include "marketstackclient.h"

//J2H71H3K5SQLU2PS Alpha Vantage
//2c7445a74b7f5ed6371d655f39ab4f4f marketstack

class DatabaseManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString ibkrConnectionStatus READ ibkrConnectionStatus NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(bool ibkrConnected READ ibkrConnected NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(bool ibkrConnecting READ ibkrConnecting NOTIFY ibkrConnectionChanged)
    Q_PROPERTY(bool ibkrDataLoading READ ibkrDataLoading NOTIFY ibkrConnectionChanged)
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
    Q_INVOKABLE QVariantList getBoughtStocks();
    Q_INVOKABLE QVariantList getTestPortfolio();
    Q_INVOKABLE void connectToIbkr();
    Q_INVOKABLE void getIbkrData(const QString &symbol);
    QString ibkrConnectionStatus() const;
    bool ibkrConnected() const;
    bool ibkrConnecting() const;
    bool ibkrDataLoading() const;
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

private:
    bool ensureSchema();
    void tryNextIbkrPort();
    void setIbkrConnectionState(const QString &status, bool connected, bool connecting);
    void finishIbkrDataRequest(int exitCode, QProcess::ExitStatus exitStatus);
    bool saveIbkrContractDetails(const QString &symbol, const QVariantMap &details);
    QVariantMap extractStock(const QSqlQuery &query);
    QSqlDatabase db;
    MarketStackClient marketStackClient; // Mitgliedsvariable hinzufügen
    QTcpSocket m_ibkrSocket;
    QTimer m_ibkrConnectTimeout;
    QList<quint16> m_ibkrPorts;
    qsizetype m_ibkrPortIndex = 0;
    QString m_ibkrConnectionStatus = QStringLiteral("IBKR-Verbindung wurde noch nicht geprüft.");
    bool m_ibkrConnected = false;
    bool m_ibkrConnecting = false;
    bool m_ibkrPortAdvanceInProgress = false;
    bool m_ibkrDataLoading = false;
    quint16 m_ibkrConnectedPort = 0;
    QProcess m_ibkrProcess;
    QTimer m_ibkrDataTimeout;
    QString m_ibkrPendingSymbol;
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
