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

#include "sharedata.h"
#include "marketstackclient.h"

//J2H71H3K5SQLU2PS Alpha Vantage
//2c7445a74b7f5ed6371d655f39ab4f4f marketstack

class DatabaseManager : public QObject
{
    Q_OBJECT
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

    Q_INVOKABLE void updateAllISINs();


    // In DatabaseManager.h, add this signal:
    Q_SIGNAL void getSharesComplete(const QVariantList &shares);

signals:
    Q_SIGNAL void saveComplete(QString symbol);

private:
    QVariantMap extractStock(const QSqlQuery &query);
    QSqlDatabase db;
    MarketStackClient marketStackClient; // Mitgliedsvariable hinzufügen
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
        int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol);


};

#endif // DATABASEMANAGER_H
