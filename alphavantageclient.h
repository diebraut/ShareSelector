#ifndef ALPHAVANTAGECLIENT_H
#define ALPHAVANTAGECLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QVariantMap>

class AlphaVantageClient : public QObject {
    Q_OBJECT
public:
    explicit AlphaVantageClient(QObject *parent = nullptr);

    // 🔥 Methode zum Abrufen historischer Daten mit optionaler Exchange-Angabe
    void fetchHistoricalData(const QString &symbol, const QString &exchange = "");
    void fetchFundamentalOverview(const QString &symbol);
    void resolveFundamentalSymbol(const QString &requestSymbol, const QString &keywords);

signals:
    void historicalDataReceived(const QMap<QString, QVariantMap> &data);
    void fundamentalOverviewReceived(const QString &symbol, const QVariantMap &data);
    void fundamentalOverviewNotFound(const QString &symbol);
    void fundamentalSymbolResolved(const QString &requestSymbol, const QStringList &resolvedSymbols);
    void fundamentalSymbolResolveFailed(const QString &requestSymbol, const QString &message);
    void errorOccurred(const QString &error);

private slots:
    void handleSearchReply(QNetworkReply *reply, const QString &symbol, const QString &exchange);
    void handleFundamentalSymbolSearchReply(QNetworkReply *reply, const QString &requestSymbol, const QString &keywords);
    void handleTimeSeriesReply(QNetworkReply *reply);
    void handleFundamentalOverviewReply(QNetworkReply *reply, const QString &symbol);
    void fetchTimeSeries(const QString &symbol);

private:
    QString getExactSymbolFromSearch(const QJsonArray &results, const QString &exchange);
    void searchExactSymbol(const QString &symbol, const QString &exchange);

    QNetworkAccessManager networkManager;
    QString apiKey = "J2H71H3K5SQLU2PS";  // 🔑 API-Key hier einfügen
};

#endif // ALPHAVANTAGECLIENT_H
