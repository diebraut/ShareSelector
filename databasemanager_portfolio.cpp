#include "databasemanager.h"

#include <QDate>
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>

namespace {
quint32 stablePortfolioSymbolSeed(const QString &symbol)
{
    quint32 seed = 2166136261u;
    for (const QChar character : symbol) {
        seed ^= character.unicode();
        seed *= 16777619u;
    }
    return seed;
}

double mockPortfolioValue(quint32 seed, int shift, double minimum, double maximum)
{
    const quint32 part = (seed >> shift) & 0xffu;
    return minimum + (maximum - minimum) * (double(part) / 255.0);
}
} // namespace
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
            b."Symbol",
            b."Name",
            TO_CHAR(b."BuyDate", 'YYYY-MM-DD') AS "BuyDate",
            TO_CHAR(b."SellDate", 'YYYY-MM-DD') AS "SellDate",
            CASE
                WHEN b."Status" = 10 THEN b."CurrentValue"
                ELSE COALESCE(lq.latest_close, b."CurrentValue")
            END AS "CurrentValue",
            b."EntryValue",
            CASE
                WHEN NULLIF(b."EntryValue", 0) IS NULL THEN b."ValueIncreasePercent"
                ELSE ROUND(((CASE WHEN b."Status" = 10 THEN b."CurrentValue" ELSE COALESCE(lq.latest_close, b."CurrentValue") END - b."EntryValue") / NULLIF(b."EntryValue", 0) * 100)::numeric, 2)
            END AS "ValueIncreasePercent",
            b."Status",
            COALESCE(b."Quantity", 1) AS "Quantity",
            COALESCE(b."AnalysisConfigName", '') AS "AnalysisConfigName"
        FROM "BoughtStocks" b
        LEFT JOIN LATERAL (
            SELECT
                q."ClosePrice" AS latest_close,
                q."CloseDate" AS latest_date
            FROM "Quotes" q
            WHERE q."Symbol" = b."Symbol"
              AND COALESCE(q."ClosePrice", 0) > 0
            ORDER BY q."CloseDate" DESC
            LIMIT 1
        ) lq ON true
        ORDER BY b."BuyDate" DESC, b."Symbol" ASC
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

QVariantMap DatabaseManager::getPortfolioChartData(const QString &symbol)
{
    QVariantMap result;
    QVariantList quotes;

    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        result["quotes"] = quotes;
        return result;
    }

    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty()) {
        result["quotes"] = quotes;
        return result;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        WITH latest_quote AS (
            SELECT
                q."Symbol",
                q."ClosePrice" AS latest_close,
                q."CloseDate" AS latest_date
            FROM "Quotes" q
            WHERE q."Symbol" = :symbol
              AND COALESCE(q."ClosePrice", 0) > 0
            ORDER BY q."CloseDate" DESC
            LIMIT 1
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
        boundaries AS (
            SELECT
                MAX(trading_date) FILTER (WHERE trading_days_back = 20) AS start_20,
                MAX(trading_date) FILTER (WHERE trading_days_back = 40) AS start_40,
                MAX(trading_date) FILTER (WHERE trading_days_back = 60) AS start_60,
                MAX(trading_date) FILTER (WHERE trading_days_back = 90) AS start_90
            FROM trading_days
        ),
        summary AS (
            SELECT
                l.latest_close,
                l.latest_date,
                bd.start_20,
                bd.start_40,
                bd.start_60,
                bd.start_90,
                p20.new_avg AS avg_20,
                p40.new_avg AS avg_40,
                p60.new_avg AS avg_60,
                p90.new_avg AS avg_90,
                ROUND(((p20.new_avg - p20.old_avg) / NULLIF(p20.old_avg, 0) * 100)::numeric, 2) AS inc_20,
                ROUND(((p40.new_avg - p40.old_avg) / NULLIF(p40.old_avg, 0) * 100)::numeric, 2) AS inc_40,
                ROUND(((p60.new_avg - p60.old_avg) / NULLIF(p60.old_avg, 0) * 100)::numeric, 2) AS inc_60,
                ROUND(((p90.new_avg - p90.old_avg) / NULLIF(p90.old_avg, 0) * 100)::numeric, 2) AS inc_90,
                p20.quote_count AS quote_count_20,
                p40.quote_count AS quote_count_40,
                p60.quote_count AS quote_count_60,
                p90.quote_count AS quote_count_90
            FROM latest_quote l
            CROSS JOIN boundaries bd
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg,
                    COUNT(*) AS quote_count
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = :symbol
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_20 AND l.latest_date
                ) ranked
            ) p20 ON true
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg,
                    COUNT(*) AS quote_count
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = :symbol
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_40 AND l.latest_date
                ) ranked
            ) p40 ON true
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg,
                    COUNT(*) AS quote_count
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = :symbol
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_60 AND l.latest_date
                ) ranked
            ) p60 ON true
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg,
                    COUNT(*) AS quote_count
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = :symbol
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_90 AND l.latest_date
                ) ranked
            ) p90 ON true
        ),
        quote_rows AS (
            SELECT
                q."CloseDate",
                q."OpenPrice",
                q."ClosePrice",
                q."HighestPrice",
                q."LowestPrice",
                q."Volume"
            FROM "Quotes" q
            CROSS JOIN summary s
            WHERE q."Symbol" = :symbol
              AND COALESCE(q."ClosePrice", 0) > 0
              AND q."CloseDate" BETWEEN s.start_90 AND s.latest_date
        )
        SELECT
            'summary' AS row_type,
            NULL::date AS close_date,
            NULL::numeric AS open_price,
            NULL::numeric AS close_price,
            NULL::numeric AS highest_price,
            NULL::numeric AS lowest_price,
            NULL::numeric AS volume,
            latest_close,
            latest_date,
            start_20,
            start_40,
            start_60,
            start_90,
            avg_20,
            avg_40,
            avg_60,
            avg_90,
            inc_20,
            inc_40,
            inc_60,
            inc_90,
            quote_count_20,
            quote_count_40,
            quote_count_60,
            quote_count_90
        FROM summary
        UNION ALL
        SELECT
            'quote' AS row_type,
            "CloseDate" AS close_date,
            "OpenPrice" AS open_price,
            "ClosePrice" AS close_price,
            "HighestPrice" AS highest_price,
            "LowestPrice" AS lowest_price,
            "Volume" AS volume,
            NULL::numeric AS latest_close,
            NULL::date AS latest_date,
            NULL::date AS start_20,
            NULL::date AS start_40,
            NULL::date AS start_60,
            NULL::date AS start_90,
            NULL::numeric AS avg_20,
            NULL::numeric AS avg_40,
            NULL::numeric AS avg_60,
            NULL::numeric AS avg_90,
            NULL::numeric AS inc_20,
            NULL::numeric AS inc_40,
            NULL::numeric AS inc_60,
            NULL::numeric AS inc_90,
            NULL::bigint AS quote_count_20,
            NULL::bigint AS quote_count_40,
            NULL::bigint AS quote_count_60,
            NULL::bigint AS quote_count_90
        FROM quote_rows
        ORDER BY row_type DESC, close_date ASC NULLS FIRST
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden der Depot-Chartdaten:" << query.lastError().text() << normalizedSymbol;
        result["quotes"] = quotes;
        return result;
    }

    while (query.next()) {
        const QString rowType = query.value(QStringLiteral("row_type")).toString();
        if (rowType == QStringLiteral("summary")) {
            const double latestClose = query.value(QStringLiteral("latest_close")).toDouble();
            result["symbol"] = normalizedSymbol;
            result["latestClose"] = latestClose;
            result["latestDate"] = query.value(QStringLiteral("latest_date")).toDate().toString(QStringLiteral("yyyy-MM-dd"));
            result["start20"] = query.value(QStringLiteral("start_20")).toDate().toString(QStringLiteral("yyyy-MM-dd"));
            result["start40"] = query.value(QStringLiteral("start_40")).toDate().toString(QStringLiteral("yyyy-MM-dd"));
            result["start60"] = query.value(QStringLiteral("start_60")).toDate().toString(QStringLiteral("yyyy-MM-dd"));
            result["start90"] = query.value(QStringLiteral("start_90")).toDate().toString(QStringLiteral("yyyy-MM-dd"));
            result["avg20"] = query.value(QStringLiteral("avg_20"));
            result["avg40"] = query.value(QStringLiteral("avg_40"));
            result["avg60"] = query.value(QStringLiteral("avg_60"));
            result["avg90"] = query.value(QStringLiteral("avg_90"));
            result["quoteCount20"] = query.value(QStringLiteral("quote_count_20"));
            result["quoteCount40"] = query.value(QStringLiteral("quote_count_40"));
            result["quoteCount60"] = query.value(QStringLiteral("quote_count_60"));
            result["quoteCount90"] = query.value(QStringLiteral("quote_count_90"));
            result["inc20"] = query.value(QStringLiteral("inc_20"));
            result["inc40"] = query.value(QStringLiteral("inc_40"));
            result["inc60"] = query.value(QStringLiteral("inc_60"));
            result["inc90"] = query.value(QStringLiteral("inc_90"));
        } else if (rowType == QStringLiteral("quote")) {
            QVariantMap quote;
            quote["closeDate"] = query.value(QStringLiteral("close_date")).toDate().toString(QStringLiteral("yyyy-MM-dd"));
            quote["displayDate"] = query.value(QStringLiteral("close_date")).toDate().toString(QStringLiteral("dd.MM.yyyy"));
            quote["openPrice"] = query.value(QStringLiteral("open_price"));
            quote["closePrice"] = query.value(QStringLiteral("close_price"));
            quote["highestPrice"] = query.value(QStringLiteral("highest_price"));
            quote["lowestPrice"] = query.value(QStringLiteral("lowest_price"));
            quote["volume"] = query.value(QStringLiteral("volume"));
            quotes << quote;
        }
    }

    result["quotes"] = quotes;
    return result;
}

QVariantList DatabaseManager::getTestPortfolioSummary()
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
            CASE
                WHEN b."Status" = 10 THEN b."CurrentValue"
                ELSE COALESCE(qp.latest_close, b."CurrentValue")
            END AS "CurrentValue",
            b."EntryValue",
            CASE
                WHEN NULLIF(b."EntryValue", 0) IS NULL THEN b."ValueIncreasePercent"
                ELSE ROUND(((CASE WHEN b."Status" = 10 THEN b."CurrentValue" ELSE COALESCE(qp.latest_close, b."CurrentValue") END - b."EntryValue") / NULLIF(b."EntryValue", 0) * 100)::numeric, 2)
            END AS "ValueIncreasePercent",
            b."Status",
            COALESCE(b."Quantity", 1) AS "Quantity",
            COALESCE(b."AnalysisConfigName", '') AS "AnalysisConfigName",
            s."MIC",
            s."ISIN",
            s."Exchange",
            s."CountryCode",
            s."City",
            qp.days20_value_inc AS "Days20ValueInc",
            qp.days40_value_inc AS "Days40ValueInc",
            qp.days60_value_inc AS "Days60ValueInc",
            qp.days90_value_inc AS "Days90ValueInc",
            TO_CHAR(qp.latest_date, 'YYYY-MM-DD') AS "QuoteLastDate"
        FROM "BoughtStocks" b
        LEFT JOIN "Stocks" s ON s."Symbol" = b."Symbol"
        LEFT JOIN LATERAL (
            WITH latest_quote AS (
                SELECT
                    q."ClosePrice" AS latest_close,
                    q."CloseDate" AS latest_date
                FROM "Quotes" q
                WHERE q."Symbol" = b."Symbol"
                  AND COALESCE(q."ClosePrice", 0) > 0
                ORDER BY q."CloseDate" DESC
                LIMIT 1
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
            boundaries AS (
                SELECT
                    MAX(trading_date) FILTER (WHERE trading_days_back = 20) AS start_20,
                    MAX(trading_date) FILTER (WHERE trading_days_back = 40) AS start_40,
                    MAX(trading_date) FILTER (WHERE trading_days_back = 60) AS start_60,
                    MAX(trading_date) FILTER (WHERE trading_days_back = 90) AS start_90
                FROM trading_days
            )
            SELECT
                l.latest_close,
                l.latest_date,
                ROUND(((p20.new_avg - p20.old_avg) / NULLIF(p20.old_avg, 0) * 100)::numeric, 2) AS days20_value_inc,
                ROUND(((p40.new_avg - p40.old_avg) / NULLIF(p40.old_avg, 0) * 100)::numeric, 2) AS days40_value_inc,
                ROUND(((p60.new_avg - p60.old_avg) / NULLIF(p60.old_avg, 0) * 100)::numeric, 2) AS days60_value_inc,
                ROUND(((p90.new_avg - p90.old_avg) / NULLIF(p90.old_avg, 0) * 100)::numeric, 2) AS days90_value_inc
            FROM latest_quote l
            CROSS JOIN boundaries bd
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = b."Symbol"
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_20 AND l.latest_date
                ) ranked
            ) p20 ON true
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = b."Symbol"
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_40 AND l.latest_date
                ) ranked
            ) p40 ON true
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = b."Symbol"
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_60 AND l.latest_date
                ) ranked
            ) p60 ON true
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = b."Symbol"
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_90 AND l.latest_date
                ) ranked
            ) p90 ON true
        ) qp ON true
        ORDER BY b."BuyDate" DESC, b."Symbol" ASC
    )SQL");

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden der Depot-Uebersicht:" << query.lastError().text();
        return results;
    }

    while (query.next()) {
        QVariantMap row;
        row["symbol"] = query.value("Symbol");
        row["name"] = query.value("Name");
        row["buyDate"] = query.value("BuyDate");
        row["sellDate"] = query.value("SellDate");
        row["currentValue"] = query.value("CurrentValue");
        row["entryValue"] = query.value("EntryValue");
        row["valueIncreasePercent"] = query.value("ValueIncreasePercent");
        row["quantity"] = query.value("Quantity");
        row["analysisConfigName"] = query.value("AnalysisConfigName");
        row["days20ValueInc"] = query.value("Days20ValueInc");
        row["days40ValueInc"] = query.value("Days40ValueInc");
        row["days60ValueInc"] = query.value("Days60ValueInc");
        row["days90ValueInc"] = query.value("Days90ValueInc");
        row["quoteLastDate"] = query.value("QuoteLastDate");
        row["status"] = query.value("Status");
        row["mic"] = query.value("MIC");
        row["isin"] = query.value("ISIN");
        row["exchange"] = query.value("Exchange");
        row["countryCode"] = query.value("CountryCode");
        row["city"] = query.value("City");

        const QStringList databaseFields = {
            QStringLiteral("symbol"), QStringLiteral("name"), QStringLiteral("buyDate"),
            QStringLiteral("sellDate"), QStringLiteral("currentValue"), QStringLiteral("entryValue"),
            QStringLiteral("valueIncreasePercent"), QStringLiteral("quantity"), QStringLiteral("analysisConfigName"),
            QStringLiteral("days20ValueInc"), QStringLiteral("days40ValueInc"), QStringLiteral("days60ValueInc"),
            QStringLiteral("days90ValueInc"), QStringLiteral("quoteLastDate"), QStringLiteral("status"),
            QStringLiteral("mic"), QStringLiteral("isin"), QStringLiteral("exchange"),
            QStringLiteral("countryCode"), QStringLiteral("city")
        };
        for (const QString &field : databaseFields)
            row[field + QStringLiteral("Origin")] = QStringLiteral("db");

        results.append(row);
    }

    return results;
}

QVariantMap DatabaseManager::getPortfolioDetails(const QString &symbol)
{
    QVariantMap row;
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return row;
    }

    const QString normalizedSymbol = symbol.trimmed();
    if (normalizedSymbol.isEmpty())
        return row;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT
            b."Symbol",
            b."Name",
            TO_CHAR(b."BuyDate", 'YYYY-MM-DD') AS "BuyDate",
            TO_CHAR(b."SellDate", 'YYYY-MM-DD') AS "SellDate",
            CASE
                WHEN b."Status" = 10 THEN b."CurrentValue"
                ELSE COALESCE(lq.latest_close, b."CurrentValue")
            END AS "CurrentValue",
            b."EntryValue",
            CASE
                WHEN NULLIF(b."EntryValue", 0) IS NULL THEN b."ValueIncreasePercent"
                ELSE ROUND(((CASE WHEN b."Status" = 10 THEN b."CurrentValue" ELSE COALESCE(lq.latest_close, b."CurrentValue") END - b."EntryValue") / NULLIF(b."EntryValue", 0) * 100)::numeric, 2)
            END AS "ValueIncreasePercent",
            b."Status",
            COALESCE(b."Quantity", 1) AS "Quantity",
            COALESCE(b."AnalysisConfigName", '') AS "AnalysisConfigName",
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
            TO_CHAR(f."UpdatedAt", 'YYYY-MM-DD HH24:MI:SS') AS "FundamentalUpdatedAt"
        FROM "BoughtStocks" b
        LEFT JOIN "Stocks" s ON s."Symbol" = b."Symbol"
        LEFT JOIN LATERAL (
            SELECT
                q."ClosePrice" AS latest_close,
                q."CloseDate" AS latest_date
            FROM "Quotes" q
            WHERE q."Symbol" = b."Symbol"
              AND COALESCE(q."ClosePrice", 0) > 0
            ORDER BY q."CloseDate" DESC
            LIMIT 1
        ) lq ON true
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
        WHERE b."Symbol" = :symbol
        LIMIT 1
    )SQL");
    query.bindValue(QStringLiteral(":symbol"), normalizedSymbol);

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden der Depot-Details:" << query.lastError().text() << normalizedSymbol;
        return row;
    }

    if (!query.next())
        return row;

    const QString localSymbol = normalizedSymbol.section(QLatin1Char('.'), 0, 0);
    const quint32 seed = stablePortfolioSymbolSeed(normalizedSymbol);
    const double currentValue = query.value("CurrentValue").toDouble();
    const double price = currentValue > 0.0 ? currentValue : 100.0;
    const double peRatio = mockPortfolioValue(seed, 0, 8.0, 32.0);
    const double priceToSales = mockPortfolioValue(seed, 8, 0.8, 7.0);
    const double dividendYield = mockPortfolioValue(seed, 16, 0.5, 5.0);
    const double sharesOutstanding = 100000000.0 + (seed % 900u) * 1000000.0;
    const double marketCapitalization = price * sharesOutstanding;
    const double revenue = marketCapitalization / priceToSales;
    const bool isEtf = query.value("Name").toString().contains(QStringLiteral("ETF"), Qt::CaseInsensitive);
    const QString mic = query.value("MIC").toString();

    auto stringOr = [&query](const char *column, const QString &fallback) {
        const QString value = query.value(column).toString().trimmed();
        return value.isEmpty() ? fallback : value;
    };

    row["symbol"] = normalizedSymbol;
    row["name"] = query.value("Name");
    row["buyDate"] = query.value("BuyDate");
    row["sellDate"] = query.value("SellDate");
    row["currentValue"] = query.value("CurrentValue");
    row["entryValue"] = query.value("EntryValue");
    row["valueIncreasePercent"] = query.value("ValueIncreasePercent");
    row["quantity"] = query.value("Quantity");
    row["analysisConfigName"] = query.value("AnalysisConfigName");
    row["status"] = query.value("Status");
    row["mic"] = mic;
    row["isin"] = query.value("ISIN");
    row["exchange"] = query.value("Exchange");
    row["countryCode"] = query.value("CountryCode");
    row["city"] = query.value("City");

    const QStringList databaseFields = {
        QStringLiteral("symbol"), QStringLiteral("name"), QStringLiteral("buyDate"),
        QStringLiteral("sellDate"), QStringLiteral("currentValue"), QStringLiteral("entryValue"),
        QStringLiteral("valueIncreasePercent"), QStringLiteral("quantity"), QStringLiteral("analysisConfigName"), QStringLiteral("status"),
        QStringLiteral("mic"), QStringLiteral("isin"), QStringLiteral("exchange"), QStringLiteral("countryCode"),
        QStringLiteral("city")
    };
    for (const QString &field : databaseFields)
        row[field + QStringLiteral("Origin")] = QStringLiteral("db");

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

    auto setIbkrString = [&query, &row](const QString &key,
                                        const char *column,
                                        const QString &fallback) {
        const QString value = query.value(column).toString().trimmed();
        const bool hasIbkrValue = !value.isEmpty();
        row[key] = hasIbkrValue ? value : fallback;
        row[key + QStringLiteral("Origin")] = hasIbkrValue ? QStringLiteral("IBKR") : QStringLiteral("mock");
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
    row["enterpriseValue"] = marketCapitalization * mockPortfolioValue(seed, 4, 0.9, 1.35);
    row["peRatio"] = peRatio;
    row["forwardPeRatio"] = peRatio * mockPortfolioValue(seed, 12, 0.75, 1.05);
    row["priceToBookRatio"] = mockPortfolioValue(seed, 4, 0.8, 8.0);
    row["priceToSalesRatio"] = priceToSales;
    row["priceToCashFlowRatio"] = mockPortfolioValue(seed, 12, 4.0, 22.0);
    row["priceToDividendRatio"] = 100.0 / dividendYield;
    row["eps"] = price / peRatio;
    row["forwardEps"] = price / row["forwardPeRatio"].toDouble();
    row["dividendPerShare"] = price * dividendYield / 100.0;
    row["dividendYield"] = dividendYield;
    row["payoutRatio"] = row["dividendPerShare"].toDouble() / row["eps"].toDouble() * 100.0;
    row["beta"] = mockPortfolioValue(seed, 20, 0.55, 1.8);
    row["revenue"] = revenue;
    row["netIncome"] = revenue * mockPortfolioValue(seed, 2, 0.05, 0.22);
    row["ebitda"] = revenue * mockPortfolioValue(seed, 10, 0.1, 0.3);
    row["returnOnEquity"] = mockPortfolioValue(seed, 6, 4.0, 30.0);
    row["returnOnAssets"] = mockPortfolioValue(seed, 14, 2.0, 18.0);
    row["debtToEquity"] = mockPortfolioValue(seed, 18, 0.1, 2.5);
    row["sharesOutstanding"] = sharesOutstanding;
    row["week52High"] = price * mockPortfolioValue(seed, 3, 1.05, 1.35);
    row["week52Low"] = price * mockPortfolioValue(seed, 11, 0.55, 0.9);
    row["source"] = QStringLiteral("TEST");
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
        QStringLiteral("fundamentalUpdatedAt"),
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
        {QStringLiteral("fundamentalUpdatedAt"), QStringLiteral("FundamentalUpdatedAt")}
    };
    const QString fundamentalSource = query.value(QStringLiteral("FundamentalSource")).toString().trimmed();
    if (!fundamentalSource.isEmpty()) {
        for (auto it = fundamentalColumns.constBegin(); it != fundamentalColumns.constEnd(); ++it) {
            const QVariant value = query.value(it.value());
            row[it.key() + QStringLiteral("Origin")] = fundamentalSource;
            row[it.key()] = value.isNull() ? QVariant() : value;
        }
    }

    return row;
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
            CASE
                WHEN b."Status" = 10 THEN b."CurrentValue"
                ELSE COALESCE(qp.latest_close, b."CurrentValue")
            END AS "CurrentValue",
            b."EntryValue",
            CASE
                WHEN NULLIF(b."EntryValue", 0) IS NULL THEN b."ValueIncreasePercent"
                ELSE ROUND(((CASE WHEN b."Status" = 10 THEN b."CurrentValue" ELSE COALESCE(qp.latest_close, b."CurrentValue") END - b."EntryValue") / NULLIF(b."EntryValue", 0) * 100)::numeric, 2)
            END AS "ValueIncreasePercent",
            b."Status",
            COALESCE(b."Quantity", 1) AS "Quantity",
            COALESCE(b."AnalysisConfigName", '') AS "AnalysisConfigName",
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
            TO_CHAR(f."UpdatedAt", 'YYYY-MM-DD HH24:MI:SS') AS "FundamentalUpdatedAt",
            qp.days20_value_inc AS "Days20ValueInc",
            qp.days40_value_inc AS "Days40ValueInc",
            qp.days60_value_inc AS "Days60ValueInc",
            qp.days90_value_inc AS "Days90ValueInc",
            TO_CHAR(qp.latest_date, 'YYYY-MM-DD') AS "QuoteLastDate"
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
        LEFT JOIN LATERAL (
            WITH latest_quote AS (
                SELECT
                    q."ClosePrice" AS latest_close,
                    q."CloseDate" AS latest_date
                FROM "Quotes" q
                WHERE q."Symbol" = b."Symbol"
                  AND COALESCE(q."ClosePrice", 0) > 0
                ORDER BY q."CloseDate" DESC
                LIMIT 1
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
            boundaries AS (
                SELECT
                    MAX(trading_date) FILTER (WHERE trading_days_back = 20) AS start_20,
                    MAX(trading_date) FILTER (WHERE trading_days_back = 40) AS start_40,
                    MAX(trading_date) FILTER (WHERE trading_days_back = 60) AS start_60,
                    MAX(trading_date) FILTER (WHERE trading_days_back = 90) AS start_90
                FROM trading_days
            )
            SELECT
                l.latest_close,
                l.latest_date,
                ROUND(((p20.new_avg - p20.old_avg) / NULLIF(p20.old_avg, 0) * 100)::numeric, 2) AS days20_value_inc,
                ROUND(((p40.new_avg - p40.old_avg) / NULLIF(p40.old_avg, 0) * 100)::numeric, 2) AS days40_value_inc,
                ROUND(((p60.new_avg - p60.old_avg) / NULLIF(p60.old_avg, 0) * 100)::numeric, 2) AS days60_value_inc,
                ROUND(((p90.new_avg - p90.old_avg) / NULLIF(p90.old_avg, 0) * 100)::numeric, 2) AS days90_value_inc
            FROM latest_quote l
            CROSS JOIN boundaries bd
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = b."Symbol"
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_20 AND l.latest_date
                ) ranked
            ) p20 ON true
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = b."Symbol"
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_40 AND l.latest_date
                ) ranked
            ) p40 ON true
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = b."Symbol"
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_60 AND l.latest_date
                ) ranked
            ) p60 ON true
            LEFT JOIN LATERAL (
                SELECT
                    AVG(close_price) FILTER (WHERE rn_asc <= LEAST(5::bigint, total_count)) AS old_avg,
                    AVG(close_price) FILTER (WHERE rn_desc <= LEAST(5::bigint, total_count)) AS new_avg
                FROM (
                    SELECT
                        q."ClosePrice" AS close_price,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" ASC) AS rn_asc,
                        ROW_NUMBER() OVER (ORDER BY q."CloseDate" DESC) AS rn_desc,
                        COUNT(*) OVER () AS total_count
                    FROM "Quotes" q
                    WHERE q."Symbol" = b."Symbol"
                      AND COALESCE(q."ClosePrice", 0) > 0
                      AND q."CloseDate" BETWEEN bd.start_90 AND l.latest_date
                ) ranked
            ) p90 ON true
        ) qp ON true
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
        const quint32 seed = stablePortfolioSymbolSeed(symbol);
        const double currentValue = query.value("CurrentValue").toDouble();
        const double price = currentValue > 0.0 ? currentValue : 100.0;
        const double peRatio = mockPortfolioValue(seed, 0, 8.0, 32.0);
        const double priceToSales = mockPortfolioValue(seed, 8, 0.8, 7.0);
        const double dividendYield = mockPortfolioValue(seed, 16, 0.5, 5.0);
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
        row["quantity"] = query.value("Quantity");
        row["analysisConfigName"] = query.value("AnalysisConfigName");
        row["days20ValueInc"] = query.value("Days20ValueInc");
        row["days40ValueInc"] = query.value("Days40ValueInc");
        row["days60ValueInc"] = query.value("Days60ValueInc");
        row["days90ValueInc"] = query.value("Days90ValueInc");
        row["quoteLastDate"] = query.value("QuoteLastDate");
        row["status"] = query.value("Status");
        row["mic"] = mic;
        row["isin"] = query.value("ISIN");
        row["exchange"] = query.value("Exchange");
        row["countryCode"] = query.value("CountryCode");
        row["city"] = query.value("City");

        const QStringList databaseFields = {
            QStringLiteral("symbol"), QStringLiteral("name"), QStringLiteral("buyDate"),
            QStringLiteral("sellDate"), QStringLiteral("currentValue"), QStringLiteral("entryValue"),
            QStringLiteral("valueIncreasePercent"), QStringLiteral("quantity"), QStringLiteral("analysisConfigName"), QStringLiteral("status"), QStringLiteral("mic"),
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
        row["enterpriseValue"] = marketCapitalization * mockPortfolioValue(seed, 4, 0.9, 1.35);
        row["peRatio"] = peRatio;
        row["forwardPeRatio"] = peRatio * mockPortfolioValue(seed, 12, 0.75, 1.05);
        row["priceToBookRatio"] = mockPortfolioValue(seed, 4, 0.8, 8.0);
        row["priceToSalesRatio"] = priceToSales;
        row["priceToCashFlowRatio"] = mockPortfolioValue(seed, 12, 4.0, 22.0);
        row["priceToDividendRatio"] = 100.0 / dividendYield;
        row["eps"] = price / peRatio;
        row["forwardEps"] = price / row["forwardPeRatio"].toDouble();
        row["dividendPerShare"] = price * dividendYield / 100.0;
        row["dividendYield"] = dividendYield;
        row["payoutRatio"] = row["dividendPerShare"].toDouble() / row["eps"].toDouble() * 100.0;
        row["beta"] = mockPortfolioValue(seed, 20, 0.55, 1.8);
        row["revenue"] = revenue;
        row["netIncome"] = revenue * mockPortfolioValue(seed, 2, 0.05, 0.22);
        row["ebitda"] = revenue * mockPortfolioValue(seed, 10, 0.1, 0.3);
        row["returnOnEquity"] = mockPortfolioValue(seed, 6, 4.0, 30.0);
        row["returnOnAssets"] = mockPortfolioValue(seed, 14, 2.0, 18.0);
        row["debtToEquity"] = mockPortfolioValue(seed, 18, 0.1, 2.5);
        row["sharesOutstanding"] = sharesOutstanding;
        row["week52High"] = price * mockPortfolioValue(seed, 3, 1.05, 1.35);
        row["week52Low"] = price * mockPortfolioValue(seed, 11, 0.55, 0.9);
        row["source"] = QStringLiteral("TEST");
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
            QStringLiteral("fundamentalUpdatedAt"),
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
            {QStringLiteral("fundamentalUpdatedAt"), QStringLiteral("FundamentalUpdatedAt")}
        };
        const QString fundamentalSource = query.value(QStringLiteral("FundamentalSource")).toString().trimmed();
        if (!fundamentalSource.isEmpty()) {
            for (auto it = fundamentalColumns.constBegin(); it != fundamentalColumns.constEnd(); ++it) {
                const QVariant value = query.value(it.value());
                row[it.key() + QStringLiteral("Origin")] = fundamentalSource;
                row[it.key()] = value.isNull() ? QVariant() : value;
            }

        }
        results.append(row);
    }

    return results;
}

double DatabaseManager::closePriceOnOrBefore(const QString &symbol, const QString &date)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return 0.0;
    }

    if (symbol.trimmed().isEmpty() || date.trimmed().isEmpty())
        return 0.0;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT "ClosePrice"
        FROM "Quotes"
        WHERE "Symbol" = :symbol
          AND "CloseDate" <= CAST(:closeDate AS date)
          AND COALESCE("ClosePrice", 0) > 0
        ORDER BY "CloseDate" DESC
        LIMIT 1
    )SQL");
    query.bindValue(":symbol", symbol.trimmed());
    query.bindValue(":closeDate", date.trimmed());

    if (!query.exec()) {
        qCritical() << "Fehler beim Laden des Schlusskurses:" << query.lastError().text();
        return 0.0;
    }

    return query.next() ? query.value(0).toDouble() : 0.0;
}

bool DatabaseManager::isBoughtStock(const QString &symbol)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT 1
        FROM "BoughtStocks"
        WHERE "Symbol" = :symbol
          AND "SellDate" IS NULL
          AND COALESCE("Status", 0) <> 10
        LIMIT 1
    )SQL");
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

bool DatabaseManager::exchangeBoughtStock(
    const QString &sellSymbol,
    const QString &buySymbol,
    const QString &buyName,
    const QString &buyDate,
    double currentValue,
    double entryValue,
    double investedAmount,
    const QString &analysisConfigName)
{
    if (!db.isOpen()) {
        qWarning() << "Datenbank nicht verbunden!";
        return false;
    }

    const QString normalizedSellSymbol = sellSymbol.trimmed();
    const QString normalizedBuySymbol = buySymbol.trimmed();
    const QString normalizedBuyName = buyName.trimmed();
    const QString normalizedBuyDate = buyDate.trimmed();
    if (normalizedSellSymbol.isEmpty()
            || normalizedBuySymbol.isEmpty()
            || normalizedBuyName.isEmpty()
            || normalizedBuyDate.isEmpty()
            || currentValue <= 0.0
            || entryValue <= 0.0
            || investedAmount <= 0.0) {
        qWarning() << "Pflichtfelder fuer Aktienaustausch fehlen.";
        return false;
    }

    const double quantity = investedAmount / entryValue;
    const double valueIncreasePercent = entryValue > 0.0
        ? (currentValue - entryValue) / entryValue * 100.0
        : 0.0;

    if (!db.transaction()) {
        qCritical() << "Fehler beim Starten der Austausch-Transaktion:" << db.lastError().text();
        return false;
    }

    QSqlQuery sellQuery(db);
    sellQuery.prepare(R"SQL(
        UPDATE "BoughtStocks"
        SET
            "SellDate" = :sellDate,
            "CurrentValue" = CAST(:saleValue AS numeric) / NULLIF(COALESCE("Quantity", 1), 0),
            "ValueIncreasePercent" = ROUND(((CAST(:saleValue AS numeric) / NULLIF(COALESCE("Quantity", 1), 0) - "EntryValue") / NULLIF("EntryValue", 0) * 100)::numeric, 2),
            "Status" = 10
        WHERE "Symbol" = :sellSymbol
    )SQL");
    sellQuery.bindValue(QStringLiteral(":sellSymbol"), normalizedSellSymbol);
    sellQuery.bindValue(QStringLiteral(":sellDate"), normalizedBuyDate);
    sellQuery.bindValue(QStringLiteral(":saleValue"), investedAmount);
    if (!sellQuery.exec() || sellQuery.numRowsAffected() <= 0) {
        qCritical() << "Fehler beim Markieren der Verkaufsposition:"
                    << sellQuery.lastError().text() << normalizedSellSymbol;
        db.rollback();
        return false;
    }

    QSqlQuery insertQuery(db);
    insertQuery.prepare(R"SQL(
        INSERT INTO "BoughtStocks" (
            "Symbol",
            "Name",
            "BuyDate",
            "SellDate",
            "CurrentValue",
            "EntryValue",
            "ValueIncreasePercent",
            "Status",
            "Quantity",
            "AnalysisConfigName"
        )
        VALUES (
            :symbol,
            :name,
            :buyDate,
            NULL,
            :currentValue,
            :entryValue,
            :valueIncreasePercent,
            0,
            :quantity,
            NULLIF(:analysisConfigName, '')
        )
        ON CONFLICT ("Symbol") DO UPDATE SET
            "Name" = EXCLUDED."Name",
            "BuyDate" = EXCLUDED."BuyDate",
            "SellDate" = EXCLUDED."SellDate",
            "CurrentValue" = EXCLUDED."CurrentValue",
            "EntryValue" = EXCLUDED."EntryValue",
            "ValueIncreasePercent" = EXCLUDED."ValueIncreasePercent",
            "Status" = EXCLUDED."Status",
            "Quantity" = EXCLUDED."Quantity",
            "AnalysisConfigName" = EXCLUDED."AnalysisConfigName"
    )SQL");
    insertQuery.bindValue(QStringLiteral(":symbol"), normalizedBuySymbol);
    insertQuery.bindValue(QStringLiteral(":name"), normalizedBuyName);
    insertQuery.bindValue(QStringLiteral(":buyDate"), normalizedBuyDate);
    insertQuery.bindValue(QStringLiteral(":currentValue"), currentValue);
    insertQuery.bindValue(QStringLiteral(":entryValue"), entryValue);
    insertQuery.bindValue(QStringLiteral(":valueIncreasePercent"), valueIncreasePercent);
    insertQuery.bindValue(QStringLiteral(":quantity"), quantity);
    insertQuery.bindValue(QStringLiteral(":analysisConfigName"), analysisConfigName.trimmed());

    if (!insertQuery.exec()) {
        qCritical() << "Fehler beim Speichern der Kaufposition:"
                    << insertQuery.lastError().text() << normalizedBuySymbol;
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        qCritical() << "Fehler beim Abschliessen der Austausch-Transaktion:" << db.lastError().text();
        db.rollback();
        return false;
    }

    return true;
}

bool DatabaseManager::saveBoughtStock(
    const QString &symbol,
    const QString &name,
    const QString &buyDate,
    const QString &sellDate,
    double currentValue,
    double entryValue,
    double valueIncreasePercent,
    int status,
    double quantity,
    const QString &analysisConfigName)
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
            "Status",
            "Quantity",
            "AnalysisConfigName"
        )
        VALUES (
            :symbol,
            :name,
            :buyDate,
            NULLIF(:sellDate, '')::date,
            :currentValue,
            :entryValue,
            :valueIncreasePercent,
            :status,
            :quantity,
            NULLIF(:analysisConfigName, '')
        )
        ON CONFLICT ("Symbol") DO UPDATE SET
            "Name" = EXCLUDED."Name",
            "BuyDate" = EXCLUDED."BuyDate",
            "SellDate" = EXCLUDED."SellDate",
            "CurrentValue" = EXCLUDED."CurrentValue",
            "EntryValue" = EXCLUDED."EntryValue",
            "ValueIncreasePercent" = EXCLUDED."ValueIncreasePercent",
            "Status" = EXCLUDED."Status",
            "Quantity" = EXCLUDED."Quantity",
            "AnalysisConfigName" = EXCLUDED."AnalysisConfigName"
    )SQL");
    query.bindValue(":symbol", symbol.trimmed());
    query.bindValue(":name", name.trimmed());
    query.bindValue(":buyDate", buyDate.trimmed());
    query.bindValue(":sellDate", sellDate.trimmed());
    query.bindValue(":currentValue", currentValue);
    query.bindValue(":entryValue", entryValue);
    query.bindValue(":valueIncreasePercent", valueIncreasePercent);
    query.bindValue(":status", status);
    query.bindValue(":quantity", quantity > 0.0 ? quantity : 1.0);
    query.bindValue(":analysisConfigName", analysisConfigName.trimmed());

    if (!query.exec()) {
        qCritical() << "Fehler beim Speichern der gekauften Aktie:" << query.lastError().text();
        return false;
    }

    return true;
}
