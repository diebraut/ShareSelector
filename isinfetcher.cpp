#include "isinfetcher.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

IsinFetcher::IsinFetcher(QObject *parent)
    : QObject(parent) {
    connect(&manager, &QNetworkAccessManager::finished, this, &IsinFetcher::onReplyFinished);
}

void IsinFetcher::fetchISIN(const QString &symbol, const QString &apiKey) {
    QString url = QString("https://eodhistoricaldata.com/api/search/%1?api_token=%2&fmt=json")
                      .arg(symbol, apiKey);

    QNetworkRequest request((QUrl(url)));
    manager.get(request);
}

void IsinFetcher::onReplyFinished(QNetworkReply *reply) {
    if (reply->error()) {
        emit errorOccurred("Fehler: " + reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray response = reply->readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(response);

    if (jsonDoc.isArray()) {
        QJsonArray array = jsonDoc.array();
        if (!array.isEmpty()) {
            QJsonObject obj = array.first().toObject();
            QString isin = obj.value("isin").toString();
            emit isinReceived(isin);
        } else {
            emit errorOccurred("Keine Ergebnisse.");
        }
    } else {
        emit errorOccurred("Antwort ist kein gültiges JSON-Array.");
    }

    reply->deleteLater();
}
