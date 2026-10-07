#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include "../src/GroundLink.h"

class GroundLinkTest : public QObject {
    Q_OBJECT
private slots:
    void rejectsInvalidConfiguration()
    {
        GroundLink link;
        link.connectToGateway("", 8765);
        QVERIFY(!link.connected());
        QVERIFY(link.status().contains(QStringLiteral("无效")));
        link.connectToGateway("127.0.0.1", 0);
        QVERIFY(!link.connected());
    }

    void acceptsOnlyValidTelemetry()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        GroundLink link;
        link.connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000));
        QTcpSocket *peer = server.nextPendingConnection();
        QVERIFY(peer);
        peer->write("{\"type\":\"telemetry\",\"protocol\":99}\n");
        peer->flush();
        QTest::qWait(80);
        QVERIFY(!link.connected());
        peer->write("{\"protocol\":1,\"type\":\"telemetry\",\"server\":\"agri-onboard-ros\","
                    "\"fcu_connected\":true,\"armed\":false,\"mission_ready\":true,"
                    "\"odom_fresh\":true,\"mode\":\"OFFBOARD\",\"battery_percent\":67.0,"
                    "\"world_x\":1.5,\"world_y\":2.5,\"world_z\":3.5}\n");
        peer->flush();
        QTRY_VERIFY_WITH_TIMEOUT(link.connected(), 2000);
        QVERIFY(link.fcuConnected());
        QVERIFY(link.missionReady());
        QCOMPARE(link.mode(), QStringLiteral("OFFBOARD"));
        QCOMPARE(link.batteryPercent(), 67.0);
        QCOMPARE(link.worldX(), 1.5);
        QVERIFY(peer->bytesAvailable() == 0);
        peer->disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(!link.connected(), 2000);
        QVERIFY(!link.fcuConnected());
    }
};

QTEST_GUILESS_MAIN(GroundLinkTest)
#include "tst_ground_link.moc"
