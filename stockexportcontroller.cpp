#include "stockexportcontroller.h"

StockExportController::StockExportController(DatabaseManager& dbManager, QObject *parent)
    : QObject(parent), m_dbManager(dbManager)
{
    // Fehler aus der API durchleiten
    connect(&m_api, &MarketStackClient::errorOccurred,
            this, &StockExportController::errorOccurred);

    // Fortschritt überwachen
    connect(&m_dbManager, &DatabaseManager::saveComplete, this, [=](const QString &symbol) {
        qDebug() << "✅ Aktie gespeichert:" << symbol;
        m_savedShares++;

        if (m_savedShares >= m_expectedShares) {
            qDebug() << "🎉 Alle Aktien gespeichert!";
            emit exportFinished();
        }
    });
}

void StockExportController::exportShares(const QString &exchange)
{
    m_savedShares = 0;
    m_expectedShares = 0;

    connect(&m_api, &MarketStackClient::sharesReceived,
            this, [=](const QList<ShareData> &shares) {
                m_expectedShares = shares.size();
                qDebug() << "📦 Empfangen:" << m_expectedShares << "Aktien";

                if (m_expectedShares == 0) {
                    emit exportFinished();
                    return;
                }

                m_dbManager.saveShares(shares);  // ⚠️ Einzelne Methode verwenden!
            });

    m_api.getShares(exchange.toUpper());
}
