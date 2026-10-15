#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtEndian>
#include <limits>
#include <cstring>
#include <QTemporaryDir>
#include <QFile>
#include "MappingClient.h"

namespace {
QByteArray packet(QJsonObject header, QByteArray payload = {}) {
    if (!header.contains("protocol")) header["protocol"] = 1;
    if (!header.contains("session")) header["session"] = "test-session";
    if (!header.contains("frame")) header["frame"] = "world";
    if (!header.contains("stamp")) header["stamp"] = 1.0;
    const auto json = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray bytes(12, '\0');
    std::memcpy(bytes.data(), "AGM1", 4);
    qToLittleEndian<quint32>(json.size(), reinterpret_cast<uchar *>(bytes.data() + 4));
    qToLittleEndian<quint32>(payload.size(), reinterpret_cast<uchar *>(bytes.data() + 8));
    return bytes + json + payload;
}
QByteArray cloud(float x, float y = 0, float z = 0, float intensity = 1) {
    QByteArray bytes(16, '\0'); int offset = 0;
    for (float value : {x, y, z, intensity}) {
        quint32 bits; std::memcpy(&bits, &value, 4);
        qToLittleEndian<quint32>(bits, reinterpret_cast<uchar *>(bytes.data() + offset)); offset += 4;
    }
    return bytes;
}
}
class MappingClientTest : public QObject {
    Q_OBJECT
private slots:
    void fragmentedFramesAndOriginReset();
    void invalidLengthsAndNonFinite();
    void stalePoseAndPause();
    void boundedMapAndExport();
    void mapCapacityAndReconnect();
    void clearAndResumeDiscardPartialFrames();
    void staleCachedSnapshotStaysStale();
    void destroyWhileConnected();
};
void MappingClientTest::fragmentedFramesAndOriginReset() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
    MappingClient client; client.setHost("127.0.0.1"); client.setPort(server.serverPort()); client.setEnabled(true);
    QTRY_VERIFY(server.hasPendingConnections());
    auto *peer = server.nextPendingConnection();
    const auto bytes = packet({{"type", "cloud"}, {"seq", 1}, {"point_count", 1}, {"snapshot", true}}, cloud(1));
    peer->write(bytes.left(5)); peer->flush(); QTest::qWait(15); QCOMPARE(client.pointCount(), 0);
    peer->write(bytes.mid(5, 18)); peer->flush(); QTest::qWait(15); QCOMPARE(client.pointCount(), 0);
    peer->write(bytes.mid(23)); peer->flush();
    QTRY_COMPARE(client.pointCount(), 1);
    QTRY_VERIFY(client.snapshot()->points.size() == 1);
    peer->write(packet({{"type", "cloud"}, {"seq", 1}, {"session", "new-origin"}, {"point_count", 1}}, cloud(5)));
    QTRY_COMPARE(client.snapshot()->points.size(), 1);
    QTRY_COMPARE(client.snapshot()->points.first().x, 5.0f);
    peer->write(packet({{"type", "cloud"}, {"seq", 2}, {"session", "new-origin"}, {"frame", "new_world"}, {"point_count", 1}}, cloud(8)));
    QTRY_COMPARE(client.frameId(), QString("new_world"));
    QTRY_COMPARE(client.snapshot()->points.first().x, 8.0f);
    client.setEnabled(false);
}
void MappingClientTest::invalidLengthsAndNonFinite() {
    for (int testCase = 0; testCase < 3; ++testCase) {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        MappingClient client; client.setHost("127.0.0.1"); client.setPort(server.serverPort()); client.setEnabled(true);
        QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
        if (testCase == 0) {
            QByteArray bad("AGM1", 4); bad.resize(12);
            qToLittleEndian<quint32>(16385, reinterpret_cast<uchar *>(bad.data() + 4));
            qToLittleEndian<quint32>(0, reinterpret_cast<uchar *>(bad.data() + 8)); peer->write(bad);
        } else if (testCase == 1) {
            peer->write(packet({{"type", "cloud"}, {"seq", 1}, {"point_count", 2}}, cloud(1)));
        } else {
            peer->write(packet({{"type", "cloud"}, {"seq", 1}, {"point_count", 1}}, cloud(std::numeric_limits<float>::quiet_NaN())));
        }
        QTRY_VERIFY(client.status().startsWith(QStringLiteral("协议错误")));
        QVERIFY(!client.connected()); QCOMPARE(client.pointCount(), 0); client.setEnabled(false);
    }
}
void MappingClientTest::stalePoseAndPause() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
    MappingClient client; client.setHost("127.0.0.1"); client.setPort(server.serverPort()); client.setEnabled(true);
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    peer->write(packet({{"type", "pose"}, {"seq", 1}, {"position", QJsonArray{1, 2, 3}}, {"orientation", QJsonArray{0, 0, 0, 1}}}));
    QTRY_VERIFY(client.hasPose()); QVERIFY(client.stale()); // Pose/heartbeats cannot make missing clouds live.
    peer->write(packet({{"type", "cloud"}, {"seq", 2}, {"point_count", 1}}, cloud(1)));
    QTRY_COMPARE(client.pointCount(), 1); QTRY_VERIFY(!client.stale());
    client.setPaused(true);
    peer->write(packet({{"type", "cloud"}, {"seq", 3}, {"point_count", 1}}, cloud(2)));
    QTest::qWait(150); QCOMPARE(client.pointCount(), 1);
    client.setPaused(false);
    peer->write(packet({{"type", "cloud"}, {"seq", 4}, {"point_count", 1}}, cloud(3)));
    QTRY_COMPARE(client.pointCount(), 2);
    QTest::qWait(3300); QTRY_VERIFY(client.stale()); QTRY_VERIFY(!client.hasPose());
    client.setEnabled(false);
}
void MappingClientTest::boundedMapAndExport() {
    MappingClient client;
    client.startDemo(); QTRY_VERIFY(client.pointCount() > 1000); QVERIFY(client.demo());
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QSignalSpy exported(&client, &MappingClient::exportFinished);
    const QString path = directory.filePath("map.pcd");
    client.savePcd(QUrl::fromLocalFile(path));
    QTRY_COMPARE(exported.count(), 1); QVERIFY(exported.first().first().toBool());
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.read(300).contains("DATA binary\n"));
    client.stopDemo(); QVERIFY(client.demo()); // Retained synthetic points remain labelled.
    QVERIFY(client.pointCount() <= 250000);
    client.clearMap(); QCOMPARE(client.pointCount(), 0);
}
void MappingClientTest::mapCapacityAndReconnect() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
    MappingClient client; client.setHost("127.0.0.1"); client.setPort(server.serverPort()); client.setEnabled(true);
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    for (int batch = 0; batch < 2; ++batch) {
        QByteArray payload; payload.reserve(140000 * 16);
        for (int i = 0; i < 140000; ++i) payload.append(cloud(float(batch * 140000 + i) * 0.2f));
        peer->write(packet({{"type", "cloud"}, {"seq", batch + 1}, {"point_count", 140000}}, payload));
        if (batch == 0) QTRY_COMPARE_WITH_TIMEOUT(client.pointCount(), 140000, 10000);
        else QTRY_COMPARE_WITH_TIMEOUT(client.pointCount(), 250000, 10000);
    }
    QCOMPARE(client.pointCount(), 250000);
    QByteArray poses;
    for (int i = 0; i < 10005; ++i)
        poses += packet({{"type", "pose"}, {"seq", i + 3}, {"position", QJsonArray{i, 0, 1}}, {"orientation", QJsonArray{0, 0, 0, 1}}});
    peer->write(poses);
    QTRY_COMPARE_WITH_TIMEOUT(client.trajectoryCount(), 9005, 10000);
    QVERIFY(client.trajectoryCount() <= 10000);
    peer->disconnectFromHost();
    QTRY_VERIFY(!client.connected()); QVERIFY(client.stale());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
    peer = server.nextPendingConnection();
    // Fresh snapshot on reconnect replaces retained points without mixing origins.
    peer->write(packet({{"type", "cloud"}, {"seq", 1}, {"snapshot", true}, {"point_count", 1}}, cloud(9)));
    QTRY_COMPARE(client.pointCount(), 1);
    QTRY_VERIFY(client.connected());
    client.setEnabled(false);
}
void MappingClientTest::clearAndResumeDiscardPartialFrames() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
    MappingClient client; client.setHost("127.0.0.1"); client.setPort(server.serverPort()); client.setEnabled(true);
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    const auto oldFrame = packet({{"type", "cloud"}, {"seq", 1}, {"point_count", 1}}, cloud(1));
    peer->write(oldFrame.left(oldFrame.size() - 8)); peer->flush(); QTest::qWait(30);
    client.clearMap();
    peer->write(oldFrame.right(8));
    peer->write(packet({{"type", "cloud"}, {"seq", 2}, {"point_count", 1}}, cloud(2)));
    QTRY_COMPARE(client.pointCount(), 1);
    QTRY_COMPARE(client.snapshot()->points.first().x, 2.0f);
    client.setPaused(true);
    const auto pausedFrame = packet({{"type", "cloud"}, {"seq", 3}, {"point_count", 1}}, cloud(3));
    peer->write(pausedFrame.left(pausedFrame.size() - 8)); peer->flush(); QTest::qWait(30);
    client.setPaused(false);
    peer->write(pausedFrame.right(8));
    peer->write(packet({{"type", "cloud"}, {"seq", 4}, {"point_count", 1}}, cloud(4)));
    QTRY_COMPARE(client.pointCount(), 2);
    QTRY_VERIFY(client.snapshot()->points.size() == 2);
    QCOMPARE(client.snapshot()->points.at(1).x, 4.0f);
    client.setEnabled(false);
}
void MappingClientTest::staleCachedSnapshotStaysStale() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
    MappingClient client; client.setHost("127.0.0.1"); client.setPort(server.serverPort()); client.setEnabled(true);
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    peer->write(packet({{"type", "cloud"}, {"seq", 1}, {"snapshot", true}, {"stale", true}, {"point_count", 1}}, cloud(1)));
    QTRY_COMPARE(client.pointCount(), 1); QVERIFY(client.stale());
    QTest::qWait(300); QVERIFY(client.stale());
    peer->write(packet({{"type", "cloud"}, {"seq", 2}, {"point_count", 1}}, cloud(2)));
    QTRY_VERIFY(!client.stale());
    peer->write(packet({{"type", "status"}, {"seq", 3}, {"state", "waiting_ros"}, {"message", "ROS master unavailable"}}));
    QTRY_VERIFY(client.stale()); QVERIFY(!client.hasPose());
    QTest::qWait(300); QVERIFY(client.stale());
    QCOMPARE(client.pointCount(), 2);
    client.setEnabled(false);
}
void MappingClientTest::destroyWhileConnected() {
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
    auto client = std::make_unique<MappingClient>();
    client->setHost("127.0.0.1"); client->setPort(server.serverPort()); client->setEnabled(true);
    QTRY_VERIFY(server.hasPendingConnections()); auto *peer = server.nextPendingConnection();
    peer->write(packet({{"type", "cloud"}, {"seq", 1}, {"point_count", 1}}, cloud(1)));
    QTRY_COMPARE(client->pointCount(), 1);
    client.reset(); // Must disconnect socket callbacks before member destruction.
    QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
}
QTEST_GUILESS_MAIN(MappingClientTest)
#include "tst_mapping_client.moc"
