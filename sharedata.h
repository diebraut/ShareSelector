// sharedata.h
#pragma once
#include <QString>
#include <QDate>

#include <QMetaType>

struct ShareData {
    QString mic;
    QString isin;
    QString name;
    QString symbol;
    QString exchange;
    QString countryCode;
    QString city;
    double  lastClosePrice;
    QDate   lastQuoteDate;

    static ShareData fromJson(const QJsonObject& obj);
};

Q_DECLARE_METATYPE(ShareData)
