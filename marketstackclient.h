#ifndef MARKETSTACKCLIENT_H
#define MARKETSTACKCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QDate>
#include <QMap>
#include <QUrl>

class MarketStackClient : public QObject {
    Q_OBJECT
public:
    explicit MarketStackClient(QObject *parent = nullptr);

    // 🔥 Methode für historische Daten (JETZT QML-AUFRUFBAR)
    Q_INVOKABLE void fetchHistoricalData(const QString &symbol, const QString &exchange ,const QDate &fromDate, int limit);

signals:
    void historicalDataReceived(const QMap<QString, QVariantMap> &data);
    void errorOccurred(const QString &error);

private slots:
    void handleHistoricalDataReply(QNetworkReply *reply);

private:
    QNetworkAccessManager networkManager;
    QString apiKey  = "2c7445a74b7f5ed6371d655f39ab4f4f"; ;

    QUrl buildHistoricalDataUrl(const QString &symbol, const QString &exchange, const QDate &fromDate, int limit);
    QMap<QString, QVariantMap> parseHistoricalData(const QJsonDocument &jsonDoc) const;
};

#endif // MARKETSTACKCLIENT_H
