#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <memory>
#include "MappingStarter.h"

struct ApiStub {
    QTcpServer server;
    QList<QByteArray> requests;
    QList<QByteArray> responses;
    ApiStub() {
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                auto input = std::make_shared<QByteArray>();
                auto done = std::make_shared<bool>(false);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [=] {
                    input->append(socket->readAll());
                    const int boundary = input->indexOf("\r\n\r\n");
                    if (boundary < 0 || *done) return;
                    int length = 0;
                    for (auto line : input->left(boundary).split('\n')) {
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    }
                    if (input->size() < boundary + 4 + length) return;
                    *done = true;
                    requests.append(*input);
                    if (!responses.isEmpty()) {
                        const auto body = responses.takeFirst();
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                      + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    }
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    static QByteArray status(const QString &state) {
        return QJsonDocument(QJsonObject{{"protocol", 1}, {"service", "agri-mapping-start"},
                                        {"state", state}, {"message", "mapping status"}}).toJson();
    }
};

class MappingStarterTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QSettings().remove("RealtimeMapping/autoStart"); }
    void cleanup() { QSettings().remove("RealtimeMapping/autoStart"); }
    void automaticRequestAndStatusPolling() {
        ApiStub api;
        api.responses = {ApiStub::status("starting"), ApiStub::status("ready")};
        MappingStarter starter;
        starter.setEndpoint("127.0.0.1", api.server.serverPort());
        QVERIFY(starter.autoStart());
        starter.startIfEnabled();
        starter.start(); // Coalesce repeated clicks while startup is pending.
        QTRY_COMPARE(api.requests.size(), 1);
        QVERIFY(api.requests[0].startsWith("POST /ensure "));
        const auto body = api.requests[0].mid(api.requests[0].indexOf("\r\n\r\n") + 4);
        QCOMPARE(QJsonDocument::fromJson(body).object(), (QJsonObject{{"protocol", 1}, {"operation", "ensure_mapping"}}));
        QTRY_COMPARE_WITH_TIMEOUT(api.requests.size(), 2, 4000);
        QVERIFY(api.requests[1].startsWith("GET /status "));
        QTRY_VERIFY(!starter.busy());
        QTest::qWait(50);
        QCOMPARE(api.requests.size(), 2);
    }
    void optOutAndDemoSuppression() {
        ApiStub api;
        api.responses = {ApiStub::status("ready")};
        MappingStarter starter;
        starter.setEndpoint("127.0.0.1", api.server.serverPort());
        starter.setAutoStart(false);
        MappingStarter persisted;
        QVERIFY(!persisted.autoStart());
        starter.startIfEnabled();
        QTest::qWait(50);
        QVERIFY(api.requests.isEmpty());
        starter.setSuspended(true);
        starter.start();
        QTest::qWait(50);
        QVERIFY(api.requests.isEmpty());
        starter.setSuspended(false);
        starter.start(); // Manual mapping remains available after opt-out.
        QTRY_COMPARE(api.requests.size(), 1);
        QTRY_VERIFY(!starter.busy());
    }
    void invalidResponseStopsAndManualRetryWorks() {
        ApiStub api;
        api.responses = {QByteArrayLiteral("{\"state\":\"ready\"}"), ApiStub::status("ready")};
        MappingStarter starter;
        starter.setEndpoint("127.0.0.1", api.server.serverPort());
        starter.start();
        QTRY_COMPARE(api.requests.size(), 1);
        QTRY_VERIFY(!starter.busy());
        QVERIFY(starter.status().contains(QStringLiteral("接口不匹配")));
        starter.start();
        QTRY_COMPARE(api.requests.size(), 2);
        QTRY_VERIFY(!starter.busy());
    }
    void destroyWithPendingRequest() {
        ApiStub api;
        auto starter = std::make_unique<MappingStarter>();
        starter->setEndpoint("127.0.0.1", api.server.serverPort());
        starter->start();
        QTRY_COMPARE(api.requests.size(), 1);
        starter.reset();
        QCoreApplication::processEvents();
    }
};
QTEST_GUILESS_MAIN(MappingStarterTest)
#include "tst_mapping_starter.moc"
