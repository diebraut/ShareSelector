#include "AlphaVantageClient.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>

AlphaVantageClient::AlphaVantageClient(QObject *parent) : QObject(parent) {}

namespace {
QStringList searchTokens(const QString &keywords)
{
    QString normalized = keywords.toUpper();
    normalized.replace(QRegularExpression(QStringLiteral("[^A-Z0-9]+")), QStringLiteral(" "));
    return normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

int fundamentalSymbolScore(const QJsonObject &match, const QStringList &tokens)
{
    const QString symbol = match.value(QStringLiteral("1. symbol")).toString();
    const QString name = match.value(QStringLiteral("2. name")).toString().toUpper();
    const QString type = match.value(QStringLiteral("3. type")).toString();
    const QString region = match.value(QStringLiteral("4. region")).toString();

    int score = 0;
    if (type.compare(QStringLiteral("Equity"), Qt::CaseInsensitive) == 0)
        score += 30;
    if (region.compare(QStringLiteral("United States"), Qt::CaseInsensitive) == 0)
        score += 35;
    if (!symbol.contains(QLatin1Char('.')))
        score += 25;
    if (!symbol.isEmpty() && !symbol.at(0).isDigit())
        score += 10;

    for (const QString &token : tokens) {
        if (name.contains(token))
            score += 12;
    }
    return score;
}
}

void AlphaVantageClient::fetchHistoricalData(const QString &symbol, const QString &exchange) {
    if (!exchange.isEmpty()) {
        searchExactSymbol(symbol, exchange);
    } else {
        fetchTimeSeries(symbol);
    }
}

void AlphaVantageClient::fetchFundamentalOverview(const QString &symbol)
{
    QUrl url("https://www.alphavantage.co/query");
    QUrlQuery query;
    query.addQueryItem("function", "OVERVIEW");
    query.addQueryItem("symbol", symbol);
    query.addQueryItem("apikey", apiKey);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "Fetching Alpha Vantage fundamentals for:" << symbol;
    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        handleFundamentalOverviewReply(reply, symbol);
    });
}

void AlphaVantageClient::resolveFundamentalSymbol(const QString &requestSymbol, const QString &keywords)
{
    QUrl url("https://www.alphavantage.co/query");
    QUrlQuery query;
    query.addQueryItem("function", "SYMBOL_SEARCH");
    query.addQueryItem("keywords", keywords);
    query.addQueryItem("apikey", apiKey);
    query.addQueryItem("datatype", "json");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "Resolving Alpha Vantage symbol for:" << requestSymbol << "keywords:" << keywords;
    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestSymbol, keywords]() {
        handleFundamentalSymbolSearchReply(reply, requestSymbol, keywords);
    });
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

void AlphaVantageClient::handleFundamentalSymbolSearchReply(QNetworkReply *reply,
                                                            const QString &requestSymbol,
                                                            const QString &keywords)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit fundamentalSymbolResolveFailed(requestSymbol, reply->errorString());
        reply->deleteLater();
        return;
    }

    const QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        emit fundamentalSymbolResolveFailed(
            requestSymbol,
            QStringLiteral("Ungueltige Alpha-Vantage-Suchantwort."));
        return;
    }

    const QJsonObject jsonObject = jsonDoc.object();
    const QString information = jsonObject.value(QStringLiteral("Information")).toString();
    const QString note = jsonObject.value(QStringLiteral("Note")).toString();
    if (!information.isEmpty() || !note.isEmpty()) {
        emit fundamentalSymbolResolveFailed(requestSymbol, !information.isEmpty() ? information : note);
        return;
    }

    struct Candidate {
        QString symbol;
        int score;
    };

    const QJsonArray results = jsonObject.value(QStringLiteral("bestMatches")).toArray();
    const QStringList tokens = searchTokens(keywords);
    QList<Candidate> candidates;
    QSet<QString> seenSymbols;
    for (const QJsonValue &value : results) {
        const QJsonObject match = value.toObject();
        const QString symbol = match.value(QStringLiteral("1. symbol")).toString().trimmed();
        if (symbol.isEmpty() || seenSymbols.contains(symbol))
            continue;

        seenSymbols.insert(symbol);
        candidates.append({symbol, fundamentalSymbolScore(match, tokens)});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &left, const Candidate &right) {
        if (left.score != right.score)
            return left.score > right.score;
        return left.symbol < right.symbol;
    });

    QStringList resolvedSymbols;
    for (const Candidate &candidate : candidates) {
        resolvedSymbols << candidate.symbol;
        if (resolvedSymbols.size() >= 2)
            break;
    }

    if (resolvedSymbols.isEmpty()) {
        emit fundamentalSymbolResolveFailed(
            requestSymbol,
            QStringLiteral("Kein Alpha-Vantage-Symbol fuer %1 gefunden.").arg(keywords));
        return;
    }

    emit fundamentalSymbolResolved(requestSymbol, resolvedSymbols);
}

void AlphaVantageClient::handleFundamentalOverviewReply(QNetworkReply *reply, const QString &symbol)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(reply->errorString());
        reply->deleteLater();
        return;
    }

    const QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        emit errorOccurred("Ungueltige Alpha-Vantage-Antwort: " + QString::fromUtf8(responseData));
        return;
    }

    const QJsonObject jsonObject = jsonDoc.object();
    const QString information = jsonObject.value("Information").toString();
    const QString note = jsonObject.value("Note").toString();
    if (!information.isEmpty() || !note.isEmpty()) {
        emit errorOccurred(!information.isEmpty() ? information : note);
        return;
    }

    if (jsonObject.value("Symbol").toString().trimmed().isEmpty()) {
        emit fundamentalOverviewNotFound(symbol);
        return;
    }

    emit fundamentalOverviewReceived(symbol, jsonObject.toVariantMap());
}
