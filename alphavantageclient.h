#ifndef ALPHAVANTAGECLIENT_H
#define ALPHAVANTAGECLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>

class AlphaVantageClient : public QObject {
    Q_OBJECT
public:
    explicit AlphaVantageClient(QObject *parent = nullptr);

    // 🔥 Methode zum Abrufen historischer Daten mit optionaler Exchange-Angabe
    void fetchHistoricalData(const QString &symbol, const QString &exchange = "");

signals:
    void historicalDataReceived(const QMap<QString, QVariantMap> &data);
    void errorOccurred(const QString &error);

private slots:
    void handleSearchReply(QNetworkReply *reply, const QString &symbol, const QString &exchange);
    void handleTimeSeriesReply(QNetworkReply *reply);
    void fetchTimeSeries(const QString &symbol);

private:
    QString getExactSymbolFromSearch(const QJsonArray &results, const QString &exchange);
    void searchExactSymbol(const QString &symbol, const QString &exchange);

    QNetworkAccessManager networkManager;
    QString apiKey = "J2H71H3K5SQLU2PS";  // 🔑 API-Key hier einfügen
};

#endif // ALPHAVANTAGECLIENT_H
