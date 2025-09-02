#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QHostAddress>
#include "InspectorServer.hpp"
int main(int argc, char** argv){
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral(qrc:/main.qml)));
    InspectorServer server(&engine, QHostAddress::LocalHost, 7777);
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
