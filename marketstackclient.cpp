#include "marketstackclient.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QDebug>
#include "sharedata.h"

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

void MarketStackClient::getShares(const QString &exchange) {
    int offset = 0;  // Start der Pagination
    fetchSharesPage(exchange, offset);  // Starte den ersten Aufruf
}

#include <QEventLoop>

ShareData MarketStackClient::getShare(const QString &exchange, const QString &symbol) {
    QUrl url("http://api.marketstack.com/v1/tickers");
    QUrlQuery query;
    query.addQueryItem("access_key", apiKey);
    query.addQueryItem("limit", "1");
    query.addQueryItem("symbols", symbol);

    if (!exchange.isEmpty()) {
        query.addQueryItem("exchange", exchange);
    }

    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QEventLoop loop;
    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();  // Warte, bis die Anfrage abgeschlossen ist

    ShareData result;

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "❌ API-Fehler:" << reply->errorString();
        reply->deleteLater();
        return result;  // Leeres ShareData-Objekt zurückgeben
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
    QJsonObject jsonObject = jsonDoc.object();

    if (!jsonObject.contains("data")) {
        qDebug() << "❌ Fehlerhafte Antwort: kein 'data'-Feld";
        return result;
    }

    QJsonArray dataArray = jsonObject["data"].toArray();
    if (dataArray.isEmpty()) {
        qDebug() << "❌ Keine Daten gefunden für Symbol:" << symbol;
        return result;
    }

    QJsonObject shareObject = dataArray.first().toObject();
    result = ShareData::fromJson(shareObject);
    return result;
}



void MarketStackClient::fetchSharesPage(const QString &exchange, int offset) {
    QUrl url("http://api.marketstack.com/v1/tickers");
    QUrlQuery query;
    query.addQueryItem("access_key", apiKey);
    query.addQueryItem("limit", "1");  // Maximales Limit für eine Anfrage
    query.addQueryItem("offset", QString::number(offset));

    if (!exchange.isEmpty()) {
        query.addQueryItem("exchange", exchange);
    }

    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, exchange, offset]() {
        bool hasMoreData = handleSharesReply(reply, offset);
        if (hasMoreData) {
            // Lade die nächste Seite
            fetchSharesPage(exchange, offset + 1000);
        }
    });
}

bool MarketStackClient::handleSharesReply(QNetworkReply *reply, int offset) {
    if (reply->error() != QNetworkReply::NoError) {
        // API-Fehler: Stoppe den Vorgang und gebe Debug-Informationen aus
        qDebug() << "❌ API-Fehler aufgetreten:";
        qDebug() << "Fehlercode:" << reply->error();
        qDebug() << "Fehlermeldung:" << reply->errorString();
        emit errorOccurred(reply->errorString());
        reply->deleteLater();
        return false;
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
        return false;
    }

    if (!jsonObject.contains("data")) {
        qDebug() << "❌ Fehler: Ungültige API-Antwort. Keine 'data' vorhanden.";
        emit errorOccurred("Ungültige API-Antwort. Keine 'data' vorhanden.");
        return false;
    }

    // Direkte Übergabe der geparsten Daten an das Signal
    emit sharesReceived(parseSharesData(jsonDoc));

    // Debug-Ausgabe: Informationen zur aktuellen Seite
    qDebug() << "Seite erfolgreich gelesen:";
    qDebug() << "Offset:" << offset;
    qDebug() << "Anzahl der Shares in dieser Seite:" << parseSharesData(jsonDoc).size();
    qDebug() << "Empfangene Daten (Auszug):" << responseData.left(200);  // Zeigt die ersten 200 Zeichen der Antwort an

    // Prüfen, ob mehr Daten verfügbar sind
    if (jsonObject.contains("pagination")) {
        QJsonObject pagination = jsonObject["pagination"].toObject();
        int total = pagination["total"].toInt();
        int count = pagination["count"].toInt();
        bool hasMoreData = (offset + count) < total;

        // Debug-Ausgabe: Paginationsinformationen
        qDebug() << "Pagination:";
        qDebug() << "Total:" << total;
        qDebug() << "Count:" << count;
        qDebug() << "Has more data:" << hasMoreData;

        return hasMoreData;
    }

    return false;  // Keine weiteren Daten verfügbar
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

QList<ShareData> MarketStackClient::parseSharesData(const QJsonDocument &jsonDoc) const {
    QList<ShareData> shares;
    QJsonArray dataArray = jsonDoc.object()["data"].toArray();

    for (const QJsonValue &value : dataArray) {
        QJsonObject obj = value.toObject();
        ShareData share = ShareData::fromJson(obj);
        shares.append(share);
    }

    return shares;
}

