#ifndef ISINFETCHER_H
#define ISINFETCHER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class IsinFetcher : public QObject {
    Q_OBJECT

public:
    explicit IsinFetcher(QObject *parent = nullptr);
    void fetchISIN(const QString &symbol, const QString &apiKey);

signals:
    void isinReceived(const QString &isin);
    void errorOccurred(const QString &message);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager manager;
};

#endif // ISINFETCHER_H
