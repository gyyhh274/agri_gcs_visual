#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <limits>
#include "../src/GroundLink.h"

class GroundLinkTest : public QObject {
    Q_OBJECT
    static QJsonObject mission(const QString &session = "session-1")
    {
        return {{"frame_id", "world"}, {"map_session_id", session}, {"mission_id", "mission-1"},
                {"revision", 1}, {"state", "READY"}, {"current_waypoint_id", 0}, {"completed", 0},
                {"total", 1}, {"reason", ""}, {"odom_fresh", true}, {"flight_ready", true},
                {"world_x", 0.}, {"world_y", 0.}, {"world_z", 1.},
                {"max_abs_xy", 30.}, {"min_z", .3}, {"max_z", 2.5}};
    }
    static void sendTelemetry(QTcpSocket *peer, const QJsonObject &status)
    {
        QJsonObject frame{{"protocol", 2}, {"type", "telemetry"}, {"server", "agri-onboard-ros"},
                          {"fcu_connected", true}, {"armed", true}, {"mission_ready", true},
                          {"odom_fresh", true}, {"mode", "OFFBOARD"}, {"battery_percent", 67.},
                          {"world_x", 0.}, {"world_y", 0.}, {"world_z", 1.}, {"mission", status}};
        peer->write(QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n'); peer->flush();
    }
    static QVariantMap angleAction()
    {
        return {{"mode", "angle"}, {"yaw_deg", -45.}, {"pitch_deg", -30.},
                {"speed", 30}, {"settle_sec", 1.5}, {"photo", true}};
    }
    static QVariantMap sweepAction()
    {
        return {{"mode", "sweep"}, {"yaw_min_deg", -80.}, {"yaw_max_deg", 65.},
                {"pitch_deg", -20.}, {"speed", 25}, {"settle_sec", 2.}, {"photo", false}};
    }
    static QVariantMap point(int id = 1)
    {
        return {{"id", id}, {"x", 1.5}, {"y", -1.}, {"z", 1.}, {"hover_sec", 2.}};
    }
    static void sendAck(QTcpSocket *peer, const QString &request, const QJsonObject &status)
    {
        const QJsonObject ack{{"protocol", 2}, {"type", "ack"}, {"request_id", request},
                              {"accepted", true}, {"reason", "Accepted"}, {"mission", status}};
        peer->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n'); peer->flush();
    }
private slots:
    void gimbalCapabilityGatePreservesFlightOnlyCompatibility()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        GroundLink link; link.connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000)); auto *peer = server.nextPendingConnection(); QVERIFY(peer);
        auto status = mission(); sendTelemetry(peer, status); QTRY_VERIFY(link.remoteAvailable());
        auto withAction = point(); withAction["gimbal"] = angleAction();
        QSignalSpy notified(&link, &GroundLink::notified);
        QVERIFY(link.uploadLocalRoute({withAction}, "session-1").isEmpty());
        QCOMPARE(notified.count(), 1); QVERIFY(!link.busy());
        QCoreApplication::processEvents(); QCOMPARE(peer->bytesAvailable(), qint64(0));

        // An older gateway still accepts a route with no camera action.
        const auto request = link.uploadLocalRoute({point()}, "session-1");
        QVERIFY(!request.isEmpty()); QTRY_VERIFY(peer->canReadLine());
        const auto sent = QJsonDocument::fromJson(peer->readLine()).object();
        QVERIFY(!sent["waypoints"].toArray()[0].toObject().contains("gimbal"));
        status["mission_id"] = sent["mission_id"]; sendAck(peer, request, status);
        QTRY_VERIFY(!link.busy());

        // Future capability versions are not silently treated as version 1.
        status["gimbal_capabilities"] = QJsonObject{{"version", 2}};
        sendTelemetry(peer, status);
        QTRY_COMPARE(link.mission().value("gimbal_capabilities").toMap().value("version").toInt(), 2);
        QVERIFY(link.uploadLocalRoute({withAction}, "session-1").isEmpty());
        QVERIFY(!link.busy()); QCoreApplication::processEvents();
        QCOMPARE(peer->bytesAvailable(), qint64(0));
    }

    void gimbalActionsSurviveUploadAndFutureUpdate()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        GroundLink link; link.connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000)); auto *peer = server.nextPendingConnection(); QVERIFY(peer);
        auto status = mission(); status["gimbal_capabilities"] = QJsonObject{{"version", 1}};
        sendTelemetry(peer, status); QTRY_VERIFY(link.remoteAvailable());
        auto first = point(1), second = point(2);
        first["gimbal"] = angleAction(); second["gimbal"] = sweepAction();
        const QVariantList points{first, second};
        const auto upload = link.uploadLocalRoute(points, "session-1");
        QVERIFY(!upload.isEmpty()); QTRY_VERIFY(peer->canReadLine());
        const auto sent = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(sent["operation"].toString(), QString("upload"));
        QCOMPARE(sent["waypoints"].toArray(), QJsonArray::fromVariantList(points));
        status["mission_id"] = sent["mission_id"]; status["total"] = 2;
        sendAck(peer, upload, status); QTRY_VERIFY(!link.busy());

        // The active point stays locked while camera settings for future points change.
        status["state"] = "HOVERING"; status["current_waypoint_id"] = 1;
        sendTelemetry(peer, status); QTRY_COMPARE(link.mission().value("state").toString(), QString("HOVERING"));
        auto replacement = point(2); auto action = angleAction();
        action["yaw_deg"] = 120.; action["speed"] = 50; action["photo"] = false;
        replacement["gimbal"] = action;
        const QVariantList suffix{replacement};
        const auto update = link.updateLocalRoute(suffix, 1, sent["mission_id"].toString(), 2, "session-1");
        QVERIFY(!update.isEmpty()); QTRY_VERIFY(peer->canReadLine());
        const auto changed = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(changed["operation"].toString(), QString("update"));
        QCOMPARE(changed["replace_from"].toInt(), 1);
        QCOMPARE(changed["waypoints"].toArray(), QJsonArray::fromVariantList(suffix));
        status["revision"] = 2; sendAck(peer, update, status); QTRY_VERIFY(!link.busy());
        QVERIFY(!peer->canReadLine()); // Camera configuration never starts a mission.
    }

    void invalidGimbalActionsNeverReachSocket_data()
    {
        QTest::addColumn<QVariant>("action");
        QTest::newRow("null") << QVariant();
        QTest::newRow("array") << QVariant(QVariantList{1, 2});
        QTest::newRow("string") << QVariant(QString("angle"));
        QTest::newRow("empty-object") << QVariant(QVariantMap());
        const auto invalid = [](const char *name, const char *key, const QVariant &value) {
            auto action = angleAction(); action[key] = value;
            QTest::newRow(name) << QVariant(action);
        };
        invalid("unknown-mode", "mode", "continuous");
        invalid("yaw-low", "yaw_deg", -135.1);
        invalid("yaw-high", "yaw_deg", 135.1);
        invalid("yaw-nan", "yaw_deg", std::numeric_limits<double>::quiet_NaN());
        invalid("pitch-low", "pitch_deg", -90.1);
        invalid("pitch-high", "pitch_deg", 25.1);
        invalid("pitch-inf", "pitch_deg", std::numeric_limits<double>::infinity());
        invalid("pitch-string", "pitch_deg", "-30");
        invalid("speed-zero", "speed", 0);
        invalid("speed-high", "speed", 101);
        invalid("speed-fraction", "speed", 30.5);
        invalid("speed-bool", "speed", true);
        invalid("settle-low", "settle_sec", -0.1);
        invalid("settle-high", "settle_sec", 10.1);
        invalid("photo-number", "photo", 1);
        auto missing = angleAction(); missing.remove("photo");
        QTest::newRow("missing-photo") << QVariant(missing);
        const auto invalidSweep = [](const char *name, double low, double high) {
            auto action = sweepAction(); action["yaw_min_deg"] = low; action["yaw_max_deg"] = high;
            QTest::newRow(name) << QVariant(action);
        };
        invalidSweep("sweep-equal", 20., 20.);
        invalidSweep("sweep-reversed", 30., -30.);
        invalidSweep("sweep-low", -135.1, 50.);
        invalidSweep("sweep-high", -50., 135.1);
    }

    void invalidGimbalActionsNeverReachSocket()
    {
        QFETCH(QVariant, action);
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        GroundLink link; link.connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000)); auto *peer = server.nextPendingConnection(); QVERIFY(peer);
        auto status = mission(); status["gimbal_capabilities"] = QJsonObject{{"version", 1}};
        status["state"] = "FLYING"; status["current_waypoint_id"] = 1; status["total"] = 3;
        sendTelemetry(peer, status); QTRY_VERIFY(link.remoteAvailable());
        auto invalidPoint = point(2); invalidPoint["gimbal"] = action;
        QSignalSpy notified(&link, &GroundLink::notified);
        QVERIFY(link.uploadLocalRoute({invalidPoint}, "session-1").isEmpty());
        QVERIFY(link.updateLocalRoute({invalidPoint}, 1, "mission-1", 2, "session-1").isEmpty());
        QCOMPARE(notified.count(), 2); QVERIFY(!link.busy()); QVERIFY(link.connected());
        QCoreApplication::processEvents(); QCOMPARE(peer->bytesAvailable(), qint64(0));
    }

    void oversizedLocalCommandDoesNotDisconnectTelemetry()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        GroundLink link; link.connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000)); auto *peer = server.nextPendingConnection(); QVERIFY(peer);
        sendTelemetry(peer, mission()); QTRY_VERIFY(link.remoteAvailable());
        auto oversized = point(); oversized["comment"] = QString(70000, QLatin1Char('x'));
        QVERIFY(link.uploadLocalRoute({oversized}, "session-1").isEmpty());
        QVERIFY(!link.busy()); QVERIFY(link.connected());
        QCoreApplication::processEvents(); QCOMPARE(peer->bytesAvailable(), qint64(0));
        // A local envelope error must not destroy a healthy telemetry connection.
        link.queryMission(); QTRY_VERIFY(peer->canReadLine());
        const auto sent = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(sent["operation"].toString(), QString("get_status"));
    }

    void connectedLinkCanBeDestroyedWhileAwaitingAck()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        auto *link = new GroundLink;
        link->connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000)); QTcpSocket *peer = server.nextPendingConnection(); QVERIFY(peer);
        sendTelemetry(peer, mission()); QTRY_VERIFY(link->remoteAvailable());
        QVERIFY(!link->startUploadedMission("mission-1", 1, "session-1").isEmpty());
        QTRY_VERIFY(peer->canReadLine()); peer->readLine();
        QSignalSpy finished(link, &GroundLink::commandFinished);
        delete link;
        QCOMPARE(finished.count(), 0);
        QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
    }

    void futureUpdatesAreVersionedAndAcknowledged()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        GroundLink link; link.connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000)); QTcpSocket *peer = server.nextPendingConnection(); QVERIFY(peer);
        auto status = mission(); status.insert("state", "FLYING"); status.insert("current_waypoint_id", 1); status.insert("total", 3);
        sendTelemetry(peer, status); QTRY_VERIFY(link.remoteAvailable());
        QVariantList suffix{QVariantMap{{"id", 2}, {"x", 2.}, {"y", -1.}, {"z", 1.}, {"hover_sec", 2.}},
                            QVariantMap{{"id", 3}, {"x", 3.}, {"y", -1.}, {"z", 1.}, {"hover_sec", 2.}}};
        QVERIFY(link.updateLocalRoute(suffix, 0, "mission-1", 2, "session-1").isEmpty()); // Current point locked.
        QVERIFY(link.updateLocalRoute(suffix, 1, "other-mission", 2, "session-1").isEmpty());
        QVERIFY(link.updateLocalRoute(suffix, 1, "mission-1", 1, "session-1").isEmpty());
        QVERIFY(link.updateLocalRoute(suffix, 1, "mission-1", 2, "old-session").isEmpty());
        auto invalid = suffix; auto point = invalid[0].toMap(); point["z"] = 120.; invalid[0] = point;
        QVERIFY(link.updateLocalRoute(invalid, 1, "mission-1", 2, "session-1").isEmpty());
        invalid = suffix; invalid[1] = invalid[0];
        QVERIFY(link.updateLocalRoute(invalid, 1, "mission-1", 2, "session-1").isEmpty());
        const QString request = link.updateLocalRoute(suffix, 1, "mission-1", 2, "session-1");
        QVERIFY(!request.isEmpty()); QVERIFY(link.busy()); QTRY_VERIFY(peer->canReadLine());
        const auto sent = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(sent.value("operation").toString(), QString("update"));
        QCOMPARE(sent.value("replace_from").toInt(), 1); QCOMPARE(sent.value("revision").toInt(), 2);
        QCOMPARE(sent.value("frame_id").toString(), QString("world"));
        QCOMPARE(sent.value("map_session_id").toString(), QString("session-1"));
        QCOMPARE(sent.value("mission_id").toString(), QString("mission-1"));
        QCOMPARE(sent.value("request_id").toString(), request);
        QCOMPARE(sent.value("waypoints").toArray().size(), 2);
        QCOMPARE(sent.value("waypoints").toArray()[0].toObject().value("id").toInt(), 2);
        QSignalSpy finished(&link, &GroundLink::commandFinished);
        status.insert("revision", 2);
        QJsonObject ack{{"protocol", 2}, {"type", "ack"}, {"request_id", "unrelated"},
                        {"accepted", true}, {"reason", "Accepted"}, {"mission", status}};
        peer->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n'); peer->flush();
        QTest::qWait(30); QVERIFY(link.busy()); QCOMPARE(finished.count(), 0);
        QCOMPARE(link.mission().value("revision").toInt(), 1);
        ack.insert("request_id", request);
        peer->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n'); peer->flush();
        QTRY_COMPARE(finished.count(), 1); QVERIFY(!link.busy());
        QCOMPARE(finished.at(0).at(1).toString(), QString("update")); QVERIFY(finished.at(0).at(2).toBool());
        QCOMPARE(link.mission().value("revision").toInt(), 2);

        // Progress overtook the edit before arrival: reject without committing a new revision.
        const QString rejected = link.updateLocalRoute(suffix, 1, "mission-1", 3, "session-1");
        QVERIFY(!rejected.isEmpty()); QTRY_VERIFY(peer->canReadLine()); peer->readLine();
        status.insert("state", "HOVERING"); status.insert("completed", 1); status.insert("current_waypoint_id", 2);
        ack.insert("request_id", rejected); ack.insert("accepted", false); ack.insert("reason", "Progress advanced"); ack.insert("mission", status);
        peer->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n'); peer->flush();
        QTRY_COMPARE(finished.count(), 2); QVERIFY(!finished.at(1).at(2).toBool());
        QCOMPARE(link.mission().value("revision").toInt(), 2);
        QVERIFY(link.updateLocalRoute(suffix, 1, "mission-1", 3, "session-1").isEmpty());
        QVERIFY(!link.updateLocalRoute({}, 2, "mission-1", 3, "session-1").isEmpty()); // Delete future suffix.
        QTRY_VERIFY(peer->canReadLine());
        const auto truncate = QJsonDocument::fromJson(peer->readLine()).object();
        QVERIFY(truncate.value("waypoints").isArray()); QVERIFY(truncate.value("waypoints").toArray().isEmpty());
        QCOMPARE(truncate.value("replace_from").toInt(), 2);
    }

    void completedAppendAndReturnCommands()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        GroundLink link; link.connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000)); auto *peer = server.nextPendingConnection(); QVERIFY(peer);
        auto status = mission(); status["state"] = "COMPLETED"; status["completed"] = 1;
        sendTelemetry(peer, status); QTRY_VERIFY(link.remoteAvailable());
        QVariantList suffix{QVariantMap{{"id", 2}, {"x", 2.}, {"y", 0.}, {"z", 1.}, {"hover_sec", 0.}}};
        QVERIFY(link.updateLocalRoute(suffix, 0, "mission-1", 2, "session-1").isEmpty());
        QVERIFY(link.updateLocalRoute({}, 1, "mission-1", 2, "session-1").isEmpty());
        const auto request = link.updateLocalRoute(suffix, 1, "mission-1", 2, "session-1");
        QVERIFY(!request.isEmpty()); QTRY_VERIFY(peer->canReadLine());
        auto command = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(command["operation"].toString(), QString("update")); QCOMPARE(command["replace_from"].toInt(), 1);
        status["state"] = "READY"; status["total"] = 2; status["revision"] = 2;
        QJsonObject ack{{"type", "ack"}, {"protocol", 2}, {"request_id", request},
                        {"accepted", true}, {"reason", "Accepted"}, {"mission", status}};
        peer->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n'); peer->flush();
        QTRY_VERIFY(!link.busy()); QVERIFY(!peer->canReadLine()); // ACK never sends start.
        QVERIFY(link.returnToHome("mission-1", 1, "session-1").isEmpty());
        QVERIFY(link.returnToHome("mission-1", 2, "old-session").isEmpty());
        QVERIFY(!link.returnToHome("mission-1", 2, "session-1").isEmpty());
        QTRY_VERIFY(peer->canReadLine()); command = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(command["operation"].toString(), QString("return"));
        QCOMPARE(command["mission_id"].toString(), QString("mission-1"));
        QCOMPARE(command["revision"].toInt(), 2);
    }

    void remoteUploadRequiresAckAndSession()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        GroundLink link; link.connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000));
        QTcpSocket *peer = server.nextPendingConnection(); QVERIFY(peer);
        sendTelemetry(peer, mission());
        QTRY_VERIFY(link.remoteAvailable());
        QVariantList points{QVariantMap{{"id", 1}, {"x", 1.5}, {"y", -1.}, {"z", 1.}, {"hover_sec", 2.}}};
        QVERIFY(link.uploadLocalRoute(points, "old-session").isEmpty());
        QVERIFY(!link.busy());
        const QString request = link.uploadLocalRoute(points, "session-1");
        QVERIFY(!request.isEmpty()); QVERIFY(link.busy());
        QTRY_VERIFY(peer->canReadLine());
        const QJsonObject sent = QJsonDocument::fromJson(peer->readLine()).object();
        QCOMPARE(sent.value("operation").toString(), QString("upload"));
        QCOMPARE(sent.value("frame_id").toString(), QString("world"));
        QVERIFY(link.startUploadedMission("mission-1", 1, "session-1").isEmpty());
        QSignalSpy finished(&link, &GroundLink::commandFinished);
        QJsonObject ack{{"protocol", 2}, {"type", "ack"}, {"request_id", "unrelated"},
                        {"accepted", true}, {"reason", "Accepted"}, {"mission", mission()}};
        peer->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n'); peer->flush();
        QTest::qWait(30); QVERIFY(link.busy()); QCOMPARE(finished.count(), 0);
        ack.insert("request_id", request);
        peer->write(QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n'); peer->flush();
        QTRY_COMPARE(finished.count(), 1); QVERIFY(!link.busy());
        QCOMPARE(finished.at(0).at(2).toBool(), true);
        sendTelemetry(peer, mission("session-2"));
        QTRY_COMPARE(link.mission().value("map_session_id").toString(), QString("session-2"));
        QVERIFY(link.startUploadedMission("mission-1", 1, "session-1").isEmpty());
    }

    void rejectsBadRemoteTelemetryAndLocalEnvelope()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        GroundLink link; link.connectToGateway("127.0.0.1", server.serverPort());
        QVERIFY(server.waitForNewConnection(2000)); QTcpSocket *peer = server.nextPendingConnection();
        auto invalid = mission(); invalid.insert("frame_id", "gps"); sendTelemetry(peer, invalid);
        QTest::qWait(30); QVERIFY(!link.remoteAvailable());
        sendTelemetry(peer, mission()); QTRY_VERIFY(link.remoteAvailable());
        QVariantList points{QVariantMap{{"id", 1}, {"x", 1.}, {"y", 0.}, {"z", 120.}, {"hover_sec", 2.}}};
        QVERIFY(link.uploadLocalRoute(points, "session-1").isEmpty()); QVERIFY(!link.busy());
        QSignalSpy finished(&link, &GroundLink::commandFinished);
        QVERIFY(!link.startUploadedMission("mission-1", 1, "session-1").isEmpty());
        peer->disconnectFromHost(); QTRY_VERIFY(!link.connected());
        QCOMPARE(finished.count(), 1); QVERIFY(!finished.at(0).at(2).toBool());
        QVERIFY(link.mission().isEmpty()); QVERIFY(!link.busy());
    }

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
