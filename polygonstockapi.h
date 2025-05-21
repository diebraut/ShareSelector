// polygonstockapi.h
#ifndef POLYGONSTOCKAPI_H
#define POLYGONSTOCKAPI_H

#include "sharedata.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QList>

class PolygonStockAPI : public QObject
{
    Q_OBJECT
public:
    explicit PolygonStockAPI(const QString& apiKey, QObject* parent = nullptr);
    void getShares(const QString& fromCountry);

signals:
    void sharesReceived(const QList<ShareData>& shares);
    void errorOccurred(const QString& errorMessage);

private slots:
    void handleReply(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_manager;
    QString m_apiKey;

    QUrl buildRequestUrl(const QString& country) const;
    QList<ShareData> parseResponse(const QJsonDocument& jsonDoc) const;
};

#endif // POLYGONSTOCKAPI_H
