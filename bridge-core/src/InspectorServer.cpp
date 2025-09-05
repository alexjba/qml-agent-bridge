#include "InspectorServer.hpp"

#include <QQmlApplicationEngine>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaMethod>
#include <QVariant>
#include <QAbstractItemModel>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQmlProperty>

InspectorServer::InspectorServer(QQmlApplicationEngine* engine,
                                 const QHostAddress& addr,
                                 quint16 port,
                                 const QString& token,
                                 QObject* parent)
    : QObject(parent), m_engine(engine), m_token(token)
{
    m_server = new QWebSocketServer(QStringLiteral("QmlAgentBridge"),
                                    QWebSocketServer::NonSecureMode, this);
    if (!m_server->listen(addr, port)) {
        return;
    }

    connect(m_server, &QWebSocketServer::newConnection, this, [this]() {
        QWebSocket* client = m_server->nextPendingConnection();
        connect(client, &QWebSocket::textMessageReceived, this, [this, client](const QString& msg){
            handleTextMessage(client, msg);
        });
        connect(client, &QWebSocket::disconnected, this, [this, client]{
            // Clean up any subscriptions for this client
            auto it = m_subscriptions.find(client);
            if (it != m_subscriptions.end()) {
                for (auto subIt = it->begin(); subIt != it->end(); ++subIt) {
                    QObject::disconnect(subIt->connection);
                }
                m_subscriptions.erase(it);
            }
            client->deleteLater();
        });
    });
}

void InspectorServer::handleTextMessage(QWebSocket* client, const QString& text)
{
    const auto doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) {
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr({}, "bad_request", "Invalid JSON")).toJson(QJsonDocument::Compact)));
        return;
    }
    const auto obj = doc.object();
    const QString id = obj.value("id").toString();
    const QString method = obj.value("method").toString();

    if (method == QLatin1String("hello")) {
        QJsonObject result{{"protocol", "qml-agent-bridge"},
                           {"version", "0.2"},
                           {"capabilities", QJsonArray{
                               QLatin1String("list_roots"), QLatin1String("find_by_name"), QLatin1String("inspect"),
                               QLatin1String("list_children"), QLatin1String("set_property"), QLatin1String("call_method"),
                               QLatin1String("evaluate"), QLatin1String("subscribe_signal"), QLatin1String("subscribe_property"),
                               QLatin1String("unsubscribe")
                           }}};
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, result)).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("list_roots")) {
        QJsonArray roots;
        for (QObject* obj : m_engine->rootObjects()) {
            QJsonObject r{{"objectId", idForObject(obj)},
                          {"type", obj->metaObject()->className()},
                          {"objectName", obj->objectName()}};
            roots.push_back(r);
        }
        QJsonObject result{{"roots", roots}};
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, result)).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("find_by_name")) {
        const auto params = obj.value("params").toObject();
        const auto name = params.value("name").toString();
        QJsonArray matches;
        for (QObject* root : m_engine->rootObjects()) {
            const auto list = root->findChildren<QObject*>(name, Qt::FindChildrenRecursively);
            for (QObject* m : list) {
                matches.push_back(QJsonObject{{"objectId", idForObject(m)},
                                             {"type", m->metaObject()->className()},
                                             {"objectName", m->objectName()}});
            }
        }
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, QJsonObject{{"matches", matches}})).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("list_children")) {
        const auto params = obj.value("params").toObject();
        const auto oid = params.value("objectId").toString();
        QObject* target = objectFromId(oid);
        if (!target) {
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "not_found", "Object not found")).toJson(QJsonDocument::Compact)));
            return;
        }
        QJsonArray children;
        for (QObject* c : target->children()) {
            children.push_back(QJsonObject{{"objectId", idForObject(c)},
                                          {"type", c->metaObject()->className()},
                                          {"objectName", c->objectName()}});
        }
        qInfo() << "RPC list_children" << oid << "->" << children.size();
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, QJsonObject{{"children", children}})).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("inspect")) {
        const auto params = obj.value("params").toObject();
        const auto oid = params.value("objectId").toString();
        QObject* target = objectFromId(oid);
        if (!target) {
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "not_found", "Object not found")).toJson(QJsonDocument::Compact)));
            return;
        }
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, inspectObject(target))).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("model_info")) {
        const auto params = obj.value("params").toObject();
        const auto oid = params.value("objectId").toString();
        QObject* target = objectFromId(oid);
        auto* model = qobject_cast<QAbstractItemModel*>(target);
        if (!model) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "bad_request", "Target is not a model")).toJson(QJsonDocument::Compact))); return; }
        QJsonObject out;
        out.insert("rowCount", model->rowCount());
        out.insert("columnCount", model->columnCount());
        QJsonArray roles;
        const auto r = model->roleNames();
        for (auto it = r.begin(); it != r.end(); ++it) roles.push_back(QString::fromUtf8(it.value()));
        out.insert("roles", roles);
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, out)).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("model_fetch")) {
        const auto params = obj.value("params").toObject();
        const auto oid = params.value("objectId").toString();
        const int start = params.value("start").toInt(0);
        const int count = params.value("count").toInt(20);
        const auto rolesParam = params.value("roles");
        QObject* target = objectFromId(oid);
        auto* model = qobject_cast<QAbstractItemModel*>(target);
        if (!model) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "bad_request", "Target is not a model")).toJson(QJsonDocument::Compact))); return; }
        const int rc = model->rowCount();
        const int cc = model->columnCount();
        const int from = qMax(0, start);
        const int to = qMin(rc, from + qMax(0, count));
        const auto roleNames = model->roleNames();

        // Build role id set
        QVector<int> roleIds;
        if (rolesParam.isArray()) {
            const auto arr = rolesParam.toArray();
            for (const auto& v : arr) {
                if (!v.isString()) continue;
                const QByteArray name = v.toString().toUtf8();
                int found = -1;
                for (auto it = roleNames.begin(); it != roleNames.end(); ++it) {
                    if (it.value() == name) { found = it.key(); break; }
                }
                if (found >= 0) roleIds.push_back(found);
            }
        } else {
            // default: all roles
            for (auto it = roleNames.begin(); it != roleNames.end(); ++it) roleIds.push_back(it.key());
        }

        QJsonObject out;
        out.insert("rowCount", rc);
        out.insert("columnCount", cc);
        if (cc <= 1) {
            QJsonArray items;
            for (int row = from; row < to; ++row) {
                QJsonObject item;
                for (int role : roleIds) {
                    const QByteArray roleName = roleNames.value(role);
                    QVariant v = model->data(model->index(row, 0), role);
                    item.insert(QString::fromUtf8(roleName), variantToJson(v));
                }
                items.push_back(item);
            }
            out.insert("items", items);
        } else {
            QJsonArray rows;
            for (int row = from; row < to; ++row) {
                QJsonObject rowObj;
                rowObj.insert("row", row);
                QJsonArray columns;
                for (int col = 0; col < cc; ++col) {
                    QJsonObject colObj;
                    for (int role : roleIds) {
                        const QByteArray roleName = roleNames.value(role);
                        QVariant v = model->data(model->index(row, col), role);
                        colObj.insert(QString::fromUtf8(roleName), variantToJson(v));
                    }
                    columns.push_back(colObj);
                }
                rowObj.insert("columns", columns);
                rows.push_back(rowObj);
            }
            out.insert("rows", rows);
        }
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, out)).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("set_property")) {
        const auto params = obj.value("params").toObject();
        const auto oid = params.value("objectId").toString();
        const auto name = params.value("name").toString();
        const auto value = params.value("value");
        QObject* target = objectFromId(oid);
        if (!target) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "not_found", "Object not found")).toJson(QJsonDocument::Compact))); return; }
        QVariant v;
        if (value.isBool()) v = value.toBool();
        else if (value.isDouble()) v = value.toDouble();
        else if (value.isString()) v = value.toString();
        else if (value.isNull()) v = QVariant();
        else { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "bad_request", "Unsupported value type")).toJson(QJsonDocument::Compact))); return; }
        bool ok = target->setProperty(name.toUtf8().constData(), v);
        if (!ok) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "failed", "setProperty returned false")).toJson(QJsonDocument::Compact))); return; }
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, QJsonObject{{"ok", true}})).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("call_method")) {
        const auto params = obj.value("params").toObject();
        const auto oid = params.value("objectId").toString();
        const auto name = params.value("name").toString();
        const auto args = params.value("args").toArray();
        QObject* target = objectFromId(oid);
        if (!target) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "not_found", "Object not found")).toJson(QJsonDocument::Compact))); return; }
        bool invoked = false;
        if (args.isEmpty()) {
            if (QQmlContext* ctx = QQmlEngine::contextForObject(target)) {
                QQmlExpression expr(ctx, target, name + QLatin1String("()"));
                QVariant v = expr.evaluate();
                if (expr.hasError()) {
                    client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "failed", expr.error().description())).toJson(QJsonDocument::Compact))); return;
                }
                QJsonObject result{{"ok", true}, {"result", variantToJson(v)}};
                client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, result)).toJson(QJsonDocument::Compact)));
                return;
            } else {
                invoked = QMetaObject::invokeMethod(target, name.toUtf8().constData());
            }
        } else {
            // Build a QML expression: name(arg0, arg1, ...)
            QStringList parts;
            parts.reserve(args.size());
            for (const QJsonValue& v : args) {
                if (v.isBool()) parts << (v.toBool() ? QLatin1String("true") : QLatin1String("false"));
                else if (v.isDouble()) parts << QString::number(v.toDouble(), 'g', 16);
                else if (v.isString()) {
                    QString s = v.toString();
                    s.replace(QLatin1String("\\"), QLatin1String("\\\\"));
                    s.replace(QLatin1String("\""), QLatin1String("\\\""));
                    s.replace(QLatin1String("\n"), QLatin1String("\\n"));
                    s.replace(QLatin1String("\r"), QLatin1String("\\r"));
                    parts << (QLatin1String("\"") + s + QLatin1String("\""));
                }
                else if (v.isNull()) parts << QLatin1String("null");
                else { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "bad_request", "Unsupported arg type")).toJson(QJsonDocument::Compact))); return; }
            }
            const QString callExpr = name + QLatin1String("(") + parts.join(QLatin1String(", ")) + QLatin1String(")");
            if (QQmlContext* ctx = QQmlEngine::contextForObject(target)) {
                QQmlExpression expr(ctx, target, callExpr);
                QVariant v = expr.evaluate();
                if (expr.hasError()) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "failed", expr.error().description())).toJson(QJsonDocument::Compact))); return; }
                QJsonObject result{{"ok", true}, {"result", variantToJson(v)}};
                client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, result)).toJson(QJsonDocument::Compact)));
                return;
            } else {
                client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "failed", "No QML context for target")).toJson(QJsonDocument::Compact))); return;
            }
        }
        if (!invoked) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "failed", "invoke failed")).toJson(QJsonDocument::Compact))); return; }
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, QJsonObject{{"ok", true}})).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("evaluate")) {
        const auto params = obj.value("params").toObject();
        const auto oid = params.value("objectId").toString();
        const auto expr = params.value("expression").toString();
        QObject* target = objectFromId(oid);
        if (!target) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "not_found", "Object not found")).toJson(QJsonDocument::Compact))); return; }
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, evaluateOnObject(target, expr))).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("subscribe_signal")) {
        const auto params = obj.value("params").toObject();
        const auto oid = params.value("objectId").toString();
        const auto sig = params.value("signal").toString(); // e.g., "clicked()" or "textChanged(QString)"
        const auto snapshot = params.value("snapshot"); // array or string of property names
        QObject* target = objectFromId(oid);
        if (!target) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "not_found", "Object not found")).toJson(QJsonDocument::Compact))); return; }

        const QMetaObject* mo = target->metaObject();
        // (debug dump removed)
        const QByteArray normalizedWanted = QMetaObject::normalizedSignature(sig.toUtf8().constData());
        const QString wantedBase = sig.section('(', 0, 0);
        int signalIndex = -1;
        QMetaMethod signalMethod;
        for (int i = 0; i < mo->methodCount(); ++i) {
            QMetaMethod m = mo->method(i);
            if (m.methodType() == QMetaMethod::Signal) {
                const QByteArray mnorm = QMetaObject::normalizedSignature(m.methodSignature().constData());
                const QString mbase = QString::fromLatin1(m.methodSignature()).section('(', 0, 0);
                if (mnorm == normalizedWanted || mbase == wantedBase) {
                    signalIndex = m.methodIndex();
                    signalMethod = m;
                    break;
                }
            }
        }
        if (signalIndex < 0) {
            // Fallback: if requesting FooChanged or FooChanged(...), attempt property notify
            QString base = wantedBase;
            if (base.endsWith(QLatin1String("Changed"))) {
                QString propName = base.left(base.size() - 7);
                int pidx = mo->indexOfProperty(propName.toUtf8().constData());
                if (pidx >= 0) {
                    QMetaProperty mp = mo->property(pidx);
                    if (mp.hasNotifySignal()) {
                        int notifyIdx = mp.notifySignal().methodIndex();
                        int slotIndex = this->metaObject()->indexOfSlot("onSignalTriggered()");
                        QMetaMethod slotMethod = this->metaObject()->method(slotIndex);
                        bool ok = QQmlProperty(target, propName).connectNotifySignal(this, slotMethod.methodIndex());
                        if (ok) {
                            const QString subId = QStringLiteral("sub:%1").arg(m_nextSubId++);
                            SubscriptionInfo info;
                            info.subscriptionId = subId;
                            info.kind = QLatin1String("signal");
                            info.name = base + QLatin1String("()");
                            info.objectId = idForObject(target);
                            info.signalIndex = notifyIdx;
                            info.target = target;
                            // no connection handle available from QQmlProperty::connectNotifySignal
                            if (snapshot.isArray()) {
                                const auto arr = snapshot.toArray();
                                for (const auto& v : arr) if (v.isString()) info.snapshotProperties.push_back(v.toString());
                            } else if (snapshot.isString()) {
                                info.snapshotProperties.push_back(snapshot.toString());
                            }
                            m_subscriptions[client].insert(subId, info);
                            qInfo() << "RPC subscribe_signal (fallback to property notify)" << oid << info.name;
                            client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, QJsonObject{{"subscriptionId", subId}})).toJson(QJsonDocument::Compact)));
                            return;
                        }
                    }
                }
            }
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "bad_request", "Signal not found on object")).toJson(QJsonDocument::Compact))); return;
        }

        int slotIndex = this->metaObject()->indexOfSlot("onSignalTriggered()");
        QMetaMethod slotMethod = this->metaObject()->method(slotIndex);

        QMetaObject::Connection conn = QObject::connect(target, signalMethod, this, slotMethod);
        // Fallback to string-based connect for broader compatibility (allows extra args)
        if (!conn) {
            const QByteArray sigStr = QByteArrayLiteral("2") + normalizedWanted; // SIGNAL()
            const QByteArray slotStr = QByteArrayLiteral("1onSignalTriggered()"); // SLOT()
            conn = QObject::connect(target, sigStr.constData(), this, slotStr.constData());
        }
        if (!conn) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "failed", "Connection failed")).toJson(QJsonDocument::Compact))); return; }

        const QString subId = QStringLiteral("sub:%1").arg(m_nextSubId++);
        SubscriptionInfo info;
        info.subscriptionId = subId;
        info.kind = QLatin1String("signal");
        info.name = QString::fromLatin1(signalMethod.methodSignature());
        info.objectId = idForObject(target);
        info.signalIndex = signalIndex;
        info.target = target;
        info.connection = conn;
        if (snapshot.isArray()) {
            const auto arr = snapshot.toArray();
            for (const auto& v : arr) if (v.isString()) info.snapshotProperties.push_back(v.toString());
        } else if (snapshot.isString()) {
            info.snapshotProperties.push_back(snapshot.toString());
        }
        m_subscriptions[client].insert(subId, info);

        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, QJsonObject{{"subscriptionId", subId}})).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("subscribe_property")) {
        const auto params = obj.value("params").toObject();
        const auto oid = params.value("objectId").toString();
        const auto name = params.value("name").toString();
        QObject* target = objectFromId(oid);
        if (!target) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "not_found", "Object not found")).toJson(QJsonDocument::Compact))); return; }

        QQmlProperty prop(target, name);
        if (!prop.isValid()) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "bad_request", "Invalid property")).toJson(QJsonDocument::Compact))); return; }

        int slotIndex = this->metaObject()->indexOfSlot("onSignalTriggered()");
        QMetaMethod slotMethod = this->metaObject()->method(slotIndex);
        bool ok = prop.connectNotifySignal(this, slotMethod.methodIndex());
        if (!ok) { client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "failed", "Notify connection failed")).toJson(QJsonDocument::Compact))); return; }

        // Try to resolve the notify signal index if available
        int notifyIndex = -1;
        const QMetaObject* mo = target->metaObject();
        int propIndex = mo->indexOfProperty(name.toUtf8().constData());
        if (propIndex >= 0) {
            QMetaProperty mp = mo->property(propIndex);
            if (mp.hasNotifySignal()) notifyIndex = mp.notifySignal().methodIndex();
        }

        const QString subId = QStringLiteral("sub:%1").arg(m_nextSubId++);
        SubscriptionInfo info;
        info.subscriptionId = subId;
        info.kind = QLatin1String("property");
        info.name = name;
        info.objectId = idForObject(target);
        info.signalIndex = notifyIndex;
        info.target = target;
        // We cannot get a QMetaObject::Connection from QQmlProperty::connectNotifySignal; leave default
        m_subscriptions[client].insert(subId, info);

        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, QJsonObject{{"subscriptionId", subId}})).toJson(QJsonDocument::Compact)));
        return;
    }

    if (method == QLatin1String("unsubscribe")) {
        const auto params = obj.value("params").toObject();
        const auto subId = params.value("subscriptionId").toString();
        auto it = m_subscriptions.find(client);
        if (it == m_subscriptions.end() || !it->contains(subId)) {
            client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "not_found", "Subscription not found")).toJson(QJsonDocument::Compact))); return;
        }
        SubscriptionInfo info = it->value(subId);
        if (info.connection) QObject::disconnect(info.connection);
        it->remove(subId);
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyOk(id, QJsonObject{{"ok", true}})).toJson(QJsonDocument::Compact)));
        return;
    }

    client->sendTextMessage(QString::fromUtf8(QJsonDocument(replyErr(id, "not_implemented", "Unknown method")).toJson(QJsonDocument::Compact)));
}

QJsonObject InspectorServer::replyOk(const QString& id, const QJsonObject& result)
{
    QJsonObject o;
    o.insert("id", id);
    o.insert("result", result);
    return o;
}

QJsonObject InspectorServer::replyErr(const QString& id, const QString& code, const QString& message)
{
    QJsonObject o;
    if (!id.isEmpty()) o.insert("id", id);
    o.insert("error", QJsonObject{{"code", code}, {"message", message}});
    return o;
}

QString InspectorServer::idForObject(QObject* obj)
{
    return QStringLiteral("qobj:%1").arg(reinterpret_cast<quintptr>(obj), 0, 16);
}

QObject* InspectorServer::objectFromId(const QString& id)
{
    if (!id.startsWith(QLatin1String("qobj:"))) return nullptr;
    bool ok = false;
    quintptr ptr = id.mid(5).toULongLong(&ok, 16);
    if (!ok || ptr == 0) return nullptr;
    return reinterpret_cast<QObject*>(ptr);
}

QJsonValue InspectorServer::variantToJson(const QVariant& v)
{
    if (!v.isValid()) {
        return QJsonValue();
    }
    switch (v.typeId()) {
    case QMetaType::Bool:
        return QJsonValue(v.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        return QJsonValue(v.toDouble());
    case QMetaType::QString:
        return QJsonValue(v.toString());
    case QMetaType::QVariantList: {
        QJsonArray arr;
        for (const auto& e : v.toList()) arr.push_back(variantToJson(e));
        return arr;
    }
    case QMetaType::QVariantMap: {
        QJsonObject o;
        const auto map = v.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            o.insert(it.key(), variantToJson(it.value()));
        return o;
    }
    default:
        return QJsonValue(QString::fromLatin1(v.typeName()));
    }
}

QJsonObject InspectorServer::inspectObject(QObject* obj)
{
    QJsonObject out;
    out.insert("objectId", idForObject(obj));
    out.insert("type", obj->metaObject()->className());
    out.insert("objectName", obj->objectName());

    QJsonObject props;
    const QMetaObject* mo = obj->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        props.insert(QString::fromLatin1(p.name()), variantToJson(obj->property(p.name())));
    }
    out.insert("properties", props);

    QJsonArray methods;
    QJsonArray signalList;
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        const QString sig = QString::fromLatin1(m.methodSignature());
        methods.push_back(sig);
        if (m.methodType() == QMetaMethod::Signal) {
            signalList.push_back(sig);
        }
    }
    out.insert("methods", methods);
    out.insert("signals", signalList);

    // Child count for quick overview
    out.insert("childrenCount", obj->children().size());

    // If model, add rowCount summary
    if (auto* model = qobject_cast<QAbstractItemModel*>(obj)) {
        out.insert("model", QJsonObject{{"rowCount", model->rowCount()}});
    }

    return out;
}

QJsonObject InspectorServer::evaluateOnObject(QObject* obj, const QString& expression)
{
    QJsonObject out;
    if (QQmlContext* ctx = QQmlEngine::contextForObject(obj)) {
        QQmlExpression expr(ctx, obj, expression);
        QVariant v = expr.evaluate();
        if (expr.hasError()) {
            out.insert("error", QJsonObject{{"message", expr.error().description()}, {"line", expr.error().line()}});
        } else {
            out.insert("result", variantToJson(v));
        }
    } else {
        out.insert("error", QJsonObject{{"message", "No QML context"}});
    }
    return out;
}

void InspectorServer::onSignalTriggered()
{
    QObject* s = sender();
    const int sigIndex = senderSignalIndex();
    if (!s) return;

    // Broadcast to all clients that have a matching subscription
    for (auto clientIt = m_subscriptions.begin(); clientIt != m_subscriptions.end(); ++clientIt) {
        QWebSocket* client = clientIt.key();
        const auto& subs = clientIt.value();
        for (auto it = subs.begin(); it != subs.end(); ++it) {
            const SubscriptionInfo& info = it.value();
            bool matches = false;
            if (info.target == s) {
                if (info.signalIndex == -1 || info.signalIndex == sigIndex) {
                    matches = true;
                } else if (sigIndex >= 0) {
                    const QMetaObject* smo = s->metaObject();
                    if (sigIndex < smo->methodCount()) {
                        const QMetaMethod em = smo->method(sigIndex);
                        if (QString::fromLatin1(em.methodSignature()) == info.name) {
                            matches = true;
                        }
                    }
                }
            }
            if (matches) {
                QJsonObject evt{{"subscriptionId", info.subscriptionId},
                                {"objectId", info.objectId},
                                {"kind", info.kind},
                                {"name", info.name}};
                if (info.kind == QLatin1String("property")) {
                    const QVariant val = s->property(info.name.toUtf8().constData());
                    evt.insert("value", variantToJson(val));
                }
                if (info.kind == QLatin1String("signal") && !info.snapshotProperties.isEmpty()) {
                    QJsonObject snap;
                    for (const QString& propName : info.snapshotProperties) {
                        snap.insert(propName, variantToJson(s->property(propName.toUtf8().constData())));
                    }
                    evt.insert("snapshot", snap);
                }
                QJsonObject envelope{{"method", "event"}, {"params", evt}};
                client->sendTextMessage(QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact)));
            }
        }
    }
}
