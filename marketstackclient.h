#ifndef MARKETSTACKCLIENT_H
#define MARKETSTACKCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QList>
#include "sharedata.h"

class MarketStackClient : public QObject {
    Q_OBJECT
public:
    explicit MarketStackClient(QObject *parent = nullptr);

    // 🔥 Methode für historische Daten (JETZT QML-AUFRUFBAR)
    Q_INVOKABLE void fetchHistoricalData(const QString &symbol, const QString &exchange ,const QDate &fromDate, int limit);

    // Methode zum Abrufen von Aktieninformationen basierend auf dem Land
    void getShares(const QString &symbol);
    ShareData getShare(const QString &exchange, const QString &symbol);

signals:
    void historicalDataReceived(const QMap<QString, QVariantMap> &data);
    void sharesReceived(const QList<ShareData> &shares);
    void errorOccurred(const QString &error);

private slots:
    void handleHistoricalDataReply(QNetworkReply *reply);
    bool handleSharesReply(QNetworkReply *reply, int offset);

private:
    QNetworkAccessManager networkManager;
    QString apiKey  = "2c7445a74b7f5ed6371d655f39ab4f4f"; ;

    QUrl buildHistoricalDataUrl(const QString &symbol, const QString &exchange, const QDate &fromDate, int limit);
    QMap<QString, QVariantMap> parseHistoricalData(const QJsonDocument &jsonDoc) const;
    QList<ShareData> parseSharesData(const QJsonDocument &jsonDoc) const;
    void fetchSharesPage(const QString &exchange, int offset);
};

#endif // MARKETSTACKCLIENT_H
