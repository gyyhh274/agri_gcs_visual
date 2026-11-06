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
        link.setManualSpeed(100);
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

    void manualSpeedScalesInputWithoutCompoundingAndStops() {
        QUdpSocket relay;
        QVERIFY(relay.bind(QHostAddress(QHostAddress::LocalHost), 0));
        GimbalLink link;
        QCOMPARE(link.manualSpeed(), 30);
        QSignalSpy speedChanged(&link, &GimbalLink::manualSpeedChanged);
        link.setManualSpeed(0);
        QCOMPARE(link.manualSpeed(), 1);
        link.setManualSpeed(500);
        QCOMPARE(link.manualSpeed(), 100);
        link.setManualSpeed(100);
        QCOMPARE(speedChanged.count(), 2);
        link.setManualSpeed(30);
        link.connectToGimbal("127.0.0.1", relay.localPort());
        QTRY_VERIFY(relay.hasPendingDatagrams());
        while (relay.hasPendingDatagrams()) relay.receiveDatagram();

        link.setRotateRate(100, -50);
        QTRY_VERIFY(relay.hasPendingDatagrams());
        auto packet = relay.receiveDatagram().data();
        QCOMPARE(quint8(packet.at(7)), quint8(0x07));
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("1ef1")); // 30, -15
        QCOMPARE(link.yawRate(), 30);
        QCOMPARE(link.pitchRate(), -15);

        link.setRotateRate(100, -50); // QML periodically resends the raw stick input.
        QTRY_VERIFY(relay.hasPendingDatagrams());
        packet = relay.receiveDatagram().data();
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("1ef1"));

        link.setManualSpeed(60); // Changing the cap while held also uses raw input.
        QTRY_VERIFY(relay.hasPendingDatagrams());
        packet = relay.receiveDatagram().data();
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("3ce2")); // 60, -30

        link.setManualSpeed(-4);
        QTRY_VERIFY(relay.hasPendingDatagrams());
        packet = relay.receiveDatagram().data();
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("01ff")); // rounded 1, -1
        link.setRotateRate(200, -200); // Raw input is clamped before scaling.
        QTRY_VERIFY(relay.hasPendingDatagrams());
        packet = relay.receiveDatagram().data();
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("01ff"));

        link.stopRotate();
        QTRY_VERIFY(relay.hasPendingDatagrams());
        packet = relay.receiveDatagram().data();
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("0000"));
        QVERIFY(!link.rotating());
        link.setManualSpeed(100);
        QTest::qWait(40);
        QVERIFY(!relay.hasPendingDatagrams()); // Settings cannot revive a released stick.
    }

    void automaticOwnershipCancelsManualTargetsRetriesAndInput() {
        QUdpSocket relay;
        QVERIFY(relay.bind(QHostAddress(QHostAddress::LocalHost), 0));
        GimbalLink link;
        QVERIFY(link.manualControlEnabled());
        link.connectToGimbal("127.0.0.1", relay.localPort());
        QTRY_VERIFY(relay.hasPendingDatagrams());
        while (relay.hasPendingDatagrams()) relay.receiveDatagram();

        // Leave one absolute-angle command unacknowledged so it would normally retry.
        link.setAttitude(60, -20);
        QTRY_VERIFY(relay.hasPendingDatagrams());
        auto packet = relay.receiveDatagram().data();
        QCOMPARE(quint8(packet.at(7)), quint8(0x0e));
        link.setRotateRate(100, 0);
        QTRY_VERIFY(relay.hasPendingDatagrams());
        packet = relay.receiveDatagram().data();
        QCOMPARE(quint8(packet.at(7)), quint8(0x07));
        QVERIFY(link.rotating());

        // Queue a second angle target and hand ownership to the onboard mission
        // before its 80 ms flush. Both this target and the old retry must disappear.
        link.setAttitude(-90, -30);
        QSignalSpy ownershipChanged(&link, &GimbalLink::manualControlEnabledChanged);
        link.setManualControlEnabled(false);
        QVERIFY(!link.manualControlEnabled());
        QCOMPARE(ownershipChanged.count(), 1);
        QVERIFY(!link.rotating());
        QTRY_VERIFY(relay.hasPendingDatagrams());
        packet = relay.receiveDatagram().data();
        QCOMPARE(quint8(packet.at(7)), quint8(0x07));
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("0000"));

        // These include delayed QML release callbacks and every manual actuator API.
        link.setRotateRate(100, -100);
        link.stopRotate();
        link.setAttitude(30, -45);
        link.jog(10, 0);
        link.centerGimbal();
        link.lookDown();
        link.setMode(1);
        link.takePhoto();
        link.toggleRecording();
        link.zoomStep(1);
        link.setZoomAbsolute(2);
        link.softReboot();
        link.setManualSpeed(60);
        link.setManualControlEnabled(false);
        QCOMPARE(ownershipChanged.count(), 1);

        QTest::qWait(1250); // Exceeds both the angle flush and SDK ACK retry delay.
        QVERIFY(relay.hasPendingDatagrams()); // Status polling must still work.
        while (relay.hasPendingDatagrams()) {
            packet = relay.receiveDatagram().data();
            const quint8 command = quint8(packet.at(7));
            QVERIFY2(command == 0x0a || command == 0x0d || command == 0x16 || command == 0x18 || command == 0x19,
                     "A manual actuator command escaped the automatic ownership lock");
        }
        link.setManualControlEnabled(true);
        QCOMPARE(ownershipChanged.count(), 2);
        QTest::qWait(100);
        QVERIFY(!relay.hasPendingDatagrams()); // Unlocking cannot replay old motion.
        QVERIFY(!link.rotating());
        link.setRotateRate(50, -100);
        QTRY_VERIFY(relay.hasPendingDatagrams());
        packet = relay.receiveDatagram().data();
        QCOMPARE(quint8(packet.at(7)), quint8(0x07));
        QCOMPARE(packet.mid(8, 2), QByteArray::fromHex("1ec4")); // Fresh input: 30, -60.
        link.stopRotate();
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
