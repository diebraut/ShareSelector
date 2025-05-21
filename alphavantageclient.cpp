#include "AlphaVantageClient.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

AlphaVantageClient::AlphaVantageClient(QObject *parent) : QObject(parent) {}

void AlphaVantageClient::fetchHistoricalData(const QString &symbol, const QString &exchange) {
    if (!exchange.isEmpty()) {
        searchExactSymbol(symbol, exchange);
    } else {
        fetchTimeSeries(symbol);
    }
}

void AlphaVantageClient::searchExactSymbol(const QString &symbol, const QString &exchange) {
    QUrl url("https://www.alphavantage.co/query");
    QUrlQuery query;
    query.addQueryItem("function", "SYMBOL_SEARCH");
    query.addQueryItem("keywords", symbol);
    query.addQueryItem("apikey", apiKey);
    query.addQueryItem("datatype", "json");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "🔍 Searching exact symbol for:" << symbol << " on exchange:" << exchange;
    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol, exchange]() {
        handleSearchReply(reply, symbol, exchange);
    });
}

void AlphaVantageClient::handleSearchReply(QNetworkReply *reply, const QString &symbol, const QString &exchange) {
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    qDebug() << "API-Antwort:" << QString(responseData);

    reply->deleteLater();

    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
    QJsonObject jsonObject = jsonDoc.object();

    if (!jsonObject.contains("bestMatches")) {
        emit errorOccurred("❌ Ungültige API-Antwort: " + QString(responseData));
        return;
    }

    QJsonArray results = jsonObject["bestMatches"].toArray();
    QString foundSymbol = getExactSymbolFromSearch(results, exchange);

    if (!foundSymbol.isEmpty()) {
        qDebug() << "✅ Exact match found for exchange:" << exchange << "->" << foundSymbol;
        fetchTimeSeries(foundSymbol);
    } else {
        emit errorOccurred("❌ Kein passender Ticker für " + symbol + " auf " + exchange + " gefunden.");
    }
}

QString AlphaVantageClient::getExactSymbolFromSearch(const QJsonArray &results, const QString &exchange) {
    QString fallbackSymbol;  // Falls keine 100% Übereinstimmung gefunden wird

    for (const QJsonValue &value : results) {
        QJsonObject match = value.toObject();
        QString matchedSymbol = match["1. symbol"].toString();
        QString matchedRegion = match["4. region"].toString();  // Gibt nur Regionen wie "United States" zurück

        // 100% Treffer: Falls die Region exakt zur Exchange passt
        if (matchedRegion.contains(exchange, Qt::CaseInsensitive)) {
            return matchedSymbol;  // Perfektes Match gefunden!
        }

        // Fallback: Falls es aus "United States" kommt, speichern
        if (matchedRegion == "United States" && fallbackSymbol.isEmpty()) {
            fallbackSymbol = matchedSymbol;
        }
    }

    return fallbackSymbol;  // Falls nichts 100% passt, nimm den ersten US-Treffer
}

void AlphaVantageClient::fetchTimeSeries(const QString &symbol) {
    QUrl url("https://www.alphavantage.co/query");
    QUrlQuery query;
    query.addQueryItem("function", "TIME_SERIES_DAILY");
    query.addQueryItem("symbol", symbol);
    query.addQueryItem("apikey", apiKey);
    query.addQueryItem("outputsize", "compact");  // Nur letztes Jahr
    query.addQueryItem("datatype", "json");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "📈 Fetching time series data for:" << symbol;
    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleTimeSeriesReply(reply);
    });
}

void AlphaVantageClient::handleTimeSeriesReply(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
    QJsonObject jsonObject = jsonDoc.object();

    if (!jsonObject.contains("Time Series (Daily)")) {
        emit errorOccurred("Ungültige API-Antwort: " + QString(responseData));
        return;
    }

    QJsonObject timeSeries = jsonObject["Time Series (Daily)"].toObject();
    QMap<QString, QVariantMap> historicalData;

    QStringList dates = timeSeries.keys();
    std::sort(dates.begin(), dates.end(), std::greater<QString>());

    int count = 0;
    for (const QString &date : dates) {
        if (count >= 365) break;
        QJsonObject dayData = timeSeries[date].toObject();
        QVariantMap values = {
            {"1. open", dayData["1. open"].toString()},
            {"2. high", dayData["2. high"].toString()},
            {"3. low", dayData["3. low"].toString()},
            {"4. close", dayData["4. close"].toString()},
            {"5. volume", dayData["5. volume"].toString()}
        };
        historicalData.insert(date, values);
        count++;
    }

    emit historicalDataReceived(historicalData);
}
