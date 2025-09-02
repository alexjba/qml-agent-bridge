#pragma once
#include <QObject>
#include <QHostAddress>
class QQmlApplicationEngine;
class InspectorServer : public QObject {
    Q_OBJECT
public:
    InspectorServer(QQmlApplicationEngine* engine, const QHostAddress& addr, quint16 port, const QString& token = QString(), QObject* parent = nullptr);
};
