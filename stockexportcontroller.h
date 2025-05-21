#pragma once
#include <QObject>
#include "marketstackclient.h"
#include "databasemanager.h"

class StockExportController : public QObject
{
    Q_OBJECT
public:
    explicit StockExportController(DatabaseManager& dbManager, QObject *parent = nullptr);
    Q_INVOKABLE void exportShares(const QString &country);

signals:
    void exportFinished();
    void errorOccurred(const QString &error);

private:
    DatabaseManager& m_dbManager;
    MarketStackClient m_api;

    int m_expectedShares = 0;
    int m_savedShares = 0;
};
