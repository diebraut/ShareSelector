#include "databasemanager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>

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

QVariantList DatabaseManager::getQuoteDetailsForTradingDays(const QString &symbol, int tradingDays)
{
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return results;
    }

    const QString normalizedSymbol = symbol.trimmed();
    const int boundedTradingDays = qBound(20, tradingDays, 90);
    if (normalizedSymbol.isEmpty()) {
        qWarning() << "UngÃ¼ltiges Symbol fÃ¼r Kursdetails nach Handelstagen:" << symbol;
        return results;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        WITH symbol_median AS (
            SELECT percentile_cont(0.5) WITHIN GROUP (ORDER BY "ClosePrice"::double precision) AS median_close
            FROM "Quotes"
            WHERE "Symbol" = :symbol
              AND COALESCE("ClosePrice", 0) > 0
        ),
        valid_quotes AS (
            SELECT q.*
            FROM "Quotes" q, symbol_median
            WHERE q."Symbol" = :symbol
              AND COALESCE(q."ClosePrice", 0) > 0
              AND (
                  symbol_median.median_close IS NULL
                  OR q."ClosePrice"::double precision BETWEEN symbol_median.median_close / 20.0
                                                        AND symbol_median.median_close * 20.0
              )
        ),
        latest_quote AS (
            SELECT MAX("CloseDate") AS latest_date
            FROM valid_quotes
        ),
        trading_days AS (
            SELECT
                day::date AS trading_date,
                ROW_NUMBER() OVER (ORDER BY day DESC) - 1 AS trading_days_back
            FROM latest_quote l
            CROSS JOIN generate_series(
                l.latest_date - INTERVAL '220 days',
                l.latest_date,
                INTERVAL '1 day'
            ) AS day
            WHERE EXTRACT(ISODOW FROM day) BETWEEN 1 AND 5
        ),
        boundary AS (
            SELECT MAX(trading_date) AS start_date
            FROM trading_days
            WHERE trading_days_back = :tradingDays
        ),
        ordered_quotes AS (
            SELECT
                vq."Symbol",
                vq."CloseDate",
                vq."OpenPrice",
                vq."ClosePrice",
                vq."HighestPrice",
                vq."LowestPrice",
                vq."Volume",
                ROW_NUMBER() OVER (PARTITION BY vq."Symbol" ORDER BY vq."CloseDate" DESC) AS dayIndex,
                LAG(vq."ClosePrice") OVER (PARTITION BY vq."Symbol" ORDER BY vq."CloseDate" ASC) AS previousClosePrice
            FROM valid_quotes vq
            CROSS JOIN latest_quote l
            CROSS JOIN boundary b
            WHERE vq."CloseDate" BETWEEN b.start_date AND l.latest_date
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
        ORDER BY dayIndex ASC
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    query.bindValue(QStringLiteral(":tradingDays"), boundedTradingDays);

    if (!query.exec()) {
        qCritical() << "SQL-Fehler bei Kursdetails nach Handelstagen:" << query.lastError().text();
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

QVariantList DatabaseManager::getStockAnalysisResults(double minIncreasePercent, int quoteCount)
{
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return results;
    }

    const int boundedQuoteCount = qBound(10, quoteCount, 90);

    QSqlQuery query(db);
    query.prepare(R"SQL(
        WITH quote_medians AS (
            SELECT
                "Symbol",
                percentile_cont(0.5) WITHIN GROUP (ORDER BY "ClosePrice"::double precision) AS median_close
            FROM "Quotes"
            WHERE COALESCE("ClosePrice", 0) > 0
            GROUP BY "Symbol"
        ),
        recent_quotes AS (
            SELECT
                q.*,
                ROW_NUMBER() OVER (PARTITION BY q."Symbol" ORDER BY q."CloseDate" DESC) AS rn_desc
            FROM "Quotes" q
            INNER JOIN quote_medians qm ON qm."Symbol" = q."Symbol"
            WHERE COALESCE(q."ClosePrice", 0) > 0
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
            WHERE rn_desc <= :quoteCount
            GROUP BY "Symbol"
        ),
        trend_summary AS (
            SELECT
                qs."Symbol",
                AVG(rq."ClosePrice") FILTER (WHERE rq.rn_desc <= LEAST(5, qs.quote_count)) AS newest_average,
                AVG(rq."ClosePrice") FILTER (WHERE rq.rn_desc > qs.quote_count - LEAST(5, qs.quote_count)) AS oldest_average
            FROM quote_summary qs
            INNER JOIN recent_quotes rq
                ON rq."Symbol" = qs."Symbol"
               AND rq.rn_desc <= :quoteCount
            GROUP BY qs."Symbol", qs.quote_count
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
            s."Currency" AS quotecurrency,
            CASE
                WHEN NOT COALESCE(s."from_IBKR", true) THEN 'MS'
                WHEN COALESCE(s."IBKRQuoteExchange", '') <> '' THEN 'IBKR'
                ELSE '-'
            END AS quotesource,
            ROUND(((trend_summary.newest_average - trend_summary.oldest_average)
                / NULLIF(trend_summary.oldest_average, 0) * 100)::numeric, 2) AS increasepercent,
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
        INNER JOIN trend_summary ON trend_summary."Symbol" = quote_summary."Symbol"
        INNER JOIN "Quotes" first_quote
            ON first_quote."Symbol" = quote_summary."Symbol"
           AND first_quote."CloseDate" = quote_summary.first_date
        INNER JOIN "Quotes" last_quote
            ON last_quote."Symbol" = quote_summary."Symbol"
           AND last_quote."CloseDate" = quote_summary.last_date
        LEFT JOIN latest_fundamentals ON latest_fundamentals."Symbol" = s."Symbol"
        WHERE quote_summary.quote_count >= 2
          AND COALESCE(s."from_IBKR", true)
          AND COALESCE(s."IBKRQuoteExchange", '') <> ''
          AND ((trend_summary.newest_average - trend_summary.oldest_average)
                / NULLIF(trend_summary.oldest_average, 0) * 100) >= :minIncreasePercent
        ORDER BY increasepercent DESC NULLS LAST, periodturnover DESC NULLS LAST
        LIMIT 500
    )SQL");
    query.bindValue(QStringLiteral(":minIncreasePercent"), minIncreasePercent);
    query.bindValue(QStringLiteral(":quoteCount"), boundedQuoteCount);

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
        WHERE COALESCE(s."from_IBKR", true)
          AND COALESCE(s."IBKRQuoteExchange", '') <> ''
          AND EXISTS (
              SELECT 1
              FROM "Quotes" q
              WHERE q."Symbol" = s."Symbol"
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

QVariantList DatabaseManager::findStockAnalysisDirectSearchStocks(const QString &isin, const QString &name)
{
    QVariantList results;

    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return results;
    }

    const QString normalizedIsin = isin.trimmed();
    const QString normalizedName = name.trimmed();
    if (normalizedIsin.isEmpty() && normalizedName.isEmpty())
        return results;

    auto toLikePattern = [](QString value, bool wrapWhenPlain) {
        value = value.trimmed();
        const bool hasWildcard = value.contains(QLatin1Char('*'));
        value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        value.replace(QStringLiteral("%"), QStringLiteral("\\%"));
        value.replace(QStringLiteral("_"), QStringLiteral("\\_"));
        value.replace(QLatin1Char('*'), QLatin1Char('%'));
        if (wrapWhenPlain && !hasWildcard)
            value = QStringLiteral("%") + value + QStringLiteral("%");
        return value;
    };

    const QString isinPattern = toLikePattern(normalizedIsin, false);
    const QString namePattern = toLikePattern(normalizedName, true);

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT
            s."Symbol" AS symbol,
            s."ISIN" AS isin,
            s."Name" AS name,
            s."MIC" AS mic,
            s."Currency" AS quotecurrency,
            CASE
                WHEN NOT COALESCE(s."from_IBKR", true) THEN 'MS'
                WHEN COALESCE(s."IBKRQuoteExchange", '') <> '' THEN 'IBKR'
                ELSE '-'
            END AS quotesource
        FROM "Stocks" s
        WHERE (:isinEmpty OR COALESCE(s."ISIN", '') ILIKE :isinPattern ESCAPE '\')
          AND (:nameEmpty OR COALESCE(s."Name", '') ILIKE :namePattern ESCAPE '\')
        ORDER BY
            CASE WHEN NOT :isinEmpty AND COALESCE(s."ISIN", '') ILIKE :isinPattern ESCAPE '\' THEN 0 ELSE 1 END,
            s."Name" ASC NULLS LAST,
            s."Symbol" ASC
        LIMIT 500
    )SQL");
    query.bindValue(QStringLiteral(":isinEmpty"), normalizedIsin.isEmpty());
    query.bindValue(QStringLiteral(":nameEmpty"), normalizedName.isEmpty());
    query.bindValue(QStringLiteral(":isinPattern"), isinPattern);
    query.bindValue(QStringLiteral(":namePattern"), namePattern);

    if (!query.exec()) {
        qCritical() << "SQL-Fehler bei Direktsuche:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        for (int i = 0; i < query.record().count(); ++i)
            row.insert(query.record().fieldName(i), query.value(i));
        results << row;
    }

    return results;
}

QVariantMap DatabaseManager::getStockAnalysisCandidate(const QString &symbol, double minIncreasePercent, int quoteCount)
{
    QVariantMap result;

    if (!db.isOpen()) {
        qWarning() << "Datenbank ist nicht verbunden!";
        return result;
    }

    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty())
        return result;

    const int boundedQuoteCount = qBound(10, quoteCount, 90);

    QSqlQuery query(db);
    query.prepare(R"SQL(
        WITH quote_median AS (
            SELECT
                percentile_cont(0.5) WITHIN GROUP (ORDER BY "ClosePrice"::double precision) AS median_close
            FROM "Quotes"
            WHERE "Symbol" = :symbol
              AND COALESCE("ClosePrice", 0) > 0
        ),
        recent_quotes AS (
            SELECT
                q.*,
                ROW_NUMBER() OVER (PARTITION BY q."Symbol" ORDER BY q."CloseDate" DESC) AS rn_desc
            FROM "Quotes" q, quote_median
            WHERE q."Symbol" = :symbol
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
            WHERE rn_desc <= :quoteCount
            GROUP BY "Symbol"
        ),
        trend_summary AS (
            SELECT
                qs."Symbol",
                AVG(rq."ClosePrice") FILTER (WHERE rq.rn_desc <= LEAST(5, qs.quote_count)) AS newest_average,
                AVG(rq."ClosePrice") FILTER (WHERE rq.rn_desc > qs.quote_count - LEAST(5, qs.quote_count)) AS oldest_average
            FROM quote_summary qs
            INNER JOIN recent_quotes rq
                ON rq."Symbol" = qs."Symbol"
               AND rq.rn_desc <= :quoteCount
            GROUP BY qs."Symbol", qs.quote_count
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
            s."Currency" AS quotecurrency,
            'IBKR' AS quotesource,
            ROUND(((trend_summary.newest_average - trend_summary.oldest_average)
                / NULLIF(trend_summary.oldest_average, 0) * 100)::numeric, 2) AS increasepercent,
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
        INNER JOIN trend_summary ON trend_summary."Symbol" = quote_summary."Symbol"
        INNER JOIN "Quotes" first_quote
            ON first_quote."Symbol" = quote_summary."Symbol"
           AND first_quote."CloseDate" = quote_summary.first_date
        INNER JOIN "Quotes" last_quote
            ON last_quote."Symbol" = quote_summary."Symbol"
           AND last_quote."CloseDate" = quote_summary.last_date
        LEFT JOIN latest_fundamentals ON latest_fundamentals."Symbol" = s."Symbol"
        WHERE quote_summary.quote_count >= 2
          AND COALESCE(s."from_IBKR", true)
          AND COALESCE(s."IBKRQuoteExchange", '') <> ''
          AND ((trend_summary.newest_average - trend_summary.oldest_average)
                / NULLIF(trend_summary.oldest_average, 0) * 100) >= :minIncreasePercent
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);
    query.bindValue(QStringLiteral(":minIncreasePercent"), minIncreasePercent);
    query.bindValue(QStringLiteral(":quoteCount"), boundedQuoteCount);

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
