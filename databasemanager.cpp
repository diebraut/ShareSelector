#include "databasemanager.h"
#include <QSqlRecord>
#include <QDate>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QtConcurrent>

namespace {
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
    });
    connect(&m_ibkrSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_ibkrConnecting || m_ibkrPortAdvanceInProgress)
            return;
        m_ibkrConnectTimeout.stop();
        ++m_ibkrPortIndex;
        QTimer::singleShot(0, this, &DatabaseManager::tryNextIbkrPort);
    });
    connect(&m_ibkrSocket, &QTcpSocket::disconnected, this, [this]() {
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
        m_ibkrDataLoading = false;
        if (m_ibkrProcess.state() != QProcess::NotRunning)
            m_ibkrProcess.kill();
        setIbkrConnectionState(
            QStringLiteral("Fehler: Zeitüberschreitung beim Abruf der IBKR-Daten."),
            m_ibkrConnected,
            false);
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
    });
    connect(&m_ibkrProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &DatabaseManager::finishIbkrDataRequest);

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
        SELECT "ISIN", "Currency", "CountryCode"
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

    const QString helperPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ibkr-helper/IbkrHelper.exe"));
    if (!QFileInfo::exists(helperPath)) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: Der IBKR-Helfer fehlt im Build-Verzeichnis."),
            m_ibkrConnected,
            false);
        return;
    }

    m_ibkrDataLoading = true;
    m_ibkrPendingSymbol = normalizedSymbol;
    m_ibkrSocket.abort();
    setIbkrConnectionState(
        QStringLiteral("IBKR-Daten für %1 werden abgerufen ...").arg(normalizedSymbol),
        true,
        false);

    const QString localSymbol = normalizedSymbol.section(QLatin1Char('.'), 0, 0);
    QStringList arguments = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(m_ibkrConnectedPort),
        QStringLiteral("--client-id"), QStringLiteral("23"),
        QStringLiteral("--symbol"), localSymbol
    };
    if (!currency.isEmpty())
        arguments << QStringLiteral("--currency") << currency;
    const QString isin = stockQuery.value(QStringLiteral("ISIN")).toString().trimmed();
    if (!isin.isEmpty())
        arguments << QStringLiteral("--isin") << isin;

    m_ibkrProcess.setProgram(helperPath);
    m_ibkrProcess.setArguments(arguments);
    m_ibkrProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_ibkrProcess.start();
    m_ibkrDataTimeout.start();
    emit ibkrConnectionChanged();
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
        return;
    }

    const QJsonObject result = document.object();
    const QString message = result.value(QStringLiteral("message")).toString();
    if (!result.value(QStringLiteral("success")).toBool()) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: %1").arg(message),
            m_ibkrConnected,
            false);
        return;
    }

    const QVariantMap details = result.value(QStringLiteral("data")).toObject().toVariantMap();
    if (!saveIbkrContractDetails(m_ibkrPendingSymbol, details)) {
        setIbkrConnectionState(
            QStringLiteral("Fehler: IBKR-Daten konnten nicht in der Datenbank gespeichert werden."),
            true,
            false);
        return;
    }

    const QString completedSymbol = m_ibkrPendingSymbol;
    setIbkrConnectionState(
        QStringLiteral("%1 Datenbank und Anzeige wurden aktualisiert.").arg(message),
        true,
        false);
    emit ibkrStockDataUpdated(completedSymbol);
}

bool DatabaseManager::saveIbkrContractDetails(const QString &symbol, const QVariantMap &details)
{
    if (!db.transaction()) {
        qCritical() << "IBKR-Update konnte keine Transaktion starten:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"SQL(
        UPDATE "Stocks"
        SET
            "IBKRConId" = :ibkrConId,
            "Currency" = COALESCE(NULLIF(:currency, ''), "Currency"),
            "PrimaryExchange" = COALESCE(NULLIF(:primaryExchange, ''), "PrimaryExchange"),
            "LocalSymbol" = COALESCE(NULLIF(:localSymbol, ''), "LocalSymbol"),
            "SecurityType" = COALESCE(NULLIF(:securityType, ''), "SecurityType"),
            "TradingClass" = COALESCE(NULLIF(:tradingClass, ''), "TradingClass"),
            "StockType" = COALESCE(NULLIF(:stockType, ''), "StockType"),
            "Industry" = COALESCE(NULLIF(:industry, ''), "Industry"),
            "Category" = COALESCE(NULLIF(:category, ''), "Category"),
            "Subcategory" = COALESCE(NULLIF(:subcategory, ''), "Subcategory"),
            "TimeZoneId" = COALESCE(NULLIF(:timeZoneId, ''), "TimeZoneId"),
            "TradingHours" = COALESCE(NULLIF(:tradingHours, ''), "TradingHours"),
            "LiquidHours" = COALESCE(NULLIF(:liquidHours, ''), "LiquidHours"),
            "MinTick" = COALESCE(:minTick, "MinTick"),
            "MarketRuleIds" = COALESCE(NULLIF(:marketRuleIds, ''), "MarketRuleIds"),
            "ValidExchanges" = COALESCE(NULLIF(:validExchanges, ''), "ValidExchanges"),
            "OrderTypes" = COALESCE(NULLIF(:orderTypes, ''), "OrderTypes"),
            "MarketName" = COALESCE(NULLIF(:marketName, ''), "MarketName"),
            "CUSIP" = COALESCE(NULLIF(:cusip, ''), "CUSIP"),
            "ISIN" = COALESCE(NULLIF(:isin, ''), "ISIN"),
            "IBKRLastSyncAt" = CURRENT_TIMESTAMP
        WHERE "Symbol" = :symbol
    )SQL");

    const QStringList textFields = {
        QStringLiteral("currency"), QStringLiteral("primaryExchange"),
        QStringLiteral("localSymbol"), QStringLiteral("securityType"),
        QStringLiteral("tradingClass"), QStringLiteral("stockType"),
        QStringLiteral("industry"), QStringLiteral("category"),
        QStringLiteral("subcategory"), QStringLiteral("timeZoneId"),
        QStringLiteral("tradingHours"), QStringLiteral("liquidHours"),
        QStringLiteral("marketRuleIds"), QStringLiteral("validExchanges"),
        QStringLiteral("orderTypes"), QStringLiteral("marketName"),
        QStringLiteral("cusip"), QStringLiteral("isin")
    };
    query.bindValue(QStringLiteral(":symbol"), symbol);
    query.bindValue(QStringLiteral(":ibkrConId"), details.value(QStringLiteral("ibkrConId")));
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
                ADD COLUMN IF NOT EXISTS "IBKRLastSyncAt" TIMESTAMPTZ
        )SQL"),
        QStringLiteral(R"SQL(
            CREATE UNIQUE INDEX IF NOT EXISTS "Stocks_IBKRConId_uidx"
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
            s."IBKRLastSyncAt"
        FROM "BoughtStocks" b
        LEFT JOIN "Stocks" s ON s."Symbol" = b."Symbol"
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
            QStringLiteral("rawData"), QStringLiteral("fundamentalUpdatedAt")
        };
        for (const QString &field : mockFundamentalFields)
            row[field + QStringLiteral("Origin")] = QStringLiteral("mock");
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
        WITH ordered_quotes AS (
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
            FROM "Quotes"
            WHERE "Symbol" = :symbol
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

