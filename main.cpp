#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include "stockexportcontroller.h"
#include "databasemanager.h"
#include "clipboardhelper.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // MarketStackClient für QML registrieren
    MarketStackClient marketStackClient;
    QQuickStyle::setStyle("Universal");

    DatabaseManager dbManager;
    StockExportController exportController(dbManager);

    QQmlApplicationEngine engine;

    // Korrekte Registrierung
    ClipboardHelper clipboardHelper;
    engine.rootContext()->setContextProperty("clipboardHelper", &clipboardHelper);

    engine.rootContext()->setContextProperty("databaseManager", &dbManager);
    engine.rootContext()->setContextProperty("exportController", &exportController);
    engine.rootContext()->setContextProperty("marketStackClient", &marketStackClient);

    const QUrl url(u"qrc:/qt/qml/ShareSelector/Main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
