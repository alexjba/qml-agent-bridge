#include <QCoreApplication>
#include <QCommandLineParser>
#include <QWebSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

int main(int argc, char** argv){
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.addHelpOption();
    QCommandLineOption urlOpt({"u", "url"}, "ws url", "url", "ws://127.0.0.1:7777");
    QCommandLineOption methodOpt({"m", "method"}, "method", "method", "hello");
    QCommandLineOption paramsOpt({"p", "params"}, "json params", "json", "{}");
    p.addOption(urlOpt); p.addOption(methodOpt); p.addOption(paramsOpt);
    p.process(app);

    const QUrl url(p.value(urlOpt));
    const QString method = p.value(methodOpt);
    const QJsonObject params = QJsonDocument::fromJson(p.value(paramsOpt).toUtf8()).object();

    QWebSocket sock;
    QObject::connect(&sock, &QWebSocket::connected, &app, [&](){
        static int id = 1;
        QJsonObject req{{"id", QString::number(id++)}, {"method", method}, {"params", params}};
        sock.sendTextMessage(QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact)));
    });
    QObject::connect(&sock, &QWebSocket::textMessageReceived, &app, [&](const QString& msg){
        printf("%s\n", msg.toUtf8().constData());
        app.quit();
    });
    QObject::connect(&sock, &QWebSocket::errorOccurred, &app, [&](QAbstractSocket::SocketError){
        fprintf(stderr, "error: %s\n", sock.errorString().toUtf8().constData());
        app.exit(2);
    });

    sock.open(url);
    QTimer::singleShot(5000, &app, [&](){ if(sock.state()!=QAbstractSocket::ConnectedState) app.exit(3); });
    return app.exec();
}