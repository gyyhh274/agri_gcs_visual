#include <QtTest>
#include <QSet>
#include <QVariantMap>
#include "../src/GroundLink.h"

// Opt-in integration test for the isolated ROS gimbal dry-run fixture. Never
// point this at the production gateway. Missing opt-in variables skip the test;
// an endpoint without the exact fixture identity fails before any command.
class JointMissionTest : public QObject {
    Q_OBJECT
    static bool fixtureIdentity(const GroundLink &link)
    {
        const auto status = link.mission();
        const auto dryRun = status.value("gimbal_dry_run");
        return link.remoteAvailable() && status.value("test_backend").toString() == "gimbal-dry-run" &&
               dryRun.userType() == QMetaType::Bool && dryRun.toBool();
    }

    static QVariantList commandReply(const QSignalSpy &spy, const QString &request)
    {
        for (const auto &reply : spy)
            if (reply.at(0).toString() == request)
                return reply;
        return {};
    }

private slots:
    void dryRunWaypointActionsThroughActualGroundLink()
    {
        const QString host = qEnvironmentVariable("AGRI_GCS_JOINT_HOST");
        const QString portText = qEnvironmentVariable("AGRI_GCS_JOINT_PORT");
        if (host.isEmpty() || portText.isEmpty())
            QSKIP("Set AGRI_GCS_JOINT_HOST and AGRI_GCS_JOINT_PORT for the isolated gimbal dry-run fixture");
        bool validPort = false;
        const int port = portText.toInt(&validPort);
        QVERIFY2(validPort && port > 0 && port <= 65535, "Invalid isolated fixture port");

        GroundLink link;
        QSignalSpy replies(&link, &GroundLink::commandFinished);
        link.connectToGateway(host, port);
        QTRY_VERIFY_WITH_TIMEOUT(link.remoteAvailable(), 10000);
        QVERIFY2(fixtureIdentity(link), "Endpoint lacks the exact gimbal-dry-run identity; no commands were sent");
        const QString session = link.mission().value("map_session_id").toString();
        QVERIFY(!session.isEmpty());
        QVERIFY(link.armed()); QVERIFY(link.odomFresh());
        QVERIFY(link.mission().value("flight_ready").toBool());
        QCOMPARE(link.mission().value("gimbal_capabilities").toMap().value("version").toInt(), 1);
        qInfo().noquote() << "[joint] verified isolated fixture" << link.endpoint() << "session" << session;

        QSet<QString> phases;
        QSet<int> actionWaypoints;
        QString lastTransition;
        bool identityLost = false;
        connect(&link, &GroundLink::changed, this, [&]() {
            if (!fixtureIdentity(link)) {
                identityLost = true;
                return;
            }
            const auto status = link.mission();
            const QString phase = status.value("action_phase").toString();
            const int waypoint = status.value("current_waypoint_id").toInt();
            if (!phase.isEmpty()) {
                phases.insert(phase);
                if (phase == "moving" || phase == "settling" || phase == "photo_sent" || phase == "complete")
                    actionWaypoints.insert(waypoint);
            }
            const QString transition = QString("%1 point=%2 action=%3 completed=%4/%5")
                .arg(status.value("state").toString()).arg(waypoint).arg(phase)
                .arg(status.value("completed").toInt()).arg(status.value("total").toInt());
            if (transition != lastTransition) {
                lastTransition = transition;
                qInfo().noquote() << "[joint]" << transition;
            }
        });

        // The fixture emits telemetry every 0.5 s. A 1 s settle window makes
        // phase observation deterministic rather than racing that sampling rate.
        const QVariantMap angle{{"mode", "angle"}, {"yaw_deg", 30.}, {"pitch_deg", -20.},
                                {"speed", 25}, {"settle_sec", 1.}, {"photo", true}};
        const QVariantMap sweep{{"mode", "sweep"}, {"yaw_min_deg", -45.}, {"yaw_max_deg", 45.},
                                {"pitch_deg", -30.}, {"speed", 45}, {"settle_sec", 1.}, {"photo", false}};
        const QVariantList points{
            QVariantMap{{"id", 1}, {"x", 1.}, {"y", 0.}, {"z", 1.}, {"hover_sec", .2}, {"gimbal", angle}},
            QVariantMap{{"id", 2}, {"x", 2.}, {"y", 0.}, {"z", 1.}, {"hover_sec", .2}, {"gimbal", sweep}}};
        // Recheck immediately before each mutation, not only when first connected.
        QVERIFY(fixtureIdentity(link));
        const QString upload = link.uploadLocalRoute(points, session);
        QVERIFY2(!upload.isEmpty(), "Dry-run upload was rejected locally");
        QTRY_VERIFY_WITH_TIMEOUT(!commandReply(replies, upload).isEmpty(), 6000);
        const auto uploadAck = commandReply(replies, upload);
        QCOMPARE(uploadAck.at(1).toString(), QString("upload"));
        QVERIFY2(uploadAck.at(2).toBool(), qPrintable(uploadAck.at(3).toString()));
        QCOMPARE(link.mission().value("state").toString(), QString("READY"));
        QCOMPARE(link.mission().value("revision").toInt(), 1);
        QCOMPARE(link.mission().value("total").toInt(), 2);
        QCOMPARE(link.mission().value("map_session_id").toString(), session);
        const QString missionId = link.mission().value("mission_id").toString();
        QVERIFY(!missionId.isEmpty());
        QTest::qWait(500);
        QVERIFY2(!identityLost, "Isolated fixture identity changed after upload");
        QCOMPARE(link.mission().value("state").toString(), QString("READY"));
        QCOMPARE(link.mission().value("completed").toInt(), 0); // Upload alone never starts flight.

        // Exercise the real read/status command before explicitly starting.
        QVERIFY(fixtureIdentity(link));
        const int repliesBeforeQuery = replies.count();
        link.queryMission();
        QTRY_VERIFY_WITH_TIMEOUT(replies.count() > repliesBeforeQuery, 6000);
        QCOMPARE(replies.last().at(1).toString(), QString("get_status"));
        QVERIFY2(replies.last().at(2).toBool(), qPrintable(replies.last().at(3).toString()));
        QCOMPARE(link.mission().value("mission_id").toString(), missionId);
        QCOMPARE(link.mission().value("revision").toInt(), 1);

        QVERIFY(fixtureIdentity(link));
        QVERIFY2(!identityLost, "Fixture identity changed; start was not sent");
        const QString start = link.startUploadedMission(missionId, 1, session);
        QVERIFY(!start.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!commandReply(replies, start).isEmpty(), 6000);
        const auto startAck = commandReply(replies, start);
        QCOMPARE(startAck.at(1).toString(), QString("start"));
        QVERIFY2(startAck.at(2).toBool(), qPrintable(startAck.at(3).toString()));
        QTRY_VERIFY_WITH_TIMEOUT(identityLost || link.mission().value("state").toString() == "FAILED" ||
                                link.mission().value("state").toString() == "COMPLETED", 60000);
        QVERIFY2(!identityLost, "Isolated fixture identity or connection was lost");
        QVERIFY2(link.mission().value("state").toString() == "COMPLETED",
                 qPrintable(link.mission().value("reason").toString()));
        QCOMPARE(link.mission().value("completed").toInt(), 2);
        QCOMPARE(link.mission().value("mission_id").toString(), missionId);
        QCOMPARE(link.mission().value("map_session_id").toString(), session);
        QCOMPARE(link.mission().value("revision").toInt(), 1);
        QVERIFY2(phases.contains("moving"), "No automatic gimbal movement phase observed");
        QVERIFY2(phases.contains("settling"), "No gimbal settling phase observed");
        QVERIFY(actionWaypoints.contains(1)); QVERIFY(actionWaypoints.contains(2));
        qInfo().noquote() << "[joint] completed two dry-run actions; speeds 25/45, angle+sweep, photo true/false";
    }
};

QTEST_GUILESS_MAIN(JointMissionTest)
#include "tst_joint_mission.moc"
