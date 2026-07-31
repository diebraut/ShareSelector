#include "databasemanager.h"
#include <QSqlRecord>
#include <QDate>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrlQuery>
#include <QtConcurrent>

#include <utility>

namespace {
const QString MarketstackApiKey = QStringLiteral("2c7445a74b7f5ed6371d655f39ab4f4f");
constexpr int MarketstackInitialDelayMs = 100;
constexpr int MarketstackLookupDelayMs = 500;
constexpr int MarketstackCandidateDelayMs = 500;
constexpr int MarketstackNextSymbolDelayMs = 700;
constexpr int MarketstackRateLimitDelayMs = 15000;
constexpr int MarketstackMaxRateLimitRetries = 5;
constexpr int MarketstackMaxNameLookupTerms = 3;

quint32 stableSymbolSeed(const QString &symbol)
{
    quint32 seed = 2166136261u;
    for (const QChar character : symbol) {
        seed ^= character.unicode();
        seed *= 16777619u;
    }
    return seed;
}

double mockValue(quint32 seed, int shift, double minimum, double maximum)
{
    const quint32 part = (seed >> shift) & 0xffu;
    return minimum + (maximum - minimum) * (double(part) / 255.0);
}

QString currencyForCountry(const QString &countryCode)
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

const QHash<QString, QString> &ibkrNameCheckIsinOverrides()
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

QString ibkrNameCheckIsinOverride(const QString &symbol)
{
    return ibkrNameCheckIsinOverrides().value(symbol.trimmed().toUpper());
}

QVariant alphaText(const QVariantMap &data, const QString &key)
{
    const QString value = data.value(key).toString().trimmed();
    if (value.isEmpty() || value.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("-"), Qt::CaseInsensitive) == 0) {
        return QVariant();
    }
    return value;
}

QVariant alphaNumber(const QVariantMap &data, const QString &key, double scale = 1.0)
{
    const QString value = alphaText(data, key).toString();
    if (value.isEmpty())
        return QVariant();

    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? QVariant(number * scale) : QVariant();
}

QVariant alphaPercent(const QVariantMap &data, const QString &key)
{
    const QVariant value = alphaNumber(data, key);
    if (!value.isValid())
        return QVariant();

    const double number = value.toDouble();
    return QVariant(qAbs(number) <= 1.0 ? number * 100.0 : number);
}

QVariant computedProduct(const QVariant &left, const QVariant &right)
{
    if (!left.isValid() || !right.isValid())
        return QVariant();
    return QVariant(left.toDouble() * right.toDouble());
}

bool hasValue(const QVariant &value)
{
    return value.isValid() && !value.isNull() && value.toString().trimmed() != QStringLiteral("");
}

bool isYahooEquitySymbolCandidate(const QString &symbol)
{
    const QString trimmed = symbol.trimmed();
    return !trimmed.isEmpty() && !trimmed.contains(QLatin1Char('='));
}

QVariant yahooNumber(const QVariantMap &data, const QString &key)
{
    const QVariant value = data.value(key);
    if (!hasValue(value))
        return QVariant();

    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? QVariant(number) : QVariant();
}

int yahooFundamentalScore(const QVariantMap &data)
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

bool isYahooNoClassicFundamentals(const QVariantMap &data)
{
    return data.value(QStringLiteral("noClassicFundamentals")).toBool();
}

void appendUniqueSymbol(QStringList &symbols, const QString &symbol)
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

QString ibkrSymbolFromYahooSymbol(const QString &symbol)
{
    return symbol.section(QLatin1Char('.'), 0, 0).trimmed();
}

QStringList ibkrDirectExchanges(const QString &exchange)
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

QStringList ibkrQuoteExchangeCandidates(const QString &validExchanges,
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

    return candidates;
}

bool ibkrValidExchangesContainSmart(const QString &validExchanges)
{
    const QStringList exchanges = validExchanges.split(QLatin1Char(','),
                                                       Qt::SkipEmptyParts);
    for (const QString &exchange : exchanges) {
        if (exchange.trimmed().compare(QStringLiteral("SMART"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QStringList ibkrQuoteFallbackExchanges(const QString &preferredExchange,
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
    return exchanges;
}

bool shouldUseMarketstackForIbkrQuoteError(const QString &error)
{
    const QString text = error.trimmed();
    return text.contains(QStringLiteral("keine Umsatzboerse"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("keine Quotes"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("No market data permissions"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("keine Daten"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("No data"), Qt::CaseInsensitive)
           || text.contains(QStringLiteral("HMDS-Anfrage ergab keine Daten"), Qt::CaseInsensitive);
}

QUrl marketstackUrl(const QString &path, const QList<QPair<QString, QString>> &items)
{
    QUrl url(QStringLiteral("http://api.marketstack.com/v1/%1").arg(path));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("access_key"), MarketstackApiKey);
    for (const auto &item : items)
        query.addQueryItem(item.first, item.second);
    url.setQuery(query);
    return url;
}

void appendMarketstackLookupUrl(QList<QUrl> &urls, const QUrl &url)
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

void appendUniqueMarketstackSymbol(QStringList &symbols, const QString &symbol)
{
    const QString normalized = symbol.trimmed().toUpper();
    if (normalized.isEmpty())
        return;
    if (normalized.contains(QLatin1Char('.')))
        return;
    if (!symbols.contains(normalized, Qt::CaseInsensitive))
        symbols << normalized;
}

QString ibkrSymbolSearchKeywords(const QString &name);

void appendMarketstackCandidate(QVariantList &candidates,
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

QString isinCountryCode(const QString &isin)
{
    const QString normalized = isin.trimmed().toUpper();
    if (normalized.size() < 2)
        return QString();
    return normalized.left(2);
}

bool marketstackIsOtcExchange(const QString &exchange)
{
    static const QSet<QString> otcExchanges = {
        QStringLiteral("OTCQ"), QStringLiteral("OTCB"), QStringLiteral("OTCM"),
        QStringLiteral("PINC"), QStringLiteral("PSGM"), QStringLiteral("XOTC")
    };
    return otcExchanges.contains(exchange.trimmed().toUpper());
}

QString marketstackCurrencyForMic(const QString &mic)
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

bool marketstackCountryMatchesStock(const QString &isin,
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

bool marketstackWebsiteTickerMatches(const QString &ticker,
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

QStringList marketstackNameTokens(const QString &name)
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

bool marketstackNameMatchesStock(const QString &stockName, const QString &candidateName)
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

QString ibkrSymbolSearchKeywords(const QString &name)
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

void appendUniqueSearchTerm(QStringList &terms, const QString &term)
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

QStringList ibkrSymbolSearchKeywordVariants(const QString &name)
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

QString normalizedCompanyNameForCheck(const QString &name)
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

QStringList nameCheckTokens(const QString &name)
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

bool companyNamesLookCompatible(const QString &databaseName, const QString &ibkrName)
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

void appendUniqueIbkrSymbol(QStringList &symbols, const QString &symbol)
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

void appendIbkrSymbolVariants(QStringList &symbols, const QString &symbol)
{
    const QString trimmed = ibkrSymbolFromYahooSymbol(symbol).toUpper();
    appendUniqueIbkrSymbol(symbols, trimmed);

    static const QRegularExpression helsinkiSeriesSuffix(QStringLiteral("^([A-Z]{2,})(1[HV])$"));
    const QRegularExpressionMatch match = helsinkiSeriesSuffix.match(trimmed);
    if (match.hasMatch())
        appendUniqueIbkrSymbol(symbols, match.captured(1));
}

QString ibkrCandidateKey(const QString &symbol)
{
    return symbol.trimmed().toUpper();
}

void appendUniqueRawIbkrSymbol(QStringList &symbols, const QString &symbol)
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

void appendIbkrMatchedSymbolVariants(QStringList &symbols, const QString &symbol)
{
    const QString trimmed = symbol.trimmed().toUpper();
    appendUniqueRawIbkrSymbol(symbols, trimmed);

    static const QRegularExpression helsinkiSeriesSuffix(QStringLiteral("^([A-Z]{2,})(1[HV])$"));
    const QRegularExpressionMatch match = helsinkiSeriesSuffix.match(trimmed);
    if (match.hasMatch())
        appendUniqueRawIbkrSymbol(symbols, match.captured(1));
}

QString yahooSuffixForIbkrExchange(const QString &exchange)
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

bool isGenericIbkrTradingClass(const QString &value)
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

QString preferredYahooSuffix(const QStringList &exchangeValues)
{
    for (const QString &exchange : exchangeValues) {
        const QString suffix = yahooSuffixForIbkrExchange(exchange);
        if (!suffix.isEmpty())
            return suffix;
    }
    return QString();
}

QStringList yahooCandidateSymbols(const QString &symbol,
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

QStringList preferYahooSuffix(const QStringList &symbols, const QString &preferredSuffix)
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

QString yahooExchangeCodeFromSymbol(const QString &yahooSymbol)
{
    const QString suffix = yahooSymbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();
    if (!suffix.isEmpty())
        return suffix;
    if (yahooSymbol.contains(QLatin1Char('=')))
        return QStringLiteral("?");
    return QStringLiteral("US");
}

QString yahooSymbolFromRawData(const QString &rawData)
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

QString alphaVantageSearchKeywords(const QString &name, const QString &symbol)
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

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent)
{
    m_ibkrPorts = {7497, 7496, 4002, 4001};
    m_ibkrConnectTimeout.setSingleShot(true);
    m_ibkrConnectTimeout.setInterval(1200);
    connect(&m_ibkrConnectTimeout, &QTimer::timeout, this, [this]() {
        m_ibkrPortAdvanceInProgress = true;
        m_ibkrSocket.abort();
        ++m_ibkrPortIndex;
        m_ibkrPortAdvanceInProgress = false;
        tryNextIbkrPort();
    });
    connect(&m_ibkrSocket, &QTcpSocket::connected, this, [this]() {
        m_ibkrConnectTimeout.stop();
        m_ibkrConnectedPort = m_ibkrSocket.peerPort();
        setIbkrConnectionState(
            QStringLiteral("IBKR TWS/IB Gateway ist auf 127.0.0.1:%1 erreichbar.")
                .arg(m_ibkrSocket.peerPort()),
            true,
            false);
        m_ibkrProbeDisconnectInProgress = true;
        m_ibkrSocket.disconnectFromHost();
    });
    connect(&m_ibkrSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_ibkrConnecting || m_ibkrPortAdvanceInProgress)
            return;
        m_ibkrConnectTimeout.stop();
        ++m_ibkrPortIndex;
        QTimer::singleShot(0, this, &DatabaseManager::tryNextIbkrPort);
    });
    connect(&m_ibkrSocket, &QTcpSocket::disconnected, this, [this]() {
        if (m_ibkrProbeDisconnectInProgress) {
            m_ibkrProbeDisconnectInProgress = false;
            return;
        }
        if (m_ibkrConnecting || m_ibkrDataLoading)
            return;
        if (m_ibkrConnected) {
            setIbkrConnectionState(
                QStringLiteral("Die Verbindung zu IBKR TWS/IB Gateway wurde getrennt."),
                false,
                false);
        }
    });

    m_ibkrDataTimeout.setSingleShot(true);
    m_ibkrDataTimeout.setInterval(25000);
    connect(&m_ibkrDataTimeout, &QTimer::timeout, this, [this]() {
        const QString timedOutSymbol = m_ibkrPendingSymbol;
        const QString timedOutNameCheckSymbol = m_pendingIbkrNameCheckSymbol;
        m_ibkrDataLoading = false;
        if (m_ibkrProcess.state() != QProcess::NotRunning) {
            m_ibkrProcess.kill();
            m_ibkrProcess.waitForFinished(2000);
        }
        setIbkrConnectionState(
            QStringLiteral("Fehler: Zeitüberschreitung beim Abruf der IBKR-Daten."),
            m_ibkrConnected,
            false);
        if (m_pendingIbkrProcessIsHistoricalQuotes || m_pendingIbkrProcessIsQuoteExchangeProbe) {
            m_pendingIbkrProcessIsHistoricalQuotes = false;
            m_pendingIbkrProcessIsQuoteExchangeProbe = false;
            updateIbkrQuoteExchangeFailure(timedOutSymbol, QStringLiteral("Timeout"));
            if (m_ibkrGetStocksBatchActive) {
                ++m_ibkrGetStocksFailureCount;
                setIbkrConnectionState(
                    QStringLiteral("IBKR Get Quotes: %1 Timeout. OK: %2, Fehler: %3.")
                        .arg(timedOutSymbol)
                        .arg(m_ibkrGetStocksSuccessCount)
                        .arg(m_ibkrGetStocksFailureCount),
                    m_ibkrConnected,
                    false);
            }
            m_pendingIbkrQuotesSymbol.clear();
            m_pendingIbkrQuotesIsin.clear();
            m_pendingIbkrQuotesIbkrSymbol.clear();
            m_pendingIbkrQuotesCurrency.clear();
            m_pendingIbkrQuotesExchange.clear();
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrQuotesProbeExchanges.clear();
            m_pendingIbkrQuotesConId = 0;
            m_pendingIbkrQuotesDays = 0;
            m_pendingIbkrQuotesFallbackIndex = 0;
            m_pendingIbkrQuotesSupportsSmart = false;
            m_pendingIbkrQuotesForceDirectProbeResult = false;
            m_ibkrPendingSymbol.clear();
            m_ibkrDataTimeout.setInterval(25000);
            if (m_ibkrGetStocksBatchActive)
                scheduleNextIbkrGetStocksSymbol(1000);
            emit ibkrConnectionChanged();
        } else if (m_pendingIbkrDataForNameCheckRecovery && m_ibkrNameCheckBatchActive) {
            m_pendingIbkrDataForNameCheckRecovery = false;
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(timedOutSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenssuche Timeout"));
            m_ibkrPendingSymbol.clear();
            scheduleNextIbkrNameCheckBatchSymbol(1000);
        } else if (m_ibkrBatchActive) {
            ++m_ibkrBatchFailureCount;
            updateIbkrBatchFailure(timedOutSymbol, QStringLiteral("Timeout"));
            m_ibkrPendingSymbol.clear();
            setIbkrConnectionState(
                QStringLiteral("IBKR-Batch: %1 Timeout. Erfolgreich: %2, Fehler: %3.")
                    .arg(timedOutSymbol)
                    .arg(m_ibkrBatchSuccessCount)
                    .arg(m_ibkrBatchFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrBatchSymbol(1000);
        } else if (m_ibkrNameCheckBatchActive && m_pendingIbkrProcessIsNameCheck) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(timedOutNameCheckSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung Timeout"));
            m_pendingIbkrProcessIsNameCheck = false;
            m_pendingIbkrNameCheckSymbol.clear();
            m_pendingIbkrNameCheckName.clear();
            m_pendingIbkrNameCheckIsin.clear();
            m_pendingIbkrNameCheckHasConId = false;
            m_pendingIbkrNameCheckRequestUsesIsin = false;
            m_pendingIbkrNameCheckCandidates.clear();
            m_pendingIbkrNameCheckCandidateIndex = 0;
            scheduleNextIbkrNameCheckBatchSymbol(1000);
        }
    });
    connect(&m_ibkrProcess,
            &QProcess::errorOccurred,
            this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_ibkrDataTimeout.stop();
        m_ibkrDataLoading = false;
        setIbkrConnectionState(
            QStringLiteral("Fehler: Der IBKR-Helfer konnte nicht gestartet werden."),
            m_ibkrConnected,
            false);
        if (m_pendingIbkrProcessIsHistoricalQuotes || m_pendingIbkrProcessIsQuoteExchangeProbe) {
            m_pendingIbkrProcessIsHistoricalQuotes = false;
            m_pendingIbkrProcessIsQuoteExchangeProbe = false;
            updateIbkrQuoteExchangeFailure(m_ibkrPendingSymbol, QStringLiteral("IBKR-Helfer konnte nicht gestartet werden"));
            if (m_ibkrGetStocksBatchActive) {
                ++m_ibkrGetStocksFailureCount;
                setIbkrConnectionState(
                    QStringLiteral("IBKR Get Quotes: %1 Helper konnte nicht gestartet werden. OK: %2, Fehler: %3.")
                        .arg(m_ibkrPendingSymbol)
                        .arg(m_ibkrGetStocksSuccessCount)
                        .arg(m_ibkrGetStocksFailureCount),
                    m_ibkrConnected,
                    false);
            }
            m_pendingIbkrQuotesSymbol.clear();
            m_pendingIbkrQuotesIsin.clear();
            m_pendingIbkrQuotesIbkrSymbol.clear();
            m_pendingIbkrQuotesCurrency.clear();
            m_pendingIbkrQuotesExchange.clear();
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrQuotesProbeExchanges.clear();
            m_pendingIbkrQuotesConId = 0;
            m_pendingIbkrQuotesDays = 0;
            m_pendingIbkrQuotesFallbackIndex = 0;
            m_pendingIbkrQuotesSupportsSmart = false;
            m_pendingIbkrQuotesForceDirectProbeResult = false;
            m_ibkrPendingSymbol.clear();
            m_ibkrDataTimeout.setInterval(25000);
            if (m_ibkrGetStocksBatchActive)
                scheduleNextIbkrGetStocksSymbol(1000);
            emit ibkrConnectionChanged();
        } else if (m_pendingIbkrDataForNameCheckRecovery && m_ibkrNameCheckBatchActive) {
            m_pendingIbkrDataForNameCheckRecovery = false;
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(m_ibkrPendingSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Helfer konnte nicht gestartet werden"));
            m_ibkrPendingSymbol.clear();
            scheduleNextIbkrNameCheckBatchSymbol(1000);
        } else if (m_ibkrBatchActive) {
            ++m_ibkrBatchFailureCount;
            updateIbkrBatchFailure(m_ibkrPendingSymbol, QStringLiteral("IBKR-Helfer konnte nicht gestartet werden"));
            m_ibkrPendingSymbol.clear();
            scheduleNextIbkrBatchSymbol(1000);
        } else if (m_ibkrNameCheckBatchActive && m_pendingIbkrProcessIsNameCheck) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(m_pendingIbkrNameCheckSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Helfer konnte nicht gestartet werden"));
            m_pendingIbkrProcessIsNameCheck = false;
            m_pendingIbkrNameCheckSymbol.clear();
            m_pendingIbkrNameCheckName.clear();
            m_pendingIbkrNameCheckIsin.clear();
            m_pendingIbkrNameCheckHasConId = false;
            m_pendingIbkrNameCheckRequestUsesIsin = false;
            m_pendingIbkrNameCheckCandidates.clear();
            m_pendingIbkrNameCheckCandidateIndex = 0;
            scheduleNextIbkrNameCheckBatchSymbol(1000);
        }
    });
    connect(&m_ibkrProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &DatabaseManager::finishIbkrDataRequest);
    m_ibkrBatchTimer.setSingleShot(true);
    connect(&m_ibkrBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextIbkrBatchSymbol);
    m_ibkrGetStocksTimer.setSingleShot(true);
    connect(&m_ibkrGetStocksTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextIbkrGetStocksSymbol);
    m_ibkrNameCheckBatchTimer.setSingleShot(true);
    connect(&m_ibkrNameCheckBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextIbkrNameCheckBatchSymbol);
    m_yahooFundamentalsBatchTimer.setSingleShot(true);
    connect(&m_yahooFundamentalsBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextYahooFundamentalsBatchSymbol);
    m_marketstackBatchTimer.setSingleShot(true);
    connect(&m_marketstackBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextMarketstackBatchSymbol);
    m_marketstackQuotesBatchTimer.setSingleShot(true);
    connect(&m_marketstackQuotesBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextMarketstackQuotesBatchSymbol);
    m_marketstackValidationBatchTimer.setSingleShot(true);
    connect(&m_marketstackValidationBatchTimer,
            &QTimer::timeout,
            this,
            &DatabaseManager::loadNextMarketstackValidationBatchSymbol);
    connect(&alphaVantageClient,
            &AlphaVantageClient::fundamentalOverviewReceived,
            this,
            [this](const QString &requestSymbol, const QVariantMap &data) {
        const QString symbol = m_pendingFundamentalSymbol.isEmpty()
            ? requestSymbol
            : m_pendingFundamentalSymbol;
        if (saveAlphaVantageFundamentals(symbol, data)) {
            if (!m_pendingResolvedAlphaVantageSymbol.isEmpty())
                cacheAlphaVantageSymbol(symbol, m_pendingResolvedAlphaVantageSymbol);
            setFundamentalDataStatus(
                QStringLiteral("Alpha-Vantage-Fundamentaldaten fuer %1 wurden gespeichert. Noch %2 freie Abrufe heute.")
                    .arg(symbol)
                    .arg(alphaVantageRequestsRemaining()),
                false);
            emit fundamentalDataUpdated(symbol);
        } else {
            setFundamentalDataStatus(
                QStringLiteral("Fehler: Alpha-Vantage-Fundamentaldaten konnten nicht gespeichert werden."),
                false);
        }
        resetFundamentalRequestState();
    });
    connect(&alphaVantageClient,
            &AlphaVantageClient::fundamentalOverviewNotFound,
            this,
            [this](const QString &apiSymbol) {
        const QString symbol = m_pendingFundamentalSymbol.isEmpty()
            ? apiSymbol
            : m_pendingFundamentalSymbol;
        if (m_pendingAlphaVantageCandidateIndex < m_pendingAlphaVantageCandidates.size()) {
            setFundamentalDataStatus(
                QStringLiteral("Alpha Vantage hat fuer %1 (%2) keine Fundamentaldaten geliefert. Naechster Kandidat startet gleich ...")
                    .arg(symbol)
                    .arg(apiSymbol),
                true);
            QTimer::singleShot(1200, this, [this, symbol]() {
                if (m_pendingFundamentalSymbol == symbol)
                    tryNextAlphaVantageCandidate();
            });
        } else {
            const QString checkedSymbols = m_pendingAlphaVantageCandidates.isEmpty()
                ? apiSymbol
                : m_pendingAlphaVantageCandidates.join(QStringLiteral(", "));
            setFundamentalDataStatus(
                QStringLiteral("Alpha Vantage hat fuer %1 keine Fundamentaldaten geliefert. Gepruefte Kandidaten: %2. Yahoo wird versucht ...")
                    .arg(symbol)
                    .arg(checkedSymbols),
                true);
            QTimer::singleShot(1200, this, [this, symbol]() {
                if (m_pendingFundamentalSymbol == symbol)
                    fetchYahooFundamentalsFallback(symbol);
            });
        }
    });
    connect(&alphaVantageClient,
            &AlphaVantageClient::fundamentalSymbolResolved,
            this,
            [this](const QString &requestSymbol, const QStringList &resolvedSymbols) {
        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        m_pendingAlphaVantageCandidates = resolvedSymbols;
        m_pendingAlphaVantageCandidateIndex = 0;
        m_pendingResolvedAlphaVantageSymbol.clear();
        setFundamentalDataStatus(
            QStringLiteral("Alpha-Vantage-Symbole fuer %1 gefunden: %2. Fundamentaldaten starten gleich ...")
                .arg(requestSymbol)
                .arg(resolvedSymbols.join(QStringLiteral(", "))),
            true);
        QTimer::singleShot(1200, this, [this, requestSymbol]() {
            if (m_pendingFundamentalSymbol != requestSymbol) {
                return;
            }
            tryNextAlphaVantageCandidate();
        });
    });
    connect(&alphaVantageClient,
            &AlphaVantageClient::fundamentalSymbolResolveFailed,
            this,
            [this](const QString &requestSymbol, const QString &message) {
        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        setFundamentalDataStatus(
            QStringLiteral("Alpha-Vantage-Symbolsuche fuer %1 fehlgeschlagen: %2. Yahoo wird versucht ...")
                .arg(requestSymbol)
                .arg(message),
            true);
        QTimer::singleShot(1200, this, [this, requestSymbol]() {
            if (m_pendingFundamentalSymbol == requestSymbol)
                fetchYahooFundamentalsFallback(requestSymbol);
        });
    });
    connect(&alphaVantageClient,
            &AlphaVantageClient::errorOccurred,
            this,
            [this](const QString &error) {
        if (!m_fundamentalDataLoading)
            return;
        resetFundamentalRequestState();
        setFundamentalDataStatus(QStringLiteral("Fehler: %1").arg(error), false);
    });
    connect(&yahooFinanceClient,
            &YahooFinanceClient::fundamentalsReceived,
            this,
            [this](const QString &requestSymbol, const QString &yahooSymbol, const QVariantMap &data) {
        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        const int score = yahooFundamentalScore(data);
        if (isYahooNoClassicFundamentals(data)) {
            if (saveYahooFundamentals(requestSymbol, yahooSymbol, data)) {
                cacheYahooSymbol(requestSymbol, yahooSymbol);
                updateYahooFundamentalSuccess(requestSymbol, 0);
                if (m_yahooFundamentalsBatchActive) {
                    ++m_yahooFundamentalsBatchSuccessCount;
                    setFundamentalDataStatus(
                        QStringLiteral("Yahoo-Batch: %1 (%2) gefunden, aber keine klassischen Fundamentaldaten fuer ETF/ETN/Fonds. Erfolgreich: %3, Fehler: %4.")
                            .arg(requestSymbol)
                            .arg(yahooSymbol)
                            .arg(m_yahooFundamentalsBatchSuccessCount)
                            .arg(m_yahooFundamentalsBatchFailureCount),
                        true);
                    emit fundamentalDataUpdated(requestSymbol);
                    resetFundamentalRequestState();
                    scheduleNextYahooFundamentalsBatchSymbol(1500);
                    return;
                }
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Symbol fuer %1 (%2) gefunden, aber Yahoo liefert keine klassischen Fundamentaldaten fuer ETF/ETN/Fonds.")
                        .arg(requestSymbol)
                        .arg(yahooSymbol),
                    false);
                emit fundamentalDataUpdated(requestSymbol);
            } else {
                if (m_yahooFundamentalsBatchActive) {
                    ++m_yahooFundamentalsBatchFailureCount;
                    updateYahooFundamentalFailure(requestSymbol, QStringLiteral("Yahoo-ETF/Fonds-Hinweis konnte nicht gespeichert werden."));
                    setFundamentalDataStatus(
                        QStringLiteral("Yahoo-Batch: %1 konnte nicht gespeichert werden. Erfolgreich: %2, Fehler: %3.")
                            .arg(requestSymbol)
                            .arg(m_yahooFundamentalsBatchSuccessCount)
                            .arg(m_yahooFundamentalsBatchFailureCount),
                        true);
                    resetFundamentalRequestState();
                    scheduleNextYahooFundamentalsBatchSymbol(3000);
                    return;
                }
                setFundamentalDataStatus(
                    QStringLiteral("Fehler: Yahoo-ETF/Fonds-Hinweis konnte nicht gespeichert werden."),
                    false);
            }
            resetFundamentalRequestState();
            return;
        }
        if (score > m_pendingYahooBestScore) {
            m_pendingYahooBestScore = score;
            m_pendingYahooBestSymbol = yahooSymbol;
            m_pendingYahooBestData = data;
        }

        if (score < 5) {
            m_pendingYahooLastError = QStringLiteral("%1 lieferte nur %2 Kennzahlen")
                                          .arg(yahooSymbol)
                                          .arg(score);
            tryNextYahooCandidate();
            return;
        }

        if (saveYahooFundamentals(requestSymbol, yahooSymbol, data)) {
            cacheYahooSymbol(requestSymbol, yahooSymbol);
            updateYahooFundamentalSuccess(requestSymbol, score);
            if (m_yahooFundamentalsBatchActive) {
                ++m_yahooFundamentalsBatchSuccessCount;
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Batch: %1 gespeichert (%2). Erfolgreich: %3, Fehler: %4.")
                        .arg(requestSymbol)
                        .arg(yahooSymbol)
                        .arg(m_yahooFundamentalsBatchSuccessCount)
                        .arg(m_yahooFundamentalsBatchFailureCount),
                    true);
                emit fundamentalDataUpdated(requestSymbol);
                resetFundamentalRequestState();
                scheduleNextYahooFundamentalsBatchSymbol(1500);
                return;
            }
            setFundamentalDataStatus(
                QStringLiteral("Yahoo-Fundamentaldaten fuer %1 (%2) wurden gespeichert.")
                    .arg(requestSymbol)
                    .arg(yahooSymbol),
                false);
            emit fundamentalDataUpdated(requestSymbol);
        } else {
            if (m_yahooFundamentalsBatchActive) {
                ++m_yahooFundamentalsBatchFailureCount;
                updateYahooFundamentalFailure(requestSymbol, QStringLiteral("Yahoo-Fundamentaldaten konnten nicht gespeichert werden."));
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Batch: %1 konnte nicht gespeichert werden. Erfolgreich: %2, Fehler: %3.")
                        .arg(requestSymbol)
                        .arg(m_yahooFundamentalsBatchSuccessCount)
                        .arg(m_yahooFundamentalsBatchFailureCount),
                    true);
                resetFundamentalRequestState();
                scheduleNextYahooFundamentalsBatchSymbol(3000);
                return;
            }
            setFundamentalDataStatus(
                QStringLiteral("Fehler: Yahoo-Fundamentaldaten konnten nicht gespeichert werden."),
                false);
        }
        resetFundamentalRequestState();
    });
    connect(&yahooFinanceClient,
            &YahooFinanceClient::fundamentalsFailed,
            this,
            [this](const QString &requestSymbol, const QString &yahooSymbol, const QString &message) {
        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        m_pendingYahooLastError = QStringLiteral("%1: %2").arg(yahooSymbol, message);
        tryNextYahooCandidate();
    });
    connect(&yahooFinanceClient,
            &YahooFinanceClient::symbolResolved,
            this,
            [this](const QString &requestSymbol, const QStringList &yahooSymbols) {
        if (m_ibkrPendingSymbol == requestSymbol && m_pendingIbkrSearchStarted) {
            for (const QString &symbol : yahooSymbols)
                appendIbkrSymbolVariants(m_pendingIbkrCandidateSymbols, symbol);
            if (!tryNextIbkrCandidate())
                finalizeIbkrDataFailure(m_pendingIbkrLastError);
            return;
        }

        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        const QStringList orderedSymbols = preferYahooSuffix(yahooSymbols, m_pendingPreferredYahooSuffix);
        for (const QString &symbol : orderedSymbols)
            appendUniqueSymbol(m_pendingYahooCandidates, symbol);
        tryNextYahooCandidate();
    });
    connect(&yahooFinanceClient,
            &YahooFinanceClient::symbolResolveFailed,
            this,
            [this](const QString &requestSymbol, const QString &message) {
        if (m_ibkrPendingSymbol == requestSymbol && m_pendingIbkrSearchStarted) {
            m_pendingIbkrLastError = message;
            if (!tryNextIbkrCandidate(message))
                finalizeIbkrDataFailure(message);
            return;
        }

        if (m_pendingFundamentalSymbol != requestSymbol)
            return;

        m_pendingYahooLastError = message;
        tryNextYahooCandidate();
    });

    db = QSqlDatabase::addDatabase("QPSQL"); // PostgreSQL-Treiber
    db.setHostName("localhost");
    db.setDatabaseName("TotalStocks");
    db.setUserName("postgres");
    db.setPassword("castell");

    if (!db.open()) {
        qDebug() << "Fehler bei der Verbindung zur Datenbank:" << db.lastError().text();
    } else {
        qDebug() << "Erfolgreich mit der Datenbank verbunden!";
        if (!ensureSchema())
            qCritical() << "Datenbankschema konnte nicht aktualisiert werden.";
        qDebug() << "Tables:" << db.tables(QSql::Tables);
    }
}

QString DatabaseManager::ibkrConnectionStatus() const
{
    return m_ibkrConnectionStatus;
}

bool DatabaseManager::ibkrConnected() const
{
    return m_ibkrConnected;
}

bool DatabaseManager::ibkrConnecting() const
{
    return m_ibkrConnecting;
}

bool DatabaseManager::ibkrDataLoading() const
{
    return m_ibkrDataLoading;
}

QString DatabaseManager::fundamentalDataStatus() const
{
    return m_fundamentalDataStatus;
}

bool DatabaseManager::fundamentalDataLoading() const
{
    return m_fundamentalDataLoading;
}

int DatabaseManager::alphaVantageRequestsRemaining() const
{
    if (!db.isOpen())
        return 0;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT COALESCE("RequestCount", 0)
        FROM "ApiDailyUsage"
        WHERE "Provider" = 'AlphaVantage'
          AND "UsageDate" = CURRENT_DATE
    )SQL");
    if (!query.exec() || !query.next())
        return 25;

    return qMax(0, 25 - query.value(0).toInt());
}

int DatabaseManager::yahooFundamentalsBatchTotal() const
{
    return m_yahooFundamentalsBatchSymbols.size();
}

int DatabaseManager::yahooFundamentalsBatchDone() const
{
    return m_yahooFundamentalsBatchSuccessCount + m_yahooFundamentalsBatchFailureCount;
}

bool DatabaseManager::yahooFundamentalsBatchActive() const
{
    return m_yahooFundamentalsBatchActive;
}

int DatabaseManager::ibkrBatchTotal() const
{
    return m_ibkrBatchSymbols.size();
}

int DatabaseManager::ibkrBatchDone() const
{
    return m_ibkrBatchSuccessCount + m_ibkrBatchFailureCount;
}

bool DatabaseManager::ibkrBatchActive() const
{
    return m_ibkrBatchActive;
}

bool DatabaseManager::ibkrGetStocksActive() const
{
    return m_ibkrGetStocksBatchActive
        || m_pendingIbkrProcessIsHistoricalQuotes
        || m_pendingIbkrProcessIsQuoteExchangeProbe;
}

int DatabaseManager::ibkrGetStocksTotal() const
{
    return m_ibkrGetStocksSymbols.size();
}

int DatabaseManager::ibkrGetStocksDone() const
{
    return m_ibkrGetStocksSuccessCount + m_ibkrGetStocksFailureCount;
}

int DatabaseManager::marketstackBatchTotal() const
{
    return m_marketstackBatchSymbols.size();
}

int DatabaseManager::marketstackBatchDone() const
{
    return m_marketstackBatchSuccessCount + m_marketstackBatchFailureCount;
}

bool DatabaseManager::marketstackBatchActive() const
{
    return m_marketstackBatchActive;
}

int DatabaseManager::marketstackQuotesBatchTotal() const
{
    return m_marketstackQuotesBatchSymbols.size();
}

int DatabaseManager::marketstackQuotesBatchDone() const
{
    return m_marketstackQuotesBatchSuccessCount + m_marketstackQuotesBatchFailureCount;
}

bool DatabaseManager::marketstackQuotesBatchActive() const
{
    return m_marketstackQuotesBatchActive;
}

int DatabaseManager::marketstackValidationBatchTotal() const
{
    return m_marketstackValidationBatchSymbols.size();
}

int DatabaseManager::marketstackValidationBatchDone() const
{
    return m_marketstackValidationBatchSuccessCount + m_marketstackValidationBatchFailureCount;
}

bool DatabaseManager::marketstackValidationBatchActive() const
{
    return m_marketstackValidationBatchActive;
}

int DatabaseManager::ibkrNameCheckBatchTotal() const
{
    return m_ibkrNameCheckBatchSymbols.size();
}

int DatabaseManager::ibkrNameCheckBatchDone() const
{
    return m_ibkrNameCheckBatchSuccessCount + m_ibkrNameCheckBatchFailureCount;
}

bool DatabaseManager::ibkrNameCheckBatchActive() const
{
    return m_ibkrNameCheckBatchActive;
}

void DatabaseManager::setIbkrConnectionState(const QString &status,
                                             bool connected,
                                             bool connecting)
{
    if (m_ibkrConnectionStatus == status
        && m_ibkrConnected == connected
        && m_ibkrConnecting == connecting) {
        return;
    }

    m_ibkrConnectionStatus = status;
    m_ibkrConnected = connected;
    m_ibkrConnecting = connecting;
    emit ibkrConnectionChanged();
}

void DatabaseManager::setFundamentalDataStatus(const QString &status, bool loading)
{
    if (m_fundamentalDataStatus == status && m_fundamentalDataLoading == loading)
        return;

    m_fundamentalDataStatus = status;
    m_fundamentalDataLoading = loading;
    emit fundamentalDataChanged();
}

void DatabaseManager::getAlphaVantageFundamentals(const QString &symbol)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || m_fundamentalDataLoading)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        resetFundamentalRequestState();
        return;
    }

    resetFundamentalRequestState();
    m_pendingFundamentalSymbol = normalizedSymbol;
    fetchYahooFundamentalsFallback(normalizedSymbol);
}

void DatabaseManager::startYahooFundamentalsBatch()
{
    if (m_fundamentalDataLoading || m_yahooFundamentalsBatchActive)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT s."Symbol"
        FROM "Stocks" s
        WHERE COALESCE(s."Symbol", '') <> ''
          AND COALESCE(s."IBKRLastError", '') = ''
          AND COALESCE(s."IBKRValidationStatus", '') NOT IN (
              'duplicate_isin',
              'ambiguous_isin',
              'review_required',
              'name_mismatch'
          )
          AND (
              s."YahooFundamentalsLastSuccessAt" IS NULL
              OR s."YahooFundamentalsLastSuccessAt" < CURRENT_TIMESTAMP - INTERVAL '30 days'
          )
          AND (
              COALESCE(s."YahooFundamentalsFailureCount", 0) < 3
              OR s."YahooFundamentalsLastAttemptAt" IS NULL
              OR s."YahooFundamentalsLastAttemptAt" < CURRENT_TIMESTAMP - INTERVAL '1 day'
          )
        ORDER BY
          CASE WHEN s."YahooFundamentalsLastSuccessAt" IS NULL THEN 0 ELSE 1 END,
          s."YahooFundamentalsLastAttemptAt" NULLS FIRST,
          s."Symbol"
    )SQL");

    if (!query.exec()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Yahoo-Batch konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            false);
        return;
    }

    m_yahooFundamentalsBatchSymbols.clear();
    while (query.next())
        m_yahooFundamentalsBatchSymbols << query.value(0).toString();

    if (m_yahooFundamentalsBatchSymbols.isEmpty()) {
        setFundamentalDataStatus(QStringLiteral("Yahoo-Batch: Keine faelligen Aktien gefunden."), false);
        return;
    }

    resetFundamentalRequestState();
    m_yahooFundamentalsBatchActive = true;
    m_yahooFundamentalsBatchIndex = 0;
    m_yahooFundamentalsBatchSuccessCount = 0;
    m_yahooFundamentalsBatchFailureCount = 0;
    setFundamentalDataStatus(
        QStringLiteral("Yahoo-Batch gestartet: %1 Aktien werden seriell aktualisiert.")
            .arg(m_yahooFundamentalsBatchSymbols.size()),
        true);
    scheduleNextYahooFundamentalsBatchSymbol(100);
}

void DatabaseManager::stopYahooFundamentalsBatch()
{
    if (!m_yahooFundamentalsBatchActive)
        return;

    m_yahooFundamentalsBatchTimer.stop();
    const int processed = qMax(0, m_yahooFundamentalsBatchIndex - 1);
    finishYahooFundamentalsBatch(
        QStringLiteral("Yahoo-Batch gestoppt: %1/%2 verarbeitet, %3 erfolgreich, %4 fehlgeschlagen.")
            .arg(processed)
            .arg(m_yahooFundamentalsBatchSymbols.size())
            .arg(m_yahooFundamentalsBatchSuccessCount)
            .arg(m_yahooFundamentalsBatchFailureCount));
}

void DatabaseManager::startIbkrBatch()
{
    if (m_ibkrBatchActive || m_ibkrDataLoading)
        return;

    if (!m_ibkrConnected || m_ibkrConnectedPort == 0) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Zuerst eine Verbindung zu IBKR herstellen."),
            false,
            false);
        return;
    }

    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT s."Symbol"
        FROM "Stocks" s
        LEFT JOIN "BoughtStocks" b ON b."Symbol" = s."Symbol"
        WHERE COALESCE(s."Symbol", '') <> ''
          AND COALESCE(s."ISIN", '') = ''
          AND COALESCE(s."IBKRValidationStatus", '') NOT IN (
              'duplicate_isin',
              'ambiguous_isin',
              'review_required',
              'name_mismatch',
              'verified_name',
              'verified_isin',
              'verified_symbol'
          )
          AND (
              s."IBKRLastSyncAt" IS NULL
              OR s."IBKRLastSyncAt" < CURRENT_TIMESTAMP - INTERVAL '30 days'
          )
          AND (
              COALESCE(s."IBKRFailureCount", 0) < 3
              OR s."IBKRLastAttemptAt" IS NULL
              OR s."IBKRLastAttemptAt" < CURRENT_TIMESTAMP - INTERVAL '1 day'
          )
        ORDER BY
          CASE
            WHEN s."IBKRConId" IS NULL AND COALESCE(s."IBKRLastError", '') <> '' THEN 0
            ELSE 1
          END,
          s."IBKRLastAttemptAt" DESC NULLS LAST,
          CASE WHEN b."Symbol" IS NOT NULL THEN 0 ELSE 1 END,
          CASE WHEN COALESCE(s."ISIN", '') <> '' THEN 0 ELSE 1 END,
          s."IBKRFailureCount" NULLS FIRST,
          s."IBKRLastSyncAt" NULLS FIRST,
          s."IBKRLastAttemptAt" NULLS FIRST,
          s."Symbol"
    )SQL");

    if (!query.exec()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: IBKR-Batch konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            m_ibkrConnected,
            false);
        return;
    }

    m_ibkrBatchSymbols.clear();
    while (query.next())
        m_ibkrBatchSymbols << query.value(0).toString();

    if (m_ibkrBatchSymbols.isEmpty()) {
        setIbkrConnectionState(QStringLiteral("IBKR-Batch: Keine faelligen Aktien gefunden."), m_ibkrConnected, false);
        return;
    }

    m_ibkrBatchActive = true;
    m_ibkrBatchIndex = 0;
    m_ibkrBatchSuccessCount = 0;
    m_ibkrBatchFailureCount = 0;
    setIbkrConnectionState(
        QStringLiteral("IBKR-Batch gestartet: %1 Aktien werden seriell aktualisiert. Reihenfolge: Depot, dann ISIN, dann uebrige Symbole.")
            .arg(m_ibkrBatchSymbols.size()),
        m_ibkrConnected,
        false);
    scheduleNextIbkrBatchSymbol(100);
}

void DatabaseManager::stopIbkrBatch()
{
    if (!m_ibkrBatchActive)
        return;

    m_ibkrBatchTimer.stop();
    if (m_ibkrDataLoading && m_ibkrProcess.state() != QProcess::NotRunning)
        m_ibkrProcess.kill();

    finishIbkrBatch(
        QStringLiteral("IBKR-Batch gestoppt: %1/%2 verarbeitet, %3 erfolgreich, %4 fehlgeschlagen.")
            .arg(ibkrBatchDone())
            .arg(m_ibkrBatchSymbols.size())
            .arg(m_ibkrBatchSuccessCount)
            .arg(m_ibkrBatchFailureCount));
}

void DatabaseManager::startIbkrGetStocks()
{
    if (m_ibkrGetStocksBatchActive || m_ibkrDataLoading || m_ibkrBatchActive || m_ibkrNameCheckBatchActive)
        return;

    if (!m_ibkrConnected || m_ibkrConnectedPort == 0) {
        setIbkrConnectionState(QStringLiteral("Fehler: Zuerst eine Verbindung zu IBKR herstellen."), false, false);
        return;
    }
    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol"
        FROM "Stocks"
        WHERE COALESCE("Symbol", '') <> ''
          AND "IBKRConId" IS NOT NULL
          AND COALESCE("use_marketstack", FALSE) = FALSE
          AND "IBKRQuoteExchangeLastSuccessAt" IS NULL
          AND COALESCE("IBKRQuoteExchange", '') = ''
        ORDER BY "Symbol"
    )SQL");
    if (!query.exec()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: IBKR Get Quotes konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            m_ibkrConnected,
            false);
        return;
    }

    m_ibkrGetStocksSymbols.clear();
    while (query.next())
        m_ibkrGetStocksSymbols << query.value(0).toString();

    if (m_ibkrGetStocksSymbols.isEmpty()) {
        setIbkrConnectionState(QStringLiteral("IBKR Get Quotes: Keine Aktien mit IBKRConId gefunden."), m_ibkrConnected, false);
        return;
    }

    m_ibkrGetStocksBatchActive = true;
    m_ibkrGetStocksIndex = 0;
    m_ibkrGetStocksSuccessCount = 0;
    m_ibkrGetStocksFailureCount = 0;
    setIbkrConnectionState(
        QStringLiteral("IBKR Get Quotes gestartet: Fuer %1 Aktien werden Boerse und 90-Tage-Quotes aktualisiert.")
            .arg(m_ibkrGetStocksSymbols.size()),
        m_ibkrConnected,
        false);
    scheduleNextIbkrGetStocksSymbol(100);
}

void DatabaseManager::stopIbkrGetStocks()
{
    if (!m_ibkrGetStocksBatchActive
        && !m_pendingIbkrProcessIsHistoricalQuotes
        && !m_pendingIbkrProcessIsQuoteExchangeProbe) {
        return;
    }

    m_ibkrGetStocksTimer.stop();
    m_ibkrDataTimeout.stop();
    if (m_ibkrProcess.state() != QProcess::NotRunning)
        m_ibkrProcess.kill();
    m_ibkrGetStocksBatchActive = false;
    m_ibkrDataLoading = false;
    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    const QString symbol = m_pendingIbkrQuotesSymbol;
    m_pendingIbkrQuotesSymbol.clear();
    m_pendingIbkrQuotesIsin.clear();
    m_pendingIbkrQuotesIbkrSymbol.clear();
    m_pendingIbkrQuotesCurrency.clear();
    m_pendingIbkrQuotesExchange.clear();
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesProbeExchanges.clear();
    m_pendingIbkrQuotesConId = 0;
    m_pendingIbkrQuotesDays = 0;
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = false;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_ibkrPendingSymbol.clear();
    m_ibkrDataTimeout.setInterval(25000);
    setIbkrConnectionState(
        QStringLiteral("IBKR Get Quotes gestoppt%1.")
            .arg(symbol.isEmpty() ? QString() : QStringLiteral(": %1").arg(symbol)),
        m_ibkrConnected,
        false);
    emit ibkrConnectionChanged();
}

void DatabaseManager::loadNextIbkrGetStocksSymbol()
{
    if (!m_ibkrGetStocksBatchActive)
        return;

    if (m_ibkrGetStocksIndex >= m_ibkrGetStocksSymbols.size()) {
        finishIbkrGetStocksBatch(
            QStringLiteral("IBKR Get Quotes abgeschlossen: %1 Aktien, %2 mit 90-Tage-Quotes gespeichert, %3 fehlgeschlagen.")
                .arg(m_ibkrGetStocksSymbols.size())
                .arg(m_ibkrGetStocksSuccessCount)
                .arg(m_ibkrGetStocksFailureCount));
        return;
    }

    const QString symbol = m_ibkrGetStocksSymbols.at(m_ibkrGetStocksIndex++).trimmed();
    if (symbol.isEmpty()) {
        scheduleNextIbkrGetStocksSymbol(50);
        return;
    }

    setIbkrConnectionState(
        QStringLiteral("IBKR Get Quotes: %1/%2 %3 - Boerse pruefen und 90-Tage-Quotes laden. OK: %4, Fehler: %5")
            .arg(m_ibkrGetStocksIndex)
            .arg(m_ibkrGetStocksSymbols.size())
            .arg(symbol)
            .arg(m_ibkrGetStocksSuccessCount)
            .arg(m_ibkrGetStocksFailureCount),
        m_ibkrConnected,
        false);
    if (!startIbkrQuoteExchangeProbeForSymbol(symbol)) {
        ++m_ibkrGetStocksFailureCount;
        scheduleNextIbkrGetStocksSymbol(500);
    }
}

void DatabaseManager::scheduleNextIbkrGetStocksSymbol(int delayMs)
{
    if (!m_ibkrGetStocksBatchActive)
        return;
    m_ibkrGetStocksTimer.start(delayMs);
}

void DatabaseManager::finishIbkrGetStocksBatch(const QString &message)
{
    m_ibkrGetStocksTimer.stop();
    m_ibkrGetStocksBatchActive = false;
    m_ibkrDataLoading = false;
    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrQuotesSymbol.clear();
    m_pendingIbkrQuotesIsin.clear();
    m_pendingIbkrQuotesIbkrSymbol.clear();
    m_pendingIbkrQuotesCurrency.clear();
    m_pendingIbkrQuotesExchange.clear();
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesProbeExchanges.clear();
    m_pendingIbkrQuotesConId = 0;
    m_pendingIbkrQuotesDays = 0;
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = false;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_ibkrDataTimeout.setInterval(25000);
    setIbkrConnectionState(message, m_ibkrConnected, false);
    emit ibkrConnectionChanged();
}

void DatabaseManager::startMarketstackBatch()
{
    if (m_marketstackBatchActive || m_yahooFundamentalsBatchActive)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol"
        FROM "Stocks"
        WHERE COALESCE("use_marketstack", FALSE) = TRUE
          AND COALESCE("marketplace_sym", '') = ''
        ORDER BY "Symbol"
    )SQL");
    if (!query.exec()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Marketstack-Batch konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            false);
        return;
    }

    m_marketstackBatchSymbols.clear();
    while (query.next())
        m_marketstackBatchSymbols << query.value(0).toString();

    if (m_marketstackBatchSymbols.isEmpty()) {
        setFundamentalDataStatus(QStringLiteral("Marketstack-Batch: Keine vorgemerkten Aktien gefunden."), false);
        return;
    }

    m_marketstackBatchActive = true;
    m_marketstackBatchIndex = 0;
    m_marketstackBatchSuccessCount = 0;
    m_marketstackBatchFailureCount = 0;
    setFundamentalDataStatus(
        QStringLiteral("Marketstack-Batch gestartet: Fuer %1 Aktien wird der umsatzstaerkste Marktplatz gesucht.")
            .arg(m_marketstackBatchSymbols.size()),
        true);
    scheduleNextMarketstackBatchSymbol(MarketstackInitialDelayMs);
}

void DatabaseManager::stopMarketstackBatch()
{
    if (!m_marketstackBatchActive)
        return;

    m_marketstackBatchTimer.stop();
    finishMarketstackBatch(
        QStringLiteral("Marketstack-Batch gestoppt: %1/%2 verarbeitet, %3 erfolgreich, %4 fehlgeschlagen.")
            .arg(marketstackBatchDone())
            .arg(m_marketstackBatchSymbols.size())
            .arg(m_marketstackBatchSuccessCount)
            .arg(m_marketstackBatchFailureCount));
}

void DatabaseManager::loadNextMarketstackBatchSymbol()
{
    if (!m_marketstackBatchActive)
        return;

    if (m_marketstackBatchIndex >= m_marketstackBatchSymbols.size()) {
        finishMarketstackBatch(
            QStringLiteral("Marketstack-Batch abgeschlossen: %1 Aktien, %2 Marktplaetze gespeichert, %3 fehlgeschlagen.")
                .arg(m_marketstackBatchSymbols.size())
                .arg(m_marketstackBatchSuccessCount)
                .arg(m_marketstackBatchFailureCount));
        return;
    }

    const QString symbol = m_marketstackBatchSymbols.at(m_marketstackBatchIndex++).trimmed();
    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol", "ISIN", "Name", "MIC", "IBKRResolvedSymbol", "YahooSymbol", "AlphaVantageSymbol"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec() || !query.next()) {
        ++m_marketstackBatchFailureCount;
        scheduleNextMarketstackBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    m_pendingMarketstackSymbol = query.value(QStringLiteral("Symbol")).toString().trimmed();
    m_pendingMarketstackIsin = query.value(QStringLiteral("ISIN")).toString().trimmed();
    m_pendingMarketstackName = query.value(QStringLiteral("Name")).toString().trimmed();
    m_pendingMarketstackMic = query.value(QStringLiteral("MIC")).toString().trimmed().toUpper();
    m_pendingMarketstackIbkrResolvedSymbol =
        query.value(QStringLiteral("IBKRResolvedSymbol")).toString().trimmed();
    m_pendingMarketstackYahooSymbol = query.value(QStringLiteral("YahooSymbol")).toString().trimmed();
    m_pendingMarketstackAlphaVantageSymbol =
        query.value(QStringLiteral("AlphaVantageSymbol")).toString().trimmed();
    setFundamentalDataStatus(
        QStringLiteral("Marketstack-Batch: %1/%2 %3 - Marktplatzsuche laeuft. OK: %4, Fehler: %5")
            .arg(m_marketstackBatchIndex)
            .arg(m_marketstackBatchSymbols.size())
            .arg(m_pendingMarketstackSymbol)
            .arg(m_marketstackBatchSuccessCount)
            .arg(m_marketstackBatchFailureCount),
        true);
    startMarketstackTickerLookup();
}

void DatabaseManager::scheduleNextMarketstackBatchSymbol(int delayMs)
{
    if (!m_marketstackBatchActive)
        return;
    m_marketstackBatchTimer.start(delayMs);
}

void DatabaseManager::finishMarketstackBatch(const QString &message)
{
    m_marketstackBatchTimer.stop();
    m_marketstackBatchActive = false;
    m_pendingMarketstackSymbol.clear();
    m_pendingMarketstackIsin.clear();
    m_pendingMarketstackName.clear();
    m_pendingMarketstackMic.clear();
    m_pendingMarketstackIbkrResolvedSymbol.clear();
    m_pendingMarketstackYahooSymbol.clear();
    m_pendingMarketstackAlphaVantageSymbol.clear();
    m_pendingMarketstackLookupUrls.clear();
    m_pendingMarketstackLookupIndex = 0;
    m_pendingMarketstackCandidates.clear();
    m_pendingMarketstackCandidateIndex = 0;
    m_pendingMarketstackRateLimitRetries = 0;
    m_pendingMarketstackBestMarketplaceSym.clear();
    m_pendingMarketstackBestExchange.clear();
    m_pendingMarketstackBestTurnover = 0.0;
    m_pendingMarketstackBestHasQuotes = false;
    setFundamentalDataStatus(message, false);
}

void DatabaseManager::startMarketstackTickerLookup()
{
    m_pendingMarketstackLookupUrls.clear();
    m_pendingMarketstackLookupIndex = 0;
    m_pendingMarketstackCandidates.clear();
    m_pendingMarketstackCandidateIndex = 0;
    m_pendingMarketstackRateLimitRetries = 0;
    m_pendingMarketstackBestMarketplaceSym.clear();
    m_pendingMarketstackBestExchange.clear();
    m_pendingMarketstackBestTurnover = 0.0;
    m_pendingMarketstackBestHasQuotes = false;

    QStringList lookupSymbols;
    appendUniqueMarketstackSymbol(lookupSymbols, m_pendingMarketstackIbkrResolvedSymbol);
    appendUniqueMarketstackSymbol(lookupSymbols, m_pendingMarketstackAlphaVantageSymbol);
    appendUniqueMarketstackSymbol(lookupSymbols, m_pendingMarketstackYahooSymbol);
    appendUniqueMarketstackSymbol(lookupSymbols, m_pendingMarketstackSymbol);
    if (!lookupSymbols.isEmpty()) {
        appendMarketstackLookupUrl(
            m_pendingMarketstackLookupUrls,
            marketstackUrl(QStringLiteral("tickers"),
                           {{QStringLiteral("symbols"), lookupSymbols.join(QLatin1Char(','))},
                            {QStringLiteral("limit"), QStringLiteral("10")}}));
    }

    const QStringList nameSearchTerms =
        ibkrSymbolSearchKeywordVariants(m_pendingMarketstackName);
    int appendedNameLookups = 0;
    for (const QString &searchTerm : nameSearchTerms) {
        if (appendedNameLookups++ >= MarketstackMaxNameLookupTerms)
            break;
        appendMarketstackLookupUrl(
            m_pendingMarketstackLookupUrls,
            marketstackUrl(QStringLiteral("tickers"),
                           {{QStringLiteral("search"), searchTerm},
                            {QStringLiteral("limit"), QStringLiteral("20")}}));
    }

    requestNextMarketstackTickerLookup();
}

void DatabaseManager::requestNextMarketstackTickerLookup()
{
    if (!m_marketstackBatchActive)
        return;

    if (m_pendingMarketstackLookupIndex >= m_pendingMarketstackLookupUrls.size()) {
        QTimer::singleShot(MarketstackLookupDelayMs,
                           this,
                           &DatabaseManager::requestNextMarketstackCandidateQuotes);
        return;
    }

    const QUrl url = m_pendingMarketstackLookupUrls.at(m_pendingMarketstackLookupIndex++);
    QNetworkReply *reply = m_marketstackNetworkManager.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMarketstackTickerLookupReply(reply);
    });
}

void DatabaseManager::handleMarketstackTickerLookupReply(QNetworkReply *reply)
{
    const QByteArray responseData = reply->readAll();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus == 429) {
        if (++m_pendingMarketstackRateLimitRetries <= MarketstackMaxRateLimitRetries) {
            --m_pendingMarketstackLookupIndex;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack-Batch: Rate-Limit erreicht, pausiere kurz und versuche erneut (%1/%2).")
                    .arg(m_pendingMarketstackRateLimitRetries)
                    .arg(MarketstackMaxRateLimitRetries),
                true);
            QTimer::singleShot(MarketstackRateLimitDelayMs,
                               this,
                               &DatabaseManager::requestNextMarketstackTickerLookup);
            return;
        }
        finishMarketstackBatch(
            QStringLiteral("Marketstack-Batch wegen Marketstack-Rate-Limit gestoppt. Bitte spaeter fortsetzen."));
        return;
    }

    m_pendingMarketstackRateLimitRetries = 0;

    if (networkOk) {
        const QJsonDocument document = QJsonDocument::fromJson(responseData);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        for (const QJsonValue &value : data) {
            const QJsonObject ticker = value.toObject();
            const QJsonObject exchange = ticker.value(QStringLiteral("stock_exchange")).toObject();
            const QString candidateName = ticker.value(QStringLiteral("name")).toString();
            const QString candidateExchange = exchange.value(QStringLiteral("mic")).toString();
            const QString candidateCountryCode =
                exchange.value(QStringLiteral("country_code")).toString();
            if (!marketstackNameMatchesStock(m_pendingMarketstackName, candidateName))
                continue;
            if (!marketstackCountryMatchesStock(m_pendingMarketstackIsin,
                                               candidateCountryCode,
                                               candidateExchange))
                continue;
            appendMarketstackCandidate(
                m_pendingMarketstackCandidates,
                ticker.value(QStringLiteral("symbol")).toString(),
                candidateExchange,
                ticker.value(QStringLiteral("has_eod")).toBool(),
                candidateName,
                candidateCountryCode);
        }
    }

    QTimer::singleShot(MarketstackLookupDelayMs,
                       this,
                       &DatabaseManager::requestNextMarketstackTickerLookup);
}

void DatabaseManager::requestNextMarketstackCandidateQuotes()
{
    if (!m_marketstackBatchActive)
        return;

    if (m_pendingMarketstackCandidateIndex >= m_pendingMarketstackCandidates.size()) {
        if (!m_pendingMarketstackBestMarketplaceSym.isEmpty()
            && saveMarketstackSelection(m_pendingMarketstackSymbol,
                                        m_pendingMarketstackBestMarketplaceSym,
                                        m_pendingMarketstackBestExchange,
                                        m_pendingMarketstackBestTurnover)) {
            ++m_marketstackBatchSuccessCount;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack-Batch: %1 -> %2 gespeichert, Umsatz %3. OK: %4, Fehler: %5")
                    .arg(m_pendingMarketstackSymbol,
                         m_pendingMarketstackBestMarketplaceSym)
                    .arg(m_pendingMarketstackBestTurnover, 0, 'f', 2)
                    .arg(m_marketstackBatchSuccessCount)
                    .arg(m_marketstackBatchFailureCount),
                true);
        } else {
            const QString noDataSymbol = m_pendingMarketstackSymbol;
            if (deleteMarketstackNoDataStock(noDataSymbol)) {
                ++m_marketstackBatchSuccessCount;
                setFundamentalDataStatus(
                    QStringLiteral("Marketstack-Batch: %1 ohne Kursdaten - aus DB geloescht. OK: %2, Fehler: %3")
                        .arg(noDataSymbol)
                        .arg(m_marketstackBatchSuccessCount)
                        .arg(m_marketstackBatchFailureCount),
                    true);
            } else {
                ++m_marketstackBatchFailureCount;
                const QString error = QStringLiteral("Marketstack lieferte keinen plausiblen Marktplatz mit Kursdaten; Loeschen fehlgeschlagen");
                saveMarketstackSelection(noDataSymbol, QString(), QString(), 0.0, error);
                setFundamentalDataStatus(
                    QStringLiteral("Marketstack-Batch: %1 ohne Kursdaten, Loeschen fehlgeschlagen. OK: %2, Fehler: %3")
                        .arg(noDataSymbol)
                        .arg(m_marketstackBatchSuccessCount)
                        .arg(m_marketstackBatchFailureCount),
                    true);
            }
        }
        scheduleNextMarketstackBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    const QVariantMap candidate =
        m_pendingMarketstackCandidates.at(m_pendingMarketstackCandidateIndex++).toMap();
    const QString symbol = candidate.value(QStringLiteral("symbol")).toString();
    const QString exchange = candidate.value(QStringLiteral("exchange")).toString();
    const QUrl url = marketstackUrl(
        QStringLiteral("eod"),
        {{QStringLiteral("symbols"), symbol},
         {QStringLiteral("exchange"), exchange},
         {QStringLiteral("limit"), QStringLiteral("10")}});

    QNetworkReply *reply = m_marketstackNetworkManager.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMarketstackCandidateQuotesReply(reply);
    });
}

void DatabaseManager::handleMarketstackCandidateQuotesReply(QNetworkReply *reply)
{
    const QByteArray responseData = reply->readAll();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus == 429) {
        if (++m_pendingMarketstackRateLimitRetries <= MarketstackMaxRateLimitRetries) {
            --m_pendingMarketstackCandidateIndex;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack-Batch: Rate-Limit erreicht, pausiere kurz und versuche erneut (%1/%2).")
                    .arg(m_pendingMarketstackRateLimitRetries)
                    .arg(MarketstackMaxRateLimitRetries),
                true);
            QTimer::singleShot(MarketstackRateLimitDelayMs,
                               this,
                               &DatabaseManager::requestNextMarketstackCandidateQuotes);
            return;
        }
        finishMarketstackBatch(
            QStringLiteral("Marketstack-Batch wegen Marketstack-Rate-Limit gestoppt. Bitte spaeter fortsetzen."));
        return;
    }

    m_pendingMarketstackRateLimitRetries = 0;

    const QVariantMap candidate =
        m_pendingMarketstackCandidates.value(m_pendingMarketstackCandidateIndex - 1).toMap();
    const QString marketplaceSym = candidate.value(QStringLiteral("marketplaceSym")).toString();
    const QString exchange = candidate.value(QStringLiteral("exchange")).toString();

    double turnover = 0.0;
    bool hasQuotes = false;
    if (networkOk) {
        const QJsonDocument document = QJsonDocument::fromJson(responseData);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        int bars = 0;
        for (const QJsonValue &value : data) {
            if (bars++ >= 10)
                break;
            hasQuotes = true;
            const QJsonObject bar = value.toObject();
            const double close = bar.value(QStringLiteral("close")).toDouble();
            const QJsonValue volumeValue = bar.value(QStringLiteral("volume"));
            const double volume = volumeValue.isDouble() ? volumeValue.toDouble() : 0.0;
            turnover += close * volume;
        }
    }

    if (hasQuotes
        && (!m_pendingMarketstackBestHasQuotes
            || turnover > m_pendingMarketstackBestTurnover)) {
        m_pendingMarketstackBestHasQuotes = true;
        m_pendingMarketstackBestTurnover = turnover;
        m_pendingMarketstackBestMarketplaceSym = marketplaceSym;
        m_pendingMarketstackBestExchange = exchange;
    }

    QTimer::singleShot(MarketstackCandidateDelayMs,
                       this,
                       &DatabaseManager::requestNextMarketstackCandidateQuotes);
}

bool DatabaseManager::saveMarketstackSelection(const QString &symbol,
                                               const QString &marketplaceSym,
                                               const QString &exchange,
                                               double turnover,
                                               const QString &error)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "marketplace_sym" = CASE
                WHEN NULLIF(:marketplaceSym, '') IS NULL THEN "marketplace_sym"
                ELSE :marketplaceSym
            END,
            "marketplace_exchange" = CASE
                WHEN NULLIF(:marketplaceExchange, '') IS NULL THEN "marketplace_exchange"
                ELSE :marketplaceExchange
            END,
            "marketplace_turnover" = CASE
                WHEN NULLIF(:marketplaceSym, '') IS NULL THEN "marketplace_turnover"
                ELSE :turnover
            END,
            "marketplace_checked_at" = CURRENT_TIMESTAMP,
            "marketplace_last_error" = NULLIF(:error, '')
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    query.bindValue(QStringLiteral(":marketplaceSym"), marketplaceSym.trimmed().toUpper());
    query.bindValue(QStringLiteral(":marketplaceExchange"), exchange.trimmed().toUpper());
    query.bindValue(QStringLiteral(":turnover"), turnover);
    query.bindValue(QStringLiteral(":error"), error.trimmed());
    if (!query.exec()) {
        qWarning() << "Marketstack-Marktplatz konnte nicht gespeichert werden:"
                   << query.lastError().text() << symbol << marketplaceSym;
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool DatabaseManager::deleteMarketstackNoDataStock(const QString &symbol)
{
    const QString normalizedSymbol = symbol.trimmed().toUpper();
    if (normalizedSymbol.isEmpty() || !db.isOpen())
        return false;

    QSqlQuery boughtQuery(db);
    boughtQuery.prepare(R"SQL(
        SELECT 1
        FROM "BoughtStocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    boughtQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!boughtQuery.exec()) {
        qWarning() << "Marketstack-No-Data-Kandidat konnte nicht gegen Portfolio geprueft werden:"
                   << boughtQuery.lastError().text() << normalizedSymbol;
        return false;
    }
    if (boughtQuery.next()) {
        qWarning() << "Marketstack-No-Data-Kandidat ist im Portfolio und wird nicht automatisch geloescht:"
                   << normalizedSymbol;
        return false;
    }

    if (!db.transaction()) {
        qWarning() << "Marketstack-No-Data-Kandidat konnte nicht zum Loeschen gesperrt werden:"
                   << db.lastError().text() << normalizedSymbol;
        return false;
    }

    const QStringList statements = {
        QStringLiteral(R"SQL(DELETE FROM "StockFundamentals" WHERE "Symbol" = :symbol)SQL"),
        QStringLiteral(R"SQL(DELETE FROM "Quotes" WHERE "Symbol" = :symbol)SQL"),
        QStringLiteral(R"SQL(DELETE FROM "Stocks_IBKRConflictBackup" WHERE "Symbol" = :symbol)SQL"),
        QStringLiteral(R"SQL(DELETE FROM "Stocks" WHERE "Symbol" = :symbol)SQL")
    };

    for (const QString &statement : statements) {
        QSqlQuery query(db);
        query.prepare(statement);
        query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        if (!query.exec()) {
            qWarning() << "Marketstack-No-Data-Kandidat konnte nicht geloescht werden:"
                       << query.lastError().text() << normalizedSymbol;
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qWarning() << "Marketstack-No-Data-Loeschung konnte nicht abgeschlossen werden:"
                   << db.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    emit ibkrStockDataUpdated(normalizedSymbol);
    return true;
}

void DatabaseManager::startMarketstackQuotesBatch()
{
    if (m_marketstackQuotesBatchActive || m_marketstackBatchActive || m_yahooFundamentalsBatchActive)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT s."Symbol"
        FROM "Stocks" s
        WHERE COALESCE(s."use_marketstack", FALSE) = TRUE
          AND COALESCE(s."marketplace_sym", '') <> ''
        ORDER BY s."Symbol"
    )SQL");
    if (!query.exec()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Marketstack Get Quotes konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            false);
        return;
    }

    m_marketstackQuotesBatchSymbols.clear();
    while (query.next())
        m_marketstackQuotesBatchSymbols << query.value(0).toString();

    if (m_marketstackQuotesBatchSymbols.isEmpty()) {
        setFundamentalDataStatus(QStringLiteral("Marketstack Get Quotes: Keine offenen Aktien gefunden."), false);
        return;
    }

    m_marketstackQuotesBatchActive = true;
    m_marketstackQuotesBatchIndex = 0;
    m_marketstackQuotesBatchSuccessCount = 0;
    m_marketstackQuotesBatchFailureCount = 0;
    m_marketstackQuotesRateLimitRetries = 0;
    setFundamentalDataStatus(
        QStringLiteral("Marketstack Get Quotes gestartet: Fuer %1 Aktien werden Quotes geladen.")
            .arg(m_marketstackQuotesBatchSymbols.size()),
        true);
    scheduleNextMarketstackQuotesBatchSymbol(MarketstackInitialDelayMs);
}

void DatabaseManager::stopMarketstackQuotesBatch()
{
    if (!m_marketstackQuotesBatchActive)
        return;

    finishMarketstackQuotesBatch(
        QStringLiteral("Marketstack Get Quotes gestoppt: %1/%2 verarbeitet, %3 erfolgreich, %4 fehlgeschlagen.")
            .arg(marketstackQuotesBatchDone())
            .arg(m_marketstackQuotesBatchSymbols.size())
            .arg(m_marketstackQuotesBatchSuccessCount)
            .arg(m_marketstackQuotesBatchFailureCount));
}

void DatabaseManager::loadNextMarketstackQuotesBatchSymbol()
{
    if (!m_marketstackQuotesBatchActive)
        return;

    if (m_marketstackQuotesBatchIndex >= m_marketstackQuotesBatchSymbols.size()) {
        finishMarketstackQuotesBatch(
            QStringLiteral("Marketstack Get Quotes abgeschlossen: %1 Aktien, %2 mit Quotes gespeichert, %3 fehlgeschlagen.")
                .arg(m_marketstackQuotesBatchSymbols.size())
                .arg(m_marketstackQuotesBatchSuccessCount)
                .arg(m_marketstackQuotesBatchFailureCount));
        return;
    }

    const QString symbol = m_marketstackQuotesBatchSymbols.at(m_marketstackQuotesBatchIndex++).trimmed();
    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol", "marketplace_sym", "marketplace_exchange"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec() || !query.next()) {
        ++m_marketstackQuotesBatchFailureCount;
        scheduleNextMarketstackQuotesBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    m_pendingMarketstackQuotesSymbol = query.value(QStringLiteral("Symbol")).toString().trimmed();
    m_pendingMarketstackQuotesMarketplaceSym =
        query.value(QStringLiteral("marketplace_sym")).toString().trimmed().toUpper();
    m_pendingMarketstackQuotesExchange =
        query.value(QStringLiteral("marketplace_exchange")).toString().trimmed().toUpper();

    if (m_pendingMarketstackQuotesExchange.isEmpty())
        m_pendingMarketstackQuotesExchange =
            m_pendingMarketstackQuotesMarketplaceSym.section(QLatin1Char('/'), 1, 1).trimmed().toUpper();

    setFundamentalDataStatus(
        QStringLiteral("Marketstack Get Quotes: %1/%2 %3 - Quotes laden. OK: %4, Fehler: %5")
            .arg(m_marketstackQuotesBatchIndex)
            .arg(m_marketstackQuotesBatchSymbols.size())
            .arg(m_pendingMarketstackQuotesSymbol)
            .arg(m_marketstackQuotesBatchSuccessCount)
            .arg(m_marketstackQuotesBatchFailureCount),
        true);
    requestMarketstackQuotesForPendingSymbol();
}

void DatabaseManager::scheduleNextMarketstackQuotesBatchSymbol(int delayMs)
{
    if (!m_marketstackQuotesBatchActive)
        return;
    m_marketstackQuotesBatchTimer.start(delayMs);
}

void DatabaseManager::finishMarketstackQuotesBatch(const QString &message)
{
    m_marketstackQuotesBatchTimer.stop();
    m_marketstackQuotesBatchActive = false;
    m_marketstackQuotesRateLimitRetries = 0;
    m_pendingMarketstackQuotesSymbol.clear();
    m_pendingMarketstackQuotesMarketplaceSym.clear();
    m_pendingMarketstackQuotesExchange.clear();
    setFundamentalDataStatus(message, false);
}

void DatabaseManager::requestMarketstackQuotesForPendingSymbol()
{
    if (!m_marketstackQuotesBatchActive)
        return;

    const QString requestSymbol =
        m_pendingMarketstackQuotesMarketplaceSym.section(QLatin1Char('/'), 0, 0).trimmed().toUpper();
    const QString requestExchange = m_pendingMarketstackQuotesExchange.trimmed().toUpper();
    if (requestSymbol.isEmpty() || requestExchange.isEmpty()) {
        ++m_marketstackQuotesBatchFailureCount;
        scheduleNextMarketstackQuotesBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    const QUrl url = marketstackUrl(
        QStringLiteral("eod"),
        {{QStringLiteral("symbols"), requestSymbol},
         {QStringLiteral("exchange"), requestExchange},
         {QStringLiteral("limit"), QStringLiteral("100")}});

    QNetworkReply *reply = m_marketstackNetworkManager.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMarketstackQuotesReply(reply);
    });
}

void DatabaseManager::handleMarketstackQuotesReply(QNetworkReply *reply)
{
    if (!m_marketstackQuotesBatchActive) {
        reply->deleteLater();
        return;
    }

    const QByteArray responseData = reply->readAll();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus == 429) {
        if (++m_marketstackQuotesRateLimitRetries <= MarketstackMaxRateLimitRetries) {
            setFundamentalDataStatus(
                QStringLiteral("Marketstack Get Quotes: Rate-Limit erreicht, pausiere kurz und versuche erneut (%1/%2).")
                    .arg(m_marketstackQuotesRateLimitRetries)
                    .arg(MarketstackMaxRateLimitRetries),
                true);
            QTimer::singleShot(MarketstackRateLimitDelayMs,
                               this,
                               &DatabaseManager::requestMarketstackQuotesForPendingSymbol);
            return;
        }
        finishMarketstackQuotesBatch(
            QStringLiteral("Marketstack Get Quotes wegen Marketstack-Rate-Limit gestoppt. Bitte spaeter fortsetzen."));
        return;
    }

    m_marketstackQuotesRateLimitRetries = 0;

    bool saved = false;
    if (networkOk) {
        const QJsonDocument document = QJsonDocument::fromJson(responseData);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        saved = saveMarketstackHistoricalQuotes(m_pendingMarketstackQuotesSymbol, data);
    }

    if (saved) {
        ++m_marketstackQuotesBatchSuccessCount;
        setFundamentalDataStatus(
            QStringLiteral("Marketstack Get Quotes: Quotes fuer %1 gespeichert. OK: %2, Fehler: %3.")
                .arg(m_pendingMarketstackQuotesSymbol)
                .arg(m_marketstackQuotesBatchSuccessCount)
                .arg(m_marketstackQuotesBatchFailureCount),
            true);
    } else {
        const QString noDataSymbol = m_pendingMarketstackQuotesSymbol;
        if (deleteMarketstackNoDataStock(noDataSymbol)) {
            ++m_marketstackQuotesBatchSuccessCount;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack Get Quotes: %1 ohne Quotes - aus DB geloescht. OK: %2, Fehler: %3.")
                    .arg(noDataSymbol)
                    .arg(m_marketstackQuotesBatchSuccessCount)
                    .arg(m_marketstackQuotesBatchFailureCount),
                true);
        } else {
            ++m_marketstackQuotesBatchFailureCount;
            setFundamentalDataStatus(
                QStringLiteral("Marketstack Get Quotes: Quotes fuer %1 konnten nicht gespeichert werden. OK: %2, Fehler: %3.")
                    .arg(noDataSymbol)
                    .arg(m_marketstackQuotesBatchSuccessCount)
                    .arg(m_marketstackQuotesBatchFailureCount),
                true);
        }
    }

    scheduleNextMarketstackQuotesBatchSymbol(MarketstackNextSymbolDelayMs);
}

bool DatabaseManager::saveMarketstackHistoricalQuotes(const QString &symbol, const QJsonArray &bars)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || bars.isEmpty())
        return false;

    if (!db.transaction()) {
        qCritical() << "Marketstack-Quotes konnten keine Transaktion starten:" << db.lastError().text();
        return false;
    }

    QSqlQuery deleteQuery(db);
    deleteQuery.prepare(R"SQL(
        DELETE FROM "Quotes"
        WHERE "Symbol" = :symbol
    )SQL");
    deleteQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!deleteQuery.exec()) {
        qCritical() << "Bestehende Marketstack-Quotes konnten nicht geloescht werden:"
                    << deleteQuery.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(R"SQL(
        INSERT INTO "Quotes" (
            "Symbol", "CloseDate", "ClosePrice", "OpenPrice",
            "HighestPrice", "LowestPrice", "Volume"
        )
        VALUES (
            :symbol, :closeDate, :closePrice, :openPrice,
            :highestPrice, :lowestPrice, :volume
        )
        ON CONFLICT ("Symbol", "CloseDate") DO UPDATE
        SET
            "ClosePrice" = EXCLUDED."ClosePrice",
            "OpenPrice" = EXCLUDED."OpenPrice",
            "HighestPrice" = EXCLUDED."HighestPrice",
            "LowestPrice" = EXCLUDED."LowestPrice",
            "Volume" = EXCLUDED."Volume"
    )SQL");

    int inserted = 0;
    for (const QJsonValue &value : bars) {
        const QJsonObject bar = value.toObject();
        const QString dateText = bar.value(QStringLiteral("date")).toString();
        QDate closeDate = QDate::fromString(dateText.left(10), QStringLiteral("yyyy-MM-dd"));
        if (!closeDate.isValid())
            continue;

        insertQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        insertQuery.bindValue(QStringLiteral(":closeDate"), closeDate);
        insertQuery.bindValue(QStringLiteral(":closePrice"), bar.value(QStringLiteral("close")).toDouble());
        insertQuery.bindValue(QStringLiteral(":openPrice"), bar.value(QStringLiteral("open")).toDouble());
        insertQuery.bindValue(QStringLiteral(":highestPrice"), bar.value(QStringLiteral("high")).toDouble());
        insertQuery.bindValue(QStringLiteral(":lowestPrice"), bar.value(QStringLiteral("low")).toDouble());
        const QJsonValue volumeValue = bar.value(QStringLiteral("volume"));
        insertQuery.bindValue(QStringLiteral(":volume"),
                              volumeValue.isDouble() ? volumeValue.toDouble() : 0.0);
        if (!insertQuery.exec()) {
            qCritical() << "Marketstack-Quote konnte nicht gespeichert werden:"
                        << insertQuery.lastError().text() << normalizedSymbol << closeDate;
            db.rollback();
            return false;
        }
        ++inserted;
    }

    if (inserted == 0) {
        qCritical() << "Marketstack-Quotes enthielten keine gueltigen Tagesdaten:" << normalizedSymbol;
        db.rollback();
        return false;
    }

    const QString marketstackCurrency =
        marketstackCurrencyForMic(m_pendingMarketstackQuotesExchange);

    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"SQL(
        UPDATE "Stocks"
        SET "LastUpdateDate" = CURRENT_DATE,
            "Currency" = CASE
                WHEN NULLIF(:currency, '') IS NULL THEN "Currency"
                ELSE :currency
            END,
            "marketplace_last_error" = NULL
        WHERE "Symbol" = :symbol
    )SQL");
    updateQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    updateQuery.bindValue(QStringLiteral(":currency"), marketstackCurrency);
    if (!updateQuery.exec()) {
        qCritical() << "Stock-Update nach Marketstack-Quotes fehlgeschlagen:"
                    << updateQuery.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Marketstack-Quotes konnten nicht abgeschlossen werden:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

void DatabaseManager::startMarketstackValidationBatch()
{
    if (m_marketstackValidationBatchActive || m_marketstackBatchActive
        || m_marketstackQuotesBatchActive || m_yahooFundamentalsBatchActive)
        return;

    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol"
        FROM "Stocks"
        WHERE COALESCE("use_marketstack", FALSE) = TRUE
          AND COALESCE("marketplace_sym", '') <> ''
        ORDER BY "Symbol"
    )SQL");
    if (!query.exec()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Marketstack Validate konnte die Aktienliste nicht laden: %1")
                .arg(query.lastError().text()),
            false);
        return;
    }

    m_marketstackValidationBatchSymbols.clear();
    while (query.next())
        m_marketstackValidationBatchSymbols << query.value(0).toString();

    if (m_marketstackValidationBatchSymbols.isEmpty()) {
        setFundamentalDataStatus(QStringLiteral("Marketstack Validate: Keine gespeicherten Mappings gefunden."), false);
        return;
    }

    m_marketstackValidationBatchActive = true;
    m_marketstackValidationBatchIndex = 0;
    m_marketstackValidationBatchSuccessCount = 0;
    m_marketstackValidationBatchFailureCount = 0;
    m_marketstackValidationRateLimitRetries = 0;
    setFundamentalDataStatus(
        QStringLiteral("Marketstack Validate gestartet: %1 Mappings werden geprueft.")
            .arg(m_marketstackValidationBatchSymbols.size()),
        true);
    scheduleNextMarketstackValidationBatchSymbol(MarketstackInitialDelayMs);
}

void DatabaseManager::stopMarketstackValidationBatch()
{
    if (!m_marketstackValidationBatchActive)
        return;

    finishMarketstackValidationBatch(
        QStringLiteral("Marketstack Validate gestoppt: %1/%2 verarbeitet, %3 OK, %4 zu pruefen.")
            .arg(marketstackValidationBatchDone())
            .arg(m_marketstackValidationBatchSymbols.size())
            .arg(m_marketstackValidationBatchSuccessCount)
            .arg(m_marketstackValidationBatchFailureCount));
}

void DatabaseManager::loadNextMarketstackValidationBatchSymbol()
{
    if (!m_marketstackValidationBatchActive)
        return;

    if (m_marketstackValidationBatchIndex >= m_marketstackValidationBatchSymbols.size()) {
        finishMarketstackValidationBatch(
            QStringLiteral("Marketstack Validate abgeschlossen: %1 Mappings, %2 OK, %3 zu pruefen.")
                .arg(m_marketstackValidationBatchSymbols.size())
                .arg(m_marketstackValidationBatchSuccessCount)
                .arg(m_marketstackValidationBatchFailureCount));
        return;
    }

    const QString symbol = m_marketstackValidationBatchSymbols.at(m_marketstackValidationBatchIndex++).trimmed();
    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Symbol", "ISIN", "Name", "marketplace_sym", "marketplace_exchange"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec() || !query.next()) {
        ++m_marketstackValidationBatchFailureCount;
        scheduleNextMarketstackValidationBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    m_pendingMarketstackValidationSymbol = query.value(QStringLiteral("Symbol")).toString().trimmed();
    m_pendingMarketstackValidationIsin = query.value(QStringLiteral("ISIN")).toString().trimmed();
    m_pendingMarketstackValidationName = query.value(QStringLiteral("Name")).toString().trimmed();
    m_pendingMarketstackValidationMarketplaceSym =
        query.value(QStringLiteral("marketplace_sym")).toString().trimmed().toUpper();
    m_pendingMarketstackValidationExchange =
        query.value(QStringLiteral("marketplace_exchange")).toString().trimmed().toUpper();
    if (m_pendingMarketstackValidationExchange.isEmpty())
        m_pendingMarketstackValidationExchange =
            m_pendingMarketstackValidationMarketplaceSym.section(QLatin1Char('/'), 1, 1).trimmed().toUpper();

    setFundamentalDataStatus(
        QStringLiteral("Marketstack Validate: %1/%2 %3 pruefen. OK: %4, Zu pruefen: %5")
            .arg(m_marketstackValidationBatchIndex)
            .arg(m_marketstackValidationBatchSymbols.size())
            .arg(m_pendingMarketstackValidationSymbol)
            .arg(m_marketstackValidationBatchSuccessCount)
            .arg(m_marketstackValidationBatchFailureCount),
        true);
    requestMarketstackValidationForPendingSymbol();
}

void DatabaseManager::scheduleNextMarketstackValidationBatchSymbol(int delayMs)
{
    if (!m_marketstackValidationBatchActive)
        return;
    m_marketstackValidationBatchTimer.start(delayMs);
}

void DatabaseManager::finishMarketstackValidationBatch(const QString &message)
{
    m_marketstackValidationBatchTimer.stop();
    m_marketstackValidationBatchActive = false;
    m_marketstackValidationRateLimitRetries = 0;
    m_pendingMarketstackValidationSymbol.clear();
    m_pendingMarketstackValidationIsin.clear();
    m_pendingMarketstackValidationName.clear();
    m_pendingMarketstackValidationMarketplaceSym.clear();
    m_pendingMarketstackValidationExchange.clear();
    setFundamentalDataStatus(message, false);
}

void DatabaseManager::requestMarketstackValidationForPendingSymbol()
{
    if (!m_marketstackValidationBatchActive)
        return;

    const QString requestSymbol =
        m_pendingMarketstackValidationMarketplaceSym.section(QLatin1Char('/'), 0, 0).trimmed().toUpper();
    QString searchTerm = requestSymbol;
    if (searchTerm.contains(QLatin1Char('.')))
        searchTerm = searchTerm.section(QLatin1Char('.'), 0, 0);
    if (searchTerm.isEmpty()) {
        ++m_marketstackValidationBatchFailureCount;
        scheduleNextMarketstackValidationBatchSymbol(MarketstackNextSymbolDelayMs);
        return;
    }

    QUrl url(QStringLiteral("https://marketstack.com/stock_api.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("exchange"), m_pendingMarketstackValidationExchange.trimmed().toUpper());
    query.addQueryItem(QStringLiteral("search"), searchTerm);
    url.setQuery(query);

    QNetworkReply *reply = m_marketstackNetworkManager.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleMarketstackValidationReply(reply);
    });
}

void DatabaseManager::handleMarketstackValidationReply(QNetworkReply *reply)
{
    if (!m_marketstackValidationBatchActive) {
        reply->deleteLater();
        return;
    }

    const QByteArray responseData = reply->readAll();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus == 429) {
        if (++m_marketstackValidationRateLimitRetries <= MarketstackMaxRateLimitRetries) {
            setFundamentalDataStatus(
                QStringLiteral("Marketstack Validate: Rate-Limit erreicht, pausiere kurz und versuche erneut (%1/%2).")
                    .arg(m_marketstackValidationRateLimitRetries)
                    .arg(MarketstackMaxRateLimitRetries),
                true);
            QTimer::singleShot(MarketstackRateLimitDelayMs,
                               this,
                               &DatabaseManager::requestMarketstackValidationForPendingSymbol);
            return;
        }
        finishMarketstackValidationBatch(
            QStringLiteral("Marketstack Validate wegen Marketstack-Rate-Limit gestoppt. Bitte spaeter fortsetzen."));
        return;
    }

    m_marketstackValidationRateLimitRetries = 0;

    QString error;
    if (!networkOk) {
        error = QStringLiteral("Marketstack-Validierung: Netzwerkfehler");
    } else {
        const QString expectedSymbol =
            m_pendingMarketstackValidationMarketplaceSym.section(QLatin1Char('/'), 0, 0).trimmed().toUpper();
        const QString expectedExchange = m_pendingMarketstackValidationExchange.trimmed().toUpper();
        const QJsonDocument document = QJsonDocument::fromJson(responseData);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        QJsonObject selectedTicker;
        for (const QJsonValue &value : data) {
            const QJsonObject ticker = value.toObject();
            const QJsonObject exchange = ticker.value(QStringLiteral("stock_exchange")).toObject();
            const QString tickerSymbol = ticker.value(QStringLiteral("ticker")).toString().trimmed().isEmpty()
                ? ticker.value(QStringLiteral("symbol")).toString().trimmed()
                : ticker.value(QStringLiteral("ticker")).toString().trimmed();
            if (marketstackWebsiteTickerMatches(tickerSymbol, expectedSymbol, expectedExchange)
                && exchange.value(QStringLiteral("mic")).toString().trimmed().compare(expectedExchange, Qt::CaseInsensitive) == 0) {
                selectedTicker = ticker;
                break;
            }
        }

        if (selectedTicker.isEmpty()) {
            error = QStringLiteral("Marketstack-Validierung: Symbol/MIC nicht gefunden");
        } else {
            const QJsonObject exchange = selectedTicker.value(QStringLiteral("stock_exchange")).toObject();
            const QString candidateName = selectedTicker.value(QStringLiteral("name")).toString();
            const QString candidateCountryCode = exchange.value(QStringLiteral("country_code")).toString();
            if (!marketstackNameMatchesStock(m_pendingMarketstackValidationName, candidateName)) {
                error = QStringLiteral("Marketstack-Validierung: Name passt nicht (%1)")
                            .arg(candidateName);
            } else if (!marketstackCountryMatchesStock(m_pendingMarketstackValidationIsin,
                                                       candidateCountryCode,
                                                       expectedExchange)) {
                error = QStringLiteral("Marketstack-Validierung: Land passt nicht (%1)")
                            .arg(candidateCountryCode);
            }
        }
    }

    if (error.isEmpty()) {
        ++m_marketstackValidationBatchSuccessCount;
        setFundamentalDataStatus(
            QStringLiteral("Marketstack Validate: %1 OK. OK: %2, Zu pruefen: %3")
                .arg(m_pendingMarketstackValidationSymbol)
                .arg(m_marketstackValidationBatchSuccessCount)
                .arg(m_marketstackValidationBatchFailureCount),
            true);
    } else {
        resetMarketstackMapping(m_pendingMarketstackValidationSymbol, error);
        ++m_marketstackValidationBatchFailureCount;
        setFundamentalDataStatus(
            QStringLiteral("Marketstack Validate: %1 zu pruefen (%2). OK: %3, Zu pruefen: %4")
                .arg(m_pendingMarketstackValidationSymbol,
                     error)
                .arg(m_marketstackValidationBatchSuccessCount)
                .arg(m_marketstackValidationBatchFailureCount),
            true);
    }

    scheduleNextMarketstackValidationBatchSymbol(MarketstackLookupDelayMs);
}

bool DatabaseManager::resetMarketstackMapping(const QString &symbol, const QString &error)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty())
        return false;

    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"SQL(
        UPDATE "Stocks"
        SET "marketplace_last_error" = LEFT(:error, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    updateQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    updateQuery.bindValue(QStringLiteral(":error"), error.trimmed());
    if (!updateQuery.exec())
        return false;

    emit ibkrStockDataUpdated(normalizedSymbol);
    return true;
}

bool DatabaseManager::startIbkrQuoteExchangeProbeForSymbol(const QString &symbol)
{
    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "Symbol", "IBKRConId", "IBKRResolvedSymbol", "LocalSymbol",
               "Currency", "CountryCode", "PrimaryExchange", "MIC", "ISIN",
               "ValidExchanges", "IBKRQuoteExchange", "IBKRBestDirectExchange"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
        LIMIT 1
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    if (!stockQuery.exec() || !stockQuery.next()) {
        updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Stock nicht in der Datenbank gefunden"));
        setIbkrConnectionState(
            QStringLiteral("IBKR Get Quotes: %1 nicht in der Datenbank gefunden.").arg(symbol),
            m_ibkrConnected,
            false);
        return false;
    }

    const qint64 conId = stockQuery.value(QStringLiteral("IBKRConId")).toLongLong();
    if (conId <= 0) {
        updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Keine gueltige IBKRConId"));
        setIbkrConnectionState(
            QStringLiteral("IBKR Get Quotes: %1 hat keine gueltige IBKRConId.").arg(symbol),
            m_ibkrConnected,
            false);
        return false;
    }

    QString ibkrSymbol = stockQuery.value(QStringLiteral("IBKRResolvedSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = stockQuery.value(QStringLiteral("LocalSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();

    QString currency = stockQuery.value(QStringLiteral("Currency")).toString().trimmed();
    if (currency.isEmpty())
        currency = currencyForCountry(stockQuery.value(QStringLiteral("CountryCode")).toString());
    const QString cachedQuoteExchange =
        stockQuery.value(QStringLiteral("IBKRQuoteExchange")).toString().trimmed().toUpper();
    const QString cachedPrimaryExchange =
        stockQuery.value(QStringLiteral("IBKRBestDirectExchange")).toString().trimmed().toUpper();
    const QString validExchanges = stockQuery.value(QStringLiteral("ValidExchanges")).toString();
    const bool supportsSmart = ibkrValidExchangesContainSmart(validExchanges);
    const QStringList probeExchanges = ibkrQuoteExchangeCandidates(
        validExchanges,
        stockQuery.value(QStringLiteral("PrimaryExchange")).toString(),
        stockQuery.value(QStringLiteral("MIC")).toString());

    if (!cachedQuoteExchange.isEmpty()) {
        m_ibkrSocket.abort();
        m_ibkrPendingSymbol = symbol.trimmed();
        m_pendingIbkrQuotesSymbol = symbol.trimmed();
        m_pendingIbkrQuotesIsin = stockQuery.value(QStringLiteral("ISIN")).toString().trimmed().toUpper();
        m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
        m_pendingIbkrQuotesCurrency = currency;
        m_pendingIbkrQuotesExchange = cachedQuoteExchange;
        m_pendingIbkrQuotesPrimaryExchange = cachedPrimaryExchange;
        if (m_pendingIbkrQuotesExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) == 0
            && !supportsSmart
            && !cachedPrimaryExchange.isEmpty()) {
            m_pendingIbkrQuotesExchange = cachedPrimaryExchange;
            m_pendingIbkrQuotesPrimaryExchange.clear();
            saveIbkrQuoteExchange(symbol.trimmed(), cachedPrimaryExchange, 0.0, cachedPrimaryExchange, 0.0);
        }
        m_pendingIbkrQuotesProbeExchanges = ibkrQuoteFallbackExchanges(cachedPrimaryExchange, probeExchanges);
        m_pendingIbkrQuotesConId = conId;
        m_pendingIbkrQuotesDays = 90;
        m_pendingIbkrQuotesFallbackIndex = 0;
        m_pendingIbkrQuotesSupportsSmart = supportsSmart;
        m_pendingIbkrQuotesForceDirectProbeResult = false;
        m_pendingIbkrProcessIsHistoricalQuotes = true;
        m_pendingIbkrProcessIsQuoteExchangeProbe = false;
        m_pendingIbkrProcessIsNameSearch = false;
        m_pendingIbkrProcessIsNameCheck = false;
        m_ibkrDataLoading = true;
        startIbkrQuoteHelperRequest(false);
        return true;
    }

    updateIbkrQuoteExchangeAttempt(symbol.trimmed());
    if (probeExchanges.isEmpty()) {
        updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Keine pruefbaren Boersen-Kandidaten"));
        setIbkrConnectionState(
            QStringLiteral("IBKR Get Quotes: %1 hat keine pruefbaren Boersen-Kandidaten.").arg(symbol),
            m_ibkrConnected,
            false);
        return false;
    }

    m_ibkrSocket.abort();
    m_ibkrPendingSymbol = symbol.trimmed();
    m_pendingIbkrQuotesSymbol = symbol.trimmed();
    m_pendingIbkrQuotesIsin = stockQuery.value(QStringLiteral("ISIN")).toString().trimmed().toUpper();
    m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
    m_pendingIbkrQuotesCurrency = currency;
    m_pendingIbkrQuotesExchange = QStringLiteral("SMART");
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesProbeExchanges = probeExchanges;
    m_pendingIbkrQuotesConId = conId;
    m_pendingIbkrQuotesDays = 90;
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = supportsSmart;
    m_pendingIbkrQuotesForceDirectProbeResult = false;

    if (probeExchanges.size() == 1) {
        const QString onlyExchange = probeExchanges.first().trimmed().toUpper();
        const QString quoteExchange = supportsSmart ? QStringLiteral("SMART") : onlyExchange;
        if (!saveIbkrQuoteExchange(symbol.trimmed(), quoteExchange, 0.0, onlyExchange, 0.0)) {
            updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("Quote-Boerse konnte nicht gespeichert werden"));
            setIbkrConnectionState(
                QStringLiteral("IBKR Get Quotes: Quote-Boerse fuer %1 konnte nicht gespeichert werden.")
                    .arg(symbol),
                m_ibkrConnected,
                false);
            return false;
        }
        updateIbkrQuoteExchangeSuccess(symbol.trimmed());
        m_pendingIbkrQuotesExchange = quoteExchange;
        m_pendingIbkrQuotesPrimaryExchange = supportsSmart ? onlyExchange : QString();
        if (m_ibkrGetStocksBatchActive) {
            setIbkrConnectionState(
                QStringLiteral("IBKR Get Quotes: %1 -> %2, beste Direktboerse %3. Quotes fuer 90 Tage werden geladen ... OK: %4, Fehler: %5.")
                    .arg(symbol, quoteExchange, onlyExchange)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            m_pendingIbkrProcessIsHistoricalQuotes = true;
            m_pendingIbkrProcessIsQuoteExchangeProbe = false;
            startIbkrQuoteHelperRequest(false);
        }
        return true;
    }

    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrProcessIsQuoteExchangeProbe = true;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrProcessIsNameCheck = false;
    m_ibkrDataLoading = true;
    startIbkrQuoteHelperRequest(true);
    return true;
}

void DatabaseManager::startIbkrQuotesRequestForIsin(const QString &isin, int days)
{
    if (m_ibkrDataLoading || m_ibkrBatchActive || m_ibkrNameCheckBatchActive)
        return;

    if (!m_ibkrConnected || m_ibkrConnectedPort == 0) {
        setIbkrConnectionState(QStringLiteral("Fehler: Zuerst eine Verbindung zu IBKR herstellen."), false, false);
        return;
    }
    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "Symbol", "IBKRConId", "IBKRResolvedSymbol", "LocalSymbol",
               "Currency", "CountryCode", "PrimaryExchange", "MIC", "ISIN",
               "ValidExchanges", "IBKRQuoteExchange", "IBKRBestDirectExchange"
        FROM "Stocks"
        WHERE "ISIN" = :isin
        ORDER BY "Symbol"
        LIMIT 1
    )SQL");
    stockQuery.bindValue(QStringLiteral(":isin"), isin.trimmed().toUpper());
    if (!stockQuery.exec() || !stockQuery.next()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Keine Aktie mit ISIN %1 gefunden.").arg(isin),
            m_ibkrConnected,
            false);
        return;
    }

    const QString symbol = stockQuery.value(QStringLiteral("Symbol")).toString().trimmed();
    const qint64 conId = stockQuery.value(QStringLiteral("IBKRConId")).toLongLong();
    QString ibkrSymbol = stockQuery.value(QStringLiteral("IBKRResolvedSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = stockQuery.value(QStringLiteral("LocalSymbol")).toString().trimmed();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();

    QString currency = stockQuery.value(QStringLiteral("Currency")).toString().trimmed();
    if (currency.isEmpty())
        currency = currencyForCountry(stockQuery.value(QStringLiteral("CountryCode")).toString());

    const QString cachedQuoteExchange =
        stockQuery.value(QStringLiteral("IBKRQuoteExchange")).toString().trimmed().toUpper();
    const QString cachedPrimaryExchange =
        stockQuery.value(QStringLiteral("IBKRBestDirectExchange")).toString().trimmed().toUpper();
    const QString validExchanges = stockQuery.value(QStringLiteral("ValidExchanges")).toString();
    const bool supportsSmart = ibkrValidExchangesContainSmart(validExchanges);
    const QStringList probeExchanges = ibkrQuoteExchangeCandidates(
        validExchanges,
        stockQuery.value(QStringLiteral("PrimaryExchange")).toString(),
        stockQuery.value(QStringLiteral("MIC")).toString());

    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        setIbkrConnectionState(QStringLiteral("Fehler: Der IBKR-Helfer fehlt im Build-Verzeichnis."), m_ibkrConnected, false);
        return;
    }

    m_ibkrSocket.abort();
    m_ibkrPendingSymbol = symbol;
    m_pendingIbkrQuotesSymbol = symbol;
    m_pendingIbkrQuotesIsin = isin.trimmed().toUpper();
    m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
    m_pendingIbkrQuotesCurrency = currency;
    m_pendingIbkrQuotesExchange = cachedQuoteExchange.isEmpty() ? QStringLiteral("SMART") : cachedQuoteExchange;
    m_pendingIbkrQuotesPrimaryExchange = cachedPrimaryExchange;
    if (m_pendingIbkrQuotesExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) == 0
        && !supportsSmart
        && !cachedPrimaryExchange.isEmpty()) {
        m_pendingIbkrQuotesExchange = cachedPrimaryExchange;
        m_pendingIbkrQuotesPrimaryExchange.clear();
        saveIbkrQuoteExchange(symbol, cachedPrimaryExchange, 0.0, cachedPrimaryExchange, 0.0);
    }
    m_pendingIbkrQuotesProbeExchanges = ibkrQuoteFallbackExchanges(cachedPrimaryExchange, probeExchanges);
    m_pendingIbkrQuotesConId = conId;
    m_pendingIbkrQuotesDays = qMax(1, days);
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = supportsSmart;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_pendingIbkrProcessIsHistoricalQuotes = true;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrProcessIsNameCheck = false;
    m_ibkrDataLoading = true;

    if (cachedQuoteExchange.isEmpty() && !probeExchanges.isEmpty()) {
        m_pendingIbkrProcessIsHistoricalQuotes = false;
        m_pendingIbkrProcessIsQuoteExchangeProbe = true;
        startIbkrQuoteHelperRequest(true);
    } else {
        startIbkrQuoteHelperRequest(false);
    }
}

void DatabaseManager::startIbkrQuoteHelperRequest(bool probeExchange)
{
    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        setIbkrConnectionState(QStringLiteral("Fehler: Der IBKR-Helfer fehlt im Build-Verzeichnis."), m_ibkrConnected, false);
        return;
    }

    QStringList arguments = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
        QStringLiteral("--client-id"), probeExchange ? QStringLiteral("25") : QStringLiteral("24"),
        QStringLiteral("--symbol"), m_pendingIbkrQuotesIbkrSymbol,
        probeExchange ? QStringLiteral("--probe-quote-exchanges") : QStringLiteral("--historical-quotes"),
        QStringLiteral("--days"), QString::number(probeExchange ? 20 : m_pendingIbkrQuotesDays)
    };
    if (m_pendingIbkrQuotesConId > 0)
        arguments << QStringLiteral("--con-id") << QString::number(m_pendingIbkrQuotesConId);
    if (!m_pendingIbkrQuotesCurrency.isEmpty())
        arguments << QStringLiteral("--currency") << m_pendingIbkrQuotesCurrency;
    if (probeExchange) {
        arguments << QStringLiteral("--exchanges")
                  << m_pendingIbkrQuotesProbeExchanges.join(QLatin1Char(','));
    } else if (!m_pendingIbkrQuotesExchange.isEmpty()
               && m_pendingIbkrQuotesExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0) {
        arguments << QStringLiteral("--exchange") << m_pendingIbkrQuotesExchange
                  << QStringLiteral("--direct-exchange");
    }
    if (!probeExchange && !m_pendingIbkrQuotesPrimaryExchange.isEmpty())
        arguments << QStringLiteral("--primary-exchange") << m_pendingIbkrQuotesPrimaryExchange;
    if (m_pendingIbkrQuotesConId <= 0 && !m_pendingIbkrQuotesIsin.isEmpty())
        arguments << QStringLiteral("--isin") << m_pendingIbkrQuotesIsin;

    m_ibkrProcess.setProgram(helperPath);
    m_ibkrProcess.setArguments(arguments);
    m_ibkrProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrDataLoading = true;
    setIbkrConnectionState(
        probeExchange
            ? QStringLiteral("IBKR Get Quotes: staerkste Umsatzboerse fuer %1 wird ermittelt ...")
                  .arg(m_pendingIbkrQuotesSymbol)
            : QStringLiteral("IBKR Get Quotes: Quotes fuer %1 (%2) werden fuer %3 Tage von %4 abgerufen ...")
                  .arg(m_pendingIbkrQuotesSymbol, m_pendingIbkrQuotesIsin)
                  .arg(m_pendingIbkrQuotesDays)
                  .arg(m_pendingIbkrQuotesExchange),
        true,
        false);
    m_ibkrProcess.start();
    m_ibkrDataTimeout.setInterval(probeExchange ? 120000 : 90000);
    m_ibkrDataTimeout.start();
    emit ibkrConnectionChanged();
}

void DatabaseManager::startIbkrNameCheckBatch()
{
    if (m_ibkrNameCheckBatchActive || m_ibkrBatchActive || m_ibkrDataLoading)
        return;

    if (m_yahooFundamentalsBatchActive)
        stopYahooFundamentalsBatch();

    if (!m_ibkrConnected || m_ibkrConnectedPort == 0) {
        setIbkrConnectionState(QStringLiteral("Fehler: Zuerst eine Verbindung zu IBKR herstellen."), false, false);
        return;
    }
    if (!db.isOpen()) {
        setIbkrConnectionState(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), m_ibkrConnected, false);
        return;
    }

    m_ibkrNameCheckBatchSymbols.clear();
    const QStringList listSymbols = ibkrNameCheckIsinOverrides().keys();
    for (const QString &symbol : listSymbols) {
        const QString listIsin = ibkrNameCheckIsinOverride(symbol);
        QSqlQuery query(db);
        if (listIsin.isEmpty()) {
            query.prepare(R"SQL(
                SELECT 1
                FROM "Stocks"
                WHERE "Symbol" = :symbol
                LIMIT 1
            )SQL");
        } else {
            query.prepare(R"SQL(
                SELECT 1
                WHERE NOT EXISTS (
                    SELECT 1
                    FROM "Stocks"
                    WHERE "Symbol" = :symbol
                      AND "IBKRConId" IS NOT NULL
                )
                LIMIT 1
            )SQL");
        }
        query.bindValue(QStringLiteral(":symbol"), symbol);
        if (!query.exec()) {
            setIbkrConnectionState(
                QStringLiteral("Fehler: IBKR-Namenspruefung konnte %1 nicht pruefen: %2")
                    .arg(symbol, query.lastError().text()),
                m_ibkrConnected,
                false);
            return;
        }
        if (query.next())
            m_ibkrNameCheckBatchSymbols << symbol;
    }
    m_ibkrNameCheckBatchSymbols.sort(Qt::CaseInsensitive);

    if (m_ibkrNameCheckBatchSymbols.isEmpty()) {
        setIbkrConnectionState(QStringLiteral("IBKR-Namenspruefung: Keine offenen Datensaetze aus der ISIN-Liste gefunden."), m_ibkrConnected, false);
        return;
    }

    m_ibkrNameCheckBatchActive = true;
    m_ibkrNameCheckBatchIndex = 0;
    m_ibkrNameCheckBatchSuccessCount = 0;
    m_ibkrNameCheckBatchFailureCount = 0;
    setIbkrConnectionState(
        QStringLiteral("IBKR-Namenspruefung gestartet: %1 Datensaetze werden geprueft.")
            .arg(m_ibkrNameCheckBatchSymbols.size()),
        m_ibkrConnected,
        false);
    scheduleNextIbkrNameCheckBatchSymbol(100);
}

void DatabaseManager::stopIbkrNameCheckBatch()
{
    if (!m_ibkrNameCheckBatchActive)
        return;

    m_ibkrNameCheckBatchTimer.stop();
    if (m_ibkrDataLoading && m_pendingIbkrProcessIsNameCheck
        && m_ibkrProcess.state() != QProcess::NotRunning) {
        m_ibkrProcess.kill();
    }

    finishIbkrNameCheckBatch(
        QStringLiteral("IBKR-Namenspruefung gestoppt: %1/%2 verarbeitet, %3 OK, %4 Fehler/Pruefen.")
            .arg(ibkrNameCheckBatchDone())
            .arg(m_ibkrNameCheckBatchSymbols.size())
            .arg(m_ibkrNameCheckBatchSuccessCount)
            .arg(m_ibkrNameCheckBatchFailureCount));
}

void DatabaseManager::connectToIbkr()
{
    if (m_ibkrSocket.state() == QAbstractSocket::ConnectedState) {
        setIbkrConnectionState(
            QStringLiteral("IBKR TWS/IB Gateway ist auf 127.0.0.1:%1 erreichbar.")
                .arg(m_ibkrSocket.peerPort()),
            true,
            false);
        return;
    }

    m_ibkrConnectTimeout.stop();
    m_ibkrSocket.abort();
    m_ibkrPortIndex = 0;
    setIbkrConnectionState(QStringLiteral("IBKR-Verbindung wird geprüft ..."), false, true);
    tryNextIbkrPort();
}

void DatabaseManager::tryNextIbkrPort()
{
    if (!m_ibkrConnecting)
        return;

    if (m_ibkrPortIndex >= m_ibkrPorts.size()) {
        setIbkrConnectionState(
            QStringLiteral("Keine IBKR-API erreichbar. TWS oder IB Gateway starten und Socket Clients aktivieren."),
            false,
            false);
        return;
    }

    const quint16 port = m_ibkrPorts.at(m_ibkrPortIndex);
    m_ibkrSocket.connectToHost(QHostAddress::LocalHost, port);
    m_ibkrConnectTimeout.start();
}

void DatabaseManager::getIbkrData(const QString &symbol)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || m_ibkrDataLoading)
        return;

    if (!m_ibkrConnected || m_ibkrConnectedPort == 0) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Zuerst eine Verbindung zu IBKR herstellen."),
            false,
            false);
        return;
    }

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "ISIN", "Currency", "CountryCode", "MIC", "PrimaryExchange",
               "IBKRResolvedSymbol", "YahooSymbol", "Name"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!stockQuery.exec() || !stockQuery.next()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Die ausgewählte Aktie wurde nicht in der Datenbank gefunden."),
            m_ibkrConnected,
            false);
        return;
    }

    QString currency = stockQuery.value(QStringLiteral("Currency")).toString().trimmed();
    if (currency.isEmpty())
        currency = currencyForCountry(stockQuery.value(QStringLiteral("CountryCode")).toString());

    m_ibkrPendingSymbol = normalizedSymbol;
    m_pendingIbkrCurrency = currency;
    m_pendingIbkrExchange = stockQuery.value(QStringLiteral("PrimaryExchange")).toString().trimmed().isEmpty()
        ? stockQuery.value(QStringLiteral("MIC")).toString().trimmed()
        : stockQuery.value(QStringLiteral("PrimaryExchange")).toString().trimmed();
    m_pendingIbkrIsin = stockQuery.value(QStringLiteral("ISIN")).toString().trimmed();
    m_pendingIbkrNameSearchTerms = ibkrSymbolSearchKeywordVariants(
        stockQuery.value(QStringLiteral("Name")).toString());
    m_pendingIbkrNameSearchIndex = 0;
    m_pendingIbkrSearchKeywords = m_pendingIbkrNameSearchTerms.isEmpty()
        ? QString()
        : m_pendingIbkrNameSearchTerms.first();
    m_pendingIbkrLastError.clear();
    m_pendingIbkrCandidateSymbols.clear();
    m_pendingIbkrCandidateCurrencies.clear();
    m_pendingIbkrCandidateExchanges.clear();
    m_pendingIbkrCurrentCandidateSymbol.clear();
    m_pendingIbkrAmbiguousIsinCandidates.clear();
    m_pendingIbkrTriedAmbiguousIsins.clear();
    m_pendingIbkrCandidateIndex = 0;
    m_pendingIbkrSearchStarted = false;
    m_pendingIbkrNameSearchStarted = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrReviewRequired = false;
    m_pendingIbkrReviewReason.clear();
    m_pendingIbkrTryWithoutIsin = false;
    m_pendingIbkrDirectExchange = false;
    m_pendingIbkrDirectExchanges = ibkrDirectExchanges(m_pendingIbkrExchange);
    m_pendingIbkrDirectExchangeIndex = 0;
    m_pendingIbkrCurrentDirectExchange.clear();
    m_pendingIbkrDirectExchange = false;

    appendIbkrSymbolVariants(m_pendingIbkrCandidateSymbols,
                             stockQuery.value(QStringLiteral("IBKRResolvedSymbol")).toString());
    appendIbkrSymbolVariants(m_pendingIbkrCandidateSymbols,
                             stockQuery.value(QStringLiteral("YahooSymbol")).toString());
    appendIbkrSymbolVariants(m_pendingIbkrCandidateSymbols, normalizedSymbol);
    if (m_ibkrBatchActive) {
        QSqlQuery attemptQuery(db);
        attemptQuery.prepare(R"SQL(
            UPDATE "Stocks"
            SET "IBKRLastAttemptAt" = CURRENT_TIMESTAMP
            WHERE "Symbol" = :symbol
        )SQL");
        attemptQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        if (!attemptQuery.exec())
            qWarning() << "IBKR-Batch-Versuch konnte nicht protokolliert werden:"
                       << attemptQuery.lastError().text() << normalizedSymbol;
    }
    m_ibkrSocket.abort();
    setIbkrConnectionState(
        QStringLiteral("IBKR-Daten für %1 werden abgerufen ...").arg(normalizedSymbol),
        true,
        false);

    tryNextIbkrCandidate();
}

void DatabaseManager::startIbkrHelperRequest(const QString &candidateSymbol)
{
    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        finalizeIbkrDataFailure(QStringLiteral("Der IBKR-Helfer fehlt im Build-Verzeichnis."));
        return;
    }

    m_ibkrDataLoading = true;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrCurrentCandidateSymbol = candidateSymbol.trimmed();
    QStringList arguments = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
        QStringLiteral("--client-id"), QStringLiteral("23"),
        QStringLiteral("--symbol"), candidateSymbol
    };
    const QString candidateKey = ibkrCandidateKey(candidateSymbol);
    const QString hintedCurrency =
        m_pendingIbkrCandidateCurrencies.value(candidateKey).trimmed();
    const QString hintedExchange =
        m_pendingIbkrCandidateExchanges.value(candidateKey).trimmed();
    const QString currency = hintedCurrency.isEmpty()
        ? m_pendingIbkrCurrency
        : hintedCurrency;
    if (!currency.isEmpty())
        arguments << QStringLiteral("--currency") << currency;
    const bool useHintedDirectExchange =
        !hintedExchange.isEmpty() && m_pendingIbkrDataForNameCheckRecovery;
    const QString exchange = m_pendingIbkrDirectExchange
        ? m_pendingIbkrCurrentDirectExchange
        : (hintedExchange.isEmpty() ? m_pendingIbkrExchange : hintedExchange);
    if (!exchange.isEmpty())
        arguments << QStringLiteral("--exchange") << exchange;
    if ((m_pendingIbkrDirectExchange || useHintedDirectExchange) && !exchange.isEmpty())
        arguments << QStringLiteral("--direct-exchange");
    if (!m_pendingIbkrTryWithoutIsin && !m_pendingIbkrIsin.isEmpty())
        arguments << QStringLiteral("--isin") << m_pendingIbkrIsin;

    m_ibkrProcess.setProgram(helperPath);
    m_ibkrProcess.setArguments(arguments);
    m_ibkrProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrProcess.start();
    m_ibkrDataTimeout.start();
    emit ibkrConnectionChanged();
}

bool DatabaseManager::tryNextIbkrAmbiguousIsin()
{
    while (!m_pendingIbkrAmbiguousIsinCandidates.isEmpty()) {
        const QString isin = m_pendingIbkrAmbiguousIsinCandidates.takeFirst().trimmed().toUpper();
        if (isin.isEmpty())
            continue;
        if (m_pendingIbkrCurrentCandidateSymbol.trimmed().isEmpty())
            return false;

        m_pendingIbkrIsin = isin;
        m_pendingIbkrTriedAmbiguousIsins << isin;
        m_pendingIbkrTryWithoutIsin = false;
        setIbkrConnectionState(
            QStringLiteral("IBKR: %1 wird mit freier ISIN %2 aus mehrdeutigem Treffer geprueft ...")
                .arg(m_ibkrPendingSymbol, isin),
            true,
            false);
        startIbkrHelperRequest(m_pendingIbkrCurrentCandidateSymbol);
        return true;
    }
    return false;
}

void DatabaseManager::startIbkrNameSearchRequest()
{
    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        finalizeIbkrDataFailure(QStringLiteral("Der IBKR-Helfer fehlt im Build-Verzeichnis."));
        return;
    }

    m_ibkrDataLoading = true;
    m_pendingIbkrProcessIsNameSearch = true;
    QStringList arguments = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
        QStringLiteral("--client-id"), QStringLiteral("23"),
        QStringLiteral("--symbol"), m_pendingIbkrSearchKeywords,
        QStringLiteral("--match-symbols")
    };

    m_ibkrProcess.setProgram(helperPath);
    m_ibkrProcess.setArguments(arguments);
    m_ibkrProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrProcess.start();
    m_ibkrDataTimeout.start();
    emit ibkrConnectionChanged();
}

bool DatabaseManager::tryNextIbkrCandidate(const QString &lastError)
{
    if (!lastError.trimmed().isEmpty())
        m_pendingIbkrLastError = lastError.trimmed();

    while (m_pendingIbkrCandidateIndex < m_pendingIbkrCandidateSymbols.size()) {
        const QString candidate = m_pendingIbkrCandidateSymbols.at(m_pendingIbkrCandidateIndex++).trimmed();
        if (candidate.isEmpty())
            continue;

        setIbkrConnectionState(
            QStringLiteral("IBKR-Daten fuer %1 werden mit Symbol %2%3%4 abgerufen ...")
                .arg(m_ibkrPendingSymbol,
                     candidate,
                     m_pendingIbkrTryWithoutIsin ? QStringLiteral(" ohne ISIN") : QString(),
                     m_pendingIbkrDirectExchange ? QStringLiteral(" direkt an FWB") : QString()),
            true,
            false);
        startIbkrHelperRequest(candidate);
        return true;
    }

    if (m_pendingIbkrIsin.isEmpty()
        && m_pendingIbkrNameSearchIndex < m_pendingIbkrNameSearchTerms.size()) {
        m_pendingIbkrSearchKeywords =
            m_pendingIbkrNameSearchTerms.at(m_pendingIbkrNameSearchIndex++).trimmed();
        m_pendingIbkrNameSearchStarted = true;
        setIbkrConnectionState(
            QStringLiteral("IBKR-Namenssuche fuer %1 mit \"%2\" laeuft ...")
                .arg(m_ibkrPendingSymbol, m_pendingIbkrSearchKeywords),
            true,
            false);
        startIbkrNameSearchRequest();
        return true;
    }

    if (!m_pendingIbkrSearchStarted && !m_pendingIbkrSearchKeywords.isEmpty()) {
        m_pendingIbkrSearchStarted = true;
        setIbkrConnectionState(
            QStringLiteral("IBKR-Symbolsuche fuer %1 ueber Yahoo mit \"%2\" laeuft ...")
                .arg(m_ibkrPendingSymbol, m_pendingIbkrSearchKeywords),
            true,
            false);
        yahooFinanceClient.resolveSymbol(m_ibkrPendingSymbol, m_pendingIbkrSearchKeywords);
        return true;
    }

    if (m_pendingIbkrDirectExchangeIndex < m_pendingIbkrDirectExchanges.size()) {
        m_pendingIbkrDirectExchange = true;
        m_pendingIbkrCurrentDirectExchange =
            m_pendingIbkrDirectExchanges.at(m_pendingIbkrDirectExchangeIndex++);
        m_pendingIbkrCandidateIndex = 0;
        setIbkrConnectionState(
            QStringLiteral("IBKR: %1 wird direkt an %2 getestet.")
                .arg(m_ibkrPendingSymbol, m_pendingIbkrCurrentDirectExchange),
            true,
            false);
        return tryNextIbkrCandidate();
    }

    if (!m_pendingIbkrTryWithoutIsin && !m_pendingIbkrIsin.isEmpty()) {
        m_pendingIbkrTryWithoutIsin = true;
        m_pendingIbkrDirectExchange = false;
        m_pendingIbkrDirectExchangeIndex = 0;
        m_pendingIbkrCurrentDirectExchange.clear();
        m_pendingIbkrCandidateIndex = 0;
        setIbkrConnectionState(
            QStringLiteral("IBKR: %1 wird erneut ohne ISIN getestet, weil die ISIN keinen Treffer geliefert hat.")
                .arg(m_ibkrPendingSymbol),
            true,
            false);
        return tryNextIbkrCandidate();
    }

    return false;
}

void DatabaseManager::finalizeIbkrDataFailure(const QString &message)
{
    const QString requestedSymbol = m_ibkrPendingSymbol;
    const QString finalMessage = message.trimmed().isEmpty()
        ? (m_pendingIbkrLastError.isEmpty()
               ? QStringLiteral("kein passender IBKR-Kontrakt gefunden")
               : m_pendingIbkrLastError)
        : message.trimmed();

    m_ibkrDataLoading = false;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrCurrency.clear();
    m_pendingIbkrExchange.clear();
    m_pendingIbkrIsin.clear();
    m_pendingIbkrSearchKeywords.clear();
    m_pendingIbkrNameSearchTerms.clear();
    m_pendingIbkrNameSearchIndex = 0;
    m_pendingIbkrLastError.clear();
    m_pendingIbkrCandidateSymbols.clear();
    m_pendingIbkrCandidateCurrencies.clear();
    m_pendingIbkrCandidateExchanges.clear();
    m_pendingIbkrCurrentCandidateSymbol.clear();
    m_pendingIbkrAmbiguousIsinCandidates.clear();
    m_pendingIbkrTriedAmbiguousIsins.clear();
    m_pendingIbkrCandidateIndex = 0;
    m_pendingIbkrSearchStarted = false;
    m_pendingIbkrNameSearchStarted = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrReviewRequired = false;
    m_pendingIbkrReviewReason.clear();
    m_pendingIbkrTryWithoutIsin = false;
    m_pendingIbkrDirectExchange = false;
    m_pendingIbkrDirectExchanges.clear();
    m_pendingIbkrDirectExchangeIndex = 0;
    m_pendingIbkrCurrentDirectExchange.clear();

    if (m_pendingIbkrDataForNameCheckRecovery) {
        m_pendingIbkrDataForNameCheckRecovery = false;
        if (m_ibkrNameCheckBatchActive) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(requestedSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenssuche fehlgeschlagen: %1").arg(finalMessage));
            setIbkrConnectionState(
                QStringLiteral("IBKR-Namenspruefung: %1 fehlgeschlagen (%2). OK: %3, Fehler/Pruefen: %4.")
                    .arg(requestedSymbol)
                    .arg(finalMessage)
                    .arg(m_ibkrNameCheckBatchSuccessCount)
                    .arg(m_ibkrNameCheckBatchFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrNameCheckBatchSymbol(1000);
            return;
        }
    }

    if (m_ibkrBatchActive) {
        ++m_ibkrBatchFailureCount;
        updateIbkrBatchFailure(requestedSymbol, finalMessage);
        setIbkrConnectionState(
            QStringLiteral("IBKR-Batch: %1 fehlgeschlagen (%2). Erfolgreich: %3, Fehler: %4.")
                .arg(requestedSymbol)
                .arg(finalMessage)
                .arg(m_ibkrBatchSuccessCount)
                .arg(m_ibkrBatchFailureCount),
            m_ibkrConnected,
            false);
        scheduleNextIbkrBatchSymbol(1000);
        return;
    }

    setIbkrConnectionState(QStringLiteral("Fehler: %1").arg(finalMessage), m_ibkrConnected, false);
}

void DatabaseManager::finishIbkrQuotesRequest(const QJsonObject &result)
{
    const QString symbol = m_pendingIbkrQuotesSymbol;
    const QString isin = m_pendingIbkrQuotesIsin;
    const QString message = result.value(QStringLiteral("message")).toString();
    const bool getStocksBatchActive = m_ibkrGetStocksBatchActive;
    const QString ibkrSymbol = m_pendingIbkrQuotesIbkrSymbol;
    const QString currency = m_pendingIbkrQuotesCurrency;
    const QString quoteExchange = m_pendingIbkrQuotesExchange;
    const QStringList fallbackExchanges = m_pendingIbkrQuotesProbeExchanges;
    const qint64 conId = m_pendingIbkrQuotesConId;
    const int days = m_pendingIbkrQuotesDays;
    const bool supportsSmart = m_pendingIbkrQuotesSupportsSmart;
    const bool forceDirectProbeResult = m_pendingIbkrQuotesForceDirectProbeResult;

    m_pendingIbkrProcessIsHistoricalQuotes = false;
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrQuotesSymbol.clear();
    m_pendingIbkrQuotesIsin.clear();
    m_pendingIbkrQuotesIbkrSymbol.clear();
    m_pendingIbkrQuotesCurrency.clear();
    m_pendingIbkrQuotesExchange.clear();
    m_pendingIbkrQuotesPrimaryExchange.clear();
    m_pendingIbkrQuotesProbeExchanges.clear();
    m_pendingIbkrQuotesConId = 0;
    m_pendingIbkrQuotesDays = 0;
    m_pendingIbkrQuotesFallbackIndex = 0;
    m_pendingIbkrQuotesSupportsSmart = false;
    m_pendingIbkrQuotesForceDirectProbeResult = false;
    m_ibkrDataTimeout.setInterval(25000);

    if (!result.value(QStringLiteral("success")).toBool()) {
        if (!forceDirectProbeResult && !fallbackExchanges.isEmpty()) {
            m_ibkrPendingSymbol = symbol;
            m_pendingIbkrQuotesSymbol = symbol;
            m_pendingIbkrQuotesIsin = isin;
            m_pendingIbkrQuotesIbkrSymbol = ibkrSymbol;
            m_pendingIbkrQuotesCurrency = currency;
            m_pendingIbkrQuotesExchange.clear();
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrQuotesProbeExchanges = fallbackExchanges;
            m_pendingIbkrQuotesConId = conId;
            m_pendingIbkrQuotesDays = days <= 0 ? 90 : days;
            m_pendingIbkrQuotesFallbackIndex = 0;
            m_pendingIbkrQuotesSupportsSmart = supportsSmart;
            m_pendingIbkrQuotesForceDirectProbeResult = true;
            m_pendingIbkrProcessIsHistoricalQuotes = false;
            m_pendingIbkrProcessIsQuoteExchangeProbe = true;
            m_ibkrDataLoading = true;
            setIbkrConnectionState(
                QStringLiteral("IBKR Get Quotes: %1 %2 fehlgeschlagen, pruefe direkte Boersen nach Umsatz ...")
                    .arg(symbol, quoteExchange.isEmpty() ? QStringLiteral("Quote-Abruf") : quoteExchange),
                m_ibkrConnected,
                false);
            startIbkrQuoteHelperRequest(true);
            return;
        }
        if (getStocksBatchActive) {
            const QString finalError = message.trimmed().isEmpty()
                ? QStringLiteral("IBKR lieferte keine Quotes")
                : message;
            const bool useMarketstack = shouldUseMarketstackForIbkrQuoteError(finalError);
            if (useMarketstack) {
                ++m_ibkrGetStocksSuccessCount;
                markStockUseMarketstack(symbol, true);
            } else {
                ++m_ibkrGetStocksFailureCount;
                updateIbkrQuoteExchangeFailure(
                    symbol,
                    finalError);
            }
            setIbkrConnectionState(
                QStringLiteral("IBKR Get Quotes: %1 Quote-Abruf %2 (%3). OK: %4, Fehler: %5.")
                    .arg(symbol,
                         useMarketstack ? QStringLiteral("an Marketstack uebergeben")
                                        : QStringLiteral("fehlgeschlagen"),
                         finalError)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrGetStocksSymbol(1000);
            emit ibkrConnectionChanged();
            return;
        }
        setIbkrConnectionState(
            QStringLiteral("Fehler: %1").arg(message.trimmed().isEmpty()
                                                ? QStringLiteral("IBKR lieferte keine Quotes.")
                                                : message),
            m_ibkrConnected,
            false);
        emit ibkrConnectionChanged();
        return;
    }

    const QJsonArray bars = result.value(QStringLiteral("data")).toArray();
    if (!saveIbkrHistoricalQuotes(symbol, bars)) {
        if (getStocksBatchActive) {
            ++m_ibkrGetStocksFailureCount;
            updateIbkrQuoteExchangeFailure(symbol, QStringLiteral("IBKR-Quotes konnten nicht gespeichert werden"));
            setIbkrConnectionState(
                QStringLiteral("IBKR Get Quotes: Quotes fuer %1 konnten nicht gespeichert werden. OK: %2, Fehler: %3.")
                    .arg(symbol)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrGetStocksSymbol(1000);
            emit ibkrConnectionChanged();
            return;
        }
        setIbkrConnectionState(
            QStringLiteral("Fehler: IBKR-Quotes fuer %1 konnten nicht gespeichert werden.").arg(symbol),
            m_ibkrConnected,
            false);
        emit ibkrConnectionChanged();
        return;
    }

    updateIbkrQuoteExchangeSuccess(symbol);

    setIbkrConnectionState(
        QStringLiteral("IBKR Get Quotes: %1 Quotes fuer %2 (%3) gespeichert; vorhandene Quotes wurden ersetzt.")
            .arg(bars.size())
            .arg(symbol, isin),
        m_ibkrConnected,
        false);
    emit saveComplete(symbol);
    emit ibkrStockDataUpdated(symbol);
    if (getStocksBatchActive) {
        ++m_ibkrGetStocksSuccessCount;
        setIbkrConnectionState(
            QStringLiteral("IBKR Get Quotes: %1 Quotes fuer %2 gespeichert. OK: %3, Fehler: %4.")
                .arg(symbol)
                .arg(bars.size())
                .arg(m_ibkrGetStocksSuccessCount)
                .arg(m_ibkrGetStocksFailureCount),
            m_ibkrConnected,
            false);
        scheduleNextIbkrGetStocksSymbol(1000);
    }
    emit ibkrConnectionChanged();
}

void DatabaseManager::finishIbkrQuoteExchangeProbe(const QJsonObject &result)
{
    const QString message = result.value(QStringLiteral("message")).toString();
    const bool forceDirectProbeResult = m_pendingIbkrQuotesForceDirectProbeResult;
    if (!result.value(QStringLiteral("success")).toBool()) {
        m_pendingIbkrProcessIsQuoteExchangeProbe = false;
        m_ibkrDataTimeout.setInterval(25000);
        const QString fallbackExchange = m_pendingIbkrQuotesProbeExchanges.isEmpty()
            ? QString()
            : m_pendingIbkrQuotesProbeExchanges.first().trimmed().toUpper();
        const QString quoteExchange = m_pendingIbkrQuotesSupportsSmart
            ? QStringLiteral("SMART")
            : fallbackExchange;
        if (!forceDirectProbeResult
            && !fallbackExchange.isEmpty()
            && saveIbkrQuoteExchange(m_pendingIbkrQuotesSymbol,
                                     quoteExchange,
                                     0.0,
                                     fallbackExchange,
                                     0.0)) {
            updateIbkrQuoteExchangeSuccess(m_pendingIbkrQuotesSymbol);
            if (m_ibkrGetStocksBatchActive) {
                m_pendingIbkrQuotesExchange = quoteExchange;
                m_pendingIbkrQuotesPrimaryExchange = m_pendingIbkrQuotesSupportsSmart
                    ? fallbackExchange
                    : QString();
                m_pendingIbkrQuotesDays = 90;
                m_pendingIbkrProcessIsHistoricalQuotes = true;
                setIbkrConnectionState(
                    QStringLiteral("IBKR Get Quotes: %1 -> %2, beste Direktboerse %3 als Fallback. Quotes fuer 90 Tage werden geladen ... OK: %4, Fehler: %5.")
                        .arg(m_pendingIbkrQuotesSymbol, quoteExchange, fallbackExchange)
                        .arg(m_ibkrGetStocksSuccessCount)
                        .arg(m_ibkrGetStocksFailureCount),
                    m_ibkrConnected,
                    false);
                startIbkrQuoteHelperRequest(false);
                return;
            }
            m_pendingIbkrQuotesExchange = quoteExchange;
            m_pendingIbkrQuotesPrimaryExchange = m_pendingIbkrQuotesSupportsSmart
                ? fallbackExchange
                : QString();
            m_pendingIbkrProcessIsHistoricalQuotes = true;
            startIbkrQuoteHelperRequest(false);
            return;
        }
        const QString finalError = message.trimmed().isEmpty()
            ? QStringLiteral("Keine Umsatzboerse ermittelt")
            : message;
        const bool useMarketstack = shouldUseMarketstackForIbkrQuoteError(finalError);
        if (useMarketstack)
            markStockUseMarketstack(m_pendingIbkrQuotesSymbol, true);
        else
            updateIbkrQuoteExchangeFailure(m_pendingIbkrQuotesSymbol, finalError);
        if (m_ibkrGetStocksBatchActive) {
            if (useMarketstack)
                ++m_ibkrGetStocksSuccessCount;
            else
                ++m_ibkrGetStocksFailureCount;
            setIbkrConnectionState(
                QStringLiteral("IBKR Get Quotes: %1 %2 (%3). OK: %4, Fehler: %5.")
                    .arg(m_pendingIbkrQuotesSymbol,
                         useMarketstack ? QStringLiteral("an Marketstack uebergeben")
                                        : QStringLiteral("fehlgeschlagen"),
                         finalError)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            m_ibkrPendingSymbol.clear();
            m_pendingIbkrQuotesSymbol.clear();
            m_pendingIbkrQuotesIsin.clear();
            m_pendingIbkrQuotesIbkrSymbol.clear();
            m_pendingIbkrQuotesCurrency.clear();
            m_pendingIbkrQuotesExchange.clear();
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrQuotesProbeExchanges.clear();
            m_pendingIbkrQuotesConId = 0;
            m_pendingIbkrQuotesDays = 0;
            m_pendingIbkrQuotesFallbackIndex = 0;
            m_pendingIbkrQuotesSupportsSmart = false;
            m_pendingIbkrQuotesForceDirectProbeResult = false;
            scheduleNextIbkrGetStocksSymbol(1000);
            emit ibkrConnectionChanged();
            return;
        }
        setIbkrConnectionState(
            QStringLiteral("Fehler: %1").arg(message.trimmed().isEmpty()
                                                ? QStringLiteral("IBKR konnte keine Umsatzboerse ermitteln.")
                                                : message),
            m_ibkrConnected,
            false);
        emit ibkrConnectionChanged();
        return;
    }

    const QJsonObject data = result.value(QStringLiteral("data")).toObject();
    const QString exchange = data.value(QStringLiteral("exchange")).toString().trimmed().toUpper();
    const double turnover = data.value(QStringLiteral("turnover")).toDouble();
    const QString quoteExchange = (!forceDirectProbeResult && m_pendingIbkrQuotesSupportsSmart)
        ? QStringLiteral("SMART")
        : exchange;
    if (exchange.isEmpty()
        || !saveIbkrQuoteExchange(m_pendingIbkrQuotesSymbol,
                                  quoteExchange,
                                  turnover,
                                  exchange,
                                  turnover)) {
        m_pendingIbkrProcessIsQuoteExchangeProbe = false;
        m_ibkrDataTimeout.setInterval(25000);
        updateIbkrQuoteExchangeFailure(
            m_pendingIbkrQuotesSymbol,
            exchange.isEmpty()
                ? QStringLiteral("IBKR lieferte keine Quote-Boerse")
                : QStringLiteral("Quote-Boerse konnte nicht gespeichert werden"));
        if (m_ibkrGetStocksBatchActive) {
            ++m_ibkrGetStocksFailureCount;
            setIbkrConnectionState(
                QStringLiteral("IBKR Get Quotes: Quote-Boerse fuer %1 konnte nicht gespeichert werden. OK: %2, Fehler: %3.")
                    .arg(m_pendingIbkrQuotesSymbol)
                    .arg(m_ibkrGetStocksSuccessCount)
                    .arg(m_ibkrGetStocksFailureCount),
                m_ibkrConnected,
                false);
            scheduleNextIbkrGetStocksSymbol(1000);
            emit ibkrConnectionChanged();
            return;
        }
        setIbkrConnectionState(
            QStringLiteral("Fehler: Ermittelte IBKR-Quote-Boerse konnte nicht gespeichert werden."),
            m_ibkrConnected,
            false);
        emit ibkrConnectionChanged();
        return;
    }

    m_pendingIbkrQuotesExchange = quoteExchange;
    m_pendingIbkrQuotesPrimaryExchange = (!forceDirectProbeResult && m_pendingIbkrQuotesSupportsSmart)
        ? exchange
        : QString();
    updateIbkrQuoteExchangeSuccess(m_pendingIbkrQuotesSymbol);
    m_pendingIbkrProcessIsQuoteExchangeProbe = false;
    if (m_ibkrGetStocksBatchActive) {
        m_pendingIbkrQuotesDays = 90;
        m_pendingIbkrProcessIsHistoricalQuotes = true;
        setIbkrConnectionState(
            QStringLiteral("IBKR Get Quotes: %1 -> %2, beste Direktboerse %3 gespeichert. Quotes fuer 90 Tage werden geladen ... OK: %4, Fehler: %5.")
                .arg(m_pendingIbkrQuotesSymbol, quoteExchange, exchange)
                .arg(m_ibkrGetStocksSuccessCount)
                .arg(m_ibkrGetStocksFailureCount),
            m_ibkrConnected,
            false);
        startIbkrQuoteHelperRequest(false);
        return;
    }
    m_pendingIbkrProcessIsHistoricalQuotes = true;
    startIbkrQuoteHelperRequest(false);
}

bool DatabaseManager::saveIbkrHistoricalQuotes(const QString &symbol, const QJsonArray &bars)
{
    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty() || bars.isEmpty())
        return false;

    if (!db.transaction()) {
        qCritical() << "IBKR-Quotes konnten keine Transaktion starten:" << db.lastError().text();
        return false;
    }

    QSqlQuery deleteQuery(db);
    deleteQuery.prepare(R"SQL(
        DELETE FROM "Quotes"
        WHERE "Symbol" = :symbol
    )SQL");
    deleteQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!deleteQuery.exec()) {
        qCritical() << "Bestehende Quotes konnten nicht geloescht werden:"
                    << deleteQuery.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(R"SQL(
        INSERT INTO "Quotes" (
            "Symbol", "CloseDate", "ClosePrice", "OpenPrice",
            "HighestPrice", "LowestPrice", "Volume"
        )
        VALUES (
            :symbol, :closeDate, :closePrice, :openPrice,
            :highestPrice, :lowestPrice, :volume
        )
        ON CONFLICT ("Symbol", "CloseDate") DO UPDATE
        SET
            "ClosePrice" = EXCLUDED."ClosePrice",
            "OpenPrice" = EXCLUDED."OpenPrice",
            "HighestPrice" = EXCLUDED."HighestPrice",
            "LowestPrice" = EXCLUDED."LowestPrice",
            "Volume" = EXCLUDED."Volume"
    )SQL");

    int inserted = 0;
    for (const QJsonValue &value : bars) {
        const QJsonObject bar = value.toObject();
        const QDate closeDate = QDate::fromString(
            bar.value(QStringLiteral("date")).toString(),
            QStringLiteral("yyyy-MM-dd"));
        if (!closeDate.isValid())
            continue;

        insertQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        insertQuery.bindValue(QStringLiteral(":closeDate"), closeDate);
        insertQuery.bindValue(QStringLiteral(":closePrice"), bar.value(QStringLiteral("close")).toDouble());
        insertQuery.bindValue(QStringLiteral(":openPrice"), bar.value(QStringLiteral("open")).toDouble());
        insertQuery.bindValue(QStringLiteral(":highestPrice"), bar.value(QStringLiteral("high")).toDouble());
        insertQuery.bindValue(QStringLiteral(":lowestPrice"), bar.value(QStringLiteral("low")).toDouble());
        insertQuery.bindValue(QStringLiteral(":volume"), bar.value(QStringLiteral("volume")).toDouble());
        if (!insertQuery.exec()) {
            qCritical() << "IBKR-Quote konnte nicht gespeichert werden:"
                        << insertQuery.lastError().text() << normalizedSymbol << closeDate;
            db.rollback();
            return false;
        }
        ++inserted;
    }

    if (inserted == 0) {
        qCritical() << "IBKR-Quotes enthielten keine gueltigen Tagesdaten:" << normalizedSymbol;
        db.rollback();
        return false;
    }

    QSqlQuery updateQuery(db);
    updateQuery.prepare(R"SQL(
        UPDATE "Stocks"
        SET "LastUpdateDate" = CURRENT_DATE
        WHERE "Symbol" = :symbol
    )SQL");
    updateQuery.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    if (!updateQuery.exec()) {
        qCritical() << "Stock-Update nach IBKR-Quotes fehlgeschlagen:"
                    << updateQuery.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "IBKR-Quotes konnten nicht abgeschlossen werden:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool DatabaseManager::saveIbkrQuoteExchange(const QString &symbol,
                                            const QString &quoteExchange,
                                            double turnover,
                                            const QString &bestDirectExchange,
                                            double bestDirectTurnover)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRQuoteExchange" = :quoteExchange,
            "IBKRQuoteExchangeTurnover" = :turnover,
            "IBKRQuoteExchangeCheckedAt" = CURRENT_TIMESTAMP,
            "IBKRBestDirectExchange" = COALESCE(NULLIF(:bestDirectExchange, ''), "IBKRBestDirectExchange"),
            "IBKRBestDirectExchangeTurnover" = CASE
                WHEN NULLIF(:bestDirectExchange, '') IS NULL THEN "IBKRBestDirectExchangeTurnover"
                ELSE :bestDirectTurnover
            END,
            "IBKRBestDirectExchangeCheckedAt" = CASE
                WHEN NULLIF(:bestDirectExchange, '') IS NULL THEN "IBKRBestDirectExchangeCheckedAt"
                ELSE CURRENT_TIMESTAMP
            END,
            "IBKRQuoteExchangeLastSuccessAt" = CURRENT_TIMESTAMP,
            "IBKRQuoteExchangeFailureCount" = 0,
            "IBKRQuoteExchangeLastError" = NULL,
            "use_marketstack" = FALSE
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    query.bindValue(QStringLiteral(":quoteExchange"), quoteExchange.trimmed().toUpper());
    query.bindValue(QStringLiteral(":turnover"), turnover);
    query.bindValue(QStringLiteral(":bestDirectExchange"), bestDirectExchange.trimmed().toUpper());
    query.bindValue(QStringLiteral(":bestDirectTurnover"), bestDirectTurnover);
    if (!query.exec()) {
        qCritical() << "IBKR-Quote-Boerse konnte nicht gespeichert werden:"
                    << query.lastError().text() << symbol << quoteExchange << bestDirectExchange;
        return false;
    }
    return query.numRowsAffected() > 0;
}

void DatabaseManager::updateIbkrQuoteExchangeAttempt(const QString &symbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRQuoteExchangeLastAttemptAt" = CURRENT_TIMESTAMP
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    if (!query.exec()) {
        qWarning() << "IBKR-Quote-Boersen-Versuch konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::updateIbkrQuoteExchangeFailure(const QString &symbol, const QString &error)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRQuoteExchangeLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRQuoteExchangeFailureCount" = COALESCE("IBKRQuoteExchangeFailureCount", 0) + 1,
            "IBKRQuoteExchangeLastError" = LEFT(:error, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    query.bindValue(QStringLiteral(":error"), error.trimmed());
    if (!query.exec()) {
        qWarning() << "IBKR-Quote-Boersen-Fehler konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol << error;
    }
}

void DatabaseManager::updateIbkrQuoteExchangeSuccess(const QString &symbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRQuoteExchangeLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRQuoteExchangeLastSuccessAt" = CURRENT_TIMESTAMP,
            "IBKRQuoteExchangeFailureCount" = 0,
            "IBKRQuoteExchangeLastError" = NULL,
            "use_marketstack" = FALSE
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    if (!query.exec()) {
        qWarning() << "IBKR-Quote-Boersen-Erfolg konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::markStockUseMarketstack(const QString &symbol, bool useMarketstack)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "use_marketstack" = :useMarketstack,
            "IBKRQuoteExchangeFailureCount" = CASE
                WHEN :useMarketstack THEN 0
                ELSE "IBKRQuoteExchangeFailureCount"
            END,
            "IBKRQuoteExchangeLastError" = CASE
                WHEN :useMarketstack THEN NULL
                ELSE "IBKRQuoteExchangeLastError"
            END
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol.trimmed());
    query.bindValue(QStringLiteral(":useMarketstack"), useMarketstack);
    if (!query.exec()) {
        qWarning() << "Marketstack-Flag konnte nicht aktualisiert werden:"
                   << query.lastError().text() << symbol << useMarketstack;
    }
}

void DatabaseManager::finishIbkrDataRequest(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    m_ibkrDataTimeout.stop();
    if (!m_ibkrDataLoading)
        return;

    m_ibkrDataLoading = false;
    const QString stderrText = QString::fromUtf8(m_ibkrProcess.readAllStandardError()).trimmed();
    if (!stderrText.isEmpty())
        qDebug().noquote() << "IBKR-Helfer:" << stderrText;

    const QByteArray output = m_ibkrProcess.readAllStandardOutput().trimmed();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (exitStatus != QProcess::NormalExit
        || parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Ungültige Antwort vom IBKR-Helfer."),
            m_ibkrConnected,
            false);
        if (m_pendingIbkrProcessIsHistoricalQuotes || m_pendingIbkrProcessIsQuoteExchangeProbe) {
            m_pendingIbkrProcessIsHistoricalQuotes = false;
            m_pendingIbkrProcessIsQuoteExchangeProbe = false;
            updateIbkrQuoteExchangeFailure(m_ibkrPendingSymbol, QStringLiteral("Ungueltige Helper-Antwort"));
            if (m_ibkrGetStocksBatchActive) {
                ++m_ibkrGetStocksFailureCount;
                setIbkrConnectionState(
                    QStringLiteral("IBKR Get Quotes: %1 ungueltige Helper-Antwort. OK: %2, Fehler: %3.")
                        .arg(m_ibkrPendingSymbol)
                        .arg(m_ibkrGetStocksSuccessCount)
                        .arg(m_ibkrGetStocksFailureCount),
                    m_ibkrConnected,
                    false);
            }
            m_pendingIbkrQuotesSymbol.clear();
            m_pendingIbkrQuotesIsin.clear();
            m_pendingIbkrQuotesIbkrSymbol.clear();
            m_pendingIbkrQuotesCurrency.clear();
            m_pendingIbkrQuotesExchange.clear();
            m_pendingIbkrQuotesPrimaryExchange.clear();
            m_pendingIbkrQuotesProbeExchanges.clear();
            m_pendingIbkrQuotesConId = 0;
            m_pendingIbkrQuotesDays = 0;
            m_pendingIbkrQuotesFallbackIndex = 0;
            m_pendingIbkrQuotesSupportsSmart = false;
            m_pendingIbkrQuotesForceDirectProbeResult = false;
            m_ibkrPendingSymbol.clear();
            m_ibkrDataTimeout.setInterval(25000);
            if (m_ibkrGetStocksBatchActive)
                scheduleNextIbkrGetStocksSymbol(1000);
            emit ibkrConnectionChanged();
            return;
        }
        if (m_pendingIbkrProcessIsNameCheck) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(m_pendingIbkrNameCheckSymbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung: ungueltige Helper-Antwort"));
            m_pendingIbkrProcessIsNameCheck = false;
            m_pendingIbkrNameCheckSymbol.clear();
            m_pendingIbkrNameCheckName.clear();
            m_pendingIbkrNameCheckIsin.clear();
            m_pendingIbkrNameCheckHasConId = false;
            m_pendingIbkrNameCheckCandidates.clear();
            m_pendingIbkrNameCheckCandidateIndex = 0;
            scheduleNextIbkrNameCheckBatchSymbol(1000);
            return;
        }
        if (m_pendingIbkrProcessIsNameSearch)
            m_pendingIbkrProcessIsNameSearch = false;
        if (tryNextIbkrCandidate(QStringLiteral("ungueltige Helper-Antwort")))
            return;
        finalizeIbkrDataFailure(QStringLiteral("ungueltige Helper-Antwort"));
        return;
    }

    const QJsonObject result = document.object();
    const QString message = result.value(QStringLiteral("message")).toString();
    if (m_pendingIbkrProcessIsHistoricalQuotes) {
        finishIbkrQuotesRequest(result);
        return;
    }
    if (m_pendingIbkrProcessIsQuoteExchangeProbe) {
        finishIbkrQuoteExchangeProbe(result);
        return;
    }
    if (m_pendingIbkrProcessIsNameCheck) {
        finishIbkrNameCheckRequest(result);
        return;
    }

    const QString reason = result.value(QStringLiteral("reason")).toString().trimmed();
    if (reason == QStringLiteral("ambiguous_isin")) {
        const QJsonObject data = result.value(QStringLiteral("data")).toObject();
        QStringList isins;
        for (const QJsonValue &value : data.value(QStringLiteral("isins")).toArray()) {
            const QString isin = value.toString().trimmed();
            if (!isin.isEmpty())
                isins << isin;
        }
        QStringList existingIsins;
        QStringList freeIsins;
        for (const QString &isin : std::as_const(isins)) {
            QSqlQuery isinQuery(db);
            isinQuery.prepare(R"SQL(
                SELECT 1
                FROM "Stocks"
                WHERE "ISIN" = :isin
                  AND "Symbol" <> :symbol
                LIMIT 1
            )SQL");
            isinQuery.bindValue(QStringLiteral(":isin"), isin);
            isinQuery.bindValue(QStringLiteral(":symbol"), m_ibkrPendingSymbol);
            if (!isinQuery.exec()) {
                qWarning() << "IBKR ambiguous_isin Dublettenpruefung fehlgeschlagen:"
                           << isinQuery.lastError().text() << m_ibkrPendingSymbol << isin;
                existingIsins << isin;
                continue;
            }
            if (isinQuery.next())
                existingIsins << isin;
            else if (!m_pendingIbkrAmbiguousIsinCandidates.contains(isin, Qt::CaseInsensitive)
                     && !m_pendingIbkrTriedAmbiguousIsins.contains(isin, Qt::CaseInsensitive))
                freeIsins << isin;
        }

        if (!freeIsins.isEmpty()) {
            m_pendingIbkrAmbiguousIsinCandidates << freeIsins;
            if (tryNextIbkrAmbiguousIsin())
                return;
        }

        const QString validationMessage = isins.isEmpty()
            ? message
            : QStringLiteral("%1 ISINs: %2%3")
                  .arg(message,
                       isins.join(QStringLiteral(", ")),
                       existingIsins.isEmpty()
                           ? QString()
                           : QStringLiteral(" Bereits vorhanden: %1")
                                 .arg(existingIsins.join(QStringLiteral(", "))));
        markIbkrValidationIssue(m_ibkrPendingSymbol,
                                QStringLiteral("ambiguous_isin"),
                                validationMessage);
        finalizeIbkrDataFailure(validationMessage);
        return;
    }

    if (m_pendingIbkrProcessIsNameSearch) {
        m_pendingIbkrProcessIsNameSearch = false;
        if (!result.value(QStringLiteral("success")).toBool()) {
            if (tryNextIbkrCandidate(message))
                return;
            finalizeIbkrDataFailure(message);
            return;
        }

        const QJsonArray samples = result.value(QStringLiteral("data")).toArray();
        QStringList labels;
        QStringList preferredSymbols;
        QStringList fallbackSymbols;
        for (const QJsonValue &value : samples) {
            const QJsonObject sample = value.toObject();
            const QString securityType = sample.value(QStringLiteral("securityType")).toString().trimmed();
            if (!securityType.isEmpty()
                && securityType.compare(QStringLiteral("STK"), Qt::CaseInsensitive) != 0) {
                continue;
            }

            const QString candidateSymbol = sample.value(QStringLiteral("symbol")).toString().trimmed();
            if (candidateSymbol.isEmpty())
                continue;

            const QString primaryExchange =
                sample.value(QStringLiteral("primaryExchange")).toString().trimmed();
            const QString currency = sample.value(QStringLiteral("currency")).toString().trimmed();
            const bool preferredByCurrency = !m_pendingIbkrCurrency.isEmpty()
                && currency.compare(m_pendingIbkrCurrency, Qt::CaseInsensitive) == 0;
            const bool preferredByExchange = !primaryExchange.isEmpty()
                && (primaryExchange.compare(m_pendingIbkrExchange, Qt::CaseInsensitive) == 0
                    || m_pendingIbkrDirectExchanges.contains(primaryExchange, Qt::CaseInsensitive));
            const QString candidateKey = ibkrCandidateKey(candidateSymbol);
            if (!candidateKey.isEmpty()) {
                if (!currency.isEmpty())
                    m_pendingIbkrCandidateCurrencies.insert(candidateKey, currency);
                if (!primaryExchange.isEmpty())
                    m_pendingIbkrCandidateExchanges.insert(candidateKey, primaryExchange);

                static const QRegularExpression helsinkiSeriesSuffix(
                    QStringLiteral("^([A-Z]{2,})(1[HV])$"));
                const QRegularExpressionMatch match = helsinkiSeriesSuffix.match(candidateKey);
                if (match.hasMatch()) {
                    if (!currency.isEmpty())
                        m_pendingIbkrCandidateCurrencies.insert(match.captured(1), currency);
                    if (!primaryExchange.isEmpty())
                        m_pendingIbkrCandidateExchanges.insert(match.captured(1), primaryExchange);
                }
            }
            appendIbkrMatchedSymbolVariants(preferredByCurrency || preferredByExchange
                                                ? preferredSymbols
                                                : fallbackSymbols,
                                            candidateSymbol);
            labels << QStringLiteral("%1/%2/%3").arg(candidateSymbol, primaryExchange, currency);
        }

        int added = 0;
        for (const QString &candidateSymbol : preferredSymbols) {
            const int before = m_pendingIbkrCandidateSymbols.size();
            appendIbkrMatchedSymbolVariants(m_pendingIbkrCandidateSymbols, candidateSymbol);
            if (m_pendingIbkrCandidateSymbols.size() > before)
                ++added;
        }
        for (const QString &candidateSymbol : fallbackSymbols) {
            const int before = m_pendingIbkrCandidateSymbols.size();
            appendIbkrMatchedSymbolVariants(m_pendingIbkrCandidateSymbols, candidateSymbol);
            if (m_pendingIbkrCandidateSymbols.size() > before)
                ++added;
        }

        if (added > 0) {
            if (m_pendingIbkrDataForNameCheckRecovery) {
                m_pendingIbkrCurrency.clear();
                m_pendingIbkrExchange.clear();
                m_pendingIbkrDirectExchange = false;
                m_pendingIbkrDirectExchanges.clear();
                m_pendingIbkrDirectExchangeIndex = 0;
                m_pendingIbkrCurrentDirectExchange.clear();
            }
            m_pendingIbkrReviewRequired = true;
            m_pendingIbkrReviewReason = QStringLiteral(
                                             "IBKR-Namenssuche nach \"%1\": %2 Kandidaten, uebernommenen Treffer bitte pruefen (%3).")
                                             .arg(m_pendingIbkrSearchKeywords)
                                             .arg(added)
                                             .arg(labels.mid(0, 5).join(QStringLiteral(", ")));
            if (tryNextIbkrCandidate())
                return;
        }

        const QString failure = message.trimmed().isEmpty()
            ? QStringLiteral("IBKR-Namenssuche fand keinen brauchbaren Aktienkandidaten.")
            : message;
        if (tryNextIbkrCandidate(failure))
            return;
        finalizeIbkrDataFailure(failure);
        return;
    }

    if (!result.value(QStringLiteral("success")).toBool()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: %1").arg(message),
            m_ibkrConnected,
            false);
        if (tryNextIbkrAmbiguousIsin())
            return;
        if (tryNextIbkrCandidate(message))
            return;
        finalizeIbkrDataFailure(message);
        return;
    }

    QVariantMap details = result.value(QStringLiteral("data")).toObject().toVariantMap();
    const QString returnedIsin = details.value(QStringLiteral("isin")).toString().trimmed().toUpper();
    if (!returnedIsin.isEmpty()) {
        QSqlQuery duplicateQuery(db);
        duplicateQuery.prepare(R"SQL(
            SELECT "Symbol"
            FROM "Stocks"
            WHERE "ISIN" = :isin
              AND "Symbol" <> :symbol
            ORDER BY "Symbol"
            LIMIT 5
        )SQL");
        duplicateQuery.bindValue(QStringLiteral(":isin"), returnedIsin);
        duplicateQuery.bindValue(QStringLiteral(":symbol"), m_ibkrPendingSymbol);
        if (!duplicateQuery.exec()) {
            qWarning() << "IBKR-ISIN-Dublettenpruefung fehlgeschlagen:"
                       << duplicateQuery.lastError().text() << m_ibkrPendingSymbol << returnedIsin;
        } else {
            QStringList duplicateSymbols;
            while (duplicateQuery.next())
                duplicateSymbols << duplicateQuery.value(0).toString();
            if (!duplicateSymbols.isEmpty()) {
                const QString validationMessage =
                    QStringLiteral("IBKR lieferte ISIN %1, diese existiert bereits bei %2. Daten wurden nicht uebernommen.")
                        .arg(returnedIsin, duplicateSymbols.join(QStringLiteral(", ")));
                markIbkrValidationIssue(m_ibkrPendingSymbol,
                                        QStringLiteral("duplicate_isin"),
                                        validationMessage);
                finalizeIbkrDataFailure(validationMessage);
                return;
            }
        }
    }

    if (m_pendingIbkrReviewRequired && !returnedIsin.isEmpty()) {
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("verified_name"));
        details.insert(QStringLiteral("validationMessage"), QString());
    } else if (m_pendingIbkrReviewRequired) {
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("review_required"));
        details.insert(QStringLiteral("validationMessage"), m_pendingIbkrReviewReason);
    } else if (!m_pendingIbkrIsin.isEmpty() && !m_pendingIbkrTryWithoutIsin) {
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("verified_isin"));
        details.insert(QStringLiteral("validationMessage"), QString());
    } else {
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("verified_symbol"));
        details.insert(QStringLiteral("validationMessage"), QString());
    }

    if (!saveIbkrContractDetails(m_ibkrPendingSymbol, details)) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: IBKR-Daten konnten nicht in der Datenbank gespeichert werden."),
            true,
            false);
        finalizeIbkrDataFailure(QStringLiteral("Datenbankfehler"));
        return;
    }

    const QString completedSymbol = m_ibkrPendingSymbol;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrCurrency.clear();
    m_pendingIbkrExchange.clear();
    m_pendingIbkrIsin.clear();
    m_pendingIbkrSearchKeywords.clear();
    m_pendingIbkrNameSearchTerms.clear();
    m_pendingIbkrNameSearchIndex = 0;
    m_pendingIbkrLastError.clear();
    m_pendingIbkrCandidateSymbols.clear();
    m_pendingIbkrCandidateCurrencies.clear();
    m_pendingIbkrCandidateExchanges.clear();
    m_pendingIbkrCurrentCandidateSymbol.clear();
    m_pendingIbkrAmbiguousIsinCandidates.clear();
    m_pendingIbkrTriedAmbiguousIsins.clear();
    m_pendingIbkrCandidateIndex = 0;
    m_pendingIbkrSearchStarted = false;
    m_pendingIbkrNameSearchStarted = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrReviewRequired = false;
    m_pendingIbkrReviewReason.clear();
    m_pendingIbkrTryWithoutIsin = false;
    m_pendingIbkrDirectExchange = false;
    m_pendingIbkrDirectExchanges.clear();
    m_pendingIbkrDirectExchangeIndex = 0;
    m_pendingIbkrCurrentDirectExchange.clear();
    if (m_pendingIbkrDataForNameCheckRecovery) {
        m_pendingIbkrDataForNameCheckRecovery = false;
        if (m_ibkrNameCheckBatchActive) {
            ++m_ibkrNameCheckBatchSuccessCount;
            updateIbkrBatchSuccess(completedSymbol);
            setIbkrConnectionState(
                QStringLiteral("IBKR-Namenspruefung: %1 per Namenssuche uebernommen. OK: %2, Fehler/Pruefen: %3.")
                    .arg(completedSymbol)
                    .arg(m_ibkrNameCheckBatchSuccessCount)
                    .arg(m_ibkrNameCheckBatchFailureCount),
                true,
                false);
            emit ibkrStockDataUpdated(completedSymbol);
            scheduleNextIbkrNameCheckBatchSymbol(1000);
            return;
        }
    }
    if (m_ibkrBatchActive) {
        ++m_ibkrBatchSuccessCount;
        updateIbkrBatchSuccess(completedSymbol);
        setIbkrConnectionState(
            QStringLiteral("IBKR-Batch: %1 gespeichert. Erfolgreich: %2, Fehler: %3.")
                .arg(completedSymbol)
                .arg(m_ibkrBatchSuccessCount)
                .arg(m_ibkrBatchFailureCount),
            true,
            false);
        emit ibkrStockDataUpdated(completedSymbol);
        scheduleNextIbkrBatchSymbol(1000);
        return;
    }

    setIbkrConnectionState(
        QStringLiteral("%1 Datenbank und Anzeige wurden aktualisiert.").arg(message),
        true,
        false);
    emit ibkrStockDataUpdated(completedSymbol);
}

void DatabaseManager::loadNextIbkrBatchSymbol()
{
    if (!m_ibkrBatchActive)
        return;

    if (m_ibkrBatchIndex >= m_ibkrBatchSymbols.size()) {
        finishIbkrBatch(
            QStringLiteral("IBKR-Batch abgeschlossen: %1 Aktien, %2 erfolgreich, %3 fehlgeschlagen.")
                .arg(m_ibkrBatchSymbols.size())
                .arg(m_ibkrBatchSuccessCount)
                .arg(m_ibkrBatchFailureCount));
        return;
    }

    const QString symbol = m_ibkrBatchSymbols.at(m_ibkrBatchIndex++).trimmed();
    if (symbol.isEmpty()) {
        scheduleNextIbkrBatchSymbol(100);
        return;
    }

    setIbkrConnectionState(
        QStringLiteral("IBKR-Batch: %1/%2 %3 wird aktualisiert ... Erfolgreich: %4, Fehler: %5")
            .arg(m_ibkrBatchIndex)
            .arg(m_ibkrBatchSymbols.size())
            .arg(symbol)
            .arg(m_ibkrBatchSuccessCount)
            .arg(m_ibkrBatchFailureCount),
        m_ibkrConnected,
        false);
    getIbkrData(symbol);
}

void DatabaseManager::scheduleNextIbkrBatchSymbol(int delayMs)
{
    if (!m_ibkrBatchActive)
        return;
    m_ibkrBatchTimer.start(delayMs);
}

void DatabaseManager::finishIbkrBatch(const QString &message)
{
    m_ibkrBatchTimer.stop();
    m_ibkrBatchActive = false;
    m_ibkrDataLoading = false;
    m_ibkrPendingSymbol.clear();
    m_pendingIbkrCurrency.clear();
    m_pendingIbkrExchange.clear();
    m_pendingIbkrIsin.clear();
    m_pendingIbkrSearchKeywords.clear();
    m_pendingIbkrNameSearchTerms.clear();
    m_pendingIbkrNameSearchIndex = 0;
    m_pendingIbkrLastError.clear();
    m_pendingIbkrCandidateSymbols.clear();
    m_pendingIbkrCandidateCurrencies.clear();
    m_pendingIbkrCandidateExchanges.clear();
    m_pendingIbkrCurrentCandidateSymbol.clear();
    m_pendingIbkrAmbiguousIsinCandidates.clear();
    m_pendingIbkrTriedAmbiguousIsins.clear();
    m_pendingIbkrCandidateIndex = 0;
    m_pendingIbkrSearchStarted = false;
    m_pendingIbkrNameSearchStarted = false;
    m_pendingIbkrProcessIsNameSearch = false;
    m_pendingIbkrReviewRequired = false;
    m_pendingIbkrReviewReason.clear();
    m_pendingIbkrTryWithoutIsin = false;
    m_pendingIbkrDirectExchange = false;
    m_pendingIbkrDirectExchanges.clear();
    m_pendingIbkrDirectExchangeIndex = 0;
    m_pendingIbkrCurrentDirectExchange.clear();
    setIbkrConnectionState(message, m_ibkrConnected, false);
    emit ibkrConnectionChanged();
}

void DatabaseManager::loadNextIbkrNameCheckBatchSymbol()
{
    if (!m_ibkrNameCheckBatchActive)
        return;

    if (m_ibkrNameCheckBatchIndex >= m_ibkrNameCheckBatchSymbols.size()) {
        finishIbkrNameCheckBatch(
            QStringLiteral("IBKR-Namenspruefung abgeschlossen: %1 Datensaetze, %2 OK, %3 Fehler/Pruefen.")
                .arg(m_ibkrNameCheckBatchSymbols.size())
                .arg(m_ibkrNameCheckBatchSuccessCount)
                .arg(m_ibkrNameCheckBatchFailureCount));
        return;
    }

    const QString symbol = m_ibkrNameCheckBatchSymbols.at(m_ibkrNameCheckBatchIndex++).trimmed();
    if (symbol.isEmpty()) {
        scheduleNextIbkrNameCheckBatchSymbol(50);
        return;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "Name", "ISIN", "IBKRConId", "IBKRResolvedSymbol", "LocalSymbol",
               "Currency", "PrimaryExchange", "CountryCode", "MIC"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec()) {
        ++m_ibkrNameCheckBatchFailureCount;
        scheduleNextIbkrNameCheckBatchSymbol(100);
        return;
    }
    const bool stockExists = query.next();

    m_pendingIbkrNameCheckSymbol = symbol;
    m_pendingIbkrNameCheckName = stockExists
        ? query.value(QStringLiteral("Name")).toString()
        : symbol.section(QLatin1Char('.'), 0, 0).trimmed();
    m_pendingIbkrNameCheckIsin = ibkrNameCheckIsinOverride(symbol);
    m_pendingIbkrNameCheckHasConId = stockExists
        && !query.value(QStringLiteral("IBKRConId")).isNull()
        && query.value(QStringLiteral("IBKRConId")).toLongLong() > 0;
    m_pendingIbkrNameCheckRequestUsesIsin = false;
    if (m_pendingIbkrNameCheckIsin.isEmpty()) {
        ++m_ibkrNameCheckBatchFailureCount;
        if (stockExists && deleteStockWithReferencedData(symbol)) {
            setIbkrConnectionState(
                QStringLiteral("IBKR-Namenspruefung: %1 ohne Listen-ISIN geloescht.")
                    .arg(symbol),
                m_ibkrConnected,
                false);
        }
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    }

    QSqlQuery duplicateQuery(db);
    duplicateQuery.prepare(R"SQL(
        SELECT "Symbol"
        FROM "Stocks"
        WHERE "ISIN" = :isin
          AND "Symbol" <> :symbol
        LIMIT 1
    )SQL");
    duplicateQuery.bindValue(QStringLiteral(":isin"), m_pendingIbkrNameCheckIsin);
    duplicateQuery.bindValue(QStringLiteral(":symbol"), symbol);
    if (!duplicateQuery.exec() || duplicateQuery.next()) {
        ++m_ibkrNameCheckBatchFailureCount;
        const QString duplicateSymbol = duplicateQuery.isActive()
            ? duplicateQuery.value(0).toString()
            : QString();
        if (stockExists && deleteStockWithReferencedData(symbol)) {
            setIbkrConnectionState(
                duplicateSymbol.isEmpty()
                    ? QStringLiteral("IBKR-Namenspruefung: %1 wegen ISIN-Dublettenpruefung geloescht.")
                          .arg(symbol)
                    : QStringLiteral("IBKR-Namenspruefung: %1 geloescht, ISIN %2 existiert bereits bei %3.")
                          .arg(symbol, m_pendingIbkrNameCheckIsin, duplicateSymbol),
                m_ibkrConnected,
                false);
        }
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    }
    QString ibkrSymbol = stockExists
        ? query.value(QStringLiteral("IBKRResolvedSymbol")).toString().trimmed()
        : QString();
    const QString localSymbol = stockExists
        ? query.value(QStringLiteral("LocalSymbol")).toString().trimmed()
        : QString();
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = localSymbol;
    if (ibkrSymbol.isEmpty())
        ibkrSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();
    QString currency = stockExists
        ? query.value(QStringLiteral("Currency")).toString().trimmed()
        : QString();
    if (currency.isEmpty() && stockExists)
        currency = currencyForCountry(query.value(QStringLiteral("CountryCode")).toString());
    if (currency.isEmpty())
        currency = currencyForCountry(m_pendingIbkrNameCheckIsin.left(2));
    if (currency.isEmpty()) {
        const QString suffix = symbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();
        if (suffix == QStringLiteral("XFRA") || suffix == QStringLiteral("FRA")
            || suffix == QStringLiteral("XETR")) {
            currency = QStringLiteral("EUR");
        }
    }
    QString exchange = stockExists
        ? query.value(QStringLiteral("PrimaryExchange")).toString().trimmed()
        : QString();
    if (exchange.isEmpty())
        exchange = stockExists
            ? query.value(QStringLiteral("MIC")).toString().trimmed()
            : symbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();

    setIbkrConnectionState(
        QStringLiteral("IBKR-Namenspruefung: %1/%2 %3 wird geprueft ... OK: %4, Fehler/Pruefen: %5")
            .arg(m_ibkrNameCheckBatchIndex)
            .arg(m_ibkrNameCheckBatchSymbols.size())
            .arg(symbol)
            .arg(m_ibkrNameCheckBatchSuccessCount)
            .arg(m_ibkrNameCheckBatchFailureCount),
        m_ibkrConnected,
        false);
    prepareIbkrNameCheckCandidates(symbol, ibkrSymbol, localSymbol, currency, exchange);
    if (!startNextIbkrNameCheckCandidate()) {
        ++m_ibkrNameCheckBatchFailureCount;
        if (!ibkrNameCheckIsinOverride(symbol).isEmpty()) {
            deleteStockWithReferencedData(symbol);
        } else {
            markIbkrValidationIssue(symbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung: keine pruefbaren Kandidaten erzeugt"));
        }
        scheduleNextIbkrNameCheckBatchSymbol(800);
    }
}

void DatabaseManager::scheduleNextIbkrNameCheckBatchSymbol(int delayMs)
{
    if (!m_ibkrNameCheckBatchActive)
        return;
    m_ibkrNameCheckBatchTimer.start(delayMs);
}

void DatabaseManager::finishIbkrNameCheckBatch(const QString &message)
{
    m_ibkrNameCheckBatchTimer.stop();
    m_ibkrNameCheckBatchActive = false;
    m_ibkrDataLoading = false;
    m_pendingIbkrProcessIsNameCheck = false;
    m_pendingIbkrNameCheckSymbol.clear();
    m_pendingIbkrNameCheckName.clear();
    m_pendingIbkrNameCheckIsin.clear();
    m_pendingIbkrNameCheckHasConId = false;
    m_pendingIbkrNameCheckCandidates.clear();
    m_pendingIbkrNameCheckCandidateIndex = 0;
    setIbkrConnectionState(message, m_ibkrConnected, false);
    emit ibkrConnectionChanged();
}

void DatabaseManager::prepareIbkrNameCheckCandidates(const QString &symbol,
                                                     const QString &ibkrSymbol,
                                                     const QString &localSymbol,
                                                     const QString &currency,
                                                     const QString &exchange)
{
    m_pendingIbkrNameCheckCandidates.clear();
    m_pendingIbkrNameCheckCandidateIndex = 0;

    const QString baseSymbol = symbol.section(QLatin1Char('.'), 0, 0).trimmed();
    const QString preferredSymbol = ibkrSymbol.trimmed().isEmpty() ? baseSymbol : ibkrSymbol.trimmed();
    const QString preferredLocalSymbol = localSymbol.trimmed().isEmpty() ? baseSymbol : localSymbol.trimmed();
    auto appendCandidate = [this](const QString &candidateSymbol,
                                  const QString &candidateCurrency,
                                  const QString &candidateExchange,
                                  bool useIsin,
                                  bool directExchange) {
        const QString cleanSymbol = candidateSymbol.trimmed();
        if (cleanSymbol.isEmpty())
            return;
        const QString entry = QStringList{
            cleanSymbol,
            candidateCurrency.trimmed(),
            candidateExchange.trimmed(),
            useIsin ? QStringLiteral("1") : QStringLiteral("0"),
            directExchange ? QStringLiteral("1") : QStringLiteral("0")
        }.join(QLatin1Char('\t'));
        if (!m_pendingIbkrNameCheckCandidates.contains(entry))
            m_pendingIbkrNameCheckCandidates << entry;
    };

    if (!ibkrNameCheckIsinOverride(symbol).isEmpty()) {
        appendCandidate(preferredSymbol, currency, exchange, true, false);
        appendCandidate(preferredSymbol, currency, QString(), true, false);
        return;
    }

    if (!m_pendingIbkrNameCheckHasConId && !m_pendingIbkrNameCheckIsin.isEmpty())
        appendCandidate(preferredSymbol, QString(), QString(), true, false);
    appendCandidate(preferredSymbol, currency, exchange, false, !exchange.trimmed().isEmpty());
    if (m_pendingIbkrNameCheckHasConId && !m_pendingIbkrNameCheckIsin.isEmpty())
        appendCandidate(preferredSymbol, QString(), QString(), true, false);
    appendCandidate(preferredSymbol, currency, QString(), false, false);

    const QString suffix = symbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();
    if (suffix == QStringLiteral("XFRA") || suffix == QStringLiteral("FRA")
        || suffix == QStringLiteral("XETR")) {
        const QStringList germanExchanges = {
            QStringLiteral("FWB2"), QStringLiteral("FWB"), QStringLiteral("IBIS"),
            QStringLiteral("GETTEX"), QStringLiteral("SWB")
        };
        for (const QString &germanExchange : germanExchanges) {
            appendCandidate(preferredLocalSymbol, currency, germanExchange, false, true);
            if (preferredSymbol.compare(preferredLocalSymbol, Qt::CaseInsensitive) != 0)
                appendCandidate(preferredSymbol, currency, germanExchange, false, true);
        }
        appendCandidate(preferredLocalSymbol, currency, QString(), false, false);
    }
}

bool DatabaseManager::startNextIbkrNameCheckCandidate(const QString &lastError)
{
    if (m_pendingIbkrNameCheckCandidateIndex >= m_pendingIbkrNameCheckCandidates.size())
        return false;

    const QString entry = m_pendingIbkrNameCheckCandidates.at(m_pendingIbkrNameCheckCandidateIndex++);
    const QStringList parts = entry.split(QLatin1Char('\t'));
    if (parts.size() < 5)
        return startNextIbkrNameCheckCandidate(lastError);

    if (!lastError.trimmed().isEmpty()) {
        setIbkrConnectionState(
            QStringLiteral("IBKR-Namenspruefung: %1 Fallback %2/%3 nach: %4")
                .arg(m_pendingIbkrNameCheckSymbol)
                .arg(m_pendingIbkrNameCheckCandidateIndex)
                .arg(m_pendingIbkrNameCheckCandidates.size())
                .arg(lastError.left(120)),
            m_ibkrConnected,
            false);
    }

    startIbkrNameCheckRequest(m_pendingIbkrNameCheckSymbol,
                              parts.value(0),
                              0,
                              parts.value(1),
                              parts.value(2),
                              parts.value(3) == QStringLiteral("1"),
                              parts.value(4) == QStringLiteral("1"));
    return true;
}

void DatabaseManager::startIbkrNameCheckRequest(const QString &requestSymbol,
                                                const QString &ibkrSymbol,
                                                qint64 conId,
                                                const QString &currency,
                                                const QString &exchange,
                                                bool useIsin,
                                                bool directExchange)
{
    Q_UNUSED(conId)
    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        finishIbkrNameCheckBatch(QStringLiteral("Fehler: Der IBKR-Helfer fehlt im Build-Verzeichnis."));
        return;
    }

    m_ibkrDataLoading = true;
    m_pendingIbkrProcessIsNameCheck = true;
    m_pendingIbkrNameCheckRequestUsesIsin = useIsin && !m_pendingIbkrNameCheckIsin.isEmpty();
    const qint64 clientIdSeed = (QDateTime::currentMSecsSinceEpoch() / 1000)
        + m_ibkrNameCheckBatchIndex;
    const int clientId = 60000 + int(clientIdSeed % 5000);
    QStringList arguments = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
        QStringLiteral("--client-id"), QString::number(clientId),
        QStringLiteral("--symbol"), ibkrSymbol.isEmpty() ? requestSymbol : ibkrSymbol
    };
    if (useIsin && !m_pendingIbkrNameCheckIsin.isEmpty())
        arguments << QStringLiteral("--isin") << m_pendingIbkrNameCheckIsin;
    if (m_pendingIbkrNameCheckRequestUsesIsin
        && !ibkrNameCheckIsinOverride(requestSymbol).isEmpty()) {
        arguments << QStringLiteral("--isin-only");
    }
    if (!currency.isEmpty())
        arguments << QStringLiteral("--currency") << currency;
    if (!exchange.isEmpty())
        arguments << QStringLiteral("--exchange") << exchange;
    if (directExchange && !exchange.isEmpty())
        arguments << QStringLiteral("--direct-exchange");

    m_ibkrProcess.setProgram(helperPath);
    m_ibkrProcess.setArguments(arguments);
    m_ibkrProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrProcess.start();
    m_ibkrDataTimeout.start();
    emit ibkrConnectionChanged();
}

void DatabaseManager::finishIbkrNameCheckRequest(const QJsonObject &result)
{
    const QString symbol = m_pendingIbkrNameCheckSymbol;
    const QString databaseName = m_pendingIbkrNameCheckName;
    const QString databaseIsin = m_pendingIbkrNameCheckIsin;
    const bool usesListIsin = m_pendingIbkrNameCheckRequestUsesIsin
        && !ibkrNameCheckIsinOverride(symbol).isEmpty();

    const QString message = result.value(QStringLiteral("message")).toString();
    if (!result.value(QStringLiteral("success")).toBool()) {
        const bool missingContract =
            message.contains(QStringLiteral("Fehler 200"), Qt::CaseInsensitive)
            || message.contains(QStringLiteral("keine Wertpapierdefinition"), Qt::CaseInsensitive);
        if (missingContract && startNextIbkrNameCheckCandidate(message))
            return;

        m_pendingIbkrProcessIsNameCheck = false;
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        m_pendingIbkrNameCheckCandidates.clear();
        m_pendingIbkrNameCheckCandidateIndex = 0;
        ++m_ibkrNameCheckBatchFailureCount;
        if (usesListIsin) {
            deleteStockWithReferencedData(symbol);
        } else {
            markIbkrValidationIssue(symbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung fehlgeschlagen: %1").arg(message));
        }
        scheduleNextIbkrNameCheckBatchSymbol(800);
        return;
    }

    const QJsonObject data = result.value(QStringLiteral("data")).toObject();
    QString ibkrName = data.value(QStringLiteral("longName")).toString().trimmed();
    if (ibkrName.isEmpty())
        ibkrName = data.value(QStringLiteral("marketName")).toString().trimmed();
    if (ibkrName.isEmpty())
        ibkrName = data.value(QStringLiteral("symbol")).toString().trimmed();
    const QString ibkrIsin = data.value(QStringLiteral("isin")).toString().trimmed().toUpper();

    if (usesListIsin && ibkrIsin != databaseIsin) {
        ++m_ibkrNameCheckBatchFailureCount;
        deleteStockWithReferencedData(symbol);
        m_pendingIbkrProcessIsNameCheck = false;
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        m_pendingIbkrNameCheckCandidates.clear();
        m_pendingIbkrNameCheckCandidateIndex = 0;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    }

    QSqlQuery query(db);
    if (!ibkrIsin.isEmpty()) {
        QSqlQuery duplicateQuery(db);
        duplicateQuery.prepare(R"SQL(
            SELECT "Symbol"
            FROM "Stocks"
            WHERE "ISIN" = :isin
              AND "Symbol" <> :symbol
            ORDER BY "Symbol"
            LIMIT 5
        )SQL");
        duplicateQuery.bindValue(QStringLiteral(":isin"), ibkrIsin);
        duplicateQuery.bindValue(QStringLiteral(":symbol"), symbol);
        if (!duplicateQuery.exec()) {
            qWarning() << "IBKR-Namenspruefung ISIN-Dublettenpruefung fehlgeschlagen:"
                       << duplicateQuery.lastError().text() << symbol << ibkrIsin;
        } else {
            QStringList duplicateSymbols;
            while (duplicateQuery.next())
                duplicateSymbols << duplicateQuery.value(0).toString();
            if (!duplicateSymbols.isEmpty()) {
                const QString validationMessage =
                    QStringLiteral("IBKR lieferte ISIN %1, diese existiert bereits bei %2. Daten wurden nicht uebernommen.")
                        .arg(ibkrIsin, duplicateSymbols.join(QStringLiteral(", ")));
                ++m_ibkrNameCheckBatchFailureCount;
                if (usesListIsin) {
                    deleteStockWithReferencedData(symbol);
                } else {
                    markIbkrValidationIssue(symbol,
                                            QStringLiteral("duplicate_isin"),
                                            validationMessage);
                }
                m_pendingIbkrProcessIsNameCheck = false;
                m_pendingIbkrNameCheckSymbol.clear();
                m_pendingIbkrNameCheckName.clear();
                m_pendingIbkrNameCheckIsin.clear();
                m_pendingIbkrNameCheckHasConId = false;
                m_pendingIbkrNameCheckRequestUsesIsin = false;
                m_pendingIbkrNameCheckCandidates.clear();
                m_pendingIbkrNameCheckCandidateIndex = 0;
                scheduleNextIbkrNameCheckBatchSymbol(600);
                return;
            }
        }

        QVariantMap details = data.toVariantMap();
        details.insert(QStringLiteral("name"), ibkrName);
        details.insert(QStringLiteral("validationStatus"),
                       usesListIsin ? QStringLiteral("verified_isin") : QStringLiteral("verified_name"));
        details.insert(QStringLiteral("validationMessage"),
                       usesListIsin
                           ? QStringLiteral("IBKR-Stammdaten per Listen-ISIN %1 uebernommen.")
                                 .arg(databaseIsin)
                           : QString());
        if (!saveIbkrContractDetails(symbol, details)) {
            ++m_ibkrNameCheckBatchFailureCount;
            if (usesListIsin) {
                deleteStockWithReferencedData(symbol);
            } else {
                markIbkrValidationIssue(symbol, QStringLiteral("name_mismatch"),
                                        QStringLiteral("IBKR-Namenspruefung: Datenbankfehler beim Uebernehmen der ISIN"));
            }
        } else {
            ++m_ibkrNameCheckBatchSuccessCount;
            emit ibkrStockDataUpdated(symbol);
        }
        m_pendingIbkrProcessIsNameCheck = false;
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckRequestUsesIsin = false;
        m_pendingIbkrNameCheckCandidates.clear();
        m_pendingIbkrNameCheckCandidateIndex = 0;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    }

    if (!databaseIsin.isEmpty() && !ibkrIsin.isEmpty() && databaseIsin != ibkrIsin) {
        query.prepare(R"SQL(
            UPDATE "Stocks"
            SET "IBKRValidationStatus" = 'name_mismatch',
                "IBKRValidationMessage" = :message,
                "IBKRValidationAt" = CURRENT_TIMESTAMP,
                "IBKRLastError" = LEFT(:message, 500),
                "IBKRFailureCount" = 0
            WHERE "Symbol" = :symbol
        )SQL");
        query.bindValue(QStringLiteral(":message"),
                        QStringLiteral("ISIN-Abweichung: DB=\"%1\", IBKR=\"%2\", DB-Name=\"%3\", IBKR=\"%4\"")
                            .arg(databaseIsin, ibkrIsin, databaseName, ibkrName));
        ++m_ibkrNameCheckBatchFailureCount;
    } else if (!databaseIsin.isEmpty() && databaseIsin == ibkrIsin) {
        QVariantMap details = data.toVariantMap();
        details.insert(QStringLiteral("name"), ibkrName);
        details.insert(QStringLiteral("validationStatus"), QStringLiteral("verified_name"));
        details.insert(QStringLiteral("validationMessage"),
                       databaseName.compare(ibkrName, Qt::CaseInsensitive) == 0
                           ? QStringLiteral("ISIN und IBKR-Stammdaten bestaetigt: %1, Name=\"%2\"")
                                 .arg(databaseIsin, ibkrName)
                           : QStringLiteral("ISIN gleich, IBKR-Stammdaten uebernommen: %1, Name \"%2\" -> \"%3\"")
                                 .arg(databaseIsin, databaseName, ibkrName));
        if (!saveIbkrContractDetails(symbol, details)) {
            ++m_ibkrNameCheckBatchFailureCount;
            markIbkrValidationIssue(symbol, QStringLiteral("name_mismatch"),
                                    QStringLiteral("IBKR-Namenspruefung: Datenbankfehler beim Uebernehmen gleicher ISIN"));
        } else {
            ++m_ibkrNameCheckBatchSuccessCount;
            emit ibkrStockDataUpdated(symbol);
        }
        m_pendingIbkrProcessIsNameCheck = false;
        m_pendingIbkrNameCheckSymbol.clear();
        m_pendingIbkrNameCheckName.clear();
        m_pendingIbkrNameCheckIsin.clear();
        m_pendingIbkrNameCheckHasConId = false;
        m_pendingIbkrNameCheckCandidates.clear();
        m_pendingIbkrNameCheckCandidateIndex = 0;
        scheduleNextIbkrNameCheckBatchSymbol(600);
        return;
    } else if (companyNamesLookCompatible(databaseName, ibkrName)) {
        query.prepare(R"SQL(
            UPDATE "Stocks"
            SET "IBKRValidationStatus" = 'verified_name',
                "IBKRValidationMessage" = :message,
                "IBKRValidationAt" = CURRENT_TIMESTAMP,
                "IBKRLastError" = NULL,
                "IBKRFailureCount" = 0
            WHERE "Symbol" = :symbol
        )SQL");
        const QString okMessage = (!databaseIsin.isEmpty() && databaseIsin == ibkrIsin)
            ? QStringLiteral("ISIN und Name OK: %1, DB-Name=\"%2\", IBKR=\"%3\"")
                  .arg(databaseIsin, databaseName, ibkrName)
            : QStringLiteral("Namenspruefung OK: DB=\"%1\", IBKR=\"%2\"")
                  .arg(databaseName, ibkrName);
        query.bindValue(QStringLiteral(":message"), okMessage);
        ++m_ibkrNameCheckBatchSuccessCount;
    } else {
        query.prepare(R"SQL(
            UPDATE "Stocks"
            SET "IBKRValidationStatus" = 'name_mismatch',
                "IBKRValidationMessage" = :message,
                "IBKRValidationAt" = CURRENT_TIMESTAMP,
                "IBKRLastError" = LEFT(:message, 500),
                "IBKRFailureCount" = 0
            WHERE "Symbol" = :symbol
        )SQL");
        const QString mismatchMessage = (!databaseIsin.isEmpty() && databaseIsin == ibkrIsin)
            ? QStringLiteral("Name trotz gleicher ISIN pruefen: %1, DB=\"%2\", IBKR=\"%3\"")
                  .arg(databaseIsin, databaseName, ibkrName)
            : QStringLiteral("Namensabweichung: DB=\"%1\", IBKR=\"%2\"")
                  .arg(databaseName, ibkrName);
        query.bindValue(QStringLiteral(":message"), mismatchMessage);
        ++m_ibkrNameCheckBatchFailureCount;
    }
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec())
        qWarning() << "IBKR-Namenspruefung konnte nicht gespeichert werden:" << query.lastError().text() << symbol;

    m_pendingIbkrProcessIsNameCheck = false;
    m_pendingIbkrNameCheckSymbol.clear();
    m_pendingIbkrNameCheckName.clear();
    m_pendingIbkrNameCheckIsin.clear();
    m_pendingIbkrNameCheckHasConId = false;
    m_pendingIbkrNameCheckCandidates.clear();
    m_pendingIbkrNameCheckCandidateIndex = 0;
    scheduleNextIbkrNameCheckBatchSymbol(600);
}

bool DatabaseManager::markIbkrValidationIssue(const QString &symbol,
                                              const QString &status,
                                              const QString &message)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRValidationStatus" = :status,
            "IBKRValidationMessage" = :message,
            "IBKRValidationAt" = CURRENT_TIMESTAMP,
            "IBKRLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRLastError" = LEFT(:message, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":status"), status);
    query.bindValue(QStringLiteral(":message"), message);
    if (!query.exec()) {
        qWarning() << "IBKR-Pruefhinweis konnte nicht gespeichert werden:"
                   << query.lastError().text() << symbol;
        return false;
    }
    return true;
}

bool DatabaseManager::deleteStockWithReferencedData(const QString &symbol)
{
    const QString normalizedSymbol = symbol.trimmed().toUpper();
    if (normalizedSymbol.isEmpty())
        return false;

    if (!db.transaction()) {
        qWarning() << "Aktie konnte nicht zum Loeschen gesperrt werden:"
                   << db.lastError().text() << normalizedSymbol;
        return false;
    }

    auto tableExists = [this](const QString &tableName) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT to_regclass(:table_name) IS NOT NULL"));
        query.bindValue(QStringLiteral(":table_name"),
                        QStringLiteral("\"%1\"").arg(tableName));
        if (!query.exec() || !query.next())
            return true;
        return query.value(0).toBool();
    };

    auto deleteFrom = [this, &normalizedSymbol](const QString &sql) {
        QSqlQuery query(db);
        query.prepare(sql);
        query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
        if (!query.exec()) {
            qWarning() << "Referenzdaten konnten nicht geloescht werden:"
                       << query.lastError().text() << normalizedSymbol;
            return false;
        }
        return true;
    };

    struct DeleteStatement {
        QString tableName;
        QString sql;
    };
    const DeleteStatement deleteStatements[] = {
        {QStringLiteral("StockFundamentals"), QStringLiteral(R"SQL(DELETE FROM "StockFundamentals" WHERE "Symbol" = :symbol)SQL")},
        {QStringLiteral("BoughtStocks"), QStringLiteral(R"SQL(DELETE FROM "BoughtStocks" WHERE "Symbol" = :symbol)SQL")},
        {QStringLiteral("Stocks_IBKRConflictBackup"), QStringLiteral(R"SQL(DELETE FROM "Stocks_IBKRConflictBackup" WHERE "Symbol" = :symbol)SQL")},
        {QStringLiteral("Quotes"), QStringLiteral(R"SQL(DELETE FROM "Quotes" WHERE "Symbol" = :symbol)SQL")},
        {QStringLiteral("Stocks"), QStringLiteral(R"SQL(DELETE FROM "Stocks" WHERE "Symbol" = :symbol)SQL")}
    };

    for (const DeleteStatement &statement : deleteStatements) {
        if (!tableExists(statement.tableName))
            continue;
        if (!deleteFrom(statement.sql)) {
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        qWarning() << "Aktie konnte nicht geloescht werden:"
                   << db.lastError().text() << normalizedSymbol;
        db.rollback();
        return false;
    }

    emit ibkrStockDataUpdated(normalizedSymbol);
    return true;
}

bool DatabaseManager::saveIbkrContractDetails(const QString &symbol, const QVariantMap &details)
{
    if (!db.transaction()) {
        qCritical() << "IBKR-Update konnte keine Transaktion starten:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "Stocks" (
            "Symbol", "MIC", "Name", "Exchange", "CountryCode", "LastUpdateDate",
            "ISIN", "IBKRConId", "IBKRResolvedSymbol", "Currency", "PrimaryExchange",
            "LocalSymbol", "SecurityType", "TradingClass", "StockType", "Industry",
            "Category", "Subcategory", "TimeZoneId", "TradingHours", "LiquidHours",
            "MinTick", "MarketRuleIds", "ValidExchanges", "OrderTypes", "MarketName",
            "CUSIP", "IBKRValidationStatus", "IBKRValidationMessage", "IBKRValidationAt",
            "IBKRLastAttemptAt", "IBKRFailureCount", "IBKRLastError", "IBKRLastSyncAt"
        )
        VALUES (
            :symbol, :mic, COALESCE(NULLIF(:name, ''), NULLIF(:ibkrResolvedSymbol, ''), :symbol),
            :exchange, NULLIF(LEFT(:isin, 2), ''),
            CURRENT_DATE, NULLIF(:isin, ''), :ibkrConId, NULLIF(:ibkrResolvedSymbol, ''),
            NULLIF(:currency, ''), NULLIF(:primaryExchange, ''), NULLIF(:localSymbol, ''),
            NULLIF(:securityType, ''), NULLIF(:tradingClass, ''), NULLIF(:stockType, ''),
            NULLIF(:industry, ''), NULLIF(:category, ''), NULLIF(:subcategory, ''),
            NULLIF(:timeZoneId, ''), NULLIF(:tradingHours, ''), NULLIF(:liquidHours, ''),
            :minTick, NULLIF(:marketRuleIds, ''), NULLIF(:validExchanges, ''),
            NULLIF(:orderTypes, ''), NULLIF(:marketName, ''), NULLIF(:cusip, ''),
            NULLIF(:validationStatus, ''), :validationMessage, CURRENT_TIMESTAMP,
            CURRENT_TIMESTAMP, 0, NULL, CURRENT_TIMESTAMP
        )
        ON CONFLICT ("Symbol") DO UPDATE
        SET
            "Name" = COALESCE(NULLIF(EXCLUDED."Name", ''), "Stocks"."Name"),
            "IBKRConId" = EXCLUDED."IBKRConId",
            "IBKRResolvedSymbol" = COALESCE(EXCLUDED."IBKRResolvedSymbol", "Stocks"."IBKRResolvedSymbol"),
            "Currency" = COALESCE(EXCLUDED."Currency", "Stocks"."Currency"),
            "PrimaryExchange" = COALESCE(EXCLUDED."PrimaryExchange", "Stocks"."PrimaryExchange"),
            "LocalSymbol" = COALESCE(EXCLUDED."LocalSymbol", "Stocks"."LocalSymbol"),
            "SecurityType" = COALESCE(EXCLUDED."SecurityType", "Stocks"."SecurityType"),
            "TradingClass" = COALESCE(EXCLUDED."TradingClass", "Stocks"."TradingClass"),
            "StockType" = COALESCE(EXCLUDED."StockType", "Stocks"."StockType"),
            "Industry" = COALESCE(EXCLUDED."Industry", "Stocks"."Industry"),
            "Category" = COALESCE(EXCLUDED."Category", "Stocks"."Category"),
            "Subcategory" = COALESCE(EXCLUDED."Subcategory", "Stocks"."Subcategory"),
            "TimeZoneId" = COALESCE(EXCLUDED."TimeZoneId", "Stocks"."TimeZoneId"),
            "TradingHours" = COALESCE(EXCLUDED."TradingHours", "Stocks"."TradingHours"),
            "LiquidHours" = COALESCE(EXCLUDED."LiquidHours", "Stocks"."LiquidHours"),
            "MinTick" = COALESCE(EXCLUDED."MinTick", "Stocks"."MinTick"),
            "MarketRuleIds" = COALESCE(EXCLUDED."MarketRuleIds", "Stocks"."MarketRuleIds"),
            "ValidExchanges" = COALESCE(EXCLUDED."ValidExchanges", "Stocks"."ValidExchanges"),
            "OrderTypes" = COALESCE(EXCLUDED."OrderTypes", "Stocks"."OrderTypes"),
            "MarketName" = COALESCE(EXCLUDED."MarketName", "Stocks"."MarketName"),
            "CUSIP" = COALESCE(EXCLUDED."CUSIP", "Stocks"."CUSIP"),
            "ISIN" = COALESCE(EXCLUDED."ISIN", "Stocks"."ISIN"),
            "IBKRValidationStatus" = COALESCE(EXCLUDED."IBKRValidationStatus", "Stocks"."IBKRValidationStatus"),
            "IBKRValidationMessage" = EXCLUDED."IBKRValidationMessage",
            "IBKRValidationAt" = CURRENT_TIMESTAMP,
            "IBKRLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRFailureCount" = 0,
            "IBKRLastError" = NULL,
            "IBKRLastSyncAt" = CURRENT_TIMESTAMP
    )SQL");

    const QStringList textFields = {
        QStringLiteral("name"),
        QStringLiteral("currency"), QStringLiteral("primaryExchange"),
        QStringLiteral("localSymbol"), QStringLiteral("securityType"),
        QStringLiteral("tradingClass"), QStringLiteral("stockType"),
        QStringLiteral("industry"), QStringLiteral("category"),
        QStringLiteral("subcategory"), QStringLiteral("timeZoneId"),
        QStringLiteral("tradingHours"), QStringLiteral("liquidHours"),
        QStringLiteral("marketRuleIds"), QStringLiteral("validExchanges"),
        QStringLiteral("orderTypes"), QStringLiteral("marketName"),
        QStringLiteral("cusip"), QStringLiteral("isin"),
        QStringLiteral("validationStatus"), QStringLiteral("validationMessage")
    };
    query.bindValue(QStringLiteral(":symbol"), symbol);
    const QString marketCode = symbol.section(QLatin1Char('.'), 1, 1).trimmed().toUpper();
    const QString fallbackMarket = marketCode.isEmpty() ? QStringLiteral("IBKR") : marketCode;
    query.bindValue(QStringLiteral(":mic"), fallbackMarket);
    query.bindValue(QStringLiteral(":exchange"), fallbackMarket);
    query.bindValue(QStringLiteral(":ibkrConId"), details.value(QStringLiteral("ibkrConId")));
    const QString ibkrResolvedSymbol = details.value(QStringLiteral("symbol")).toString().trimmed();
    query.bindValue(QStringLiteral(":ibkrResolvedSymbol"), ibkrResolvedSymbol);
    for (const QString &field : textFields)
        query.bindValue(QLatin1Char(':') + field, details.value(field).toString());
    const double minTick = details.value(QStringLiteral("minTick")).toDouble();
    query.bindValue(QStringLiteral(":minTick"), minTick > 0.0 ? QVariant(minTick) : QVariant());

    if (!query.exec() || query.numRowsAffected() != 1) {
        qCritical() << "IBKR-Stammdaten konnten nicht gespeichert werden:" << query.lastError().text();
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        qCritical() << "IBKR-Update konnte nicht abgeschlossen werden:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

void DatabaseManager::updateIbkrBatchFailure(const QString &symbol, const QString &error)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRFailureCount" = COALESCE("IBKRFailureCount", 0) + 1,
            "IBKRLastError" = LEFT(:error, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":error"), error);
    if (!query.exec()) {
        qWarning() << "IBKR-Batch-Fehler konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::updateIbkrBatchSuccess(const QString &symbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "IBKRLastAttemptAt" = CURRENT_TIMESTAMP,
            "IBKRFailureCount" = 0,
            "IBKRLastError" = NULL
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec()) {
        qWarning() << "IBKR-Batch-Erfolg konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

bool DatabaseManager::reserveAlphaVantageRequest()
{
    if (!db.transaction()) {
        qCritical() << "Alpha-Vantage-Limit konnte keine Transaktion starten:"
                    << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "ApiDailyUsage" ("Provider", "UsageDate", "RequestCount")
        VALUES ('AlphaVantage', CURRENT_DATE, 1)
        ON CONFLICT ("Provider", "UsageDate") DO UPDATE SET
            "RequestCount" = "ApiDailyUsage"."RequestCount" + 1,
            "UpdatedAt" = CURRENT_TIMESTAMP
        WHERE "ApiDailyUsage"."RequestCount" < 25
        RETURNING "RequestCount"
    )SQL");

    if (!query.exec() || !query.next()) {
        if (query.lastError().isValid())
            qCritical() << "Alpha-Vantage-Limit konnte nicht aktualisiert werden:"
                        << query.lastError().text();
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Alpha-Vantage-Limit konnte nicht abgeschlossen werden:"
                    << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool DatabaseManager::cacheAlphaVantageSymbol(const QString &symbol, const QString &alphaVantageSymbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "AlphaVantageSymbol" = :alphaVantageSymbol
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":alphaVantageSymbol"), alphaVantageSymbol.trimmed());
    if (!query.exec()) {
        qCritical() << "Alpha-Vantage-Symbol konnte nicht gespeichert werden:"
                    << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool DatabaseManager::cacheYahooSymbol(const QString &symbol, const QString &yahooSymbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "YahooSymbol" = :yahooSymbol
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":yahooSymbol"), yahooSymbol.trimmed());
    if (!query.exec()) {
        qCritical() << "Yahoo-Symbol konnte nicht gespeichert werden:"
                    << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

void DatabaseManager::updateYahooFundamentalAttempt(const QString &symbol)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "YahooFundamentalsLastAttemptAt" = CURRENT_TIMESTAMP
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    if (!query.exec()) {
        qWarning() << "Yahoo-Batch-Versuch konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::updateYahooFundamentalSuccess(const QString &symbol, int qualityScore)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "YahooFundamentalsLastSuccessAt" = CURRENT_TIMESTAMP,
            "YahooFundamentalsLastAttemptAt" = CURRENT_TIMESTAMP,
            "YahooFundamentalsFailureCount" = 0,
            "YahooFundamentalsLastError" = NULL,
            "YahooFundamentalsQualityScore" = :qualityScore
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":qualityScore"), qualityScore);
    if (!query.exec()) {
        qWarning() << "Yahoo-Batch-Erfolg konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::updateYahooFundamentalFailure(const QString &symbol, const QString &error)
{
    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET "YahooFundamentalsLastAttemptAt" = CURRENT_TIMESTAMP,
            "YahooFundamentalsFailureCount" = COALESCE("YahooFundamentalsFailureCount", 0) + 1,
            "YahooFundamentalsLastError" = LEFT(:error, 500)
        WHERE "Symbol" = :symbol
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":error"), error);
    if (!query.exec()) {
        qWarning() << "Yahoo-Batch-Fehler konnte nicht protokolliert werden:"
                   << query.lastError().text() << symbol;
    }
}

void DatabaseManager::fetchAlphaVantageFundamentalOverview(const QString &symbol, const QString &apiSymbol)
{
    if (!reserveAlphaVantageRequest()) {
        setFundamentalDataStatus(
            QStringLiteral("Alpha-Vantage-Tageslimit erreicht: 25 Abrufe wurden heute bereits verwendet."),
            false);
        resetFundamentalRequestState();
        return;
    }

    m_pendingFundamentalSymbol = symbol;
    setFundamentalDataStatus(
        QStringLiteral("Alpha-Vantage-Fundamentaldaten fuer %1 (%2) werden abgerufen ... Noch %3 freie Abrufe heute.")
            .arg(symbol)
            .arg(apiSymbol)
            .arg(alphaVantageRequestsRemaining()),
        true);
    alphaVantageClient.fetchFundamentalOverview(apiSymbol);
}

void DatabaseManager::tryNextAlphaVantageCandidate()
{
    if (m_pendingFundamentalSymbol.isEmpty())
        return;

    if (m_pendingAlphaVantageCandidateIndex >= m_pendingAlphaVantageCandidates.size()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Kein Alpha-Vantage-Kandidat hat Fundamentaldaten geliefert."),
            false);
        resetFundamentalRequestState();
        return;
    }

    const QString symbol = m_pendingFundamentalSymbol;
    const QString apiSymbol = m_pendingAlphaVantageCandidates.at(m_pendingAlphaVantageCandidateIndex++);
    m_pendingResolvedAlphaVantageSymbol = apiSymbol;
    fetchAlphaVantageFundamentalOverview(symbol, apiSymbol);
}

void DatabaseManager::fetchYahooFundamentalsFallback(const QString &symbol)
{
    if (!db.isOpen()) {
        setFundamentalDataStatus(QStringLiteral("Fehler: Datenbank ist nicht verbunden."), false);
        return;
    }

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "YahooSymbol", "ISIN", "Name", "MIC", "PrimaryExchange",
               "IBKRResolvedSymbol", "LocalSymbol", "TradingClass", "ValidExchanges"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), symbol);
    if (!stockQuery.exec() || !stockQuery.next()) {
        setFundamentalDataStatus(
            QStringLiteral("Fehler: Die ausgewaehlte Aktie wurde nicht in der Datenbank gefunden."),
            false);
        resetFundamentalRequestState();
        return;
    }

    const QString name = stockQuery.value(QStringLiteral("Name")).toString();
    QStringList exchangeValues;
    appendUniqueSymbol(exchangeValues, stockQuery.value(QStringLiteral("MIC")).toString());
    appendUniqueSymbol(exchangeValues, stockQuery.value(QStringLiteral("PrimaryExchange")).toString());
    const QStringList validExchanges = stockQuery.value(QStringLiteral("ValidExchanges"))
                                           .toString()
                                           .split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &exchange : validExchanges)
        appendUniqueSymbol(exchangeValues, exchange);

    QStringList ibkrSymbols;
    appendUniqueSymbol(ibkrSymbols, stockQuery.value(QStringLiteral("IBKRResolvedSymbol")).toString());
    appendUniqueSymbol(ibkrSymbols, stockQuery.value(QStringLiteral("LocalSymbol")).toString());
    const QString tradingClass = stockQuery.value(QStringLiteral("TradingClass")).toString();
    if (!isGenericIbkrTradingClass(tradingClass))
        appendUniqueSymbol(ibkrSymbols, tradingClass);
    appendUniqueSymbol(ibkrSymbols, symbol.section(QLatin1Char('.'), 0, 0));

    m_pendingFundamentalSymbol = symbol;
    m_pendingFundamentalSearchKeywords = alphaVantageSearchKeywords(name, symbol);
    m_pendingPreferredYahooSuffix = preferredYahooSuffix(exchangeValues);
    m_pendingYahooCandidates = yahooCandidateSymbols(
        symbol,
        stockQuery.value(QStringLiteral("YahooSymbol")).toString(),
        m_pendingPreferredYahooSuffix,
        ibkrSymbols);
    m_pendingYahooCandidateIndex = 0;
    m_pendingYahooSearchStarted = false;
    m_pendingYahooLastError.clear();
    m_pendingYahooBestSymbol.clear();
    m_pendingYahooBestData.clear();
    m_pendingYahooBestScore = 0;
    m_pendingYahooSymbol.clear();
    m_pendingResolvedAlphaVantageSymbol.clear();
    m_pendingAlphaVantageCandidates.clear();
    m_pendingAlphaVantageCandidateIndex = 0;
    tryNextYahooCandidate();
}

void DatabaseManager::tryNextYahooCandidate()
{
    if (m_pendingFundamentalSymbol.isEmpty())
        return;

    while (m_pendingYahooCandidateIndex < m_pendingYahooCandidates.size()) {
        const QString yahooSymbol = m_pendingYahooCandidates.at(m_pendingYahooCandidateIndex++).trimmed();
        if (yahooSymbol.isEmpty())
            continue;

        m_pendingYahooSymbol = yahooSymbol;
        setFundamentalDataStatus(
            QStringLiteral("Yahoo-Fundamentaldaten fuer %1 (%2) werden abgerufen ...")
                .arg(m_pendingFundamentalSymbol)
                .arg(yahooSymbol),
            true);
        yahooFinanceClient.fetchFundamentals(m_pendingFundamentalSymbol, yahooSymbol);
        return;
    }

    if (!m_pendingYahooSearchStarted && !m_pendingFundamentalSearchKeywords.isEmpty()) {
        m_pendingYahooSearchStarted = true;
        setFundamentalDataStatus(
            QStringLiteral("Yahoo-Symbolsuche fuer %1 mit \"%2\" laeuft ...")
                .arg(m_pendingFundamentalSymbol)
                .arg(m_pendingFundamentalSearchKeywords),
            true);
        yahooFinanceClient.resolveSymbol(m_pendingFundamentalSymbol, m_pendingFundamentalSearchKeywords);
        return;
    }

    const QString checkedSymbols = m_pendingYahooCandidates.isEmpty()
        ? QStringLiteral("keine")
        : m_pendingYahooCandidates.join(QStringLiteral(", "));
    if (!m_pendingYahooBestSymbol.isEmpty() && !m_pendingYahooBestData.isEmpty()) {
        const QString symbol = m_pendingFundamentalSymbol;
        const QString yahooSymbol = m_pendingYahooBestSymbol;
        const int score = m_pendingYahooBestScore;
        const QVariantMap data = m_pendingYahooBestData;
        if (saveYahooFundamentals(symbol, yahooSymbol, data)) {
            updateYahooFundamentalSuccess(symbol, score);
            if (m_yahooFundamentalsBatchActive) {
                ++m_yahooFundamentalsBatchSuccessCount;
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Batch: %1 gespeichert (%2, nur %3 Kennzahlen). Erfolgreich: %4, Fehler: %5.")
                        .arg(symbol)
                        .arg(yahooSymbol)
                        .arg(score)
                        .arg(m_yahooFundamentalsBatchSuccessCount)
                        .arg(m_yahooFundamentalsBatchFailureCount),
                    true);
                emit fundamentalDataUpdated(symbol);
                resetFundamentalRequestState();
                scheduleNextYahooFundamentalsBatchSymbol(1500);
                return;
            }
            setFundamentalDataStatus(
                QStringLiteral("Yahoo-Fundamentaldaten fuer %1 (%2) wurden gespeichert, aber Yahoo lieferte nur %3 Kennzahlen. Das Symbol wurde deshalb nicht als Standard gespeichert. Gepruefte Kandidaten: %4")
                    .arg(symbol)
                    .arg(yahooSymbol)
                    .arg(score)
                    .arg(checkedSymbols),
                false);
            emit fundamentalDataUpdated(symbol);
        } else {
            if (m_yahooFundamentalsBatchActive) {
                ++m_yahooFundamentalsBatchFailureCount;
                updateYahooFundamentalFailure(symbol, QStringLiteral("Yahoo-Fundamentaldaten konnten nicht gespeichert werden."));
                setFundamentalDataStatus(
                    QStringLiteral("Yahoo-Batch: %1 konnte nicht gespeichert werden. Erfolgreich: %2, Fehler: %3.")
                        .arg(symbol)
                        .arg(m_yahooFundamentalsBatchSuccessCount)
                        .arg(m_yahooFundamentalsBatchFailureCount),
                    true);
                resetFundamentalRequestState();
                scheduleNextYahooFundamentalsBatchSymbol(3000);
                return;
            }
            setFundamentalDataStatus(
                QStringLiteral("Fehler: Yahoo-Fundamentaldaten konnten nicht gespeichert werden."),
                false);
        }
        resetFundamentalRequestState();
        return;
    }

    const QString lastError = m_pendingYahooLastError.isEmpty()
        ? QStringLiteral("keine nutzbaren Kennzahlen gefunden")
        : m_pendingYahooLastError;
    if (m_yahooFundamentalsBatchActive) {
        const QString failedSymbol = m_pendingFundamentalSymbol;
        ++m_yahooFundamentalsBatchFailureCount;
        updateYahooFundamentalFailure(
            failedSymbol,
            QStringLiteral("Gepruefte Kandidaten: %1. Letzter Fehler: %2")
                .arg(checkedSymbols)
                .arg(lastError));
        setFundamentalDataStatus(
            QStringLiteral("Yahoo-Batch: %1 ohne Fundamentaldaten. Erfolgreich: %2, Fehler: %3.")
                .arg(failedSymbol)
                .arg(m_yahooFundamentalsBatchSuccessCount)
                .arg(m_yahooFundamentalsBatchFailureCount),
            true);
        resetFundamentalRequestState();
        scheduleNextYahooFundamentalsBatchSymbol(3000);
        return;
    }
    setFundamentalDataStatus(
        QStringLiteral("Fehler: Auch Yahoo lieferte fuer %1 keine Fundamentaldaten. Gepruefte Kandidaten: %2. Letzter Fehler: %3")
            .arg(m_pendingFundamentalSymbol)
            .arg(checkedSymbols)
            .arg(lastError),
        false);
    resetFundamentalRequestState();
}

void DatabaseManager::loadNextYahooFundamentalsBatchSymbol()
{
    if (!m_yahooFundamentalsBatchActive)
        return;

    if (m_yahooFundamentalsBatchIndex >= m_yahooFundamentalsBatchSymbols.size()) {
        finishYahooFundamentalsBatch(
            QStringLiteral("Yahoo-Batch abgeschlossen: %1 Aktien, %2 erfolgreich, %3 fehlgeschlagen.")
                .arg(m_yahooFundamentalsBatchSymbols.size())
                .arg(m_yahooFundamentalsBatchSuccessCount)
                .arg(m_yahooFundamentalsBatchFailureCount));
        return;
    }

    const QString symbol = m_yahooFundamentalsBatchSymbols.at(m_yahooFundamentalsBatchIndex++).trimmed();
    if (symbol.isEmpty()) {
        scheduleNextYahooFundamentalsBatchSymbol(100);
        return;
    }

    updateYahooFundamentalAttempt(symbol);
    setFundamentalDataStatus(
        QStringLiteral("Yahoo-Batch: %1/%2 %3 wird aktualisiert ... Erfolgreich: %4, Fehler: %5")
            .arg(m_yahooFundamentalsBatchIndex)
            .arg(m_yahooFundamentalsBatchSymbols.size())
            .arg(symbol)
            .arg(m_yahooFundamentalsBatchSuccessCount)
            .arg(m_yahooFundamentalsBatchFailureCount),
        true);
    fetchYahooFundamentalsFallback(symbol);
}

void DatabaseManager::scheduleNextYahooFundamentalsBatchSymbol(int delayMs)
{
    if (!m_yahooFundamentalsBatchActive)
        return;
    m_yahooFundamentalsBatchTimer.start(delayMs);
}

void DatabaseManager::finishYahooFundamentalsBatch(const QString &message)
{
    m_yahooFundamentalsBatchTimer.stop();
    m_yahooFundamentalsBatchActive = false;
    resetFundamentalRequestState();
    setFundamentalDataStatus(message, false);
    emit fundamentalDataChanged();
}

void DatabaseManager::resetFundamentalRequestState()
{
    m_pendingFundamentalSymbol.clear();
    m_pendingFundamentalSearchKeywords.clear();
    m_pendingResolvedAlphaVantageSymbol.clear();
    m_pendingAlphaVantageCandidates.clear();
    m_pendingAlphaVantageCandidateIndex = 0;
    m_pendingYahooSymbol.clear();
    m_pendingPreferredYahooSuffix.clear();
    m_pendingYahooCandidates.clear();
    m_pendingYahooCandidateIndex = 0;
    m_pendingYahooSearchStarted = false;
    m_pendingYahooLastError.clear();
    m_pendingYahooBestSymbol.clear();
    m_pendingYahooBestData.clear();
    m_pendingYahooBestScore = 0;
}

bool DatabaseManager::saveAlphaVantageFundamentals(const QString &symbol, const QVariantMap &overview)
{
    if (!db.transaction()) {
        qCritical() << "Alpha-Vantage-Fundamentaldaten konnten keine Transaktion starten:"
                    << db.lastError().text();
        return false;
    }

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "IBKRConId", "Currency"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), symbol);
    if (!stockQuery.exec() || !stockQuery.next()) {
        qCritical() << "Aktie fuer Alpha-Vantage-Fundamentaldaten nicht gefunden:" << symbol;
        db.rollback();
        return false;
    }

    const QVariant revenue = alphaNumber(overview, QStringLiteral("RevenueTTM"));
    const QVariant profitMargin = alphaPercent(overview, QStringLiteral("ProfitMargin"));
    const QVariant evToRevenue = alphaNumber(overview, QStringLiteral("EVToRevenue"));
    const QVariant dividendYield = alphaPercent(overview, QStringLiteral("DividendYield"));

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "StockFundamentals" (
            "Symbol", "IBKRConId", "AsOfDate", "Currency",
            "MarketCapitalization", "EnterpriseValue", "PERatio", "ForwardPERatio",
            "PriceToBookRatio", "PriceToSalesRatio", "EPS", "ForwardEPS",
            "DividendPerShare", "DividendYield", "PayoutRatio", "Beta",
            "Revenue", "NetIncome", "EBITDA", "ReturnOnEquity", "ReturnOnAssets",
            "SharesOutstanding", "Week52High", "Week52Low", "Source", "RawData",
            "UpdatedAt"
        )
        VALUES (
            :symbol, :ibkrConId, CURRENT_DATE, :currency,
            :marketCapitalization, :enterpriseValue, :peRatio, :forwardPeRatio,
            :priceToBookRatio, :priceToSalesRatio, :eps, :forwardEps,
            :dividendPerShare, :dividendYield, :payoutRatio, :beta,
            :revenue, :netIncome, :ebitda, :returnOnEquity, :returnOnAssets,
            :sharesOutstanding, :week52High, :week52Low, 'AlphaVantage', CAST(:rawData AS jsonb),
            CURRENT_TIMESTAMP
        )
        ON CONFLICT ("Symbol", "AsOfDate", "Source") DO UPDATE SET
            "IBKRConId" = EXCLUDED."IBKRConId",
            "Currency" = EXCLUDED."Currency",
            "MarketCapitalization" = EXCLUDED."MarketCapitalization",
            "EnterpriseValue" = EXCLUDED."EnterpriseValue",
            "PERatio" = EXCLUDED."PERatio",
            "ForwardPERatio" = EXCLUDED."ForwardPERatio",
            "PriceToBookRatio" = EXCLUDED."PriceToBookRatio",
            "PriceToSalesRatio" = EXCLUDED."PriceToSalesRatio",
            "EPS" = EXCLUDED."EPS",
            "ForwardEPS" = EXCLUDED."ForwardEPS",
            "DividendPerShare" = EXCLUDED."DividendPerShare",
            "DividendYield" = EXCLUDED."DividendYield",
            "PayoutRatio" = EXCLUDED."PayoutRatio",
            "Beta" = EXCLUDED."Beta",
            "Revenue" = EXCLUDED."Revenue",
            "NetIncome" = EXCLUDED."NetIncome",
            "EBITDA" = EXCLUDED."EBITDA",
            "ReturnOnEquity" = EXCLUDED."ReturnOnEquity",
            "ReturnOnAssets" = EXCLUDED."ReturnOnAssets",
            "SharesOutstanding" = EXCLUDED."SharesOutstanding",
            "Week52High" = EXCLUDED."Week52High",
            "Week52Low" = EXCLUDED."Week52Low",
            "RawData" = EXCLUDED."RawData",
            "UpdatedAt" = CURRENT_TIMESTAMP
    )SQL");

    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":ibkrConId"), stockQuery.value(QStringLiteral("IBKRConId")));
    query.bindValue(QStringLiteral(":currency"),
                    alphaText(overview, QStringLiteral("Currency")).isValid()
                        ? alphaText(overview, QStringLiteral("Currency"))
                        : stockQuery.value(QStringLiteral("Currency")));
    query.bindValue(QStringLiteral(":marketCapitalization"), alphaNumber(overview, QStringLiteral("MarketCapitalization")));
    query.bindValue(QStringLiteral(":enterpriseValue"), computedProduct(revenue, evToRevenue));
    query.bindValue(QStringLiteral(":peRatio"), alphaNumber(overview, QStringLiteral("PERatio")));
    query.bindValue(QStringLiteral(":forwardPeRatio"), alphaNumber(overview, QStringLiteral("ForwardPE")));
    query.bindValue(QStringLiteral(":priceToBookRatio"), alphaNumber(overview, QStringLiteral("PriceToBookRatio")));
    query.bindValue(QStringLiteral(":priceToSalesRatio"), alphaNumber(overview, QStringLiteral("PriceToSalesRatioTTM")));
    query.bindValue(QStringLiteral(":eps"), alphaNumber(overview, QStringLiteral("EPS")));
    query.bindValue(QStringLiteral(":forwardEps"), alphaNumber(overview, QStringLiteral("ForwardEPS")));
    query.bindValue(QStringLiteral(":dividendPerShare"), alphaNumber(overview, QStringLiteral("DividendPerShare")));
    query.bindValue(QStringLiteral(":dividendYield"), dividendYield);
    query.bindValue(QStringLiteral(":payoutRatio"), alphaPercent(overview, QStringLiteral("PayoutRatio")));
    query.bindValue(QStringLiteral(":beta"), alphaNumber(overview, QStringLiteral("Beta")));
    query.bindValue(QStringLiteral(":revenue"), revenue);
    query.bindValue(QStringLiteral(":netIncome"),
                    profitMargin.isValid() && revenue.isValid()
                        ? QVariant(revenue.toDouble() * profitMargin.toDouble() / 100.0)
                        : QVariant());
    query.bindValue(QStringLiteral(":ebitda"), alphaNumber(overview, QStringLiteral("EBITDA")));
    query.bindValue(QStringLiteral(":returnOnEquity"), alphaPercent(overview, QStringLiteral("ReturnOnEquityTTM")));
    query.bindValue(QStringLiteral(":returnOnAssets"), alphaPercent(overview, QStringLiteral("ReturnOnAssetsTTM")));
    query.bindValue(QStringLiteral(":sharesOutstanding"), alphaNumber(overview, QStringLiteral("SharesOutstanding")));
    query.bindValue(QStringLiteral(":week52High"), alphaNumber(overview, QStringLiteral("52WeekHigh")));
    query.bindValue(QStringLiteral(":week52Low"), alphaNumber(overview, QStringLiteral("52WeekLow")));
    query.bindValue(QStringLiteral(":rawData"),
                    QString::fromUtf8(QJsonDocument::fromVariant(overview).toJson(QJsonDocument::Compact)));

    if (!query.exec()) {
        qCritical() << "Alpha-Vantage-Fundamentaldaten konnten nicht gespeichert werden:"
                    << query.lastError().text();
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Alpha-Vantage-Fundamentaldaten konnten nicht abgeschlossen werden:"
                    << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool DatabaseManager::saveYahooFundamentals(const QString &symbol,
                                            const QString &yahooSymbol,
                                            const QVariantMap &data)
{
    if (!db.transaction()) {
        qCritical() << "Yahoo-Fundamentaldaten konnten keine Transaktion starten:"
                    << db.lastError().text();
        return false;
    }

    QSqlQuery stockQuery(db);
    stockQuery.prepare(R"SQL(
        SELECT "IBKRConId", "Currency"
        FROM "Stocks"
        WHERE "Symbol" = :symbol
    )SQL");
    stockQuery.bindValue(QStringLiteral(":symbol"), symbol);
    if (!stockQuery.exec() || !stockQuery.next()) {
        qCritical() << "Aktie fuer Yahoo-Fundamentaldaten nicht gefunden:" << symbol;
        db.rollback();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "StockFundamentals" (
            "Symbol", "IBKRConId", "AsOfDate", "Currency",
            "MarketCapitalization", "EnterpriseValue", "PERatio", "ForwardPERatio",
            "PriceToBookRatio", "PriceToSalesRatio", "EPS", "ForwardEPS",
            "DividendPerShare", "DividendYield", "PayoutRatio", "Beta",
            "Revenue", "NetIncome", "EBITDA", "ReturnOnEquity", "ReturnOnAssets",
            "DebtToEquity", "SharesOutstanding", "Week52High", "Week52Low",
            "Source", "RawData", "UpdatedAt"
        )
        VALUES (
            :symbol, :ibkrConId, CURRENT_DATE, :currency,
            :marketCapitalization, :enterpriseValue, :peRatio, :forwardPeRatio,
            :priceToBookRatio, :priceToSalesRatio, :eps, :forwardEps,
            :dividendPerShare, :dividendYield, :payoutRatio, :beta,
            :revenue, :netIncome, :ebitda, :returnOnEquity, :returnOnAssets,
            :debtToEquity, :sharesOutstanding, :week52High, :week52Low,
            'Yahoo', CAST(:rawData AS jsonb), CURRENT_TIMESTAMP
        )
        ON CONFLICT ("Symbol", "AsOfDate", "Source") DO UPDATE SET
            "IBKRConId" = EXCLUDED."IBKRConId",
            "Currency" = EXCLUDED."Currency",
            "MarketCapitalization" = EXCLUDED."MarketCapitalization",
            "EnterpriseValue" = EXCLUDED."EnterpriseValue",
            "PERatio" = EXCLUDED."PERatio",
            "ForwardPERatio" = EXCLUDED."ForwardPERatio",
            "PriceToBookRatio" = EXCLUDED."PriceToBookRatio",
            "PriceToSalesRatio" = EXCLUDED."PriceToSalesRatio",
            "EPS" = EXCLUDED."EPS",
            "ForwardEPS" = EXCLUDED."ForwardEPS",
            "DividendPerShare" = EXCLUDED."DividendPerShare",
            "DividendYield" = EXCLUDED."DividendYield",
            "PayoutRatio" = EXCLUDED."PayoutRatio",
            "Beta" = EXCLUDED."Beta",
            "Revenue" = EXCLUDED."Revenue",
            "NetIncome" = EXCLUDED."NetIncome",
            "EBITDA" = EXCLUDED."EBITDA",
            "ReturnOnEquity" = EXCLUDED."ReturnOnEquity",
            "ReturnOnAssets" = EXCLUDED."ReturnOnAssets",
            "DebtToEquity" = EXCLUDED."DebtToEquity",
            "SharesOutstanding" = EXCLUDED."SharesOutstanding",
            "Week52High" = EXCLUDED."Week52High",
            "Week52Low" = EXCLUDED."Week52Low",
            "RawData" = EXCLUDED."RawData",
            "UpdatedAt" = CURRENT_TIMESTAMP
    )SQL");

    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":ibkrConId"), stockQuery.value(QStringLiteral("IBKRConId")));
    query.bindValue(QStringLiteral(":currency"),
                    hasValue(data.value(QStringLiteral("currency")))
                        ? data.value(QStringLiteral("currency"))
                        : stockQuery.value(QStringLiteral("Currency")));
    query.bindValue(QStringLiteral(":marketCapitalization"), yahooNumber(data, QStringLiteral("marketCapitalization")));
    query.bindValue(QStringLiteral(":enterpriseValue"), yahooNumber(data, QStringLiteral("enterpriseValue")));
    query.bindValue(QStringLiteral(":peRatio"), yahooNumber(data, QStringLiteral("peRatio")));
    query.bindValue(QStringLiteral(":forwardPeRatio"), yahooNumber(data, QStringLiteral("forwardPeRatio")));
    query.bindValue(QStringLiteral(":priceToBookRatio"), yahooNumber(data, QStringLiteral("priceToBookRatio")));
    query.bindValue(QStringLiteral(":priceToSalesRatio"), yahooNumber(data, QStringLiteral("priceToSalesRatio")));
    query.bindValue(QStringLiteral(":eps"), yahooNumber(data, QStringLiteral("eps")));
    query.bindValue(QStringLiteral(":forwardEps"), yahooNumber(data, QStringLiteral("forwardEps")));
    query.bindValue(QStringLiteral(":dividendPerShare"), yahooNumber(data, QStringLiteral("dividendPerShare")));
    query.bindValue(QStringLiteral(":dividendYield"), yahooNumber(data, QStringLiteral("dividendYield")));
    query.bindValue(QStringLiteral(":payoutRatio"), yahooNumber(data, QStringLiteral("payoutRatio")));
    query.bindValue(QStringLiteral(":beta"), yahooNumber(data, QStringLiteral("beta")));
    query.bindValue(QStringLiteral(":revenue"), yahooNumber(data, QStringLiteral("revenue")));
    query.bindValue(QStringLiteral(":netIncome"), yahooNumber(data, QStringLiteral("netIncome")));
    query.bindValue(QStringLiteral(":ebitda"), yahooNumber(data, QStringLiteral("ebitda")));
    query.bindValue(QStringLiteral(":returnOnEquity"), yahooNumber(data, QStringLiteral("returnOnEquity")));
    query.bindValue(QStringLiteral(":returnOnAssets"), yahooNumber(data, QStringLiteral("returnOnAssets")));
    query.bindValue(QStringLiteral(":debtToEquity"), yahooNumber(data, QStringLiteral("debtToEquity")));
    query.bindValue(QStringLiteral(":sharesOutstanding"), yahooNumber(data, QStringLiteral("sharesOutstanding")));
    query.bindValue(QStringLiteral(":week52High"), yahooNumber(data, QStringLiteral("week52High")));
    query.bindValue(QStringLiteral(":week52Low"), yahooNumber(data, QStringLiteral("week52Low")));
    const QString rawData = hasValue(data.value(QStringLiteral("rawData")))
        ? data.value(QStringLiteral("rawData")).toString()
        : QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));
    query.bindValue(QStringLiteral(":rawData"), rawData);

    if (!query.exec()) {
        qCritical() << "Yahoo-Fundamentaldaten konnten nicht gespeichert werden:"
                    << query.lastError().text() << yahooSymbol;
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Yahoo-Fundamentaldaten konnten nicht abgeschlossen werden:"
                    << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

DatabaseManager::~DatabaseManager()
{
    if (m_ibkrProcess.state() != QProcess::NotRunning) {
        m_ibkrProcess.kill();
        m_ibkrProcess.waitForFinished(2000);
    }
    if (db.isOpen()) {
        db.close();
    }
}

bool DatabaseManager::connectToDatabase(const QString &host, const QString &dbName, const QString &user, const QString &password)
{
    db.setHostName(host);
    db.setDatabaseName(dbName);
    db.setUserName(user);
    db.setPassword(password);

    if (!db.open()) {
        qDebug() << "Fehler bei der Verbindung zur Datenbank:" << db.lastError().text();
        return false;
    }
    qDebug() << "Erfolgreich mit der Datenbank verbunden!";
    return ensureSchema();
}

bool DatabaseManager::ensureSchema()
{
    if (!db.isOpen())
        return false;

    const QStringList statements = {
        QStringLiteral(R"SQL(
            ALTER TABLE "Stocks"
                ADD COLUMN IF NOT EXISTS "IBKRConId" BIGINT,
                ADD COLUMN IF NOT EXISTS "IBKRResolvedSymbol" VARCHAR(64),
                ADD COLUMN IF NOT EXISTS "Currency" VARCHAR(8),
                ADD COLUMN IF NOT EXISTS "PrimaryExchange" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "LocalSymbol" VARCHAR(64),
                ADD COLUMN IF NOT EXISTS "SecurityType" VARCHAR(16),
                ADD COLUMN IF NOT EXISTS "TradingClass" VARCHAR(64),
                ADD COLUMN IF NOT EXISTS "StockType" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "Industry" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "Category" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "Subcategory" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "TimeZoneId" VARCHAR(64),
                ADD COLUMN IF NOT EXISTS "TradingHours" TEXT,
                ADD COLUMN IF NOT EXISTS "LiquidHours" TEXT,
                ADD COLUMN IF NOT EXISTS "MinTick" NUMERIC(20, 10),
                ADD COLUMN IF NOT EXISTS "MarketRuleIds" TEXT,
                ADD COLUMN IF NOT EXISTS "ValidExchanges" TEXT,
                ADD COLUMN IF NOT EXISTS "OrderTypes" TEXT,
                ADD COLUMN IF NOT EXISTS "MarketName" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "CUSIP" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "AlphaVantageSymbol" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "YahooSymbol" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsLastAttemptAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsLastSuccessAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsFailureCount" INTEGER NOT NULL DEFAULT 0,
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsLastError" TEXT,
                ADD COLUMN IF NOT EXISTS "YahooFundamentalsQualityScore" INTEGER,
                ADD COLUMN IF NOT EXISTS "IBKRLastSyncAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRLastAttemptAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRFailureCount" INTEGER NOT NULL DEFAULT 0,
                ADD COLUMN IF NOT EXISTS "IBKRLastError" TEXT,
                ADD COLUMN IF NOT EXISTS "IBKRValidationStatus" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "IBKRValidationMessage" TEXT,
                ADD COLUMN IF NOT EXISTS "IBKRValidationAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchange" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeTurnover" NUMERIC(28, 4),
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeCheckedAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeLastAttemptAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeLastSuccessAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeFailureCount" INTEGER NOT NULL DEFAULT 0,
                ADD COLUMN IF NOT EXISTS "IBKRQuoteExchangeLastError" TEXT,
                ADD COLUMN IF NOT EXISTS "IBKRBestDirectExchange" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "IBKRBestDirectExchangeTurnover" NUMERIC(28, 4),
                ADD COLUMN IF NOT EXISTS "IBKRBestDirectExchangeCheckedAt" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "use_marketstack" BOOLEAN NOT NULL DEFAULT FALSE,
                ADD COLUMN IF NOT EXISTS "marketplace_sym" VARCHAR(128),
                ADD COLUMN IF NOT EXISTS "marketplace_exchange" VARCHAR(32),
                ADD COLUMN IF NOT EXISTS "marketplace_turnover" NUMERIC(28, 4),
                ADD COLUMN IF NOT EXISTS "marketplace_checked_at" TIMESTAMPTZ,
                ADD COLUMN IF NOT EXISTS "marketplace_last_error" TEXT
        )SQL"),
        QStringLiteral(R"SQL(
            UPDATE "Stocks"
            SET "AlphaVantageSymbol" = 'NVDA'
            WHERE "ISIN" = 'US67066G1040'
              AND COALESCE("AlphaVantageSymbol", '') = ''
        )SQL"),
        QStringLiteral(R"SQL(
            UPDATE "Stocks"
            SET "AlphaVantageSymbol" = 'STX'
            WHERE "ISIN" = 'IE00BKVD2N49'
              AND COALESCE("AlphaVantageSymbol", '') = ''
        )SQL"),
        QStringLiteral(R"SQL(
            DROP INDEX IF EXISTS "Stocks_IBKRConId_uidx"
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE INDEX IF NOT EXISTS "Stocks_IBKRConId_idx"
                ON "Stocks" ("IBKRConId")
                WHERE "IBKRConId" IS NOT NULL
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS "StockFundamentals" (
                "Id" BIGSERIAL PRIMARY KEY,
                "Symbol" VARCHAR NOT NULL,
                "IBKRConId" BIGINT,
                "AsOfDate" DATE NOT NULL,
                "Currency" VARCHAR(8),
                "MarketCapitalization" NUMERIC(24, 4),
                "EnterpriseValue" NUMERIC(24, 4),
                "PERatio" NUMERIC(20, 8),
                "ForwardPERatio" NUMERIC(20, 8),
                "PriceToBookRatio" NUMERIC(20, 8),
                "PriceToSalesRatio" NUMERIC(20, 8),
                "PriceToCashFlowRatio" NUMERIC(20, 8),
                "PriceToDividendRatio" NUMERIC(20, 8),
                "EPS" NUMERIC(20, 8),
                "ForwardEPS" NUMERIC(20, 8),
                "DividendPerShare" NUMERIC(20, 8),
                "DividendYield" NUMERIC(20, 8),
                "PayoutRatio" NUMERIC(20, 8),
                "Beta" NUMERIC(20, 8),
                "Revenue" NUMERIC(24, 4),
                "NetIncome" NUMERIC(24, 4),
                "EBITDA" NUMERIC(24, 4),
                "ReturnOnEquity" NUMERIC(20, 8),
                "ReturnOnAssets" NUMERIC(20, 8),
                "DebtToEquity" NUMERIC(20, 8),
                "SharesOutstanding" NUMERIC(24, 4),
                "Week52High" NUMERIC(20, 8),
                "Week52Low" NUMERIC(20, 8),
                "Source" VARCHAR(32) NOT NULL DEFAULT 'IBKR',
                "RawData" JSONB,
                "UpdatedAt" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
                CONSTRAINT "StockFundamentals_symbol_date_source_key"
                    UNIQUE ("Symbol", "AsOfDate", "Source")
            )
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE INDEX IF NOT EXISTS "StockFundamentals_IBKRConId_idx"
                ON "StockFundamentals" ("IBKRConId")
                WHERE "IBKRConId" IS NOT NULL
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE INDEX IF NOT EXISTS "StockFundamentals_Symbol_AsOfDate_idx"
                ON "StockFundamentals" ("Symbol", "AsOfDate" DESC)
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS "ApiDailyUsage" (
                "Provider" VARCHAR(64) NOT NULL,
                "UsageDate" DATE NOT NULL,
                "RequestCount" INTEGER NOT NULL DEFAULT 0,
                "UpdatedAt" TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
                CONSTRAINT "ApiDailyUsage_provider_date_key"
                    PRIMARY KEY ("Provider", "UsageDate")
            )
        )SQL")
    };

    if (!db.transaction()) {
        qCritical() << "Schema-Migration konnte keine Transaktion starten:"
                    << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    for (const QString &statement : statements) {
        if (query.exec(statement))
            continue;

        qCritical() << "Schema-Migration fehlgeschlagen:" << query.lastError().text();
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Schema-Migration konnte nicht abgeschlossen werden:"
                    << db.lastError().text();
        db.rollback();
        return false;
    }

    return true;
}

QVariantList DatabaseManager::getBoughtStocks()
{
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return results;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT
            "Symbol",
            "Name",
            TO_CHAR("BuyDate", 'YYYY-MM-DD') AS "BuyDate",
            TO_CHAR("SellDate", 'YYYY-MM-DD') AS "SellDate",
            "CurrentValue",
            "EntryValue",
            "ValueIncreasePercent",
            "Status"
        FROM "BoughtStocks"
        ORDER BY "BuyDate" DESC, "Symbol" ASC
    )SQL");

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden der gekauften Aktien:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.insert(query.record().fieldName(i), query.value(i));
        }
        results << row;
    }

    return results;
}

QVariantList DatabaseManager::getTestPortfolio()
{
    QVariantList results;
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return results;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT
            b."Symbol",
            b."Name",
            TO_CHAR(b."BuyDate", 'YYYY-MM-DD') AS "BuyDate",
            TO_CHAR(b."SellDate", 'YYYY-MM-DD') AS "SellDate",
            b."CurrentValue",
            b."EntryValue",
            b."ValueIncreasePercent",
            b."Status",
            s."MIC",
            s."ISIN",
            s."Exchange",
            s."CountryCode",
            s."City",
            s."IBKRConId",
            s."Currency",
            s."PrimaryExchange",
            s."LocalSymbol",
            s."SecurityType",
            s."TradingClass",
            s."StockType",
            s."Industry",
            s."Category",
            s."Subcategory",
            s."TimeZoneId",
            s."TradingHours",
            s."LiquidHours",
            s."MinTick",
            s."MarketRuleIds",
            s."ValidExchanges",
            s."OrderTypes",
            s."MarketName",
            s."CUSIP",
            s."IBKRLastSyncAt",
            TO_CHAR(f."AsOfDate", 'YYYY-MM-DD') AS "FundamentalAsOfDate",
            f."Currency" AS "FundamentalCurrency",
            f."MarketCapitalization" AS "FundamentalMarketCapitalization",
            f."EnterpriseValue" AS "FundamentalEnterpriseValue",
            f."PERatio" AS "FundamentalPERatio",
            f."ForwardPERatio" AS "FundamentalForwardPERatio",
            f."PriceToBookRatio" AS "FundamentalPriceToBookRatio",
            f."PriceToSalesRatio" AS "FundamentalPriceToSalesRatio",
            f."PriceToCashFlowRatio" AS "FundamentalPriceToCashFlowRatio",
            f."PriceToDividendRatio" AS "FundamentalPriceToDividendRatio",
            f."EPS" AS "FundamentalEPS",
            f."ForwardEPS" AS "FundamentalForwardEPS",
            f."DividendPerShare" AS "FundamentalDividendPerShare",
            f."DividendYield" AS "FundamentalDividendYield",
            f."PayoutRatio" AS "FundamentalPayoutRatio",
            f."Beta" AS "FundamentalBeta",
            f."Revenue" AS "FundamentalRevenue",
            f."NetIncome" AS "FundamentalNetIncome",
            f."EBITDA" AS "FundamentalEBITDA",
            f."ReturnOnEquity" AS "FundamentalReturnOnEquity",
            f."ReturnOnAssets" AS "FundamentalReturnOnAssets",
            f."DebtToEquity" AS "FundamentalDebtToEquity",
            f."SharesOutstanding" AS "FundamentalSharesOutstanding",
            f."Week52High" AS "FundamentalWeek52High",
            f."Week52Low" AS "FundamentalWeek52Low",
            f."Source" AS "FundamentalSource",
            f."RawData"::text AS "FundamentalRawData",
            TO_CHAR(f."UpdatedAt", 'YYYY-MM-DD HH24:MI:SS') AS "FundamentalUpdatedAt"
        FROM "BoughtStocks" b
        LEFT JOIN "Stocks" s ON s."Symbol" = b."Symbol"
        LEFT JOIN LATERAL (
            SELECT *
            FROM "StockFundamentals" sf
            WHERE sf."Symbol" = b."Symbol"
              AND sf."Source" IN ('AlphaVantage', 'Yahoo')
            ORDER BY
                sf."AsOfDate" DESC,
                sf."UpdatedAt" DESC,
                CASE sf."Source" WHEN 'Yahoo' THEN 0 ELSE 1 END
            LIMIT 1
        ) f ON true
        ORDER BY b."BuyDate" DESC, b."Symbol" ASC
    )SQL");

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden der Depot-Testdaten:" << query.lastError().text();
        return results;
    }

    const QStringList industries = {
        QStringLiteral("Technology"),
        QStringLiteral("Industrials"),
        QStringLiteral("Financial"),
        QStringLiteral("Consumer")
    };
    const QStringList categories = {
        QStringLiteral("Hardware"),
        QStringLiteral("Manufacturing"),
        QStringLiteral("Capital Markets"),
        QStringLiteral("Consumer Products")
    };

    while (query.next()) {
        const QString symbol = query.value("Symbol").toString();
        const QString localSymbol = symbol.section(QLatin1Char('.'), 0, 0);
        const quint32 seed = stableSymbolSeed(symbol);
        const double currentValue = query.value("CurrentValue").toDouble();
        const double price = currentValue > 0.0 ? currentValue : 100.0;
        const double peRatio = mockValue(seed, 0, 8.0, 32.0);
        const double priceToSales = mockValue(seed, 8, 0.8, 7.0);
        const double dividendYield = mockValue(seed, 16, 0.5, 5.0);
        const double sharesOutstanding = 100000000.0 + (seed % 900u) * 1000000.0;
        const double marketCapitalization = price * sharesOutstanding;
        const double revenue = marketCapitalization / priceToSales;
        const bool isEtf = query.value("Name").toString().contains(
            QStringLiteral("ETF"), Qt::CaseInsensitive);
        const QString mic = query.value("MIC").toString();

        auto stringOr = [&query](const char *column, const QString &fallback) {
            const QString value = query.value(column).toString().trimmed();
            return value.isEmpty() ? fallback : value;
        };

        QVariantMap row;
        row["symbol"] = symbol;
        row["name"] = query.value("Name");
        row["buyDate"] = query.value("BuyDate");
        row["sellDate"] = query.value("SellDate");
        row["currentValue"] = currentValue;
        row["entryValue"] = query.value("EntryValue");
        row["valueIncreasePercent"] = query.value("ValueIncreasePercent");
        row["status"] = query.value("Status");
        row["mic"] = mic;
        row["isin"] = query.value("ISIN");
        row["exchange"] = query.value("Exchange");
        row["countryCode"] = query.value("CountryCode");
        row["city"] = query.value("City");

        const QStringList databaseFields = {
            QStringLiteral("symbol"), QStringLiteral("name"), QStringLiteral("buyDate"),
            QStringLiteral("sellDate"), QStringLiteral("currentValue"), QStringLiteral("entryValue"),
            QStringLiteral("valueIncreasePercent"), QStringLiteral("status"), QStringLiteral("mic"),
            QStringLiteral("isin"), QStringLiteral("exchange"), QStringLiteral("countryCode"),
            QStringLiteral("city")
        };
        for (const QString &field : databaseFields)
            row[field + QStringLiteral("Origin")] = QStringLiteral("db");

        auto setIbkrString = [&query, &row](const QString &key,
                                            const char *column,
                                            const QString &fallback) {
            const QString value = query.value(column).toString().trimmed();
            const bool hasIbkrValue = !value.isEmpty();
            row[key] = hasIbkrValue ? value : fallback;
            row[key + QStringLiteral("Origin")] = hasIbkrValue
                ? QStringLiteral("IBKR")
                : QStringLiteral("mock");
        };

        const bool hasConId = !query.value("IBKRConId").isNull();
        row["ibkrConId"] = hasConId ? query.value("IBKRConId") : QVariant::fromValue(-qint64(seed) - 1);
        row["ibkrConIdOrigin"] = hasConId ? QStringLiteral("IBKR") : QStringLiteral("mock");
        setIbkrString(QStringLiteral("currency"), "Currency", QStringLiteral("EUR"));
        setIbkrString(QStringLiteral("primaryExchange"), "PrimaryExchange", mic);
        setIbkrString(QStringLiteral("localSymbol"), "LocalSymbol", localSymbol);
        setIbkrString(QStringLiteral("securityType"), "SecurityType", isEtf ? QStringLiteral("ETF") : QStringLiteral("STK"));
        setIbkrString(QStringLiteral("tradingClass"), "TradingClass", localSymbol);
        setIbkrString(QStringLiteral("stockType"), "StockType", isEtf ? QStringLiteral("ETF") : QStringLiteral("COMMON"));
        setIbkrString(QStringLiteral("industry"), "Industry", industries.at(seed % industries.size()));
        setIbkrString(QStringLiteral("category"), "Category", categories.at(seed % categories.size()));
        setIbkrString(QStringLiteral("subcategory"), "Subcategory", QStringLiteral("TEST-%1").arg(seed % 10u));
        setIbkrString(QStringLiteral("timeZoneId"), "TimeZoneId", QStringLiteral("Europe/Berlin"));
        setIbkrString(QStringLiteral("tradingHours"), "TradingHours", QStringLiteral("08:00-22:00"));
        setIbkrString(QStringLiteral("liquidHours"), "LiquidHours", QStringLiteral("09:00-17:30"));
        const bool hasMinTick = !query.value("MinTick").isNull();
        row["minTick"] = hasMinTick ? query.value("MinTick") : QVariant::fromValue(0.01);
        row["minTickOrigin"] = hasMinTick ? QStringLiteral("IBKR") : QStringLiteral("mock");
        setIbkrString(QStringLiteral("marketRuleIds"), "MarketRuleIds", QStringLiteral("TEST-26"));
        setIbkrString(QStringLiteral("validExchanges"), "ValidExchanges", QStringLiteral("SMART,%1").arg(mic));
        setIbkrString(QStringLiteral("orderTypes"), "OrderTypes", QStringLiteral("MKT,LMT,STP,STP LMT"));
        setIbkrString(QStringLiteral("marketName"), "MarketName", stringOr("Exchange", mic));
        setIbkrString(QStringLiteral("cusip"), "CUSIP", QStringLiteral("TEST%1").arg(seed % 100000u, 5, 10, QLatin1Char('0')));
        const bool hasSyncTime = !query.value("IBKRLastSyncAt").isNull();
        row["ibkrLastSyncAt"] = hasSyncTime
            ? query.value("IBKRLastSyncAt").toDateTime().toString(Qt::ISODate)
            : QDateTime::currentDateTime().toString(Qt::ISODate);
        row["ibkrLastSyncAtOrigin"] = hasSyncTime ? QStringLiteral("IBKR") : QStringLiteral("mock");
        if (hasSyncTime && !query.value("ISIN").toString().trimmed().isEmpty())
            row["isinOrigin"] = QStringLiteral("IBKR");

        row["asOfDate"] = QDate::currentDate().toString(Qt::ISODate);
        row["fundamentalCurrency"] = row["currency"];
        row["marketCapitalization"] = marketCapitalization;
        row["enterpriseValue"] = marketCapitalization * mockValue(seed, 4, 0.9, 1.35);
        row["peRatio"] = peRatio;
        row["forwardPeRatio"] = peRatio * mockValue(seed, 12, 0.75, 1.05);
        row["priceToBookRatio"] = mockValue(seed, 4, 0.8, 8.0);
        row["priceToSalesRatio"] = priceToSales;
        row["priceToCashFlowRatio"] = mockValue(seed, 12, 4.0, 22.0);
        row["priceToDividendRatio"] = 100.0 / dividendYield;
        row["eps"] = price / peRatio;
        row["forwardEps"] = price / row["forwardPeRatio"].toDouble();
        row["dividendPerShare"] = price * dividendYield / 100.0;
        row["dividendYield"] = dividendYield;
        row["payoutRatio"] = row["dividendPerShare"].toDouble() / row["eps"].toDouble() * 100.0;
        row["beta"] = mockValue(seed, 20, 0.55, 1.8);
        row["revenue"] = revenue;
        row["netIncome"] = revenue * mockValue(seed, 2, 0.05, 0.22);
        row["ebitda"] = revenue * mockValue(seed, 10, 0.1, 0.3);
        row["returnOnEquity"] = mockValue(seed, 6, 4.0, 30.0);
        row["returnOnAssets"] = mockValue(seed, 14, 2.0, 18.0);
        row["debtToEquity"] = mockValue(seed, 18, 0.1, 2.5);
        row["sharesOutstanding"] = sharesOutstanding;
        row["week52High"] = price * mockValue(seed, 3, 1.05, 1.35);
        row["week52Low"] = price * mockValue(seed, 11, 0.55, 0.9);
        row["source"] = QStringLiteral("TEST");
        row["rawData"] = QStringLiteral("{\"environment\":\"mock\"}");
        row["fundamentalUpdatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        const QStringList mockFundamentalFields = {
            QStringLiteral("asOfDate"), QStringLiteral("fundamentalCurrency"),
            QStringLiteral("marketCapitalization"), QStringLiteral("enterpriseValue"),
            QStringLiteral("peRatio"), QStringLiteral("forwardPeRatio"),
            QStringLiteral("priceToBookRatio"), QStringLiteral("priceToSalesRatio"),
            QStringLiteral("priceToCashFlowRatio"), QStringLiteral("priceToDividendRatio"),
            QStringLiteral("eps"), QStringLiteral("forwardEps"),
            QStringLiteral("dividendPerShare"), QStringLiteral("dividendYield"),
            QStringLiteral("payoutRatio"), QStringLiteral("beta"),
            QStringLiteral("revenue"), QStringLiteral("netIncome"),
            QStringLiteral("ebitda"), QStringLiteral("returnOnEquity"),
            QStringLiteral("returnOnAssets"), QStringLiteral("debtToEquity"),
            QStringLiteral("sharesOutstanding"), QStringLiteral("week52High"),
            QStringLiteral("week52Low"), QStringLiteral("source"),
            QStringLiteral("rawData"), QStringLiteral("fundamentalUpdatedAt"),
            QStringLiteral("fundamentalYahooSymbol"), QStringLiteral("fundamentalExchange")
        };
        for (const QString &field : mockFundamentalFields)
            row[field + QStringLiteral("Origin")] = QStringLiteral("mock");

        const QHash<QString, QString> fundamentalColumns = {
            {QStringLiteral("asOfDate"), QStringLiteral("FundamentalAsOfDate")},
            {QStringLiteral("fundamentalCurrency"), QStringLiteral("FundamentalCurrency")},
            {QStringLiteral("marketCapitalization"), QStringLiteral("FundamentalMarketCapitalization")},
            {QStringLiteral("enterpriseValue"), QStringLiteral("FundamentalEnterpriseValue")},
            {QStringLiteral("peRatio"), QStringLiteral("FundamentalPERatio")},
            {QStringLiteral("forwardPeRatio"), QStringLiteral("FundamentalForwardPERatio")},
            {QStringLiteral("priceToBookRatio"), QStringLiteral("FundamentalPriceToBookRatio")},
            {QStringLiteral("priceToSalesRatio"), QStringLiteral("FundamentalPriceToSalesRatio")},
            {QStringLiteral("priceToCashFlowRatio"), QStringLiteral("FundamentalPriceToCashFlowRatio")},
            {QStringLiteral("priceToDividendRatio"), QStringLiteral("FundamentalPriceToDividendRatio")},
            {QStringLiteral("eps"), QStringLiteral("FundamentalEPS")},
            {QStringLiteral("forwardEps"), QStringLiteral("FundamentalForwardEPS")},
            {QStringLiteral("dividendPerShare"), QStringLiteral("FundamentalDividendPerShare")},
            {QStringLiteral("dividendYield"), QStringLiteral("FundamentalDividendYield")},
            {QStringLiteral("payoutRatio"), QStringLiteral("FundamentalPayoutRatio")},
            {QStringLiteral("beta"), QStringLiteral("FundamentalBeta")},
            {QStringLiteral("revenue"), QStringLiteral("FundamentalRevenue")},
            {QStringLiteral("netIncome"), QStringLiteral("FundamentalNetIncome")},
            {QStringLiteral("ebitda"), QStringLiteral("FundamentalEBITDA")},
            {QStringLiteral("returnOnEquity"), QStringLiteral("FundamentalReturnOnEquity")},
            {QStringLiteral("returnOnAssets"), QStringLiteral("FundamentalReturnOnAssets")},
            {QStringLiteral("debtToEquity"), QStringLiteral("FundamentalDebtToEquity")},
            {QStringLiteral("sharesOutstanding"), QStringLiteral("FundamentalSharesOutstanding")},
            {QStringLiteral("week52High"), QStringLiteral("FundamentalWeek52High")},
            {QStringLiteral("week52Low"), QStringLiteral("FundamentalWeek52Low")},
            {QStringLiteral("source"), QStringLiteral("FundamentalSource")},
            {QStringLiteral("rawData"), QStringLiteral("FundamentalRawData")},
            {QStringLiteral("fundamentalUpdatedAt"), QStringLiteral("FundamentalUpdatedAt")}
        };
        const QString fundamentalSource = query.value(QStringLiteral("FundamentalSource")).toString().trimmed();
        if (!fundamentalSource.isEmpty()) {
            for (auto it = fundamentalColumns.constBegin(); it != fundamentalColumns.constEnd(); ++it) {
                const QVariant value = query.value(it.value());
                row[it.key() + QStringLiteral("Origin")] = fundamentalSource;
                row[it.key()] = value.isNull() ? QVariant() : value;
            }
            const QString rawData = query.value(QStringLiteral("FundamentalRawData")).toString();
            const QString yahooSymbol = yahooSymbolFromRawData(rawData);
            if (!yahooSymbol.isEmpty()) {
                row[QStringLiteral("fundamentalYahooSymbol")] = yahooSymbol;
                row[QStringLiteral("fundamentalYahooSymbolOrigin")] = fundamentalSource;
                row[QStringLiteral("fundamentalExchange")] = yahooExchangeCodeFromSymbol(yahooSymbol);
                row[QStringLiteral("fundamentalExchangeOrigin")] = fundamentalSource;
            }
        }
        results.append(row);
    }

    return results;
}

bool DatabaseManager::isBoughtStock(const QString &symbol)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("SELECT 1 FROM \"BoughtStocks\" WHERE \"Symbol\" = :symbol LIMIT 1");
    query.bindValue(":symbol", symbol.trimmed());

    if (!query.exec()) {
        qCritical() << "Fehler beim Prüfen der gekauften Aktie:" << query.lastError().text();
        return false;
    }

    return query.next();
}

bool DatabaseManager::deleteBoughtStock(const QString &symbol)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM \"BoughtStocks\" WHERE \"Symbol\" = :symbol");
    query.bindValue(":symbol", symbol.trimmed());

    if (!query.exec()) {
        qCritical() << "Fehler beim Löschen der gekauften Aktie:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

bool DatabaseManager::saveBoughtStock(
    const QString &symbol,
    const QString &name,
    const QString &buyDate,
    const QString &sellDate,
    double currentValue,
    double entryValue,
    double valueIncreasePercent,
    int status)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    if (symbol.trimmed().isEmpty() || name.trimmed().isEmpty() || buyDate.trimmed().isEmpty()) {
        qWarning() << "Pflichtfelder für gekaufte Aktie fehlen.";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        INSERT INTO "BoughtStocks" (
            "Symbol",
            "Name",
            "BuyDate",
            "SellDate",
            "CurrentValue",
            "EntryValue",
            "ValueIncreasePercent",
            "Status"
        )
        VALUES (
            :symbol,
            :name,
            :buyDate,
            NULLIF(:sellDate, '')::date,
            :currentValue,
            :entryValue,
            :valueIncreasePercent,
            :status
        )
        ON CONFLICT ("Symbol") DO UPDATE SET
            "Name" = EXCLUDED."Name",
            "BuyDate" = EXCLUDED."BuyDate",
            "SellDate" = EXCLUDED."SellDate",
            "CurrentValue" = EXCLUDED."CurrentValue",
            "EntryValue" = EXCLUDED."EntryValue",
            "ValueIncreasePercent" = EXCLUDED."ValueIncreasePercent",
            "Status" = EXCLUDED."Status"
    )SQL");
    query.bindValue(":symbol", symbol.trimmed());
    query.bindValue(":name", name.trimmed());
    query.bindValue(":buyDate", buyDate.trimmed());
    query.bindValue(":sellDate", sellDate.trimmed());
    query.bindValue(":currentValue", currentValue);
    query.bindValue(":entryValue", entryValue);
    query.bindValue(":valueIncreasePercent", valueIncreasePercent);
    query.bindValue(":status", status);

    if (!query.exec()) {
        qCritical() << "Fehler beim Speichern der gekauften Aktie:" << query.lastError().text();
        return false;
    }

    return true;
}

QVariantList DatabaseManager::searchByTickerAndExchange(const QString &symbol, const QString &exchange) {
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return results;
    }

    QSqlQuery query(db);
    query.prepare("SELECT * FROM \"Stocks\" WHERE \"Symbol\" = :symbol ");
    query.bindValue(":symbol",symbol);

    if (!query.exec()) {
        qCritical() << "Query error:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap stock;
        stock["symbol"] = query.value("Symbol").toString();
        stock["name"] = query.value("Name").toString();
        stock["exchange"] = query.value("MIC").toString();
        stock["lastQuoteDate"] = query.value("LastQuoteDate").toString();
        stock["days5Success"] = query.value("5DaysSuccess");
        stock["days10Success"] = query.value("10DaysSuccess");
        stock["days20Success"] = query.value("20DaysSuccess");
        stock["days40Success"] = query.value("40DaysSuccess");
        stock["days5ValueInc"] = query.value("5DaysValueInc");
        stock["days10ValueInc"] = query.value("10DaysValueInc");
        stock["days20ValueInc"] = query.value("20DaysValueInc");
        stock["days40ValueInc"] = query.value("40DaysValueInc");
        stock["days5Volumen"] = query.value("5DaysVolumen");
        stock["days10Volumen"] = query.value("10DaysVolumen");
        stock["days20Volumen"] = query.value("20DaysVolumen");
        stock["days40Volumen"] = query.value("40DaysVolumen");
        results.append(stock);
        break;
    }

    return results;
}

QVariantMap DatabaseManager::extractStock(const QSqlQuery &query) {
    return {
            {"ticker", query.value("ticker")},
            {"name", query.value("name")},
            {"exchange", query.value("exchange")},
            {"currency", query.value("currency")},
            {"market_cap", query.value("market_cap")},
            {"sector", query.value("sector")},
            {"industry", query.value("industry")},
            };
}

void DatabaseManager::saveShares(const QList<ShareData>& shares) {
    for (const ShareData &share : shares) {
        saveShare(share);
    }
}

void DatabaseManager::updateShares(const QList<ShareData>& shares) {
    for (const ShareData &share : shares) {
        updateShare(share);
    }
}



// databasemanager.cpp
void DatabaseManager::saveShare(const ShareData &share) {
    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return;
    }

    QSqlQuery query(db);
    if (!query.prepare(
            "INSERT INTO \"Stocks\" (\"MIC\", \"ISIN\", \"Name\", \"Symbol\", \"Exchange\", \"CountryCode\", \"City\") "
            "VALUES (:mic, :name, :symbol, :exchange, :country_code, :city) "
            "ON CONFLICT (\"Symbol\") DO UPDATE "
            "SET \"Name\" = EXCLUDED.\"Name\", "
            "    \"Exchange\" = EXCLUDED.\"Exchange\", "
            "    \"CountryCode\" = EXCLUDED.\"CountryCode\", "
            "    \"City\" = EXCLUDED.\"City\";")) {
        qCritical() << "SQL-Fehler beim Vorbereiten:" << query.lastError().text();
        return;
    }
    query.bindValue(":mic", share.mic);
    query.bindValue(":isin", share.isin);
    query.bindValue(":symbol", share.symbol);
    query.bindValue(":name", share.name);
    query.bindValue(":exchange", share.exchange);
    query.bindValue(":country_code", share.countryCode);
    query.bindValue(":city", share.city);

    if (!query.exec()) {
        qCritical() << "❌ Fehler beim Speichern der Aktie:" << query.lastError().text();
        qCritical() << "Fehlerhafte Aktie:" << share.symbol << share.name;
        return;
    }
    qDebug() << "✅ Aktie gespeichert:" << share.symbol;
    emit saveComplete(share.symbol);  // jetzt eindeutig!
}

#include <QNetworkRequest>

QString DatabaseManager::getISINFromOpenFIGI(const QString &apiKey, const QString &ticker, const QString &exchangeCode) {
    QNetworkAccessManager manager;
    QUrl url("https://api.openfigi.com/v3/mapping");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-OPENFIGI-APIKEY", apiKey.toUtf8());

    // Erstelle den JSON Body
    QJsonArray jsonArray;
    QJsonObject query;
    query["idType"] = "TICKER";
    //query["idValue"] = ticker;
    //query["exchCode"] = exchangeCode;
    query["idValue"] = "APC";
    query["exchCode"] = "XETR";
    jsonArray.append(query);

    QJsonDocument doc(jsonArray);
    QByteArray payload = doc.toJson();

    // Senden der Anfrage synchron mit QEventLoop (Achtung: nicht im GUI-Thread)
    QNetworkReply *reply = manager.post(request, payload);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QString isin;
    if(reply->error() == QNetworkReply::NoError) {
        QByteArray response = reply->readAll();
        QJsonDocument responseDoc = QJsonDocument::fromJson(response);
        QJsonArray responseArray = responseDoc.array();
        if (!responseArray.isEmpty()) {
            QJsonObject obj = responseArray.first().toObject();
            if (obj.contains("data")) {
                QJsonArray dataArray = obj["data"].toArray();
                if (!dataArray.isEmpty()) {
                    QJsonObject dataObj = dataArray.first().toObject();
                    isin = dataObj.value("isin").toString();
                    qDebug() << "ISIN gefunden:" << isin;
                }
            }
        }
    } else {
        qDebug() << "Fehler bei OpenFIGI Anfrage:" << reply->errorString();
    }
    reply->deleteLater();
    return isin;
}


void DatabaseManager::updateAllISINs() {
    if (!db.isOpen()) {
        qWarning() << "⚠️ Datenbank nicht verbunden!";
        return;
    }

    const QString openFigiApiKey = "4b5256ce-2580-4e19-b746-d599b0dcffd0"; // 👉 Ersetze durch deinen API-Key

    QSqlQuery query(db);
    if (!query.exec(R"SQL(
        SELECT "Symbol", "MIC"
        FROM "Stocks"
        WHERE "ISIN" IS NULL OR "ISIN" = ''
    )SQL")) {
        qCritical() << "❌ Fehler beim Abrufen leerer ISINs:" << query.lastError().text();
        return;
    }

    int updatedCount = 0;
    int totalCount = 0;

    while (query.next()) {
        totalCount++;
        QString symbol = query.value("Symbol").toString();
        QString mic = query.value("MIC").toString();

        // OpenFIGI verwendet z. B. "XETR", "XFRA" usw.
        QString isin = getISINFromOpenFIGI(openFigiApiKey, symbol.section('.', 0, 0), mic); // Symbol ohne ".MIC"

        if (!isin.isEmpty()) {
            QSqlQuery updateQuery(db);
            updateQuery.prepare(R"SQL(
                UPDATE "Stocks"
                SET "ISIN" = :isin
                WHERE "Symbol" = :symbol
            )SQL");
            updateQuery.bindValue(":isin", isin);
            updateQuery.bindValue(":symbol", symbol);

            if (!updateQuery.exec()) {
                qWarning() << "❌ Fehler beim Aktualisieren der ISIN für" << symbol << ":" << updateQuery.lastError().text();
            } else {
                updatedCount++;
                qDebug() << "✅ ISIN aktualisiert für" << symbol << ":" << isin;
            }
        } else {
            qDebug() << "⚠️ Keine ISIN gefunden für" << symbol;
        }
    }

    qDebug() << QString("🏁 ISIN-Update abgeschlossen: %1 von %2 Aktien aktualisiert.").arg(updatedCount).arg(totalCount);
}

QString DatabaseManager::convertToEodTicker(const QString& symbol) {
    QString ticker = symbol;

    static const QMap<QString, QString> exchangeMap = {
        {"XFRA", "F"},
        {"XETR", "DE"},
        {"XNAS", "US"},
        {"XNYS", "US"},
        // weitere falls nötig
    };

    QString exch = symbol.section('.', 1, 1);
    if (exchangeMap.contains(exch)) {
        ticker = symbol.section('.', 0, 0) + "." + exchangeMap.value(exch);
    }

    return ticker;
}



void DatabaseManager::updateShare(const ShareData &share) {
    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return;
    }

    QSqlQuery query(db);
    if (!query.prepare(
            "UPDATE \"Stocks\" "
            "SET \"ISIN\" = :isin "
            "WHERE \"Symbol\" = :symbol")) {
        qCritical() << "SQL-Fehler beim Vorbereiten:" << query.lastError().text();
        return;
    }

    query.bindValue(":isin", share.isin);
    query.bindValue(":symbol", share.symbol);

    if (!query.exec()) {
        qCritical() << "❌ Fehler beim Aktualisieren der ISIN:" << query.lastError().text();
        qCritical() << "Symbol:" << share.symbol << " | ISIN:" << share.isin;
        return;
    }

    if (query.numRowsAffected() == 0) {
        qWarning() << "⚠️ Kein Eintrag aktualisiert – Symbol nicht gefunden:" << share.symbol;
    } else {
        qDebug() << "✅ ISIN aktualisiert für Symbol:" << share.symbol;
    }
}


void DatabaseManager::createQuotesForStock(const QString symbol, const QString exchange) {
    qDebug() << "🟢 Starte Verarbeitung für Stock:" << symbol << "| Exchange:" << exchange;

    // Trenne alle bestehenden Verbindungen für historicalDataReceived
    disconnect(&marketStackClient, &MarketStackClient::historicalDataReceived, this, nullptr);

    connect(&marketStackClient, &MarketStackClient::errorOccurred, this, [](const QString error) {
        qDebug() << "❌ Fehler beim Abrufen der historischen Daten:" << error;
    });

    // Letztes gespeichertes Datum abrufen
    QSqlQuery query(db);
    query.prepare("SELECT Max(\"CloseDate\") AS LastCloseDate FROM \"Quotes\" WHERE \"Symbol\" = :symbol");
    query.bindValue(":symbol", symbol);

    if (!query.exec()) {
        qDebug() << "❌ Fehler beim Abrufen des LastUpdateDate:" << query.lastError().text();
        emit saveComplete(symbol); // Signal auslösen, um die Verarbeitung abzuschließen
        return;
    }

    int limit = 1000; // Standardwert, wenn 1 Jahr zurück
    QDate fromDate = QDate::currentDate().addYears(-1); // Standardwert: 1 Jahr zurück
    if (query.next()) {
        QVariant lastQuoteDate = query.value("LastCloseDate");
        if (!lastQuoteDate.isNull()) {
            fromDate = lastQuoteDate.toDate();
            qDebug() << "🟠 Letztes gespeichertes Datum gefunden. Limit auf" << limit << "gesetzt.";
        }
    }

    // Verbinde das Signal mit einer Lambda-Funktion, die den aktuellen Stock verarbeitet
    connect(&marketStackClient, &MarketStackClient::historicalDataReceived, this, [this, symbol, exchange, dbCopy = QSqlDatabase::database()](QMap<QString, QVariantMap> data) mutable {
        qDebug() << "🔵 Empfangene historische Daten für Stock:" << symbol << "| Anzahl der Datensätze:" << data.size();

        if (data.isEmpty()) {
            qDebug() << "⚠️ Keine historischen Daten verfügbar für Stock:" << symbol;
            emit saveComplete(symbol); // Signal auslösen, um die Verarbeitung abzuschließen
            return;
        }

        QList<QDate> closeDates;
        QList<double> closePrices;
        QList<double> volumes;
        QDate latestDate;

        QSqlQuery insertQuery(dbCopy);
        insertQuery.prepare("INSERT INTO \"Quotes\" (\"Symbol\", \"CloseDate\", \"ClosePrice\", \"OpenPrice\", \"HighestPrice\", \"LowestPrice\", \"Volume\") "
                            "VALUES (:symbol, :closeDate, :closePrice, :openPrice, :highestPrice, :lowestPrice, :volume) "
                            "ON CONFLICT (\"Symbol\", \"CloseDate\") DO NOTHING");

        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            QString dateOnly = it.key().split("T").first();
            QDate closeDate = QDate::fromString(dateOnly, "yyyy-MM-dd");
            if (!closeDate.isValid()) {
                qDebug() << "⚠️ Ungültiges Datum gefunden:" << dateOnly;
                continue;
            }

            insertQuery.bindValue(":symbol", symbol);
            insertQuery.bindValue(":closeDate", closeDate);
            insertQuery.bindValue(":closePrice", it.value()["close"]);
            insertQuery.bindValue(":openPrice", it.value()["open"]);
            insertQuery.bindValue(":highestPrice", it.value()["high"]);
            insertQuery.bindValue(":lowestPrice", it.value()["low"]);
            insertQuery.bindValue(":volume", it.value()["volume"]);
            latestDate = closeDate;
            if (!insertQuery.exec()) {
                qDebug() << "❌ Fehler beim Einfügen des Quotes:" << insertQuery.lastError().text();
            }
            closeDates.append(closeDate);
            closePrices.append(it.value()["close"].toDouble());
            volumes.append(it.value()["volume"].toDouble());
        }
        QSqlQuery updateQuery(db);
        if (!updateQuery.prepare(
                "UPDATE \"Stocks\" "
                "SET \"LastUpdateDate\" = :lastUpdateDate "
                "WHERE \"Symbol\" = :symbol")) {
            qCritical() << "SQL-Fehler beim Vorbereiten:" << updateQuery.lastError().text();
            return;
        }

        updateQuery.bindValue(":lastUpdateDate", QDate::currentDate());
        updateQuery.bindValue(":symbol", symbol);

        if (!updateQuery.exec()) {
            qCritical() << "❌ Fehler beim Aktualisieren des aktualisierungs Datum:" << updateQuery.lastError().text();
            return;
        }

        emit saveComplete(symbol); // Signal auslösen, um die Verarbeitung abzuschließen
    });

    qDebug() << "API-Anfrage für Stock:" << symbol << "| Exchange:" << exchange << "| Von:" << fromDate.toString("yyyy-MM-dd") << "| Limit:" << limit;
    marketStackClient.fetchHistoricalData(symbol, exchange, fromDate, limit);
}

#include <QEventLoop>

void DatabaseManager::generateQuoteForStock(const QString symbol, const QString exchange) {

    // **Erstelle eine EventLoop, um auf den Abschluss der Verarbeitung zu warten**
    QEventLoop loop;
    connect(this, &DatabaseManager::saveComplete, &loop, &QEventLoop::quit);

    // **Rufe die Methode auf**
    createQuotesForStock(symbol, exchange);
}

void DatabaseManager::generateQuotesForAllStocks() {
    if (!db.isOpen()) {
        qCritical() << "❌ Fehler: Datenbank ist nicht verbunden!";
        return;
    }

    QSqlQuery query(db);
    query.prepare("SELECT \"Symbol\", \"MIC\" FROM \"Stocks\"  where \"LastUpdateDate\" !=  CURRENT_DATE;" );

    if (!query.exec()) {
        qCritical() << "❌ Fehler beim Abrufen der Stocks-Liste: " << query.lastError().text();
        return;
    }

    int stockCounter = 0;

    while (query.next() ) {
        QString symbol = query.value("Symbol").toString();
        QString mic = query.value("MIC").toString();

        qDebug() << "📈 [" << ++stockCounter << "] Verarbeite Stock:" << symbol << " | MIC:" << mic;

        // **Erstelle eine EventLoop, um auf den Abschluss der Verarbeitung zu warten**
        QEventLoop loop;
        connect(this, &DatabaseManager::saveComplete, &loop, &QEventLoop::quit);

        // **Rufe die Methode auf**
        createQuotesForStock(symbol, mic);

        // **Warte, bis `saveComplete()` gesendet wird**
        loop.exec();

    }

    qDebug() << "✅ Verarbeitung aller Stocks abgeschlossen. Gesamtzahl: " << stockCounter;
}

QVariantList DatabaseManager::getQuoteDetails(const QString &symbol, int fromDay, int toDay)
{
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return results;
    }

    if (symbol.trimmed().isEmpty() || fromDay < 1 || toDay < fromDay) {
        qWarning() << "Ungültige Parameter für Kursdetails:" << symbol << fromDay << toDay;
        return results;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        WITH symbol_median AS (
            SELECT percentile_cont(0.5) WITHIN GROUP (ORDER BY "ClosePrice"::double precision) AS median_close
            FROM "Quotes"
            WHERE "Symbol" = :symbol
              AND COALESCE("Volume", 0) > 0
              AND COALESCE("ClosePrice", 0) > 0
        ),
        ordered_quotes AS (
            SELECT
                "Symbol",
                "CloseDate",
                "OpenPrice",
                "ClosePrice",
                "HighestPrice",
                "LowestPrice",
                "Volume",
                ROW_NUMBER() OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" DESC) AS dayIndex,
                LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS previousClosePrice
            FROM "Quotes", symbol_median
            WHERE "Symbol" = :symbol
              AND COALESCE("Volume", 0) > 0
              AND COALESCE("ClosePrice", 0) > 0
              AND (
                  symbol_median.median_close IS NULL
                  OR "ClosePrice"::double precision BETWEEN symbol_median.median_close / 20.0
                                                        AND symbol_median.median_close * 20.0
              )
        )
        SELECT
            dayIndex,
            TO_CHAR("CloseDate", 'DD.MM.YYYY') AS closeDate,
            "OpenPrice" AS openPrice,
            "ClosePrice" AS closePrice,
            "HighestPrice" AS highestPrice,
            "LowestPrice" AS lowestPrice,
            "Volume" AS volume,
            ROUND((("ClosePrice" - previousClosePrice) / NULLIF(previousClosePrice, 0) * 100)::numeric, 2) AS changePercent
        FROM ordered_quotes
        WHERE dayIndex BETWEEN :fromDay AND :toDay
        ORDER BY dayIndex ASC
    )SQL");
    query.bindValue(":symbol", symbol);
    query.bindValue(":fromDay", fromDay);
    query.bindValue(":toDay", toDay);

    if (!query.exec()) {
        qCritical() << "SQL-Fehler bei Kursdetails:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.insert(query.record().fieldName(i), query.value(i));
        }
        results << row;
    }

    return results;
}

QVariantList DatabaseManager::getStockAnalysisResults(double minIncreasePercent)
{
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return results;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        WITH quote_medians AS (
            SELECT
                "Symbol",
                percentile_cont(0.5) WITHIN GROUP (ORDER BY "ClosePrice"::double precision) AS median_close
            FROM "Quotes"
            WHERE COALESCE("Volume", 0) > 0
              AND COALESCE("ClosePrice", 0) > 0
            GROUP BY "Symbol"
        ),
        recent_quotes AS (
            SELECT
                q.*,
                ROW_NUMBER() OVER (PARTITION BY q."Symbol" ORDER BY q."CloseDate" DESC) AS rn_desc
            FROM "Quotes" q
            INNER JOIN quote_medians qm ON qm."Symbol" = q."Symbol"
            WHERE COALESCE(q."Volume", 0) > 0
              AND COALESCE(q."ClosePrice", 0) > 0
              AND q."ClosePrice"::double precision BETWEEN qm.median_close / 20.0
                                                       AND qm.median_close * 20.0
        ),
        quote_summary AS (
            SELECT
                "Symbol",
                MIN("CloseDate") AS first_date,
                MAX("CloseDate") AS last_date,
                COUNT(*) AS quote_count
            FROM recent_quotes
            WHERE rn_desc <= 90
            GROUP BY "Symbol"
        ),
        turnover_summary AS (
            SELECT
                "Symbol",
                SUM(COALESCE("Volume", 0) * COALESCE("ClosePrice", 0)) AS total_turnover,
                COUNT(*) AS total_quote_count
            FROM recent_quotes
            GROUP BY "Symbol"
        ),
        latest_fundamentals AS (
            SELECT DISTINCT ON ("Symbol")
                "Symbol",
                "Revenue",
                "PERatio"
            FROM "StockFundamentals"
            ORDER BY "Symbol", "AsOfDate" DESC NULLS LAST, "UpdatedAt" DESC NULLS LAST
        )
        SELECT
            s."Symbol" AS symbol,
            s."ISIN" AS isin,
            s."Name" AS name,
            s."MIC" AS mic,
            CASE
                WHEN COALESCE(s.use_marketstack, false) THEN 'MS'
                WHEN COALESCE(s."IBKRQuoteExchange", '') <> '' THEN 'IBKR'
                ELSE '-'
            END AS quotesource,
            ROUND(((last_quote."ClosePrice" - first_quote."ClosePrice")
                / NULLIF(first_quote."ClosePrice", 0) * 100)::numeric, 2) AS increasepercent,
            TO_CHAR(first_quote."CloseDate", 'DD.MM.YYYY') AS firstquotedate,
            first_quote."ClosePrice" AS firstcloseprice,
            TO_CHAR(last_quote."CloseDate", 'DD.MM.YYYY') AS lastquotedate,
            last_quote."ClosePrice" AS lastcloseprice,
            turnover_summary.total_turnover AS periodturnover,
            turnover_summary.total_quote_count AS totalquotecount,
            quote_summary.quote_count AS quotecount,
            latest_fundamentals."Revenue" AS revenue,
            latest_fundamentals."PERatio" AS peratio
        FROM quote_summary
        INNER JOIN "Stocks" s ON s."Symbol" = quote_summary."Symbol"
        INNER JOIN turnover_summary ON turnover_summary."Symbol" = quote_summary."Symbol"
        INNER JOIN "Quotes" first_quote
            ON first_quote."Symbol" = quote_summary."Symbol"
           AND first_quote."CloseDate" = quote_summary.first_date
        INNER JOIN "Quotes" last_quote
            ON last_quote."Symbol" = quote_summary."Symbol"
           AND last_quote."CloseDate" = quote_summary.last_date
        LEFT JOIN latest_fundamentals ON latest_fundamentals."Symbol" = s."Symbol"
        WHERE quote_summary.quote_count >= 2
          AND NOT COALESCE(s.use_marketstack, false)
          AND COALESCE(s."IBKRQuoteExchange", '') <> ''
          AND ((last_quote."ClosePrice" - first_quote."ClosePrice")
                / NULLIF(first_quote."ClosePrice", 0) * 100) >= :minIncreasePercent
        ORDER BY increasepercent DESC NULLS LAST, periodturnover DESC NULLS LAST
        LIMIT 500
    )SQL");
    query.bindValue(QStringLiteral(":minIncreasePercent"), minIncreasePercent);

    if (!query.exec()) {
        qCritical() << "SQL-Fehler bei Stock-Analyse:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.insert(query.record().fieldName(i), query.value(i));
        }
        results << row;
    }

    return results;
}

QVariantList DatabaseManager::getStockAnalysisIbkrSymbols()
{
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return results;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT s."Symbol"
        FROM "Stocks" s
        WHERE NOT COALESCE(s.use_marketstack, false)
          AND COALESCE(s."IBKRQuoteExchange", '') <> ''
          AND EXISTS (
              SELECT 1
              FROM "Quotes" q
              WHERE q."Symbol" = s."Symbol"
                AND COALESCE(q."Volume", 0) > 0
                AND COALESCE(q."ClosePrice", 0) > 0
          )
        ORDER BY s."Symbol"
    )SQL");

    if (!query.exec()) {
        qCritical() << "SQL-Fehler bei Stock-Analyse-Symbolen:" << query.lastError().text();
        return results;
    }

    while (query.next())
        results << query.value(0).toString();

    return results;
}

QVariantMap DatabaseManager::getStockAnalysisCandidate(const QString &symbol, double minIncreasePercent)
{
    QVariantMap result;

    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return result;
    }

    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty())
        return result;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        WITH quote_median AS (
            SELECT
                percentile_cont(0.5) WITHIN GROUP (ORDER BY "ClosePrice"::double precision) AS median_close
            FROM "Quotes"
            WHERE "Symbol" = :symbol
              AND COALESCE("Volume", 0) > 0
              AND COALESCE("ClosePrice", 0) > 0
        ),
        recent_quotes AS (
            SELECT
                q.*,
                ROW_NUMBER() OVER (PARTITION BY q."Symbol" ORDER BY q."CloseDate" DESC) AS rn_desc
            FROM "Quotes" q, quote_median
            WHERE q."Symbol" = :symbol
              AND COALESCE(q."Volume", 0) > 0
              AND COALESCE(q."ClosePrice", 0) > 0
              AND q."ClosePrice"::double precision BETWEEN quote_median.median_close / 20.0
                                                       AND quote_median.median_close * 20.0
        ),
        quote_summary AS (
            SELECT
                "Symbol",
                MIN("CloseDate") AS first_date,
                MAX("CloseDate") AS last_date,
                COUNT(*) AS quote_count
            FROM recent_quotes
            WHERE rn_desc <= 90
            GROUP BY "Symbol"
        ),
        turnover_summary AS (
            SELECT
                "Symbol",
                SUM(COALESCE("Volume", 0) * COALESCE("ClosePrice", 0)) AS total_turnover,
                COUNT(*) AS total_quote_count
            FROM recent_quotes
            GROUP BY "Symbol"
        ),
        latest_fundamentals AS (
            SELECT DISTINCT ON ("Symbol")
                "Symbol",
                "Revenue",
                "PERatio"
            FROM "StockFundamentals"
            WHERE "Symbol" = :symbol
            ORDER BY "Symbol", "AsOfDate" DESC NULLS LAST, "UpdatedAt" DESC NULLS LAST
        )
        SELECT
            s."Symbol" AS symbol,
            s."ISIN" AS isin,
            s."Name" AS name,
            s."MIC" AS mic,
            'IBKR' AS quotesource,
            ROUND(((last_quote."ClosePrice" - first_quote."ClosePrice")
                / NULLIF(first_quote."ClosePrice", 0) * 100)::numeric, 2) AS increasepercent,
            TO_CHAR(first_quote."CloseDate", 'DD.MM.YYYY') AS firstquotedate,
            first_quote."ClosePrice" AS firstcloseprice,
            TO_CHAR(last_quote."CloseDate", 'DD.MM.YYYY') AS lastquotedate,
            last_quote."ClosePrice" AS lastcloseprice,
            turnover_summary.total_turnover AS periodturnover,
            turnover_summary.total_quote_count AS totalquotecount,
            quote_summary.quote_count AS quotecount,
            latest_fundamentals."Revenue" AS revenue,
            latest_fundamentals."PERatio" AS peratio
        FROM quote_summary
        INNER JOIN "Stocks" s ON s."Symbol" = quote_summary."Symbol"
        INNER JOIN turnover_summary ON turnover_summary."Symbol" = quote_summary."Symbol"
        INNER JOIN "Quotes" first_quote
            ON first_quote."Symbol" = quote_summary."Symbol"
           AND first_quote."CloseDate" = quote_summary.first_date
        INNER JOIN "Quotes" last_quote
            ON last_quote."Symbol" = quote_summary."Symbol"
           AND last_quote."CloseDate" = quote_summary.last_date
        LEFT JOIN latest_fundamentals ON latest_fundamentals."Symbol" = s."Symbol"
        WHERE quote_summary.quote_count >= 2
          AND NOT COALESCE(s.use_marketstack, false)
          AND COALESCE(s."IBKRQuoteExchange", '') <> ''
          AND ((last_quote."ClosePrice" - first_quote."ClosePrice")
                / NULLIF(first_quote."ClosePrice", 0) * 100) >= :minIncreasePercent
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    query.bindValue(QStringLiteral(":minIncreasePercent"), minIncreasePercent);

    if (!query.exec()) {
        qCritical() << "SQL-Fehler bei Stock-Analyse-Kandidat:" << query.lastError().text() << normalizedSymbol;
        return result;
    }

    if (query.next()) {
        for (int i = 0; i < query.record().count(); ++i)
            result.insert(query.record().fieldName(i), query.value(i));
    }

    return result;
}

QVariantList DatabaseManager::runShareQuery(const QString& sql)
{
    QVariantList results;

    QSqlQuery query(db);
    if (!query.exec(sql)) {
        qCritical() << "❌ SQL-Fehler:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < query.record().count(); ++i) {
            row.insert(query.record().fieldName(i), query.value(i));
        }
        results << row;
    }

    return results;
}


QVariantList DatabaseManager::getShares(
    int firstTo, int firstThreshold, bool firstGreaterThan,
    int secondTo, int secondThreshold, bool secondGreaterThan,
    int thirdTo, int thirdThreshold, bool thirdGreaterThan,
    int fourthTo, int fourthThreshold, bool fourthGreaterThan,
    int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol)
{
    QString sql = buildShareQuery(
        firstTo, firstThreshold, firstGreaterThan,
        secondTo, secondThreshold, secondGreaterThan,
        thirdTo, thirdThreshold, thirdGreaterThan,
        fourthTo, fourthThreshold, fourthGreaterThan,
        greaterThanSalesPrice, sortPeriod, sortAsc, symbol
        );
    return runShareQuery(sql);
}

void DatabaseManager::getSharesAsync(
    int firstTo, int firstThreshold, bool firstGreaterThan,
    int secondTo, int secondThreshold, bool secondGreaterThan,
    int thirdTo, int thirdThreshold, bool thirdGreaterThan,
    int fourthTo, int fourthThreshold, bool fourthGreaterThan,
    int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol)
{
    (void) QtConcurrent::run([=]() {
        QString sql = buildShareQuery(
            firstTo, firstThreshold, firstGreaterThan,
            secondTo, secondThreshold, secondGreaterThan,
            thirdTo, thirdThreshold, thirdGreaterThan,
            fourthTo, fourthThreshold, fourthGreaterThan,
            greaterThanSalesPrice, sortPeriod, sortAsc, symbol
            );

        QVariantList results = runShareQuery(sql);

        QMetaObject::invokeMethod(this, [=]() {
                emit getSharesComplete(results);
            }, Qt::QueuedConnection);
    });
}

void DatabaseManager::getSharesByNameAsync(
    int firstTo, int firstThreshold, bool firstGreaterThan,
    int secondTo, int secondThreshold, bool secondGreaterThan,
    int thirdTo, int thirdThreshold, bool thirdGreaterThan,
    int fourthTo, int fourthThreshold, bool fourthGreaterThan,
    int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& name)
{
    (void) QtConcurrent::run([=]() {
        QString sql = buildShareQuery(
            firstTo, firstThreshold, firstGreaterThan,
            secondTo, secondThreshold, secondGreaterThan,
            thirdTo, thirdThreshold, thirdGreaterThan,
            fourthTo, fourthThreshold, fourthGreaterThan,
            greaterThanSalesPrice, sortPeriod, sortAsc, "", name
            );

        QVariantList results = runShareQuery(sql);

        QMetaObject::invokeMethod(this, [=]() {
                emit getSharesComplete(results);
            }, Qt::QueuedConnection);
    });
}



QString DatabaseManager::buildShareQuery(
    int firstTo, int firstThreshold, bool firstGreaterThan,
    int secondTo, int secondThreshold, bool secondGreaterThan,
    int thirdTo, int thirdThreshold, bool thirdGreaterThan,
    int fourthTo, int fourthThreshold, bool fourthGreaterThan,
    int greaterThanSalesPrice, int sortPeriod, bool sortAsc, const QString& symbol, const QString& name)
{
    bool isSymbolMode = !symbol.isNull() && !symbol.trimmed().isEmpty();
    bool isNameMode = !isSymbolMode && !name.isNull() && !name.trimmed().isEmpty();

    QString sqlTemplate = R"SQL(
            WITH
            ordered_quotes AS (
                SELECT *, ROW_NUMBER() OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" DESC) AS rn_desc
                FROM "Quotes"
                WHERE 1=1
                %13
            ),
            quotes_q1 AS (SELECT * FROM ordered_quotes WHERE rn_desc <= %1),
            quotes_q2 AS (SELECT * FROM ordered_quotes WHERE rn_desc > %2 AND rn_desc <= %3),
            quotes_q3 AS (SELECT * FROM ordered_quotes WHERE rn_desc > %4 AND rn_desc <= %5),
            quotes_q4 AS (SELECT * FROM ordered_quotes WHERE rn_desc > %6 AND rn_desc <= %7),

            quotes_q1_lag AS (
                SELECT *, LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS prev_close
                FROM quotes_q1
            ),
            quotes_q2_lag AS (
                SELECT *, LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS prev_close
                FROM quotes_q2
            ),
            quotes_q3_lag AS (
                SELECT *, LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS prev_close
                FROM quotes_q3
            ),
            quotes_q4_lag AS (
                SELECT *, LAG("ClosePrice") OVER (PARTITION BY "Symbol" ORDER BY "CloseDate" ASC) AS prev_close
                FROM quotes_q4
            ),

            q1 AS (
                SELECT "Symbol",
                    COUNT(*) FILTER (WHERE prev_close IS NOT NULL AND "ClosePrice" > prev_close) AS daysSuccess,
                    (MAX("ClosePrice") - MIN("ClosePrice")) / NULLIF(MIN("ClosePrice"), 0) * 100 AS valueInc,
                    SUM("Volume") AS volumeSum,
                    SUM("ClosePrice") AS closeSum,
                    SUM("Volume") * SUM("ClosePrice") AS volumePrice
                FROM quotes_q1_lag
                WHERE rn_desc <= %8
                GROUP BY "Symbol"
            ),
            q2 AS (
                SELECT "Symbol",
                    COUNT(*) FILTER (WHERE prev_close IS NOT NULL AND "ClosePrice" > prev_close) AS daysSuccess,
                    (MAX("ClosePrice") - MIN("ClosePrice")) / NULLIF(MIN("ClosePrice"), 0) * 100 AS valueInc,
                    SUM("Volume") AS volumeSum,
                    SUM("ClosePrice") AS closeSum,
                    SUM("Volume") * SUM("ClosePrice") AS volumePrice
                FROM quotes_q2_lag
                WHERE rn_desc > %2 AND rn_desc <= %9
                GROUP BY "Symbol"
            ),
            q3 AS (
                SELECT "Symbol",
                    COUNT(*) FILTER (WHERE prev_close IS NOT NULL AND "ClosePrice" > prev_close) AS daysSuccess,
                    (MAX("ClosePrice") - MIN("ClosePrice")) / NULLIF(MIN("ClosePrice"), 0) * 100 AS valueInc,
                    SUM("Volume") AS volumeSum,
                    SUM("ClosePrice") AS closeSum,
                    SUM("Volume") * SUM("ClosePrice") AS volumePrice
                FROM quotes_q3_lag
                WHERE rn_desc > %4 AND rn_desc <= %10
                GROUP BY "Symbol"
            ),
            q4 AS (
                SELECT "Symbol",
                    COUNT(*) FILTER (WHERE prev_close IS NOT NULL AND "ClosePrice" > prev_close) AS daysSuccess,
                    (MAX("ClosePrice") - MIN("ClosePrice")) / NULLIF(MIN("ClosePrice"), 0) * 100 AS valueInc,
                    SUM("Volume") AS volumeSum,
                    SUM("ClosePrice") AS closeSum,
                    SUM("Volume") * SUM("ClosePrice") AS volumePrice
                FROM quotes_q4_lag
                WHERE rn_desc > %6 AND rn_desc <= %11
                GROUP BY "Symbol"
            ),

            "last_close" AS (
                SELECT q."Symbol", q."ClosePrice" AS lastClosePrice,  q."CloseDate" AS lastClosePriceDate
                FROM "Quotes" q
                INNER JOIN (
                    SELECT "Symbol", MAX("CloseDate") AS maxDate
                    FROM "Quotes"
                    GROUP BY "Symbol"
                ) latest ON q."Symbol" = latest."Symbol" AND q."CloseDate" = latest.maxDate
            )

            SELECT
                s."Symbol", s."MIC", s."Name",
                TO_CHAR(s."LastUpdateDate", 'DD.MM.YYYY') AS "LastUpdateDate",

                q1.daysSuccess AS daysFirstPeriodSuccess,
                ROUND(q1.valueInc, 2) AS firstPeriodValueInc,
                q1.volumeSum AS firstPeriodVolume,
                q1.volumePrice AS firstPeriodVolumePrice,

                q2.daysSuccess AS daysSecondPeriodSuccess,
                ROUND(q2.valueInc, 2) AS secondPeriodValueInc,
                q2.volumeSum AS secondPeriodVolume,
                q2.volumePrice AS secondPeriodVolumePrice,

                q3.daysSuccess AS daysThirdPeriodSuccess,
                ROUND(q3.valueInc, 2) AS thirdPeriodValueInc,
                q3.volumeSum AS thirdPeriodVolume,
                q3.volumePrice AS thirdPeriodVolumePrice,

                q4.daysSuccess AS daysFourthPeriodSuccess,
                ROUND(q4.valueInc, 2) AS fourthPeriodValueInc,
                q4.volumeSum AS fourthPeriodVolume,
                q4.volumePrice AS fourthPeriodVolumePrice,

                "last_close".lastClosePrice     AS lastClosePrice,
                TO_CHAR("last_close".lastClosePriceDate, 'DD.MM.YYYY') AS lastClosePriceDate

            FROM "Stocks" s
            LEFT JOIN q1 ON s."Symbol" = q1."Symbol"
            LEFT JOIN q2 ON s."Symbol" = q2."Symbol"
            LEFT JOIN q3 ON s."Symbol" = q3."Symbol"
            LEFT JOIN q4 ON s."Symbol" = q4."Symbol"
            LEFT JOIN "last_close" ON s."Symbol" = "last_close"."Symbol"
            WHERE 1=1
            %12
        )SQL";

    QString filterClause;
    QString quotesFilterClause;
    if (isSymbolMode) {
        QString escapedSymbol = symbol;
        escapedSymbol.replace("'", "''");
        filterClause = QString(" AND s.\"Symbol\" = '%1' LIMIT 1").arg(escapedSymbol);
        quotesFilterClause = QString(" AND \"Symbol\" = '%1'").arg(escapedSymbol);

    } else {
        if (isNameMode) {
            QString escapedName = name.trimmed();
            escapedName.replace("'", "''");
            filterClause = QString(" AND s.\"Name\" ILIKE '%%' || '%1' || '%%'").arg(escapedName);
            quotesFilterClause = QString(R"SQL(
                AND "Symbol" IN (
                    SELECT "Symbol"
                    FROM "Stocks"
                    WHERE "Name" ILIKE '%%' || '%1' || '%%'
                )
            )SQL").arg(escapedName);
            filterClause += " ORDER BY s.\"Name\" ASC";
        } else {

            //filterClause = QString(" AND (s.\"Symbol\" = 'R9GA.XFRA' OR s.\"Symbol\" = 'H6F.XFRA') AND q1.volumePrice > %1").arg(greaterThanSalesPrice);
            filterClause += QString(" AND q1.volumePrice > %1").arg(greaterThanSalesPrice);
            if (firstThreshold > 0)
                filterClause += QString(" AND q1.daysSuccess %1 %2")
                                    .arg(firstGreaterThan ? ">" : "<")
                                    .arg(firstThreshold);
            if (secondThreshold > 0)
                filterClause += QString(" AND q2.daysSuccess %1 %2")
                                    .arg(secondGreaterThan ? ">" : "<")
                                    .arg(secondThreshold);
            if (thirdThreshold > 0)
                filterClause += QString(" AND q3.daysSuccess %1 %2")
                                    .arg(thirdGreaterThan ? ">" : "<")
                                    .arg(thirdThreshold);
            if (fourthThreshold > 0)
                filterClause += QString(" AND q4.daysSuccess %1 %2")
                                    .arg(fourthGreaterThan ? ">" : "<")
                                    .arg(fourthThreshold);

            QString orderDirection = sortAsc ? "ASC" : "DESC";

            switch (sortPeriod) {
            case 1:
                filterClause += QString(" ORDER BY q1.daysSuccess %1").arg(orderDirection);
                break;
            case 2:
                filterClause += QString(" ORDER BY q2.daysSuccess %1").arg(orderDirection);
                break;
            case 3:
                filterClause += QString(" ORDER BY q3.daysSuccess %1").arg(orderDirection);
                break;
            case 4:
                filterClause += QString(" ORDER BY q4.daysSuccess %1").arg(orderDirection);
                break;
            default:
                filterClause += ""; // keine Sortierung
                break;
            }
        }
    }

    QString sql = sqlTemplate
                      .arg(firstTo + 1)
                      .arg(firstTo)
                      .arg(secondTo + 1)
                      .arg(secondTo)
                      .arg(thirdTo + 1)
                      .arg(thirdTo)
                      .arg(fourthTo + 1)
                      .arg(firstTo)
                      .arg(secondTo)
                      .arg(thirdTo)
                      .arg(fourthTo)
                      .arg(filterClause)
                      .arg(quotesFilterClause);

    qDebug().noquote() << "\n[DEBUG] Generiertes SQL:\n" << sql;
    return sql;
}

