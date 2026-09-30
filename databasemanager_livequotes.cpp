#include "databasemanager.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>

#include <cmath>

QVariantList DatabaseManager::getActiveDepotLiveQuoteStocks(int depotId, bool observedOnly)
{
    QVariantList results;
    if (!db.isOpen())
        return results;

    QSqlQuery query(db);
    query.prepare(R"SQL(
        SELECT b."Symbol" AS "Symbol",
               COALESCE(NULLIF(s."Name", ''), NULLIF(b."Name", ''), b."Symbol") AS "Name",
               COALESCE(b."Quantity", 1) AS "Quantity",
               COALESCE(s."IBKRConId", 0) AS "ConId",
               COALESCE(NULLIF(s."IBKRResolvedSymbol", ''), NULLIF(s."LocalSymbol", ''),
                        split_part(b."Symbol", '.', 1)) AS "IbkrSymbol",
               COALESCE(NULLIF(s."IBKRContractCurrency", ''), s."Currency", '') AS "Currency",
               COALESCE(NULLIF(s."IBKRQuoteExchange", ''), NULLIF(s."PrimaryExchange", ''),
                        'SMART') AS "Exchange",
               COALESCE(s."PrimaryExchange", '') AS "PrimaryExchange",
               COALESCE(s."ValidExchanges", '') AS "ValidExchanges"
        FROM "BoughtStocks" b
        LEFT JOIN "Stocks" s ON s."Symbol" = b."Symbol"
        WHERE b."DepotId" = :depotId
          AND b."SellDate" IS NULL
          AND COALESCE(b."Status", 0) <> 10
          AND (:observedOnly = FALSE OR COALESCE(b."Observed", FALSE) = TRUE)
        ORDER BY "Name" ASC
    )SQL");
    query.bindValue(QStringLiteral(":depotId"), depotId);
    query.bindValue(QStringLiteral(":observedOnly"), observedOnly);
    if (!query.exec()) {
        qWarning() << "IBKR-Livepositionen konnten nicht geladen werden:" << query.lastError().text();
        return results;
    }
    while (query.next()) {
        QVariantMap row;
        QString directExchange = query.value(QStringLiteral("Exchange")).toString().trimmed();
        const QString primaryExchange = query.value(QStringLiteral("PrimaryExchange")).toString().trimmed();
        QStringList validExchanges = query.value(QStringLiteral("ValidExchanges"))
                                         .toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString &validExchange : validExchanges)
            validExchange = validExchange.trimmed();
        if (!validExchanges.isEmpty()
            && !validExchanges.contains(directExchange, Qt::CaseInsensitive)) {
            const QString withoutSuffix = directExchange.endsWith(QLatin1Char('2'))
                ? directExchange.chopped(1) : directExchange;
            if (validExchanges.contains(withoutSuffix, Qt::CaseInsensitive))
                directExchange = withoutSuffix;
            else if (validExchanges.contains(primaryExchange, Qt::CaseInsensitive))
                directExchange = primaryExchange;
            else {
                directExchange.clear();
                for (const QString &candidate : validExchanges) {
                    if (candidate.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0) {
                        directExchange = candidate;
                        break;
                    }
                }
            }
        }
        const bool supportsSmart = validExchanges.contains(QStringLiteral("SMART"), Qt::CaseInsensitive);
        const QString exchange = supportsSmart ? QStringLiteral("SMART") : directExchange;
        const QString fallbackExchange = supportsSmart
            && directExchange.compare(QStringLiteral("SMART"), Qt::CaseInsensitive) != 0
            ? directExchange : QString();
        row[QStringLiteral("symbol")] = query.value(QStringLiteral("Symbol"));
        row[QStringLiteral("name")] = query.value(QStringLiteral("Name"));
        row[QStringLiteral("quantity")] = query.value(QStringLiteral("Quantity"));
        row[QStringLiteral("conId")] = query.value(QStringLiteral("ConId"));
        row[QStringLiteral("ibkrSymbol")] = query.value(QStringLiteral("IbkrSymbol"));
        row[QStringLiteral("currency")] = query.value(QStringLiteral("Currency"));
        row[QStringLiteral("exchange")] = exchange;
        row[QStringLiteral("fallbackExchange")] = fallbackExchange;
        row[QStringLiteral("primaryExchange")] = primaryExchange;
        results.append(row);
    }
    return results;
}

bool DatabaseManager::ibkrLiveQuotesActive() const
{
    return m_ibkrLiveProcess.state() != QProcess::NotRunning;
}

QString DatabaseManager::ibkrLiveQuotesStatus() const
{
    return m_ibkrLiveQuotesStatus;
}

QVariantMap DatabaseManager::ibkrLiveQuotes() const
{
    return m_ibkrLiveQuotes;
}

void DatabaseManager::initializeIbkrLiveQuotes()
{
    m_ibkrLivePersistTimer.setInterval(20000);
    connect(&m_ibkrLivePersistTimer, &QTimer::timeout,
            this, &DatabaseManager::flushIbkrLiveMidQuotes);
    connect(&m_ibkrLiveProcess, &QProcess::readyReadStandardOutput,
            this, &DatabaseManager::readIbkrLiveQuotesOutput);
    connect(&m_ibkrLiveProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QString error = QString::fromUtf8(m_ibkrLiveProcess.readAllStandardError()).trimmed();
        if (!error.isEmpty())
            qWarning().noquote() << "IBKR-Livekurse:" << error;
    });
    connect(&m_ibkrLiveProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
        m_ibkrLiveQuotesStatus = QStringLiteral("IBKR-Liveprozess konnte nicht gestartet werden.");
        emit ibkrLiveQuotesChanged();
    });
    connect(&m_ibkrLiveProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        readIbkrLiveQuotesOutput();
        m_ibkrLivePersistTimer.stop();
        if (exitStatus == QProcess::NormalExit && exitCode == 0)
            flushIbkrLiveMidQuotes();
        m_ibkrLivePendingQuotes.clear();
        m_ibkrLivePendingTradeTimes.clear();
        if (m_ibkrLiveQuotesStatus != QStringLiteral("IBKR-Livekurse sind ausgeschaltet.")) {
            m_ibkrLiveQuotesStatus = exitStatus == QProcess::NormalExit && exitCode == 0
                ? QStringLiteral("IBKR-Liveprozess beendet.")
                : QStringLiteral("IBKR-Liveprozess beendet (Code %1). Details im Anwendungslog.")
                      .arg(exitCode);
        }
        emit ibkrLiveQuotesChanged();
    });
}

void DatabaseManager::readIbkrLiveQuotesOutput()
{
    m_ibkrLiveBuffer += m_ibkrLiveProcess.readAllStandardOutput();
    while (true) {
        const qsizetype newline = m_ibkrLiveBuffer.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = m_ibkrLiveBuffer.left(newline).trimmed();
        m_ibkrLiveBuffer.remove(0, newline + 1);
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject())
            continue;
        const QJsonArray rows = document.object().value(QStringLiteral("rows")).toArray();
        QVariantMap quotes;
        int pricedCount = 0;
        int waitingCount = 0;
        int closedCount = 0;
        int errorCount = 0;
        const QDateTime receivedAtUtc = QDateTime::fromString(
            document.object().value(QStringLiteral("receivedAtUtc")).toString(), Qt::ISODateWithMs);
        for (const QJsonValue &value : rows) {
            const QJsonObject object = value.toObject();
            const QString symbol = object.value(QStringLiteral("symbol")).toString();
            if (symbol.isEmpty())
                continue;
            const QVariantMap quote = object.toVariantMap();
            quotes.insert(symbol, quote);
            const QDateTime tradeAt = QDateTime::fromString(
                object.value(QStringLiteral("lastTradeAtUtc")).toString(), Qt::ISODateWithMs);
            if (tradeAt.isValid() && tradeAt <= QDateTime::currentDateTimeUtc().addSecs(60)
                && object.value(QStringLiteral("marketDataType")).toInt() < 2
                && tradeAt > m_ibkrLiveSavedTradeTimes.value(symbol).toDateTime()
                && tradeAt > m_ibkrLivePendingTradeTimes.value(symbol).toDateTime()) {
                m_ibkrLivePendingTradeTimes.insert(symbol, tradeAt);
                if (!m_ibkrLivePersistTimer.isActive())
                    m_ibkrLivePersistTimer.start();
            }
            const QString status = object.value(QStringLiteral("status")).toString();
            if (status == QStringLiteral("Handelsplatz geschlossen"))
                ++closedCount;
            else if (status.startsWith(QStringLiteral("IBKR ")))
                ++errorCount;
            else if (object.value(QStringLiteral("bid")).isDouble()
                     && object.value(QStringLiteral("ask")).isDouble())
                ++pricedCount;
            else
                ++waitingCount;
            if (!receivedAtUtc.isValid() || !object.value(QStringLiteral("mid")).isDouble()
                || !object.value(QStringLiteral("bid")).isDouble()
                || !object.value(QStringLiteral("ask")).isDouble()
                || object.value(QStringLiteral("currency")).toString() != QStringLiteral("EUR")
                || object.value(QStringLiteral("marketDataType")).toInt() >= 2)
                continue;
            const double bid = object.value(QStringLiteral("bid")).toDouble();
            const double ask = object.value(QStringLiteral("ask")).toDouble();
            const double mid = (bid + ask) / 2.0;
            const QDate closeDate = QDate::fromString(
                object.value(QStringLiteral("tradingDate")).toString(), Qt::ISODate);
            const QDateTime bidAt = QDateTime::fromString(
                object.value(QStringLiteral("bidReceivedUtc")).toString(), Qt::ISODateWithMs);
            const QDateTime askAt = QDateTime::fromString(
                object.value(QStringLiteral("askReceivedUtc")).toString(), Qt::ISODateWithMs);
            if (!std::isfinite(mid) || bid <= 0 || ask < bid || !closeDate.isValid()
                || !bidAt.isValid() || !askAt.isValid()
                || qAbs(bidAt.secsTo(receivedAtUtc)) > 60
                || qAbs(askAt.secsTo(receivedAtUtc)) > 60
                || qAbs(bidAt.secsTo(askAt)) > 60)
                continue;
            m_ibkrLivePendingQuotes.insert(symbol, quote);
            if (!m_ibkrLivePersistTimer.isActive())
                m_ibkrLivePersistTimer.start();
        }
        m_ibkrLiveQuotes = quotes;
        m_ibkrLiveQuotesStatus = QStringLiteral("%1 Positionen: %2 mit Geld/Brief, %3 warten, %4 geschlossen, %5 IBKR-Fehler. Max. 40 Leitungen, Rotation etwa alle 12 s.")
                                     .arg(rows.size()).arg(pricedCount).arg(waitingCount)
                                     .arg(closedCount).arg(errorCount);
        emit ibkrLiveQuotesChanged();
    }
}

void DatabaseManager::flushIbkrLiveMidQuotes()
{
    if ((m_ibkrLivePendingQuotes.isEmpty() && m_ibkrLivePendingTradeTimes.isEmpty())
        || !db.isOpen())
        return;
    if (!db.transaction()) {
        qWarning() << "IBKR-Live-Mittelwerte: Transaktion konnte nicht gestartet werden:"
                   << db.lastError().text();
        return;
    }
    QSqlQuery saveQuery(db);
    if (!saveQuery.prepare(R"SQL(
        INSERT INTO "Quotes" (
            "Symbol", "CloseDate", "ClosePrice", "IBKRCloseSource",
            "IBKRLiveBid", "IBKRLiveAsk", "IBKRLiveBidReceivedAt",
            "IBKRLiveAskReceivedAt", "IBKRLiveQuoteAt", "IBKRLiveLastTradeAt",
            "IBKRLiveExchange"
        ) VALUES (
            :symbol, :closeDate, :mid, 'live-mid',
            :bid, :ask, :bidAt, :askAt, :quoteAt, :lastTradeAt, :exchange
        )
        ON CONFLICT ("Symbol", "CloseDate") DO UPDATE SET
            "ClosePrice" = EXCLUDED."ClosePrice",
            "OpenPrice" = NULL,
            "HighestPrice" = NULL,
            "LowestPrice" = NULL,
            "Volume" = NULL,
            "IBKRCloseSource" = 'live-mid',
            "IBKRLiveBid" = EXCLUDED."IBKRLiveBid",
            "IBKRLiveAsk" = EXCLUDED."IBKRLiveAsk",
            "IBKRLiveBidReceivedAt" = EXCLUDED."IBKRLiveBidReceivedAt",
            "IBKRLiveAskReceivedAt" = EXCLUDED."IBKRLiveAskReceivedAt",
            "IBKRLiveQuoteAt" = EXCLUDED."IBKRLiveQuoteAt",
            "IBKRLiveLastTradeAt" = GREATEST(
                "Quotes"."IBKRLiveLastTradeAt", EXCLUDED."IBKRLiveLastTradeAt"),
            "IBKRLiveExchange" = EXCLUDED."IBKRLiveExchange"
        -- Retain the newest bid/ask receipt independently of the last-trade time.
        WHERE "Quotes"."IBKRCloseSource" IS DISTINCT FROM 'snapshot'
          AND EXCLUDED."IBKRLiveQuoteAt" > COALESCE(
              "Quotes"."IBKRLiveQuoteAt", '-infinity'::timestamptz)
    )SQL")) {
        qWarning() << "IBKR-Live-Mittelwerte: SQL konnte nicht vorbereitet werden:"
                   << saveQuery.lastError().text();
        db.rollback();
        return;
    }
    QSqlQuery positionQuery(db);
    if (!positionQuery.prepare(R"SQL(
        UPDATE "BoughtStocks"
        SET "CurrentValue" = :mid,
            "ValueIncreasePercent" = CASE
                WHEN NULLIF("EntryValue", 0) IS NULL THEN "ValueIncreasePercent"
                ELSE ROUND(((CAST(:mid AS numeric) - "EntryValue")
                    / NULLIF("EntryValue", 0) * 100)::numeric, 2)
            END,
            "UpdatedAt" = CURRENT_TIMESTAMP
        WHERE "Symbol" = :symbol
          AND "SellDate" IS NULL
          AND COALESCE("Status", 0) <> 10
          AND "CurrentValue" IS DISTINCT FROM CAST(:mid AS numeric)
    )SQL")) {
        qWarning() << "IBKR-Live-Mittelwerte: Depot-Update konnte nicht vorbereitet werden:"
                   << positionQuery.lastError().text();
        db.rollback();
        return;
    }
    QSqlQuery tradeTimeQuery(db);
    if (!tradeTimeQuery.prepare(R"SQL(
        UPDATE "Quotes" q
        SET "IBKRLiveLastTradeAt" = :lastTradeAt
        WHERE q."Symbol" = :symbol
          AND q."CloseDate" = (
              SELECT MAX(latest."CloseDate")
              FROM "Quotes" latest
              WHERE latest."Symbol" = :symbol
                AND COALESCE(latest."ClosePrice", 0) > 0
          )
          AND q."IBKRLiveLastTradeAt" IS DISTINCT FROM :lastTradeAt
          AND :lastTradeAt > COALESCE(q."IBKRLiveLastTradeAt", '-infinity'::timestamptz)
    )SQL")) {
        qWarning() << "IBKR-Handelszeit: SQL konnte nicht vorbereitet werden:"
                   << tradeTimeQuery.lastError().text();
        db.rollback();
        return;
    }

    QVariantMap savedMidQuotes;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (auto it = m_ibkrLivePendingQuotes.cbegin(); it != m_ibkrLivePendingQuotes.cend(); ++it) {
        const QVariantMap quote = it.value().toMap();
        const QVariantMap current = m_ibkrLiveQuotes.value(it.key()).toMap();
        if (current.value(QStringLiteral("status")).toString()
                == QStringLiteral("Handelsplatz geschlossen"))
            continue;
        const double bid = quote.value(QStringLiteral("bid")).toDouble();
        const double ask = quote.value(QStringLiteral("ask")).toDouble();
        const QDate closeDate = QDate::fromString(
            quote.value(QStringLiteral("tradingDate")).toString(), Qt::ISODate);
        const QDateTime bidAt = QDateTime::fromString(
            quote.value(QStringLiteral("bidReceivedUtc")).toString(), Qt::ISODateWithMs);
        const QDateTime askAt = QDateTime::fromString(
            quote.value(QStringLiteral("askReceivedUtc")).toString(), Qt::ISODateWithMs);
        const QDateTime quoteAt = bidAt > askAt ? bidAt : askAt;
        const QDateTime lastTradeAt = QDateTime::fromString(
            quote.value(QStringLiteral("lastTradeAtUtc")).toString(), Qt::ISODateWithMs);
        if (bid <= 0 || ask < bid || !closeDate.isValid()
            || !bidAt.isValid() || !askAt.isValid()
            || qAbs(quoteAt.secsTo(now)) > 60)
            continue;
        const double mid = (bid + ask) / 2.0;
        saveQuery.bindValue(QStringLiteral(":symbol"), it.key());
        saveQuery.bindValue(QStringLiteral(":closeDate"), closeDate);
        saveQuery.bindValue(QStringLiteral(":mid"), mid);
        saveQuery.bindValue(QStringLiteral(":bid"), bid);
        saveQuery.bindValue(QStringLiteral(":ask"), ask);
        saveQuery.bindValue(QStringLiteral(":bidAt"), bidAt);
        saveQuery.bindValue(QStringLiteral(":askAt"), askAt);
        saveQuery.bindValue(QStringLiteral(":quoteAt"), quoteAt);
        saveQuery.bindValue(QStringLiteral(":lastTradeAt"), lastTradeAt.isValid()
                                ? QVariant(lastTradeAt) : QVariant());
        saveQuery.bindValue(QStringLiteral(":exchange"),
                            quote.value(QStringLiteral("exchange")).toString());
        if (!saveQuery.exec()) {
            qWarning() << "IBKR-Live-Mittelwert konnte nicht gespeichert werden:"
                       << it.key() << saveQuery.lastError().text();
            db.rollback();
            return;
        }
        if (saveQuery.numRowsAffected() > 0) {
            positionQuery.bindValue(QStringLiteral(":mid"), mid);
            positionQuery.bindValue(QStringLiteral(":symbol"), it.key());
            if (!positionQuery.exec()) {
                qWarning() << "IBKR-Live-Depotwert konnte nicht gespeichert werden:"
                           << it.key() << positionQuery.lastError().text();
                db.rollback();
                return;
            }
            savedMidQuotes.insert(it.key(), mid);
        }
    }
    for (auto it = m_ibkrLivePendingTradeTimes.cbegin();
         it != m_ibkrLivePendingTradeTimes.cend(); ++it) {
        tradeTimeQuery.bindValue(QStringLiteral(":symbol"), it.key());
        tradeTimeQuery.bindValue(QStringLiteral(":lastTradeAt"), it.value());
        if (!tradeTimeQuery.exec()) {
            qWarning() << "IBKR-Handelszeit konnte nicht gespeichert werden:"
                       << it.key() << tradeTimeQuery.lastError().text();
            db.rollback();
            return;
        }
        if (tradeTimeQuery.numRowsAffected() > 0)
            savedMidQuotes.insert(it.key(), QVariant());
    }
    if (!db.commit()) {
        qWarning() << "IBKR-Live-Mittelwerte konnten nicht bestaetigt werden:"
                   << db.lastError().text();
        db.rollback();
        return;
    }
    for (auto it = m_ibkrLivePendingTradeTimes.cbegin();
         it != m_ibkrLivePendingTradeTimes.cend(); ++it)
        m_ibkrLiveSavedTradeTimes.insert(it.key(), it.value());
    m_ibkrLivePendingQuotes.clear();
    m_ibkrLivePendingTradeTimes.clear();
    if (!savedMidQuotes.isEmpty())
        emit ibkrLiveMidQuotesSaved(savedMidQuotes);
}

bool DatabaseManager::startIbkrLiveQuotes(int depotId, bool observedOnly)
{
    if (ibkrLiveQuotesActive())
        return true;
    const QVariantList rows = getActiveDepotLiveQuoteStocks(depotId, observedOnly);
    QJsonArray contracts;
    int skipped = 0;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("conId")).toInt() <= 0
            || row.value(QStringLiteral("ibkrSymbol")).toString().isEmpty()
            || row.value(QStringLiteral("currency")).toString().isEmpty()) {
            ++skipped;
            continue;
        }
        QJsonObject object;
        object.insert(QStringLiteral("symbol"), row.value(QStringLiteral("symbol")).toString());
        object.insert(QStringLiteral("ibkrSymbol"), row.value(QStringLiteral("ibkrSymbol")).toString());
        object.insert(QStringLiteral("conId"), row.value(QStringLiteral("conId")).toInt());
        object.insert(QStringLiteral("currency"), row.value(QStringLiteral("currency")).toString());
        object.insert(QStringLiteral("exchange"), row.value(QStringLiteral("exchange")).toString());
        object.insert(QStringLiteral("fallbackExchange"), row.value(QStringLiteral("fallbackExchange")).toString());
        object.insert(QStringLiteral("primaryExchange"), row.value(QStringLiteral("primaryExchange")).toString());
        contracts.append(object);
    }
    if (contracts.isEmpty()) {
        m_ibkrLiveQuotesStatus = QStringLiteral("Keine Depotposition mit IBKR-Kontrakt gefunden.");
        emit ibkrLiveQuotesChanged();
        return false;
    }
    if (!refreshIbkrConnectionState(QStringLiteral("IBKR-Livekurse"))) {
        m_ibkrLiveQuotesStatus = QStringLiteral("IBKR TWS/Gateway ist nicht erreichbar.");
        emit ibkrLiveQuotesChanged();
        return false;
    }
    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        m_ibkrLiveQuotesStatus = QStringLiteral("IBKR-Helfer fehlt im Build-Verzeichnis.");
        emit ibkrLiveQuotesChanged();
        return false;
    }
    m_ibkrLiveBuffer.clear();
    m_ibkrLiveQuotes.clear();
    m_ibkrLivePendingQuotes.clear();
    m_ibkrLivePendingTradeTimes.clear();
    m_ibkrLiveSavedTradeTimes.clear();
    m_ibkrLivePersistTimer.stop();
    const QByteArray payload = QJsonDocument(contracts).toJson(QJsonDocument::Compact).toBase64();
    m_ibkrLiveProcess.setProgram(helperPath);
    m_ibkrLiveProcess.setArguments({QStringLiteral("--stream-quotes"),
                                    QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                                    QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
                                    QStringLiteral("--client-id"), QStringLiteral("943"),
                                    QStringLiteral("--contracts-base64"), QString::fromLatin1(payload)});
    m_ibkrLiveProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrLiveQuotesStatus = QStringLiteral("IBKR-Livekurse starten für %1 Positionen%2 …")
                                 .arg(contracts.size())
                                 .arg(skipped > 0 ? QStringLiteral(" (%1 ohne Kontrakt)").arg(skipped)
                                                  : QString());
    m_ibkrLiveProcess.start();
    emit ibkrLiveQuotesChanged();
    return true;
}

void DatabaseManager::stopIbkrLiveQuotes()
{
    m_ibkrLivePersistTimer.stop();
    m_ibkrLivePendingQuotes.clear();
    m_ibkrLivePendingTradeTimes.clear();
    if (m_ibkrLiveProcess.state() != QProcess::NotRunning) {
        m_ibkrLiveProcess.kill();
        m_ibkrLiveProcess.waitForFinished(2000);
    }
    m_ibkrLiveQuotes.clear();
    m_ibkrLiveBuffer.clear();
    m_ibkrLiveQuotesStatus = QStringLiteral("IBKR-Livekurse sind ausgeschaltet.");
    emit ibkrLiveQuotesChanged();
}
