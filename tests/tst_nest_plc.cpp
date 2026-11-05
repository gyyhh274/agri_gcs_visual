#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <memory>
#include "ModbusTcpClient.h"
#include "NestPlc.h"

namespace {
int word(const QByteArray &data, int offset) { return (quint8(data.at(offset)) << 8) | quint8(data.at(offset + 1)); }
void appendWord(QByteArray &data, int value) { data.append(char(value >> 8)); data.append(char(value)); }
QByteArray response(const QByteArray &request, const QByteArray &pdu) {
    QByteArray frame = request.left(4); appendWord(frame, pdu.size() + 1); frame.append(request.at(6)); return frame + pdu;
}
class Server : public QObject {
public:
    QTcpServer server;
    QList<QByteArray> requests;
    std::function<QByteArray(const QByteArray &)> reply;
    bool fragmented = false;
    Server() {
        if (!server.listen(QHostAddress::LocalHost, 0)) qFatal("Cannot start fake PLC");
        connect(&server, &QTcpServer::newConnection, this, [this] {
            auto *socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                buffer->append(socket->readAll());
                while (buffer->size() >= 7 && buffer->size() >= word(*buffer, 4) + 6) {
                    const auto request = buffer->left(word(*buffer, 4) + 6); buffer->remove(0, request.size());
                    requests.append(request);
                    const auto frame = reply ? reply(request) : QByteArray();
                    if (fragmented && !frame.isEmpty()) {
                        socket->write(frame.left(5));
                        QTimer::singleShot(20, socket, [socket, frame] { socket->write(frame.mid(5)); });
                    } else if (!frame.isEmpty()) socket->write(frame);
                }
            });
        });
    }
    void connectClient(ModbusTcpClient &client) { client.connectToHost("127.0.0.1", server.serverPort(), 1); }
    int writes() const { int count = 0; for (const auto &request : requests) if (quint8(request.at(7)) == 16) ++count; return count; }
};
QVariantMap config(int port) { return {{"host", "127.0.0.1"}, {"port", port}, {"unit", 1}, {"statusFunction", 3}, {"statusAddress", 100}, {"commandAddress", 200}}; }
class ModelServer : public Server {
public:
    int flags = 0x82, fault = 0, ack = 10, heartbeat = 0, command = 0, sequence = 0;
    bool advanceHeartbeat = true, acknowledge = false, feedbackException = false, dropWrite = false;
    ModelServer() {
        reply = [this](const QByteArray &request) {
            const int function = quint8(request.at(7));
            if (function == 16) {
                command = word(request, 13); sequence = word(request, 15);
                if (acknowledge) ack = sequence;
                return dropWrite ? QByteArray() : response(request, QByteArray(1, char(16)) + request.mid(8, 4));
            }
            if (function == 3) {
                if (feedbackException) return response(request, QByteArray::fromHex("8302"));
                if (advanceHeartbeat) ++heartbeat;
                QByteArray pdu = QByteArray::fromHex("0308");
                for (int value : {flags, fault, ack, heartbeat}) appendWord(pdu, value);
                return response(request, pdu);
            }
            return response(request, QByteArray::fromHex("ab01"));
        };
    }
};
}

class NestPlcTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("AgriGcsPlcQA"); QCoreApplication::setApplicationName("PlcTest");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void init() { QSettings().clear(); }
    void fragmentedExceptionProvesProtocolOnly() {
        Server server; server.fragmented = true;
        server.reply = [](const QByteArray &request) { return response(request, QByteArray::fromHex("ab01")); };
        ModbusTcpClient client; QSignalSpy spy(&client, &ModbusTcpClient::response);
        server.connectClient(client); QTRY_VERIFY(client.connected()); QVERIFY(!client.verified());
        QVERIFY(client.identify()); QVERIFY(!client.identify()); QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 43); QCOMPARE(spy.first().at(2).toInt(), 1);
        QVERIFY(client.verified()); QVERIFY(!client.busy()); QCOMPARE(server.writes(), 0);
        client.disconnectFromHost(); QVERIFY(!client.verified());
    }
    void readsAndBounds() {
        Server server;
        server.reply = [](const QByteArray &request) { return response(request, QByteArray::fromHex("03040001ffff")); };
        ModbusTcpClient client; QSignalSpy spy(&client, &ModbusTcpClient::response);
        server.connectClient(client); QTRY_VERIFY(client.connected());
        QVERIFY(!client.read(3, -1, 1)); QVERIFY(!client.read(3, 65535, 2)); QVERIFY(!client.read(3, 0, 126));
        QVERIFY(!client.read(1, 0, 2001)); QVERIFY(!client.read(5, 0, 1)); QVERIFY(!client.writeMailbox(65535, 1, 1));
        QVERIFY(client.read(3, 65534, 2)); QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(1).toByteArray(), QByteArray::fromHex("040001ffff")); QCOMPARE(server.writes(), 0);
        server.reply = [](const QByteArray &request) { return response(request, QByteArray::fromHex("010105")); };
        QVERIFY(client.read(1, 0, 3)); QTRY_COMPARE(spy.count(), 2); QVERIFY(client.verified());
    }
    void malformed_data() {
        QTest::addColumn<int>("kind");
        for (int kind = 0; kind < 8; ++kind) QTest::newRow(qPrintable(QString::number(kind))) << kind;
    }
    void malformed() {
        QFETCH(int, kind);
        Server server;
        server.reply = [kind](const QByteArray &request) {
            auto frame = response(request, QByteArray::fromHex("0302002a"));
            if (kind == 0) frame[1] = char(quint8(frame.at(1)) + 1);
            if (kind == 1) frame[3] = 1;
            if (kind == 2) frame[6] = 2;
            if (kind == 3) frame[5] = char(255);
            if (kind == 4) frame[7] = 4;
            if (kind == 5) frame[8] = 4;
            if (kind == 6) return response(request, QByteArray::fromHex("8300"));
            if (kind == 7) frame.append(char(0));
            return frame;
        };
        ModbusTcpClient client; QSignalSpy failure(&client, &ModbusTcpClient::failed);
        server.connectClient(client); QTRY_VERIFY(client.connected()); QVERIFY(client.read(3, 0, 1));
        QTRY_COMPARE(failure.count(), 1); QVERIFY(!client.connected()); QVERIFY(!client.verified());
    }
    void invalidWriteEchoAndTimeoutNeverRetry() {
        Server server;
        server.reply = [](const QByteArray &request) { return response(request, QByteArray::fromHex("1000c90002")); };
        ModbusTcpClient client; QSignalSpy failure(&client, &ModbusTcpClient::failed);
        server.connectClient(client); QTRY_VERIFY(client.connected()); QVERIFY(client.writeMailbox(200, 1, 11));
        QTRY_COMPARE(failure.count(), 1); QCOMPARE(server.writes(), 1); QVERIFY(!client.verified());
        server.reply = nullptr; server.connectClient(client); QTRY_VERIFY(client.connected());
        QVERIFY(client.writeMailbox(200, 3, 12)); QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 2, 2200);
        QTest::qWait(300); QCOMPARE(server.writes(), 2); QVERIFY(!client.connected());
    }
    void configurationAndReadOnlyDefaults() {
        NestPlc plc; QCOMPARE(plc.configuration().value("host").toString(), QString("192.168.2.8"));
        QCOMPARE(plc.configuration().value("statusAddress").toInt(), -1);
        QVERIFY(!plc.canMove()); QVERIFY(!plc.canStop()); QVERIFY(!plc.sendCommand(1)); QVERIFY(!plc.setWriteEnabled(true));
        auto values = config(15020); values["statusAddress"] = 65533; QVERIFY(!plc.configure(values));
        values = config(15020); values["commandAddress"] = 101; QVERIFY(!plc.configure(values));
        values = config(15020); values["unit"] = "1.5"; QVERIFY(!plc.configure(values));
        QVERIFY(plc.configure(config(15020))); NestPlc restored;
        QCOMPARE(restored.configuration(), plc.configuration()); QVERIFY(!restored.writeEnabled()); QVERIFY(!restored.monitoring()); QVERIFY(!restored.connected());
    }
    void commandRequiresFeedbackAndCompletion() {
        ModelServer server; NestPlc plc; QVERIFY(plc.configure(config(server.server.serverPort())));
        plc.connectPlc(); QTRY_VERIFY(plc.connected()); QVERIFY(!plc.protocolVerified());
        QVERIFY(plc.identify()); QTRY_VERIFY(plc.protocolVerified()); QVERIFY(!plc.feedbackFresh());
        QVERIFY(!plc.readRegisters(3, -1, 1)); QVERIFY(plc.setMonitoring(true)); QTRY_VERIFY_WITH_TIMEOUT(plc.feedbackFresh(), 2200);
        QVERIFY(!plc.canMove()); QVERIFY(plc.setWriteEnabled(true)); QTRY_VERIFY(plc.canMove());
        QVERIFY(plc.sendCommand(1)); QTRY_COMPARE(server.writes(), 1);
        QCOMPARE(server.command, 1); QCOMPARE(server.sequence, 11); QCOMPARE(plc.doorState(), QString("关闭到位"));
        QTRY_VERIFY(!plc.busy()); QVERIFY(!plc.canMove()); QVERIFY(plc.commandStatus().contains("等待"));
        server.ack = 11; server.flags = 0x84;
        QTRY_COMPARE(plc.doorState(), QString("正在开启")); QVERIFY(!plc.canMove());
        server.flags = 0x81; QTRY_COMPARE(plc.doorState(), QString("开启到位")); QTRY_VERIFY(plc.canMove());
        QVERIFY(plc.commandStatus().contains("动作完成")); QCOMPARE(server.writes(), 1);
        plc.disconnectPlc(); QVERIFY(!plc.writeEnabled()); QVERIFY(!plc.feedbackFresh());
        plc.connectPlc(); QTRY_VERIFY(plc.connected()); QTest::qWait(300); QCOMPARE(server.writes(), 1); QVERIFY(!plc.monitoring());
    }
    void stopRemainsAvailableWithFaultAndPendingMove() {
        ModelServer server; server.acknowledge = true; NestPlc plc; QVERIFY(plc.configure(config(server.server.serverPort())));
        plc.connectPlc(); QTRY_VERIFY(plc.connected()); QVERIFY(plc.setMonitoring(true)); QTRY_VERIFY_WITH_TIMEOUT(plc.feedbackFresh(), 2200);
        QVERIFY(plc.setWriteEnabled(true)); QTRY_VERIFY(plc.canMove()); QVERIFY(plc.sendCommand(1)); QTRY_COMPARE(server.writes(), 1);
        server.flags = 0x54; server.fault = 7; // Moving, fault + emergency stop; remote-ready cleared.
        QTRY_COMPARE(plc.faultCode(), 7); QVERIFY(!plc.canMove()); QTRY_VERIFY(plc.canStop());
        QVERIFY(plc.sendCommand(3)); QTRY_COMPARE(server.writes(), 2); QCOMPARE(server.command, 3); QCOMPARE(server.sequence, 12);
        plc.disconnectPlc(); QVERIFY(plc.uncertain()); QVERIFY(!plc.writeEnabled());
    }
    void conflictsExceptionsAndFrozenHeartbeatDisarm() {
        ModelServer server; NestPlc plc; QVERIFY(plc.configure(config(server.server.serverPort())));
        plc.connectPlc(); QTRY_VERIFY(plc.connected()); QVERIFY(plc.setMonitoring(true)); QTRY_VERIFY_WITH_TIMEOUT(plc.feedbackFresh(), 2200);
        QVERIFY(plc.setWriteEnabled(true)); server.flags = 0x83;
        QTRY_VERIFY(!plc.feedbackFresh()); QVERIFY(!plc.writeEnabled());
        server.flags = 0x82; QTRY_VERIFY(plc.feedbackFresh()); QVERIFY(!plc.writeEnabled());
        QVERIFY(plc.setWriteEnabled(true)); server.feedbackException = true;
        QTRY_VERIFY(!plc.feedbackFresh()); QVERIFY(!plc.writeEnabled());
        server.feedbackException = false; QTRY_VERIFY(plc.feedbackFresh());
        QVERIFY(plc.setWriteEnabled(true)); server.advanceHeartbeat = false;
        QTRY_VERIFY_WITH_TIMEOUT(!plc.feedbackFresh(), 6000); QTRY_VERIFY(!plc.writeEnabled()); QVERIFY(!plc.canStop()); QCOMPARE(server.writes(), 0);
    }
    void pythonSimulatorEndToEnd() {
        const QString python = QStandardPaths::findExecutable("python3"); QVERIFY(!python.isEmpty());
        QProcess simulator;
        simulator.start(python, {QStringLiteral(PLC_SIMULATOR_PATH), "--port", "0", "--fragment-response"});
        QVERIFY(simulator.waitForStarted(3000));
        QByteArray output;
        QTRY_VERIFY_WITH_TIMEOUT((output += simulator.readAllStandardOutput()).contains("SIMULATOR"), 3000);
        const auto match = QRegularExpression("127\\.0\\.0\\.1:(\\d+)").match(QString::fromUtf8(output)); QVERIFY(match.hasMatch());
        NestPlc plc; QVERIFY(plc.configure(config(match.captured(1).toInt())));
        plc.connectPlc(); QTRY_VERIFY(plc.connected()); QVERIFY(plc.identify()); QTRY_VERIFY(plc.protocolVerified());
        QVERIFY(!plc.feedbackFresh()); QVERIFY(plc.setMonitoring(true)); QTRY_VERIFY_WITH_TIMEOUT(plc.feedbackFresh(), 3500);
        QVERIFY(plc.setWriteEnabled(true)); QTRY_VERIFY(plc.canMove()); QVERIFY(plc.sendCommand(1));
        QTRY_COMPARE_WITH_TIMEOUT(plc.doorState(), QString("正在开启"), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(plc.doorState(), QString("开启到位"), 4000); QTRY_VERIFY(plc.canMove());
        QVERIFY(plc.sendCommand(2)); QTRY_COMPARE_WITH_TIMEOUT(plc.doorState(), QString("正在关闭"), 2000);
        QTRY_VERIFY(plc.canStop()); QVERIFY(plc.sendCommand(3));
        QTRY_COMPARE_WITH_TIMEOUT(plc.doorState(), QString("中间位置 / 已停止"), 2500);
        QTRY_VERIFY(plc.commandStatus().contains("动作完成"));
        plc.disconnectPlc(); simulator.terminate(); QVERIFY(simulator.waitForFinished(3000));
    }
    void lostAcknowledgementLatchesUncertainty() {
        ModelServer server; NestPlc plc; QVERIFY(plc.configure(config(server.server.serverPort())));
        plc.connectPlc(); QTRY_VERIFY(plc.connected()); QVERIFY(plc.setMonitoring(true)); QTRY_VERIFY_WITH_TIMEOUT(plc.feedbackFresh(), 2200);
        QVERIFY(plc.setWriteEnabled(true)); QTRY_VERIFY(plc.canMove()); QVERIFY(plc.sendCommand(1)); QTRY_COMPARE(server.writes(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(plc.uncertain(), 4000); QVERIFY(!plc.canMove()); QCOMPARE(server.writes(), 1);
        QTRY_VERIFY(!plc.busy()); QVERIFY(!plc.acknowledgeStopped());
        server.ack = server.sequence; QTRY_COMPARE(plc.ackSequence(), server.sequence);
        QTRY_VERIFY(!plc.busy()); QVERIFY(plc.acknowledgeStopped()); QVERIFY(!plc.writeEnabled()); QVERIFY(!plc.uncertain());
    }
};
QTEST_GUILESS_MAIN(NestPlcTest)
#include "tst_nest_plc.moc"
