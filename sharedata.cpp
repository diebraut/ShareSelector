#include "sharedata.h"
#include <QJsonObject>

ShareData ShareData::fromJson(const QJsonObject& obj) {
    ShareData data;

    data.name = obj["name"].toString();
    data.symbol = obj["symbol"].toString();

    if (obj.contains("stock_exchange") && obj["stock_exchange"].isObject()) {
        QJsonObject stockExchange = obj["stock_exchange"].toObject();
        data.mic = stockExchange["mic"].toString();
        data.isin = stockExchange["isin"].toString();
        data.exchange = stockExchange["name"].toString();
        data.countryCode = stockExchange["country_code"].toString();
        data.city = stockExchange["city"].toString();
        data.lastQuoteDate = QDate::fromString(stockExchange["lastquotedate"].toString(), Qt::ISODate);
    }

    // Handle lastClosePrice if it exists at root level
    if (obj.contains("lastClosePrice")) {
        data.lastClosePrice = obj["lastClosePrice"].toDouble();
    }

    return data;
}
