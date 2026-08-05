#include "marketstackclient.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QDebug>

MarketStackClient::MarketStackClient(QObject *parent)
    : QObject(parent) {}

void MarketStackClient::fetchHistoricalData(const QString &symbol, const QString &exchange, const QDate &fromDate, int limit) {
    QUrl url = buildHistoricalDataUrl(symbol, exchange, fromDate, limit);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleHistoricalDataReply(reply); });
}

QUrl MarketStackClient::buildHistoricalDataUrl(const QString &symbol, const QString &exchange, const QDate &fromDate, int limit)  {
    QUrl url("http://api.marketstack.com/v1/eod");
    QUrlQuery query;
    query.addQueryItem("access_key", apiKey);
    query.addQueryItem("symbols", symbol);
    query.addQueryItem("exchange", exchange);
    query.addQueryItem("limit", QString::number(limit));  // Maximales Limit für eine Anfrage
    query.addQueryItem("date_from", fromDate.toString("yyyy-MM-dd"));
    query.addQueryItem("date_to", QDate::currentDate().toString("yyyy-MM-dd"));
    url.setQuery(query);
    qDebug() << url.query();
    return url;
}

void MarketStackClient::handleHistoricalDataReply(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        // API-Fehler: Stoppe den Vorgang und gebe Debug-Informationen aus
        qDebug() << "❌ API-Fehler aufgetreten:";
        qDebug() << "Fehlercode:" << reply->error();
        qDebug() << "Fehlermeldung:" << reply->errorString();
        emit errorOccurred(reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
    QJsonObject jsonObject = jsonDoc.object();

    // Überprüfe, ob die API einen Fehler zurückgibt (z. B. bei ungültigem API-Schlüssel)
    if (jsonObject.contains("error")) {
        QJsonObject errorObject = jsonObject["error"].toObject();
        int errorCode = errorObject["code"].toInt();
        QString errorMessage = errorObject["message"].toString();

        qDebug() << "❌ API-Fehler aufgetreten:";
        qDebug() << "Fehlercode:" << errorCode;
        qDebug() << "Fehlermeldung:" << errorMessage;
        emit errorOccurred(errorMessage);
        return;
    }

    if (!jsonObject.contains("data")) {
        qDebug() << "❌ Fehler: Ungültige API-Antwort. Keine 'data' vorhanden.";
        emit errorOccurred("Ungültige API-Antwort. Keine 'data' vorhanden.");
        return;
    }

    QMap<QString, QVariantMap> data = parseHistoricalData(jsonDoc);
    emit historicalDataReceived(data);
}


QMap<QString, QVariantMap> MarketStackClient::parseHistoricalData(const QJsonDocument &jsonDoc) const {
    QMap<QString, QVariantMap> result;
    QJsonArray dataArray = jsonDoc.object()["data"].toArray();

    for (const QJsonValue &value : dataArray) {
        QJsonObject obj = value.toObject();
        QString date = obj["date"].toString();
        QVariantMap map;
        map["open"] = obj["open"].toDouble();
        map["high"] = obj["high"].toDouble();
        map["low"] = obj["low"].toDouble();
        map["close"] = obj["close"].toDouble();
        map["volume"] = obj["volume"].toDouble();
        result[date] = map;
    }

    return result;
}

