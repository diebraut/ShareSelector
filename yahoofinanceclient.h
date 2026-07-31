#ifndef YAHOOFINANCECLIENT_H
#define YAHOOFINANCECLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QStringList>
#include <QVariantMap>

class YahooFinanceClient : public QObject
{
    Q_OBJECT
public:
    explicit YahooFinanceClient(QObject *parent = nullptr);

    void fetchFundamentals(const QString &requestSymbol, const QString &yahooSymbol);
    void resolveSymbol(const QString &requestSymbol, const QString &keywords);

signals:
    void fundamentalsReceived(const QString &requestSymbol, const QString &yahooSymbol, const QVariantMap &data);
    void fundamentalsFailed(const QString &requestSymbol, const QString &yahooSymbol, const QString &message);
    void symbolResolved(const QString &requestSymbol, const QStringList &yahooSymbols);
    void symbolResolveFailed(const QString &requestSymbol, const QString &message);

private:
    void fetchCookieAndCrumb(const QString &requestSymbol, const QString &yahooSymbol);
    void fetchCrumbAndFundamentals(const QString &requestSymbol, const QString &yahooSymbol);
    void fetchFundamentalsSummary(const QString &requestSymbol, const QString &yahooSymbol);
    void fetchFundamentalsPage(const QString &requestSymbol, const QString &yahooSymbol);
    void handleCookieReply(QNetworkReply *reply, const QString &requestSymbol, const QString &yahooSymbol);
    void handleCrumbReply(QNetworkReply *reply, const QString &requestSymbol, const QString &yahooSymbol);
    void handleFundamentalsReply(QNetworkReply *reply, const QString &requestSymbol, const QString &yahooSymbol);
    void handleFundamentalsPageReply(QNetworkReply *reply, const QString &requestSymbol, const QString &yahooSymbol);
    void handleSymbolSearchReply(QNetworkReply *reply, const QString &requestSymbol, const QString &keywords);

    QNetworkAccessManager networkManager;
    QString m_yahooCrumb;
};

#endif // YAHOOFINANCECLIENT_H
