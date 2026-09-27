#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>
#include <QtWebView>
#include "databasemanager.h"
#include "windowgeometryhelper.h"

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

    const QSettings settings;
    const QString tradeRepublicPhone = qEnvironmentVariableIsSet("SHARESELECTOR_TR_PHONE")
        ? qEnvironmentVariable("SHARESELECTOR_TR_PHONE")
        : settings.value(QStringLiteral("tradeRepublic/phone")).toString();
    const QString tradeRepublicPin = qEnvironmentVariableIsSet("SHARESELECTOR_TR_PIN")
        ? qEnvironmentVariable("SHARESELECTOR_TR_PIN")
        : settings.value(QStringLiteral("tradeRepublic/pin")).toString();

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
