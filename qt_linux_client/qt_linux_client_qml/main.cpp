#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "AppController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ESP32Project");
    app.setApplicationName("esp32_qt_client");

    QQmlApplicationEngine engine;
    AppController controller;

    engine.rootContext()->setContextProperty("appController", &controller);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
