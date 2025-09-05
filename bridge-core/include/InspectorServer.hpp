#pragma once
#include <QObject>
#include <QHostAddress>
#include <QJsonObject>
#include <QHash>
#include <QPointer>
#include <QStringList>
class QQmlApplicationEngine;
class QWebSocketServer;
class QWebSocket;

class InspectorServer : public QObject {
    Q_OBJECT
public:
    InspectorServer(QQmlApplicationEngine* engine,
                    const QHostAddress& addr,
                    quint16 port,
                    const QString& token = QString(),
                    QObject* parent = nullptr);

private:
    QQmlApplicationEngine* m_engine { nullptr };
    QWebSocketServer* m_server { nullptr };
    QString m_token;

    struct SubscriptionInfo {
        QString subscriptionId;
        QString kind;            // "signal" or "property"
        QString name;            // signal signature or property name
        QString objectId;        // cached string id for sender
        int signalIndex { -1 };  // senderSignalIndex for matching
        QPointer<QObject> target;
        QMetaObject::Connection connection;
        QStringList snapshotProperties; // for signal kind, include these props in event
    };

    // Per-client subscriptions keyed by subscriptionId
    QHash<QWebSocket*, QHash<QString, SubscriptionInfo>> m_subscriptions;
    quint64 m_nextSubId { 1 };

    void handleTextMessage(QWebSocket* client, const QString& text);
    QJsonObject replyOk(const QString& id, const QJsonObject& result = {});
    QJsonObject replyErr(const QString& id, const QString& code, const QString& message);

    // helpers
    static QString idForObject(QObject* obj);
    static QObject* objectFromId(const QString& id);
    static QJsonValue variantToJson(const QVariant& v);
    QJsonObject inspectObject(QObject* obj);
    QJsonObject evaluateOnObject(QObject* obj, const QString& expression);

private slots:
    void onSignalTriggered();
};
