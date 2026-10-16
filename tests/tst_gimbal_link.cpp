#include <QtTest>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include "../src/GimbalLink.h"

class GimbalLinkTest : public QObject {
    Q_OBJECT
    static QByteArray frame(quint8 command, const QByteArray &payload) {
        QByteArray data = QByteArray::fromHex("5566020000000000");
        data[3] = char(payload.size()); data[7] = char(command); data += payload;
        quint16 crc = 0;
        for (char byte : data) {
            crc ^= quint16(quint8(byte)) << 8;
            for (int bit = 0; bit < 8; ++bit)
                crc = crc & 0x8000 ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
        }
        data += char(crc & 0xff); data += char(crc >> 8);
        return data;
    }
private slots:
    void acceptsBundledRepliesAndRecoversAfterCorruptFrame() {
        QUdpSocket relay;
        QVERIFY(relay.bind(QHostAddress(QHostAddress::LocalHost), 0));
        GimbalLink link;
        link.connectToGimbal("127.0.0.1", relay.localPort());
        QTRY_VERIFY(relay.hasPendingDatagrams());
        const auto request = relay.receiveDatagram();
        auto bad = frame(0x0d, QByteArray::fromHex("000000000000"));
        bad[bad.size()-1] = char(quint8(bad.at(bad.size()-1)) ^ 1);
        const auto packet = QByteArray("junk") + bad +
            frame(0x0d, QByteArray::fromHex("c8ff9cff0a00000000000000")) +
            frame(0x19, QByteArray(1, char(2))) + frame(0x18, QByteArray::fromHex("0205"));
        relay.writeDatagram(packet, request.senderAddress(), request.senderPort());
        QTRY_VERIFY(link.attitudeFresh());
        QCOMPARE(link.yaw(), 5.6); QCOMPARE(link.pitch(), -10.0); QCOMPARE(link.roll(), 1.0);
        QCOMPARE(link.mode(), 2); QCOMPARE(link.zoom(), 2.5);
        QVERIFY(link.connected());
    }

    void reconnectPreservesPortAndResetsOnlineStatus() {
        QUdpSocket relay;
        QVERIFY(relay.bind(QHostAddress(QHostAddress::LocalHost), 0));
        GimbalLink link;
        link.connectToGimbal("127.0.0.1", relay.localPort());
        QTRY_VERIFY(relay.hasPendingDatagrams());
        auto request = relay.receiveDatagram();
        const auto sourcePort = request.senderPort();
        relay.writeDatagram(frame(0x0d, QByteArray(12, '\0')), request.senderAddress(), sourcePort);
        QTRY_VERIFY(link.connected());
        while (relay.hasPendingDatagrams()) relay.receiveDatagram();
        link.connectToGimbal("127.0.0.1", relay.localPort());
        QVERIFY(!link.connected()); QVERIFY(!link.attitudeFresh());
        QTRY_VERIFY(relay.hasPendingDatagrams());
        request = relay.receiveDatagram();
        QCOMPARE(request.senderPort(), sourcePort);
        relay.writeDatagram(frame(0x19, QByteArray(1, char(1))), request.senderAddress(), sourcePort);
        QTRY_VERIFY(link.connected());
        QVERIFY(link.status().startsWith(QStringLiteral("云台在线")));
        link.disconnectFromGimbal();
        relay.writeDatagram(frame(0x0d, QByteArray(12, '\0')), request.senderAddress(), sourcePort);
        QTest::qWait(50);
        QVERIFY(!link.connected()); QVERIFY(link.endpoint().isEmpty());
    }

    void disconnectSendsImmediateStop() {
        QUdpSocket relay;
        QVERIFY(relay.bind(QHostAddress(QHostAddress::LocalHost), 0));
        GimbalLink link;
        link.connectToGimbal("127.0.0.1", relay.localPort());
        QTRY_VERIFY(relay.hasPendingDatagrams());
        while (relay.hasPendingDatagrams()) relay.receiveDatagram();
        link.setRotateRate(35, -20);
        QTRY_VERIFY(relay.hasPendingDatagrams());
        auto packet = relay.receiveDatagram().data();
        QCOMPARE(quint8(packet.at(7)), quint8(0x07));
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("23ec"));
        link.disconnectFromGimbal();
        QTRY_VERIFY(relay.hasPendingDatagrams());
        packet = relay.receiveDatagram().data();
        QCOMPARE(quint8(packet.at(7)), quint8(0x07));
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("0000"));
        QVERIFY(!link.rotating());
    }

    void ignoresReplyFromOtherRelay() {
        QUdpSocket relay, stranger;
        QVERIFY(relay.bind(QHostAddress(QHostAddress::LocalHost), 0));
        QVERIFY(stranger.bind(QHostAddress(QHostAddress::LocalHost), 0));
        GimbalLink link;
        link.connectToGimbal("127.0.0.1", relay.localPort());
        QTRY_VERIFY(relay.hasPendingDatagrams());
        const auto request = relay.receiveDatagram();
        stranger.writeDatagram(frame(0x0d, QByteArray(12, '\0')), request.senderAddress(), request.senderPort());
        QTest::qWait(50); QVERIFY(!link.connected());
        relay.writeDatagram(frame(0x0d, QByteArray(12, '\0')), request.senderAddress(), request.senderPort());
        QTRY_VERIFY(link.connected());
    }
};
QTEST_GUILESS_MAIN(GimbalLinkTest)
#include "tst_gimbal_link.moc"
