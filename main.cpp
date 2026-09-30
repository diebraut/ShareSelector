#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QtWebView>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif
#include "databasemanager.h"
#include "windowgeometryhelper.h"

static QJsonObject tradeRepublicCredentials()
{
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (CredReadW(L"ShareSelector/TradeRepublic", CRED_TYPE_GENERIC, 0, &credential)) {
        const QByteArray blob(reinterpret_cast<const char *>(credential->CredentialBlob),
                              static_cast<qsizetype>(credential->CredentialBlobSize));
        CredFree(credential);
        return QJsonDocument::fromJson(blob).object();
    }
#endif
    return {};
}

int main(int argc, char *argv[])
{
    qputenv("QT_WEBVIEW_PLUGIN", "webview2");
    QtWebView::initialize();
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ShareSelector"));
    QCoreApplication::setApplicationName(QStringLiteral("ShareSelector"));

    QQuickStyle::setStyle("Universal");

    DatabaseManager dbManager;
    WindowGeometryHelper windowGeometryHelper;

    QQmlApplicationEngine engine;

    const QJsonObject credentials = tradeRepublicCredentials();
    const QString tradeRepublicPhone = credentials.value(QStringLiteral("phone")).toString();
    const QString tradeRepublicPin = credentials.value(QStringLiteral("pin")).toString();
    QSettings settings;
    settings.remove(QStringLiteral("tradeRepublic/phone"));
    settings.remove(QStringLiteral("tradeRepublic/pin"));

    engine.rootContext()->setContextProperty("databaseManager", &dbManager);
    engine.rootContext()->setContextProperty("windowGeometryHelper", &windowGeometryHelper);
    engine.rootContext()->setContextProperty("tradeRepublicLoginPhone", tradeRepublicPhone);
    engine.rootContext()->setContextProperty("tradeRepublicLoginPin", tradeRepublicPin);

    const QUrl url(u"qrc:/qt/qml/ShareSelector/Main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
