#include "yahoofinanceclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace {
QVariant yahooValue(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (value.isObject()) {
        const QJsonObject valueObject = value.toObject();
        if (valueObject.contains(QStringLiteral("raw")))
            return valueObject.value(QStringLiteral("raw")).toVariant();
        if (valueObject.contains(QStringLiteral("fmt")))
            return valueObject.value(QStringLiteral("fmt")).toVariant();
    }
    return value.toVariant();
}

QVariantMap yahooModule(const QJsonObject &root, const QString &name)
{
    return root.value(name).toObject().toVariantMap();
}

QVariant rawJsonNumber(const QString &content, const QString &key)
{
    const QString escapedKey = QRegularExpression::escape(key);
    const QRegularExpression expression(
        QStringLiteral("\\\\?\"%1\\\\?\"\\s*:\\s*\\{\\s*\\\\?\"raw\\\\?\"\\s*:\\s*([-+0-9.eE]+)")
            .arg(escapedKey));
    QRegularExpressionMatch match = expression.match(content);
    if (!match.hasMatch())
        return QVariant();

    bool ok = false;
    const double value = match.captured(1).toDouble(&ok);
    return ok ? QVariant(value) : QVariant();
}

QVariant rawJsonString(const QString &content, const QString &key)
{
    const QRegularExpression expression(
        QStringLiteral("\\\\?\"%1\\\\?\"\\s*:\\s*\\\\?\"([^\"\\\\]+)")
            .arg(QRegularExpression::escape(key)));
    const QRegularExpressionMatch match = expression.match(content);
    return match.hasMatch() ? QVariant(match.captured(1)) : QVariant();
}

QVariant compactNumber(const QString &value)
{
    QString text = value.trimmed();
    if (text.isEmpty() || text == QStringLiteral("--"))
        return QVariant();

    text.remove(QLatin1Char(','));
    double multiplier = 1.0;
    const QChar suffix = text.at(text.size() - 1).toUpper();
    if (suffix == QLatin1Char('K') || suffix == QLatin1Char('M')
        || suffix == QLatin1Char('B') || suffix == QLatin1Char('T')) {
        text.chop(1);
        if (suffix == QLatin1Char('K'))
            multiplier = 1000.0;
        else if (suffix == QLatin1Char('M'))
            multiplier = 1000000.0;
        else if (suffix == QLatin1Char('B'))
            multiplier = 1000000000.0;
        else if (suffix == QLatin1Char('T'))
            multiplier = 1000000000000.0;
    }

    bool ok = false;
    const double number = text.toDouble(&ok);
    return ok ? QVariant(number * multiplier) : QVariant();
}

QVariant finStreamerValue(const QString &content, const QString &field)
{
    const QRegularExpression expression(
        QStringLiteral("<fin-streamer[^>]*data-value=\"([^\"]*)\"[^>]*data-field=\"%1\"")
            .arg(QRegularExpression::escape(field)));
    const QRegularExpressionMatch match = expression.match(content);
    return match.hasMatch() ? compactNumber(match.captured(1)) : QVariant();
}

QStringList searchTokens(const QString &keywords)
{
    QString normalized = keywords.toUpper();
    normalized.replace(QRegularExpression(QStringLiteral("[^A-Z0-9]+")), QStringLiteral(" "));
    return normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

int yahooSearchScore(const QJsonObject &quote, const QStringList &tokens)
{
    const QString symbol = quote.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
    const QString exchange = quote.value(QStringLiteral("exchange")).toString().trimmed().toUpper();
    const QString quoteType = quote.value(QStringLiteral("quoteType")).toString().trimmed().toUpper();
    const QString name = (quote.value(QStringLiteral("shortname")).toString()
                          + QLatin1Char(' ')
                          + quote.value(QStringLiteral("longname")).toString()).toUpper();

    int score = int(quote.value(QStringLiteral("score")).toDouble() / 100.0);
    if (quoteType == QStringLiteral("EQUITY"))
        score += 500;
    else if (quoteType == QStringLiteral("ETF")
             || quoteType == QStringLiteral("ETN")
             || quoteType == QStringLiteral("MUTUALFUND"))
        score += 240;
    else
        score -= 500;

    for (const QString &token : tokens) {
        if (name.contains(token))
            score += 80;
        if (symbol.startsWith(token))
            score += 20;
    }

    static const QSet<QString> primaryExchanges = {
        QStringLiteral("NMS"), QStringLiteral("NYQ"), QStringLiteral("NGM"),
        QStringLiteral("LSE"),
        QStringLiteral("PAR"), QStringLiteral("AMS"), QStringLiteral("SWX"),
        QStringLiteral("MIL"), QStringLiteral("MCE"), QStringLiteral("STO"),
        QStringLiteral("CPH"), QStringLiteral("OSL"), QStringLiteral("TOR"),
        QStringLiteral("ASX"), QStringLiteral("VIE"), QStringLiteral("BRU")
    };
    if (primaryExchanges.contains(exchange))
        score += 160;
    if (exchange == QStringLiteral("GER"))
        score += 100;
    if (exchange == QStringLiteral("FRA"))
        score += 40;
    if (exchange == QStringLiteral("PNK") || exchange == QStringLiteral("OBB")
        || exchange.contains(QStringLiteral("OTC"))) {
        score -= 220;
    }

    if (symbol.endsWith(QStringLiteral(".L")))
        score += 80;
    if (symbol.endsWith(QStringLiteral(".PA")) || symbol.endsWith(QStringLiteral(".AS"))
        || symbol.endsWith(QStringLiteral(".SW")) || symbol.endsWith(QStringLiteral(".MI"))
        || symbol.endsWith(QStringLiteral(".MC")) || symbol.endsWith(QStringLiteral(".ST"))
        || symbol.endsWith(QStringLiteral(".CO")) || symbol.endsWith(QStringLiteral(".OL"))
        || symbol.endsWith(QStringLiteral(".VI")) || symbol.endsWith(QStringLiteral(".BR"))
        || symbol.endsWith(QStringLiteral(".LS")) || symbol.endsWith(QStringLiteral(".TO"))
        || symbol.endsWith(QStringLiteral(".AX"))) {
        score += 80;
    }
    if (symbol.endsWith(QStringLiteral(".DE")))
        score += 30;
    if (symbol.endsWith(QStringLiteral(".F")))
        score += 10;
    if (!symbol.contains(QLatin1Char('.'))
        && (symbol.endsWith(QLatin1Char('F')) || symbol.endsWith(QLatin1Char('Y')))) {
        score -= 90;
    }

    return score;
}

bool isSupportedYahooQuoteType(const QString &quoteType)
{
    const QString normalized = quoteType.trimmed().toUpper();
    return normalized == QStringLiteral("EQUITY")
        || normalized == QStringLiteral("ETF")
        || normalized == QStringLiteral("ETN")
        || normalized == QStringLiteral("MUTUALFUND");
}

bool hasClassicYahooFundamentals(const QVariantMap &data)
{
    return data.value(QStringLiteral("marketCapitalization")).isValid()
        || data.value(QStringLiteral("revenue")).isValid()
        || data.value(QStringLiteral("peRatio")).isValid();
}

bool isFundLikeYahooQuoteType(const QString &quoteType)
{
    const QString normalized = quoteType.trimmed().toUpper();
    return normalized == QStringLiteral("ETF")
        || normalized == QStringLiteral("ETN")
        || normalized == QStringLiteral("MUTUALFUND");
}
}

YahooFinanceClient::YahooFinanceClient(QObject *parent)
    : QObject(parent)
{
    networkManager.setCookieJar(new QNetworkCookieJar(this));
}

void YahooFinanceClient::fetchFundamentals(const QString &requestSymbol, const QString &yahooSymbol)
{
    if (m_yahooCrumb.isEmpty()) {
        fetchCookieAndCrumb(requestSymbol, yahooSymbol);
        return;
    }

    fetchFundamentalsSummary(requestSymbol, yahooSymbol);
}

void YahooFinanceClient::fetchCookieAndCrumb(const QString &requestSymbol, const QString &yahooSymbol)
{
    QUrl url(QStringLiteral("https://fc.yahoo.com"));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");

    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestSymbol, yahooSymbol]() {
        handleCookieReply(reply, requestSymbol, yahooSymbol);
    });
}

void YahooFinanceClient::fetchCrumbAndFundamentals(const QString &requestSymbol, const QString &yahooSymbol)
{
    QUrl url(QStringLiteral("https://query1.finance.yahoo.com/v1/test/getcrumb"));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");

    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestSymbol, yahooSymbol]() {
        handleCrumbReply(reply, requestSymbol, yahooSymbol);
    });
}

void YahooFinanceClient::fetchFundamentalsSummary(const QString &requestSymbol, const QString &yahooSymbol)
{
    QUrl url(QStringLiteral("https://query2.finance.yahoo.com/v10/finance/quoteSummary/%1")
                 .arg(yahooSymbol));
    QUrlQuery query;
    query.addQueryItem(
        QStringLiteral("modules"),
        QStringLiteral("price,summaryDetail,defaultKeyStatistics,financialData"));
    query.addQueryItem(QStringLiteral("formatted"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("crumb"), m_yahooCrumb);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestSymbol, yahooSymbol]() {
        handleFundamentalsReply(reply, requestSymbol, yahooSymbol);
    });
}

void YahooFinanceClient::resolveSymbol(const QString &requestSymbol, const QString &keywords)
{
    const QString searchText = keywords.trimmed();
    if (searchText.isEmpty()) {
        emit symbolResolveFailed(requestSymbol, QStringLiteral("Kein Suchbegriff fuer Yahoo vorhanden."));
        return;
    }

    QUrl url(QStringLiteral("https://query1.finance.yahoo.com/v1/finance/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), searchText);
    query.addQueryItem(QStringLiteral("quotesCount"), QStringLiteral("10"));
    query.addQueryItem(QStringLiteral("newsCount"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("enableFuzzyQuery"), QStringLiteral("true"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestSymbol, searchText]() {
        handleSymbolSearchReply(reply, requestSymbol, searchText);
    });
}

void YahooFinanceClient::handleCookieReply(QNetworkReply *reply,
                                           const QString &requestSymbol,
                                           const QString &yahooSymbol)
{
    reply->deleteLater();
    fetchCrumbAndFundamentals(requestSymbol, yahooSymbol);
}

void YahooFinanceClient::handleCrumbReply(QNetworkReply *reply,
                                          const QString &requestSymbol,
                                          const QString &yahooSymbol)
{
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        fetchFundamentalsPage(requestSymbol, yahooSymbol);
        return;
    }

    m_yahooCrumb = QString::fromUtf8(reply->readAll()).trimmed();
    reply->deleteLater();

    if (m_yahooCrumb.isEmpty()) {
        fetchFundamentalsPage(requestSymbol, yahooSymbol);
        return;
    }

    fetchFundamentalsSummary(requestSymbol, yahooSymbol);
}

void YahooFinanceClient::fetchFundamentalsPage(const QString &requestSymbol, const QString &yahooSymbol)
{
    QUrl url(QStringLiteral("https://finance.yahoo.com/quote/%1").arg(yahooSymbol));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");

    QNetworkReply *reply = networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestSymbol, yahooSymbol]() {
        handleFundamentalsPageReply(reply, requestSymbol, yahooSymbol);
    });
}

void YahooFinanceClient::handleFundamentalsReply(QNetworkReply *reply,
                                                 const QString &requestSymbol,
                                                 const QString &yahooSymbol)
{
    if (reply->error() != QNetworkReply::NoError) {
        const QString apiError = reply->errorString();
        reply->deleteLater();
        Q_UNUSED(apiError)
        fetchFundamentalsPage(requestSymbol, yahooSymbol);
        return;
    }

    const QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit fundamentalsFailed(requestSymbol, yahooSymbol, QStringLiteral("Ungueltige Yahoo-Antwort."));
        return;
    }

    const QJsonObject quoteSummary = document.object().value(QStringLiteral("quoteSummary")).toObject();
    const QJsonArray resultArray = quoteSummary.value(QStringLiteral("result")).toArray();
    if (resultArray.isEmpty()) {
        emit fundamentalsFailed(requestSymbol, yahooSymbol, QStringLiteral("Yahoo hat keine Fundamentaldaten geliefert."));
        return;
    }

    const QJsonObject result = resultArray.at(0).toObject();
    const QJsonObject price = result.value(QStringLiteral("price")).toObject();
    const QJsonObject summary = result.value(QStringLiteral("summaryDetail")).toObject();
    const QJsonObject stats = result.value(QStringLiteral("defaultKeyStatistics")).toObject();
    const QJsonObject financial = result.value(QStringLiteral("financialData")).toObject();

    QVariantMap data;
    data[QStringLiteral("symbol")] = yahooSymbol;
    data[QStringLiteral("quoteType")] = yahooValue(price, QStringLiteral("quoteType"));
    data[QStringLiteral("longName")] = yahooValue(price, QStringLiteral("longName"));
    data[QStringLiteral("currency")] = yahooValue(price, QStringLiteral("currency"));
    data[QStringLiteral("marketCapitalization")] = yahooValue(price, QStringLiteral("marketCap"));
    data[QStringLiteral("enterpriseValue")] = yahooValue(stats, QStringLiteral("enterpriseValue"));
    data[QStringLiteral("peRatio")] = yahooValue(summary, QStringLiteral("trailingPE"));
    data[QStringLiteral("forwardPeRatio")] = yahooValue(summary, QStringLiteral("forwardPE"));
    data[QStringLiteral("priceToBookRatio")] = yahooValue(stats, QStringLiteral("priceToBook"));
    data[QStringLiteral("priceToSalesRatio")] = yahooValue(summary, QStringLiteral("priceToSalesTrailing12Months"));
    data[QStringLiteral("eps")] = yahooValue(stats, QStringLiteral("trailingEps"));
    data[QStringLiteral("forwardEps")] = yahooValue(stats, QStringLiteral("forwardEps"));
    data[QStringLiteral("dividendPerShare")] = yahooValue(summary, QStringLiteral("dividendRate"));
    const QVariant dividendYield = yahooValue(summary, QStringLiteral("dividendYield"));
    data[QStringLiteral("dividendYield")] = dividendYield.isValid() ? dividendYield.toDouble() * 100.0 : QVariant();
    const QVariant payoutRatio = yahooValue(summary, QStringLiteral("payoutRatio"));
    data[QStringLiteral("payoutRatio")] = payoutRatio.isValid() ? payoutRatio.toDouble() * 100.0 : QVariant();
    data[QStringLiteral("beta")] = yahooValue(summary, QStringLiteral("beta"));
    data[QStringLiteral("revenue")] = yahooValue(financial, QStringLiteral("totalRevenue"));
    data[QStringLiteral("netIncome")] = yahooValue(financial, QStringLiteral("netIncomeToCommon"));
    data[QStringLiteral("ebitda")] = yahooValue(financial, QStringLiteral("ebitda"));
    const QVariant returnOnEquity = yahooValue(financial, QStringLiteral("returnOnEquity"));
    data[QStringLiteral("returnOnEquity")] = returnOnEquity.isValid() ? returnOnEquity.toDouble() * 100.0 : QVariant();
    const QVariant returnOnAssets = yahooValue(financial, QStringLiteral("returnOnAssets"));
    data[QStringLiteral("returnOnAssets")] = returnOnAssets.isValid() ? returnOnAssets.toDouble() * 100.0 : QVariant();
    data[QStringLiteral("debtToEquity")] = yahooValue(financial, QStringLiteral("debtToEquity"));
    data[QStringLiteral("sharesOutstanding")] = yahooValue(stats, QStringLiteral("sharesOutstanding"));
    data[QStringLiteral("week52High")] = yahooValue(summary, QStringLiteral("fiftyTwoWeekHigh"));
    data[QStringLiteral("week52Low")] = yahooValue(summary, QStringLiteral("fiftyTwoWeekLow"));
    QJsonObject rawResult = result;
    rawResult[QStringLiteral("yahooSymbol")] = yahooSymbol;
    data[QStringLiteral("rawData")] = QString::fromUtf8(QJsonDocument(rawResult).toJson(QJsonDocument::Compact));

    if (!hasClassicYahooFundamentals(data)) {
        const QString quoteType = data.value(QStringLiteral("quoteType")).toString();
        if (isFundLikeYahooQuoteType(quoteType)) {
            data[QStringLiteral("noClassicFundamentals")] = true;
            emit fundamentalsReceived(requestSymbol, yahooSymbol, data);
            return;
        }
        emit fundamentalsFailed(requestSymbol, yahooSymbol, QStringLiteral("Yahoo-Antwort enthaelt keine nutzbaren Kennzahlen."));
        return;
    }

    emit fundamentalsReceived(requestSymbol, yahooSymbol, data);
}

void YahooFinanceClient::handleFundamentalsPageReply(QNetworkReply *reply,
                                                     const QString &requestSymbol,
                                                     const QString &yahooSymbol)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit fundamentalsFailed(requestSymbol, yahooSymbol, reply->errorString());
        reply->deleteLater();
        return;
    }

    const QString content = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    QVariantMap data;
    data[QStringLiteral("symbol")] = yahooSymbol;
    data[QStringLiteral("currency")] = rawJsonString(content, QStringLiteral("currency"));
    data[QStringLiteral("marketCapitalization")] = rawJsonNumber(content, QStringLiteral("marketCap"));
    if (!data.value(QStringLiteral("marketCapitalization")).isValid())
        data[QStringLiteral("marketCapitalization")] = finStreamerValue(content, QStringLiteral("marketCap"));
    data[QStringLiteral("enterpriseValue")] = rawJsonNumber(content, QStringLiteral("enterpriseValue"));
    data[QStringLiteral("peRatio")] = rawJsonNumber(content, QStringLiteral("trailingPE"));
    if (!data.value(QStringLiteral("peRatio")).isValid())
        data[QStringLiteral("peRatio")] = finStreamerValue(content, QStringLiteral("trailingPE"));
    data[QStringLiteral("forwardPeRatio")] = rawJsonNumber(content, QStringLiteral("forwardPE"));
    data[QStringLiteral("priceToBookRatio")] = rawJsonNumber(content, QStringLiteral("priceToBook"));
    data[QStringLiteral("priceToSalesRatio")] = rawJsonNumber(content, QStringLiteral("priceToSalesTrailing12Months"));
    data[QStringLiteral("eps")] = rawJsonNumber(content, QStringLiteral("trailingEps"));
    data[QStringLiteral("forwardEps")] = rawJsonNumber(content, QStringLiteral("forwardEps"));
    data[QStringLiteral("dividendPerShare")] = rawJsonNumber(content, QStringLiteral("dividendRate"));
    const QVariant dividendYield = rawJsonNumber(content, QStringLiteral("dividendYield"));
    data[QStringLiteral("dividendYield")] = dividendYield.isValid() ? dividendYield.toDouble() * 100.0 : QVariant();
    const QVariant payoutRatio = rawJsonNumber(content, QStringLiteral("payoutRatio"));
    data[QStringLiteral("payoutRatio")] = payoutRatio.isValid() ? payoutRatio.toDouble() * 100.0 : QVariant();
    data[QStringLiteral("beta")] = rawJsonNumber(content, QStringLiteral("beta"));
    data[QStringLiteral("revenue")] = rawJsonNumber(content, QStringLiteral("totalRevenue"));
    data[QStringLiteral("netIncome")] = rawJsonNumber(content, QStringLiteral("netIncomeToCommon"));
    data[QStringLiteral("ebitda")] = rawJsonNumber(content, QStringLiteral("ebitda"));
    const QVariant returnOnEquity = rawJsonNumber(content, QStringLiteral("returnOnEquity"));
    data[QStringLiteral("returnOnEquity")] = returnOnEquity.isValid() ? returnOnEquity.toDouble() * 100.0 : QVariant();
    const QVariant returnOnAssets = rawJsonNumber(content, QStringLiteral("returnOnAssets"));
    data[QStringLiteral("returnOnAssets")] = returnOnAssets.isValid() ? returnOnAssets.toDouble() * 100.0 : QVariant();
    data[QStringLiteral("debtToEquity")] = rawJsonNumber(content, QStringLiteral("debtToEquity"));
    data[QStringLiteral("sharesOutstanding")] = rawJsonNumber(content, QStringLiteral("sharesOutstanding"));
    data[QStringLiteral("week52High")] = rawJsonNumber(content, QStringLiteral("fiftyTwoWeekHigh"));
    data[QStringLiteral("week52Low")] = rawJsonNumber(content, QStringLiteral("fiftyTwoWeekLow"));
    data[QStringLiteral("rawData")] = QStringLiteral("{\"source\":\"finance.yahoo.com/quote\",\"yahooSymbol\":\"%1\"}")
                                         .arg(yahooSymbol);

    if (!data.value(QStringLiteral("marketCapitalization")).isValid()
        && !data.value(QStringLiteral("revenue")).isValid()
        && !data.value(QStringLiteral("peRatio")).isValid()) {
        emit fundamentalsFailed(requestSymbol, yahooSymbol, QStringLiteral("Yahoo-Seite enthaelt keine nutzbaren Kennzahlen."));
        return;
    }

    emit fundamentalsReceived(requestSymbol, yahooSymbol, data);
}

void YahooFinanceClient::handleSymbolSearchReply(QNetworkReply *reply,
                                                 const QString &requestSymbol,
                                                 const QString &keywords)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit symbolResolveFailed(requestSymbol, reply->errorString());
        reply->deleteLater();
        return;
    }

    const QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit symbolResolveFailed(requestSymbol, QStringLiteral("Ungueltige Yahoo-Suchantwort."));
        return;
    }

    const QJsonArray quotes = document.object().value(QStringLiteral("quotes")).toArray();
    if (quotes.isEmpty()) {
        emit symbolResolveFailed(requestSymbol, QStringLiteral("Yahoo-Suche fand kein Symbol."));
        return;
    }

    struct Candidate {
        QString symbol;
        int score = 0;
    };

    const QStringList tokens = searchTokens(keywords);
    QList<Candidate> candidates;
    QSet<QString> seen;
    for (const QJsonValue &value : quotes) {
        const QJsonObject quote = value.toObject();
        const QString quoteType = quote.value(QStringLiteral("quoteType")).toString().trimmed();
        if (!isSupportedYahooQuoteType(quoteType))
            continue;

        const QString symbol = quote.value(QStringLiteral("symbol")).toString().trimmed();
        if (symbol.isEmpty() || symbol.contains(QLatin1Char('=')))
            continue;

        const QString key = symbol.toUpper();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        candidates << Candidate{symbol, yahooSearchScore(quote, tokens)};
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &left, const Candidate &right) {
        return left.score > right.score;
    });

    QStringList symbols;
    for (const Candidate &candidate : candidates) {
        symbols << candidate.symbol;
        if (symbols.size() >= 6)
            break;
    }

    if (symbols.isEmpty()) {
        emit symbolResolveFailed(requestSymbol, QStringLiteral("Yahoo-Suche fand kein Aktiensymbol."));
        return;
    }

    emit symbolResolved(requestSymbol, symbols);
}
