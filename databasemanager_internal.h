#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>
#include <utility>
namespace DatabaseManagerInternal {
const QString MarketstackApiKey = QStringLiteral("2c7445a74b7f5ed6371d655f39ab4f4f");
constexpr int MarketstackInitialDelayMs = 100;
constexpr int MarketstackLookupDelayMs = 500;
constexpr int MarketstackCandidateDelayMs = 500;
constexpr int MarketstackNextSymbolDelayMs = 700;
constexpr int MarketstackRateLimitDelayMs = 15000;
constexpr int MarketstackMaxRateLimitRetries = 5;
constexpr int MarketstackMaxNameLookupTerms = 3;

inline QString currencyForCountry(const QString &countryCode)
{
    static const QHash<QString, QString> currencies = {
        {QStringLiteral("AT"), QStringLiteral("EUR")},
        {QStringLiteral("BE"), QStringLiteral("EUR")},
        {QStringLiteral("CH"), QStringLiteral("CHF")},
        {QStringLiteral("DE"), QStringLiteral("EUR")},
        {QStringLiteral("ES"), QStringLiteral("EUR")},
        {QStringLiteral("FI"), QStringLiteral("EUR")},
        {QStringLiteral("FR"), QStringLiteral("EUR")},
        {QStringLiteral("GB"), QStringLiteral("GBP")},
        {QStringLiteral("IE"), QStringLiteral("EUR")},
        {QStringLiteral("IT"), QStringLiteral("EUR")},
        {QStringLiteral("NL"), QStringLiteral("EUR")},
        {QStringLiteral("NO"), QStringLiteral("NOK")},
        {QStringLiteral("SE"), QStringLiteral("SEK")},
        {QStringLiteral("US"), QStringLiteral("USD")}
    };
    return currencies.value(countryCode.trimmed().toUpper());
}

inline const QHash<QString, QString> &ibkrNameCheckIsinOverrides()
{
    static const QHash<QString, QString> isins = {
        {QStringLiteral("217A.XFRA"), QString()},
        {QStringLiteral("0JZ.XFRA"), QStringLiteral("FR0013333077")},
        {QStringLiteral("BRW.XFRA"), QStringLiteral("GRS517003000")},
        {QStringLiteral("F9P.XFRA"), QStringLiteral("GRS364253005")},
        {QStringLiteral("J97.XFRA"), QStringLiteral("GRS222213001")},
        {QStringLiteral("Y1O.XFRA"), QStringLiteral("GRS397003005")},
        {QStringLiteral("HC6A.XFRA"), QStringLiteral("GRS281003004")},
        {QStringLiteral("99Q.XFRA"), QStringLiteral("FR0010424697")},
        {QStringLiteral("EH8.XFRA"), QStringLiteral("FR0000120669")},
        {QStringLiteral("FPH.XFRA"), QStringLiteral("DE000FPH9000")},
        {QStringLiteral("IA6.XFRA"), QStringLiteral("IT0001487047")},
        {QStringLiteral("GF8A.XFRA"), QStringLiteral("GRS419003009")},
        {QStringLiteral("3HB.XFRA"), QStringLiteral("FR0004153930")},
        {QStringLiteral("F40.XFRA"), QStringLiteral("GRS372003004")},
        {QStringLiteral("IC8.XFRA"), QStringLiteral("DE000A0HNF96")},
        {QStringLiteral("Z8M.XFRA"), QStringLiteral("GRS432003028")},
        {QStringLiteral("KIFF.XFRA"), QStringLiteral("TH0121010019")},
        {QStringLiteral("V4OC.XFRA"), QStringLiteral("FI4000312251")},
        {QStringLiteral("NVAI.XFRA"), QStringLiteral("TH0376010R12")},
        {QStringLiteral("MGP1.XFRA"), QStringLiteral("US56656T1051")},
        {QStringLiteral("OLU.XFRA"), QStringLiteral("TH0803010R15")},
        {QStringLiteral("MSRB.XFRA"), QStringLiteral("FI0009000665")},
        {QStringLiteral("6NF.XFRA"), QStringLiteral("IT0005385213")},
        {QStringLiteral("XXT.XFRA"), QStringLiteral("FR0010428771")},
        {QStringLiteral("JY0.XFRA"), QStringLiteral("DE000A3E5A34")},
        {QStringLiteral("PKTM.XFRA"), QStringLiteral("AT0000KTMI02")},
        {QStringLiteral("1R9.XFRA"), QStringLiteral("FR0013252186")},
        {QStringLiteral("8WU.XFRA"), QStringLiteral("US76882G1076")},
        {QStringLiteral("R4Q.XFRA"), QStringLiteral("TH0750010Y19")},
        {QStringLiteral("SLL.XFRA"), QStringLiteral("AT0000946652")},
        {QStringLiteral("407.XFRA"), QStringLiteral("US81728A2078")},
        {QStringLiteral("4QVA.XFRA"), QStringLiteral("TH0979010R13")},
        {QStringLiteral("NYVL.XFRA"), QStringLiteral("TH0371010R13")},
        {QStringLiteral("3VG.XFRA"), QStringLiteral("FR0013286259")},
        {QStringLiteral("NVP5.XFRA"), QStringLiteral("TH0219010Z14")},
        {QStringLiteral("7V91.XFRA"), QStringLiteral("FR0013481835")},
        {QStringLiteral("9VP.XFRA"), QStringLiteral("FR0000062796")},
        {QStringLiteral("YPF.XFRA"), QStringLiteral("US9842451000")}
    };
    return isins;
}

inline QString ibkrNameCheckIsinOverride(const QString &symbol)
{
    return ibkrNameCheckIsinOverrides().value(symbol.trimmed().toUpper());
}

inline QVariant alphaText(const QVariantMap &data, const QString &key)
{
    const QString value = data.value(key).toString().trimmed();
    if (value.isEmpty() || value.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("-"), Qt::CaseInsensitive) == 0) {
        return QVariant();
    }
    return value;
}

inline QVariant alphaNumber(const QVariantMap &data, const QString &key, double scale = 1.0)
{
    const QString value = alphaText(data, key).toString();
    if (value.isEmpty())
        return QVariant();

    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? QVariant(number * scale) : QVariant();
}

inline QVariant alphaPercent(const QVariantMap &data, const QString &key)
{
    const QVariant value = alphaNumber(data, key);
    if (!value.isValid())
        return QVariant();

    const double number = value.toDouble();
    return QVariant(qAbs(number) <= 1.0 ? number * 100.0 : number);
}

inline QVariant computedProduct(const QVariant &left, const QVariant &right)
{
    if (!left.isValid() || !right.isValid())
        return QVariant();
    return QVariant(left.toDouble() * right.toDouble());
}

inline bool hasValue(const QVariant &value)
{
    return value.isValid() && !value.isNull() && value.toString().trimmed() != QStringLiteral("");
}

inline bool isYahooEquitySymbolCandidate(const QString &symbol)
{
    const QString trimmed = symbol.trimmed();
    return !trimmed.isEmpty() && !trimmed.contains(QLatin1Char('='));
}

inline QVariant yahooNumber(const QVariantMap &data, const QString &key)
{
    const QVariant value = data.value(key);
    if (!hasValue(value))
        return QVariant();

    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? QVariant(number) : QVariant();
}

inline int yahooFundamentalScore(const QVariantMap &data)
{
    static const QStringList fields = {
        QStringLiteral("marketCapitalization"),
        QStringLiteral("enterpriseValue"),
        QStringLiteral("peRatio"),
        QStringLiteral("forwardPeRatio"),
        QStringLiteral("priceToBookRatio"),
        QStringLiteral("priceToSalesRatio"),
        QStringLiteral("eps"),
        QStringLiteral("forwardEps"),
        QStringLiteral("dividendPerShare"),
        QStringLiteral("dividendYield"),
        QStringLiteral("payoutRatio"),
        QStringLiteral("beta"),
        QStringLiteral("revenue"),
        QStringLiteral("netIncome"),
        QStringLiteral("ebitda"),
        QStringLiteral("returnOnEquity"),
        QStringLiteral("returnOnAssets"),
        QStringLiteral("debtToEquity"),
        QStringLiteral("sharesOutstanding"),
        QStringLiteral("week52High"),
        QStringLiteral("week52Low")
    };

    int score = 0;
    for (const QString &field : fields) {
        if (hasValue(data.value(field)))
            ++score;
    }
    return score;
}

inline bool isYahooNoClassicFundamentals(const QVariantMap &data)
{
    return data.value(QStringLiteral("noClassicFundamentals")).toBool();
}

inline void appendUniqueSymbol(QStringList &symbols, const QString &symbol)
{
    const QString trimmed = symbol.trimmed();
    if (!isYahooEquitySymbolCandidate(trimmed))
        return;

    for (const QString &existing : std::as_const(symbols)) {
        if (existing.compare(trimmed, Qt::CaseInsensitive) == 0)
            return;
    }

    symbols << trimmed;
}

inline QString ibkrSymbolFromYahooSymbol(const QString &symbol)
{
    return symbol.section(QLatin1Char('.'), 0, 0).trimmed();
}

inline QStringList ibkrDirectExchanges(const QString &exchange)
{
    const QString normalized = exchange.trimmed().toUpper();
    if (normalized == QStringLiteral("XFRA")
        || normalized == QStringLiteral("FRA")
        || normalized == QStringLiteral("GETTEX")
        || normalized == QStringLiteral("GETTEX2")
        || normalized == QStringLiteral("TGATE")) {
        return {QStringLiteral("FWB"), QStringLiteral("FWB2"),
                QStringLiteral("GETTEX"), QStringLiteral("TGATE")};
    }
    return normalized.isEmpty() ? QStringList{} : QStringList{normalized};
}

inline bool ibkrIsEuroQuoteExchange(const QString &exchange)
{
    const QString normalized = exchange.trimmed().toUpper();
    static const QSet<QString> euroExchanges = {
        QStringLiteral("AEB"), QStringLiteral("BATEEN"),
        QStringLiteral("BM"), QStringLiteral("BME"),
        QStringLiteral("BRU"),
        QStringLiteral("BVME"),
        QStringLiteral("ENEXT.BE"), QStringLiteral("ENEXT.FR"),
        QStringLiteral("ENEXT.NL"), QStringLiteral("ENEXT.PT"),
        QStringLiteral("FRA"), QStringLiteral("FWB"), QStringLiteral("FWB2"),
        QStringLiteral("GETTEX"), QStringLiteral("GETTEX2"),
        QStringLiteral("IBIS"), QStringLiteral("IBIS2"),
        QStringLiteral("LISB"),
        QStringLiteral("MCE"),
        QStringLiteral("MIL"),
        QStringLiteral("PAR"),
        QStringLiteral("SBF"),
        QStringLiteral("SWB"),
        QStringLiteral("TGATE"),
        QStringLiteral("VIE"), QStringLiteral("VSE"),
        QStringLiteral("XAMS"), QStringLiteral("XATH"), QStringLiteral("XBRU"),
        QStringLiteral("XDUB"), QStringLiteral("XETR"), QStringLiteral("XFRA"),
        QStringLiteral("XHEL"), QStringLiteral("XLIS"), QStringLiteral("XMAD"),
        QStringLiteral("XMIL"), QStringLiteral("XPAR"), QStringLiteral("XVIE")
    };
    return euroExchanges.contains(normalized);
}

inline bool ibkrQuoteExchangesContainEuro(const QStringList &exchanges)
{
    for (const QString &exchange : exchanges) {
        if (ibkrIsEuroQuoteExchange(exchange))
            return true;
    }
    return false;
}

inline int ibkrQuoteExchangePreferenceRank(const QString &exchange)
{
    const QString normalized = exchange.trimmed().toUpper();
    if (normalized == QStringLiteral("FWB"))
        return 0;
    if (normalized == QStringLiteral("FWB2"))
        return 1;
    if (normalized == QStringLiteral("GETTEX"))
        return 2;
    if (normalized == QStringLiteral("GETTEX2"))
        return 3;
    if (normalized == QStringLiteral("TGATE"))
        return 4;
    if (normalized == QStringLiteral("IBIS") || normalized == QStringLiteral("IBIS2"))
        return 5;
    if (normalized == QStringLiteral("SBF"))
        return 90;
    return 50;
}

inline QStringList preferEuroQuoteExchanges(const QStringList &exchanges)
{
    QStringList preferred;
    QStringList fallback;
    const bool hasFwb = exchanges.contains(QStringLiteral("FWB"), Qt::CaseInsensitive);
    for (const QString &exchange : exchanges) {
        if (hasFwb && exchange.trimmed().compare(QStringLiteral("SBF"), Qt::CaseInsensitive) == 0)
            continue;
        if (ibkrIsEuroQuoteExchange(exchange))
            preferred << exchange;
        else
            fallback << exchange;
    }
    std::stable_sort(preferred.begin(), preferred.end(), [](const QString &left, const QString &right) {
        return ibkrQuoteExchangePreferenceRank(left) < ibkrQuoteExchangePreferenceRank(right);
    });
    preferred << fallback;
    return preferred;
}

inline QStringList ibkrQuoteExchangeCandidates(const QString &validExchanges,
                                        const QString &primaryExchange,
                                        const QString &mic)
{
    QStringList candidates;
    auto append = [&candidates](const QString &exchange) {
        const QString normalized = exchange.trimmed().toUpper();
        if (normalized.isEmpty() || normalized == QStringLiteral("SMART"))
            return;
        if (!candidates.contains(normalized, Qt::CaseInsensitive))
            candidates << normalized;
    };

    append(primaryExchange);
    const QStringList exchanges = validExchanges.split(QLatin1Char(','),
                                                       Qt::SkipEmptyParts);
    for (const QString &exchange : exchanges)
        append(exchange);

    if (candidates.isEmpty()) {
        const QStringList fallbackExchanges = ibkrDirectExchanges(mic);
        for (const QString &exchange : fallbackExchanges)
            append(exchange);
    }

    return preferEuroQuoteExchanges(candidates);
}

inline bool ibkrValidExchangesContainSmart(const QString &validExchanges)
{
    const QStringList exchanges = validExchanges.split(QLatin1Char(','),
                                                       Qt::SkipEmptyParts);
    for (const QString &exchange : exchanges) {
        if (exchange.trimmed().compare(QStringLiteral("SMART"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

inline QStringList ibkrQuoteFallbackExchanges(const QString &preferredExchange,
                                       const QStringList &candidates)
{
    QStringList exchanges;
    auto append = [&exchanges](const QString &exchange) {
        const QString normalized = exchange.trimmed().toUpper();
        if (normalized.isEmpty() || normalized == QStringLiteral("SMART"))
            return;
        if (!exchanges.contains(normalized, Qt::CaseInsensitive))
            exchanges << normalized;
    };

    append(preferredExchange);
    for (const QString &candidate : candidates)
        append(candidate);
    return preferEuroQuoteExchanges(exchanges);
}

inline bool shouldUseMarketstackForIbkrQuoteError(const QString &error)
{
    const QString text = error.trimmed();
    return text.contains(QStringLiteral("keine Umsatzboerse"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("keine Quotes"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("No market data permissions"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("keine Daten"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("No data"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("HMDS-Anfrage ergab keine Daten"), Qt::CaseInsensitive);
}

inline QUrl marketstackUrl(const QString &path, const QList<QPair<QString, QString>> &items)
{
    QUrl url(QStringLiteral("http://api.marketstack.com/v1/%1").arg(path));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("access_key"), MarketstackApiKey);
    for (const auto &item : items)
        query.addQueryItem(item.first, item.second);
    url.setQuery(query);
    return url;
}

inline void appendMarketstackLookupUrl(QList<QUrl> &urls, const QUrl &url)
{
    if (!url.isValid())
        return;
    const QString encoded = url.toString(QUrl::FullyEncoded);
    for (const QUrl &existing : std::as_const(urls)) {
        if (existing.toString(QUrl::FullyEncoded) == encoded)
            return;
    }
    urls << url;
}

inline void appendUniqueMarketstackSymbol(QStringList &symbols, const QString &symbol)
{
    const QString normalized = symbol.trimmed().toUpper();
    if (normalized.isEmpty())
        return;
    if (normalized.contains(QLatin1Char('.')))
        return;
    if (!symbols.contains(normalized, Qt::CaseInsensitive))
        symbols << normalized;
}

inline QString ibkrSymbolSearchKeywords(const QString &name);

inline void appendMarketstackCandidate(QVariantList &candidates,
                                const QString &symbol,
                                const QString &exchange,
                                bool hasEod,
                                const QString &name = QString(),
                                const QString &countryCode = QString(),
                                bool trusted = false)
{
    const QString normalizedSymbol = symbol.trimmed().toUpper();
    const QString normalizedExchange = exchange.trimmed().toUpper();
    if (normalizedSymbol.isEmpty() || normalizedExchange.isEmpty() || !hasEod)
        return;

    const QString marketplaceSym =
        QStringLiteral("%1/%2").arg(normalizedSymbol, normalizedExchange);
    for (const QVariant &candidate : std::as_const(candidates)) {
        if (candidate.toMap().value(QStringLiteral("marketplaceSym")).toString()
            .compare(marketplaceSym, Qt::CaseInsensitive) == 0) {
            return;
        }
    }

    QVariantMap candidate;
    candidate.insert(QStringLiteral("symbol"), normalizedSymbol);
    candidate.insert(QStringLiteral("exchange"), normalizedExchange);
    candidate.insert(QStringLiteral("marketplaceSym"), marketplaceSym);
    candidate.insert(QStringLiteral("name"), name.trimmed());
    candidate.insert(QStringLiteral("countryCode"), countryCode.trimmed().toUpper());
    candidate.insert(QStringLiteral("trusted"), trusted);
    candidates << candidate;
}

inline QString isinCountryCode(const QString &isin)
{
    const QString normalized = isin.trimmed().toUpper();
    if (normalized.size() < 2)
        return QString();
    return normalized.left(2);
}

inline bool marketstackIsOtcExchange(const QString &exchange)
{
    static const QSet<QString> otcExchanges = {
        QStringLiteral("OTCQ"), QStringLiteral("OTCB"), QStringLiteral("OTCM"),
        QStringLiteral("PINC"), QStringLiteral("PSGM"), QStringLiteral("XOTC")
    };
    return otcExchanges.contains(exchange.trimmed().toUpper());
}

inline QString marketstackCurrencyForMic(const QString &mic)
{
    static const QHash<QString, QString> currencies = {
        {QStringLiteral("ARCX"), QStringLiteral("USD")},
        {QStringLiteral("NASDAQ"), QStringLiteral("USD")},
        {QStringLiteral("OTCB"), QStringLiteral("USD")},
        {QStringLiteral("OTCM"), QStringLiteral("USD")},
        {QStringLiteral("OTCQ"), QStringLiteral("USD")},
        {QStringLiteral("PINC"), QStringLiteral("USD")},
        {QStringLiteral("PSGM"), QStringLiteral("USD")},
        {QStringLiteral("XASE"), QStringLiteral("USD")},
        {QStringLiteral("XNYS"), QStringLiteral("USD")},
        {QStringLiteral("XNAS"), QStringLiteral("USD")},
        {QStringLiteral("XOTC"), QStringLiteral("USD")},

        {QStringLiteral("XTSE"), QStringLiteral("CAD")},
        {QStringLiteral("XTSX"), QStringLiteral("CAD")},

        {QStringLiteral("XLON"), QStringLiteral("GBP")},

        {QStringLiteral("XWAR"), QStringLiteral("PLN")},

        {QStringLiteral("XFRA"), QStringLiteral("EUR")},
        {QStringLiteral("XETR"), QStringLiteral("EUR")},
        {QStringLiteral("XMIL"), QStringLiteral("EUR")},
        {QStringLiteral("XSTU"), QStringLiteral("EUR")},
        {QStringLiteral("XPAR"), QStringLiteral("EUR")},
        {QStringLiteral("XAMS"), QStringLiteral("EUR")},
        {QStringLiteral("XBRU"), QStringLiteral("EUR")},
        {QStringLiteral("XLIS"), QStringLiteral("EUR")},
        {QStringLiteral("XMAD"), QStringLiteral("EUR")},
        {QStringLiteral("XDUB"), QStringLiteral("EUR")},
        {QStringLiteral("XHEL"), QStringLiteral("EUR")},
        {QStringLiteral("XATH"), QStringLiteral("EUR")},
        {QStringLiteral("XWBO"), QStringLiteral("EUR")},

        {QStringLiteral("XSTO"), QStringLiteral("SEK")},
        {QStringLiteral("XCSE"), QStringLiteral("DKK")},
        {QStringLiteral("XOSL"), QStringLiteral("NOK")},
        {QStringLiteral("XSWX"), QStringLiteral("CHF")},

        {QStringLiteral("XHKG"), QStringLiteral("HKD")},
        {QStringLiteral("XSES"), QStringLiteral("SGD")},
        {QStringLiteral("XASX"), QStringLiteral("AUD")},
        {QStringLiteral("XNZE"), QStringLiteral("NZD")},
        {QStringLiteral("XJPX"), QStringLiteral("JPY")},
        {QStringLiteral("XTKS"), QStringLiteral("JPY")}
    };
    return currencies.value(mic.trimmed().toUpper());
}

inline bool marketstackCountryMatchesStock(const QString &isin,
                                    const QString &candidateCountryCode,
                                    const QString &candidateExchange)
{
    const QString stockCountryCode = isinCountryCode(isin);
    const QString normalizedCandidateCountry = candidateCountryCode.trimmed().toUpper();
    if (stockCountryCode.isEmpty() || normalizedCandidateCountry.isEmpty())
        return true;
    if (stockCountryCode == normalizedCandidateCountry)
        return true;
    return marketstackIsOtcExchange(candidateExchange);
}

inline bool marketstackWebsiteTickerMatches(const QString &ticker,
                                     const QString &expectedSymbol,
                                     const QString &expectedExchange)
{
    const QString normalizedTicker = ticker.trimmed().toUpper();
    const QString normalizedExpected = expectedSymbol.trimmed().toUpper();
    if (normalizedTicker.isEmpty() || normalizedExpected.isEmpty())
        return false;
    if (normalizedTicker == normalizedExpected)
        return true;

    const QString exchange = expectedExchange.trimmed().toUpper();
    const QString baseSymbol = normalizedExpected.section(QLatin1Char('.'), 0, 0);
    static const QHash<QString, QStringList> websiteSuffixes = {
        {QStringLiteral("XFRA"), {QStringLiteral(".F")}},
        {QStringLiteral("XSTU"), {QStringLiteral(".SG")}},
        {QStringLiteral("XWAR"), {QStringLiteral(".WA")}},
        {QStringLiteral("XMIL"), {QStringLiteral(".MI")}},
        {QStringLiteral("XETR"), {QStringLiteral(".DE")}},
        {QStringLiteral("XLON"), {QStringLiteral(".L")}},
        {QStringLiteral("XTSE"), {QStringLiteral(".TO")}},
        {QStringLiteral("XNAS"), {QString()}},
        {QStringLiteral("XNYS"), {QString()}},
        {QStringLiteral("ARCX"), {QString()}},
        {QStringLiteral("OTCQ"), {QString()}},
        {QStringLiteral("OTCB"), {QString()}},
        {QStringLiteral("OTCM"), {QString()}},
        {QStringLiteral("PINC"), {QString()}},
        {QStringLiteral("PSGM"), {QString()}}
    };
    const auto suffixes = websiteSuffixes.value(exchange);
    for (const QString &suffix : suffixes) {
        if (normalizedTicker == baseSymbol + suffix)
            return true;
    }
    return false;
}

inline QStringList marketstackNameTokens(const QString &name)
{
    QString normalized = ibkrSymbolSearchKeywords(name).toUpper();
    const QSet<QString> ignored = {
        QStringLiteral("INC"), QStringLiteral("LTD"), QStringLiteral("PLC"),
        QStringLiteral("CORP"), QStringLiteral("CORPORATION"), QStringLiteral("AG"),
        QStringLiteral("SA"), QStringLiteral("SE"), QStringLiteral("NV"),
        QStringLiteral("DL"), QStringLiteral("LS"), QStringLiteral("ZY"),
        QStringLiteral("MR"), QStringLiteral("GROUP"), QStringLiteral("HOLDING"),
        QStringLiteral("HOLDINGS")
    };
    QStringList tokens;
    const QStringList parts = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.size() < 2 || ignored.contains(part))
            continue;
        tokens << part;
    }
    return tokens;
}

inline bool marketstackNameMatchesStock(const QString &stockName, const QString &candidateName)
{
    const QStringList stockTokens = marketstackNameTokens(stockName);
    const QStringList candidateTokens = marketstackNameTokens(candidateName);
    if (stockTokens.isEmpty() || candidateTokens.isEmpty())
        return false;

    const QString firstStockToken = stockTokens.first();
    if (!candidateTokens.contains(firstStockToken, Qt::CaseInsensitive))
        return false;

    int matches = 0;
    for (const QString &stockToken : stockTokens) {
        for (const QString &candidateToken : candidateTokens) {
            if (candidateToken.compare(stockToken, Qt::CaseInsensitive) == 0
                || (stockToken.size() >= 5 && candidateToken.startsWith(stockToken.left(5), Qt::CaseInsensitive))
                || (candidateToken.size() >= 5 && stockToken.startsWith(candidateToken.left(5), Qt::CaseInsensitive))) {
                ++matches;
                break;
            }
        }
    }

    if (stockTokens.size() == 1)
        return matches >= 1;
    if (firstStockToken.size() >= 7)
        return matches >= 1;
    return matches >= 2;
}

inline QString ibkrSymbolSearchKeywords(const QString &name)
{
    QString normalized = name.trimmed();
    normalized = normalized.normalized(QString::NormalizationForm_D);
    QString asciiOnly;
    asciiOnly.reserve(normalized.size());
    for (const QChar character : normalized) {
        if (character.category() != QChar::Mark_NonSpacing)
            asciiOnly.append(character);
    }
    normalized = asciiOnly;
    normalized.replace(QRegularExpression(QStringLiteral("\\bINTL\\.?\\b"), QRegularExpression::CaseInsensitiveOption),
                       QStringLiteral("International"));
    normalized.replace(QRegularExpression(QStringLiteral("\\bINT\\.\\b"), QRegularExpression::CaseInsensitiveOption),
                       QStringLiteral("International"));
    normalized.replace(QRegularExpression(QStringLiteral("\\bINVESTM\\.?\\b"), QRegularExpression::CaseInsensitiveOption),
                       QStringLiteral("Investments"));
    normalized.replace(QRegularExpression(QStringLiteral("\\bINV\\.?\\b"), QRegularExpression::CaseInsensitiveOption),
                       QStringLiteral("Investments"));
    normalized.replace(QRegularExpression(QStringLiteral("[+.,;:/()\\-]+")), QStringLiteral(" "));
    normalized.replace(QRegularExpression(QStringLiteral("\\b(DK|DL|LS|ZY|EO|NK|NOK|SEK|DKK|GBP|GBX|USD|CAD|CHF)\\s+[0-9]+.*$"),
                                          QRegularExpression::CaseInsensitiveOption),
                       QString());
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return normalized.trimmed();
}

inline void appendUniqueSearchTerm(QStringList &terms, const QString &term)
{
    const QString trimmed = term.trimmed();
    if (trimmed.isEmpty())
        return;

    for (const QString &existing : std::as_const(terms)) {
        if (existing.compare(trimmed, Qt::CaseInsensitive) == 0)
            return;
    }

    terms << trimmed;
}

inline QStringList ibkrSymbolSearchKeywordVariants(const QString &name)
{
    QStringList terms;
    const QString normalized = ibkrSymbolSearchKeywords(name);
    appendUniqueSearchTerm(terms, normalized);

    const QRegularExpression leadingTickerPattern(
        QStringLiteral("^\\s*[A-Z0-9]{1,6}\\s*-\\s*(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch leadingTickerMatch = leadingTickerPattern.match(name);
    if (leadingTickerMatch.hasMatch()) {
        const QString withoutLeadingTicker =
            ibkrSymbolSearchKeywords(leadingTickerMatch.captured(1));
        appendUniqueSearchTerm(terms, withoutLeadingTicker);

        const QStringList suffixTokens =
            withoutLeadingTicker.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (suffixTokens.size() >= 2)
            appendUniqueSearchTerm(terms, suffixTokens.mid(0, 2).join(QLatin1Char(' ')));
        if (!suffixTokens.isEmpty())
            appendUniqueSearchTerm(terms, suffixTokens.first());
    }

    const QStringList tokens = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() >= 2)
        appendUniqueSearchTerm(terms, tokens.mid(0, 2).join(QLatin1Char(' ')));
    if (!tokens.isEmpty())
        appendUniqueSearchTerm(terms, tokens.first());

    const QStringList significantTokens = marketstackNameTokens(name);
    if (significantTokens.size() >= 2)
        appendUniqueSearchTerm(terms, significantTokens.mid(0, 2).join(QLatin1Char(' ')));
    if (!significantTokens.isEmpty())
        appendUniqueSearchTerm(terms, significantTokens.first());

    return terms;
}

inline QString normalizedCompanyNameForCheck(const QString &name)
{
    QString normalized = name.normalized(QString::NormalizationForm_D).toUpper();
    QString asciiOnly;
    asciiOnly.reserve(normalized.size());
    for (const QChar character : normalized) {
        if (character.category() != QChar::Mark_NonSpacing)
            asciiOnly.append(character);
    }

    normalized = asciiOnly;
    normalized.replace(QRegularExpression(QStringLiteral("[^A-Z0-9]+")), QStringLiteral(" "));
    normalized.replace(QRegularExpression(QStringLiteral("\\bGRP\\b")), QStringLiteral("GROUP"));
    normalized.replace(QRegularExpression(QStringLiteral("\\bINTL\\b")), QStringLiteral("INTERNATIONAL"));
    normalized.replace(QRegularExpression(QStringLiteral("\\bHLD\\b")), QStringLiteral("HOLDINGS"));
    normalized.replace(QRegularExpression(QStringLiteral("\\bHLDG\\b")), QStringLiteral("HOLDINGS"));
    normalized.replace(QRegularExpression(QStringLiteral("\\bTEC\\b")), QStringLiteral("TECHNOLOGY"));
    normalized.replace(QRegularExpression(QStringLiteral("\\bTECH\\b")), QStringLiteral("TECHNOLOGY"));
    normalized.replace(QRegularExpression(QStringLiteral("\\b(ADR|GDR|SPONSORED|UNSPONSORED|UNSP|SP|ORD|O|N|ON|EO|EUR|DL|USD|INH|NAM|NEW|REG|SHS|PLC|INC|CORP|CORPORATION|LTD|LIMITED|SA|S\\s*A|AG|SE|NV|N\\s*V|SPA|S\\s*P\\s*A)\\b"),
                                          QRegularExpression::CaseInsensitiveOption),
                       QStringLiteral(" "));
    normalized.replace(QRegularExpression(QStringLiteral("\\b[0-9]+\\b")), QStringLiteral(" "));
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return normalized.trimmed();
}

inline QStringList nameCheckTokens(const QString &name)
{
    static const QSet<QString> stopWords = {
        QStringLiteral("THE"), QStringLiteral("AND"), QStringLiteral("GROUP"),
        QStringLiteral("HOLDING"), QStringLiteral("HOLDINGS"), QStringLiteral("COMPANY"),
        QStringLiteral("INTERNATIONAL"), QStringLiteral("TECHNOLOGY")
    };

    QStringList tokens;
    const QStringList rawTokens = normalizedCompanyNameForCheck(name).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &token : rawTokens) {
        if (token.size() < 2 || stopWords.contains(token))
            continue;
        tokens << token;
    }
    return tokens;
}

inline bool companyNamesLookCompatible(const QString &databaseName, const QString &ibkrName)
{
    const QString normalizedDbName = normalizedCompanyNameForCheck(databaseName);
    const QString normalizedIbkrName = normalizedCompanyNameForCheck(ibkrName);
    if (normalizedDbName.isEmpty() || normalizedIbkrName.isEmpty())
        return false;
    if (normalizedDbName == normalizedIbkrName
        || normalizedDbName.contains(normalizedIbkrName)
        || normalizedIbkrName.contains(normalizedDbName)) {
        return true;
    }

    const QStringList dbTokens = nameCheckTokens(databaseName);
    const QStringList ibkrTokens = nameCheckTokens(ibkrName);
    if (dbTokens.isEmpty() || ibkrTokens.isEmpty())
        return false;
    if (dbTokens.first().size() >= 4 && dbTokens.first() == ibkrTokens.first())
        return true;
    if (dbTokens.first().size() >= 5
        && ibkrTokens.first().size() >= dbTokens.first().size()
        && ibkrTokens.first().startsWith(dbTokens.first()))
        return true;
    if (dbTokens.size() < 2 || ibkrTokens.size() < 2)
        return false;

    return ibkrTokens.contains(dbTokens.at(0)) && ibkrTokens.contains(dbTokens.at(1));
}

inline void appendUniqueIbkrSymbol(QStringList &symbols, const QString &symbol)
{
    const QString trimmed = ibkrSymbolFromYahooSymbol(symbol);
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('=')))
        return;

    for (const QString &existing : std::as_const(symbols)) {
        if (existing.compare(trimmed, Qt::CaseInsensitive) == 0)
            return;
    }

    symbols << trimmed;
}

inline void appendIbkrSymbolVariants(QStringList &symbols, const QString &symbol)
{
    const QString trimmed = ibkrSymbolFromYahooSymbol(symbol).toUpper();
    appendUniqueIbkrSymbol(symbols, trimmed);

    static const QRegularExpression helsinkiSeriesSuffix(QStringLiteral("^([A-Z]{2,})(1[HV])$"));
    const QRegularExpressionMatch match = helsinkiSeriesSuffix.match(trimmed);
    if (match.hasMatch())
        appendUniqueIbkrSymbol(symbols, match.captured(1));
}

inline QString ibkrCandidateKey(const QString &symbol)
{
    return symbol.trimmed().toUpper();
}

inline void appendUniqueRawIbkrSymbol(QStringList &symbols, const QString &symbol)
{
    const QString trimmed = symbol.trimmed().toUpper();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('=')))
        return;

    for (const QString &existing : std::as_const(symbols)) {
        if (existing.compare(trimmed, Qt::CaseInsensitive) == 0)
            return;
    }

    symbols << trimmed;
}

inline void appendIbkrMatchedSymbolVariants(QStringList &symbols, const QString &symbol)
{
    const QString trimmed = symbol.trimmed().toUpper();
    appendUniqueRawIbkrSymbol(symbols, trimmed);

    static const QRegularExpression helsinkiSeriesSuffix(QStringLiteral("^([A-Z]{2,})(1[HV])$"));
    const QRegularExpressionMatch match = helsinkiSeriesSuffix.match(trimmed);
    if (match.hasMatch())
        appendUniqueRawIbkrSymbol(symbols, match.captured(1));
}

inline QString yahooSuffixForIbkrExchange(const QString &exchange)
{
    const QString value = exchange.trimmed().toUpper();
    static const QHash<QString, QString> suffixes = {
        {QStringLiteral("FWB"), QStringLiteral(".F")},
        {QStringLiteral("FWB2"), QStringLiteral(".F")},
        {QStringLiteral("FRA"), QStringLiteral(".F")},
        {QStringLiteral("XFRA"), QStringLiteral(".F")},
        {QStringLiteral("GETTEX"), QStringLiteral(".F")},
        {QStringLiteral("GETTEX2"), QStringLiteral(".F")},
        {QStringLiteral("IBIS"), QStringLiteral(".DE")},
        {QStringLiteral("IBIS2"), QStringLiteral(".DE")},
        {QStringLiteral("XETR"), QStringLiteral(".DE")},
        {QStringLiteral("LSE"), QStringLiteral(".L")},
        {QStringLiteral("XLON"), QStringLiteral(".L")},
        {QStringLiteral("CPH"), QStringLiteral(".CO")},
        {QStringLiteral("XCSE"), QStringLiteral(".CO")},
        {QStringLiteral("SFB"), QStringLiteral(".ST")},
        {QStringLiteral("XSTO"), QStringLiteral(".ST")},
        {QStringLiteral("SWB"), QStringLiteral(".SW")},
        {QStringLiteral("SIX"), QStringLiteral(".SW")},
        {QStringLiteral("XSWX"), QStringLiteral(".SW")},
        {QStringLiteral("PAR"), QStringLiteral(".PA")},
        {QStringLiteral("XPAR"), QStringLiteral(".PA")},
        {QStringLiteral("AEB"), QStringLiteral(".AS")},
        {QStringLiteral("XAMS"), QStringLiteral(".AS")},
        {QStringLiteral("BVME"), QStringLiteral(".MI")},
        {QStringLiteral("XMIL"), QStringLiteral(".MI")},
        {QStringLiteral("BM"), QStringLiteral(".MC")},
        {QStringLiteral("XMAD"), QStringLiteral(".MC")},
        {QStringLiteral("VSE"), QStringLiteral(".VI")},
        {QStringLiteral("XVIE"), QStringLiteral(".VI")},
        {QStringLiteral("BRU"), QStringLiteral(".BR")},
        {QStringLiteral("XBRU"), QStringLiteral(".BR")},
        {QStringLiteral("TSE"), QStringLiteral(".TO")},
        {QStringLiteral("XTSE"), QStringLiteral(".TO")},
        {QStringLiteral("ASX"), QStringLiteral(".AX")},
        {QStringLiteral("XASX"), QStringLiteral(".AX")}
    };
    return suffixes.value(value);
}

inline bool isGenericIbkrTradingClass(const QString &value)
{
    const QString normalized = value.trimmed().toUpper();
    static const QSet<QString> genericValues = {
        QStringLiteral("ETF"), QStringLiteral("ETN"), QStringLiteral("ETC"),
        QStringLiteral("XETRA"), QStringLiteral("FWB"), QStringLiteral("FWB2"),
        QStringLiteral("IBIS"), QStringLiteral("IBIS2"), QStringLiteral("GETTEX"),
        QStringLiteral("GETTEX2"), QStringLiteral("SMART")
    };
    return genericValues.contains(normalized);
}

inline QString preferredYahooSuffix(const QStringList &exchangeValues)
{
    for (const QString &exchange : exchangeValues) {
        const QString suffix = yahooSuffixForIbkrExchange(exchange);
        if (!suffix.isEmpty())
            return suffix;
    }
    return QString();
}

inline QStringList yahooCandidateSymbols(const QString &symbol,
                                  const QString &configuredYahooSymbol,
                                  const QString &preferredSuffix,
                                  const QStringList &ibkrSymbols)
{
    QStringList candidates;

    const QString localSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();
    for (const QString &ibkrSymbol : ibkrSymbols) {
        if (!preferredSuffix.isEmpty())
            appendUniqueSymbol(candidates, ibkrSymbol + preferredSuffix);
    }

    if (!preferredSuffix.isEmpty())
        appendUniqueSymbol(candidates, localSymbol + preferredSuffix);

    if (configuredYahooSymbol.endsWith(preferredSuffix, Qt::CaseInsensitive))
        appendUniqueSymbol(candidates, configuredYahooSymbol);

    const QString suffix = symbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();
    if (suffix == QStringLiteral("XFRA") || suffix == QStringLiteral("XETR")
        || suffix == QStringLiteral("FRA")) {
        appendUniqueSymbol(candidates, localSymbol + QStringLiteral(".DE"));
        appendUniqueSymbol(candidates, localSymbol + QStringLiteral(".F"));
    }

    appendUniqueSymbol(candidates, configuredYahooSymbol);
    appendUniqueSymbol(candidates, symbol);
    return candidates;
}

inline QStringList preferYahooSuffix(const QStringList &symbols, const QString &preferredSuffix)
{
    if (preferredSuffix.isEmpty())
        return symbols;

    QStringList preferred;
    QStringList fallback;
    for (const QString &symbol : symbols) {
        if (symbol.endsWith(preferredSuffix, Qt::CaseInsensitive))
            appendUniqueSymbol(preferred, symbol);
        else
            appendUniqueSymbol(fallback, symbol);
    }
    preferred << fallback;
    return preferred;
}

inline QString yahooExchangeCodeFromSymbol(const QString &yahooSymbol)
{
    const QString suffix = yahooSymbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();
    if (!suffix.isEmpty())
        return suffix;
    if (yahooSymbol.contains(QLatin1Char('=')))
        return QStringLiteral("?");
    return QStringLiteral("US");
}

inline QString yahooSymbolFromRawData(const QString &rawData)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawData.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QString();

    const QVariantMap root = document.object().toVariantMap();
    const QString directSymbol = root.value(QStringLiteral("yahooSymbol")).toString().trimmed();
    if (!directSymbol.isEmpty())
        return directSymbol;

    const QVariantMap price = root.value(QStringLiteral("price")).toMap();
    return price.value(QStringLiteral("symbol")).toString().trimmed();
}

inline QString alphaVantageSearchKeywords(const QString &name, const QString &symbol)
{
    static const QSet<QString> skipWords = {
        QStringLiteral("AG"), QStringLiteral("ADR"), QStringLiteral("CDR"),
        QStringLiteral("CORP"), QStringLiteral("CORPORATION"), QStringLiteral("DL"),
        QStringLiteral("HLD"), QStringLiteral("HOLDINGS"), QStringLiteral("INC"),
        QStringLiteral("INH"), QStringLiteral("LTD"), QStringLiteral("NAM"),
        QStringLiteral("ORD"), QStringLiteral("PLC"), QStringLiteral("REG"),
        QStringLiteral("SA"), QStringLiteral("SE"), QStringLiteral("TECH"),
        QStringLiteral("TECHNOLOGY")
    };

    QString normalized = name.toUpper();
    normalized.replace(QRegularExpression(QStringLiteral("[^A-Z0-9]+")), QStringLiteral(" "));
    const QStringList rawTokens = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    QStringList tokens;
    for (const QString &token : rawTokens) {
        if (token.size() < 3 || skipWords.contains(token) || token.at(0).isDigit()
            || QRegularExpression(QStringLiteral("^[A-Z]{1,3}[0-9]+$")).match(token).hasMatch()) {
            continue;
        }
        tokens << token;
        if (tokens.size() >= 2)
            break;
    }

    if (!tokens.isEmpty())
        return tokens.join(QLatin1Char(' '));

    return symbol.section(QLatin1Char('.'), 0, 0).trimmed();
}
}
