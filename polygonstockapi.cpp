// polygonstockapi.cpp
#include "polygonstockapi.h"
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QDebug>
// Füge am Anfang der Datei hinzu
#include <stdexcept>  // Für std::runtime_error
PolygonStockAPI::PolygonStockAPI(const QString& apiKey, QObject* parent)
    : QObject(parent), m_apiKey(apiKey)
{
    m_manager = new QNetworkAccessManager(this);
    connect(m_manager, &QNetworkAccessManager::finished, this, &PolygonStockAPI::handleReply);
}

void PolygonStockAPI::getShares(const QString& fromCountry)
{
    if (m_apiKey.isEmpty()) {
        emit errorOccurred("API-Key nicht gesetzt");
        return;
    }

    QNetworkRequest request(buildRequestUrl(fromCountry));
    m_manager->get(request);
}

QUrl PolygonStockAPI::buildRequestUrl(const QString& country) const
{
    QUrl url("https://api.polygon.io/v3/reference/tickers");
    QUrlQuery query;

    query.addQueryItem("type", "CS");
    query.addQueryItem("market", "stocks");
    query.addQueryItem("locale", country.toUpper());
    query.addQueryItem("active", "true");
    query.addQueryItem("sort", "ticker");
    query.addQueryItem("limit", "1000");
    query.addQueryItem("apiKey", m_apiKey);

    url.setQuery(query);
    return url;
}

void PolygonStockAPI::handleReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(QString("Netzwerkfehler: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    const QByteArray data = reply->readAll();
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

    if (jsonDoc.isNull()) {
        emit errorOccurred("Ungültiges JSON-Format");
        reply->deleteLater();
        return;
    }

    try {
        QList<ShareData> shares = parseResponse(jsonDoc);
        emit sharesReceived(shares);
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Parse-Fehler: %1").arg(e.what()));
    }

    reply->deleteLater();
}

QList<ShareData> PolygonStockAPI::parseResponse(const QJsonDocument& jsonDoc) const
{
    const QJsonObject rootObj = jsonDoc.object();

    if (rootObj["status"].toString() != "OK") {
        throw std::runtime_error("API-Status nicht OK");
    }

    if (!rootObj.contains("results") || !rootObj["results"].isArray()) {
        throw std::runtime_error("Ungültiges Antwortformat");
    }

    QList<ShareData> shares;
    const QJsonArray results = rootObj["results"].toArray();

    for (const QJsonValue& value : results) {
        if (!value.isObject()) {
            qWarning() << "Überspringe ungültigen Datensatz";
            continue;
        }

        shares.append(ShareData::fromJson(value.toObject()));
    }

    return shares;
}
