#include <QtTest>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <cstring>
#include "GroundLink.h"
#include "MappingClient.h"
#include "RoundTripMission.h"

namespace {
quint16 listenLocally(QTcpServer &server) {
    if (!server.listen(QHostAddress::LocalHost)) qFatal("Cannot start loopback test server");
    return server.serverPort();
}
QByteArray mapFrame(const QJsonObject &header, const QByteArray &payload = {}) {
    const auto json = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray prefix(12, 0);
    std::memcpy(prefix.data(), "AGM1", 4);
    qToLittleEndian<quint32>(json.size(), reinterpret_cast<uchar *>(prefix.data()+4));
    qToLittleEndian<quint32>(payload.size(), reinterpret_cast<uchar *>(prefix.data()+8));
    return prefix + json + payload;
}

// Both peers bind ephemeral loopback ports. Never enable the production task tunnel.
class MissionHarness : public QObject {
public:
    QTcpServer taskServer, mapServer;
    GroundLink link;
    MappingClient map;
    RoundTripMission mission{&link, &map, listenLocally(mapServer)};
    QPointer<QTcpSocket> taskPeer, mapPeer;
    QList<QJsonObject> commands;
    QByteArray incoming;
    QJsonObject state{{"frame_id","world"}, {"map_session_id","task-session"},
        {"mission_id",""}, {"revision",0}, {"state","IDLE"},
        {"current_waypoint_id",0}, {"completed",0}, {"total",0}, {"reason",""},
        {"odom_fresh",true}, {"flight_ready",false}, {"world_x",.4},
        {"world_y",-.2}, {"world_z",.2}, {"max_abs_xy",30.},
        {"min_z",.3}, {"max_z",2.5}, {"automatic_supported",true},
        {"automatic",false}, {"takeoff_ready",true}, {"takeoff_height",1.}};
    QJsonArray bounds{-2.,-2.,-.2,2.,2.,2.5};
    QString mapSession = "map-session";
    bool autoAck = true, aligned = true, armed = false, odomFresh = true;
    int protocol = 2;
    double worldX = .4, worldZ = .2;
    QTimer heartbeat;
    int sequence = 0;

    MissionHarness() {
        listenLocally(taskServer);
        connect(&taskServer, &QTcpServer::newConnection, this, [this] {
            taskPeer = taskServer.nextPendingConnection(); incoming.clear();
            connect(taskPeer, &QTcpSocket::readyRead, this, [this] {
                incoming += taskPeer->readAll();
                while (incoming.contains('\n')) {
                    const int end = incoming.indexOf('\n');
                    const auto command = QJsonDocument::fromJson(incoming.left(end)).object();
                    incoming.remove(0,end+1); commands.append(command);
                    if (!autoAck) continue;
                    const auto operation = command.value("operation").toString();
                    if (operation == "upload") state = uploadedState(command);
                    if (operation == "start") {
                        state["state"] = "TAKING_OFF"; state["takeoff_ready"] = false;
                    }
                    if (operation == "cancel") state["state"] = "FAILED";
                    acknowledge(command, state);
                }
            });
        });
        connect(&mapServer, &QTcpServer::newConnection, this, [this] {
            mapPeer = mapServer.nextPendingConnection();
        });
        link.connectToGateway("127.0.0.1", taskServer.serverPort());
        map.setHost("127.0.0.1"); map.setPort(mapServer.serverPort()); map.setEnabled(true);
        connect(&heartbeat, &QTimer::timeout, this, [this] { publish(); });
        heartbeat.start(40);
    }
    ~MissionHarness() override { heartbeat.stop(); }

    QJsonObject uploadedState(const QJsonObject &command) const {
        auto result = state;
        result["mission_id"] = command.value("mission_id"); result["revision"] = 1;
        result["state"] = "READY"; result["total"] = 2; result["completed"] = 0;
        result["automatic"] = true;
        return result;
    }
    void acknowledge(const QJsonObject &command, const QJsonObject &status,
                     bool accepted = true, const QString &requestId = {}) {
        QJsonObject ack{{"type","ack"}, {"protocol",2},
            {"request_id",requestId.isEmpty() ? command.value("request_id").toString() : requestId},
            {"accepted",accepted}, {"reason",accepted ? "accepted" : "rejected"}, {"mission",status}};
        taskPeer->write(QJsonDocument(ack).toJson(QJsonDocument::Compact)+'\n');
        taskPeer->flush();
    }
    void publish() {
        const double now = QDateTime::currentMSecsSinceEpoch()/1000.;
        QJsonObject telemetry{{"type","telemetry"}, {"protocol",protocol},
            {"server","agri-onboard-ros"}, {"fcu_connected",true}, {"armed",armed},
            {"mission_ready",armed}, {"odom_fresh",odomFresh}, {"mode",armed ? "OFFBOARD" : "STABILIZED"},
            {"battery_percent",80.}, {"world_x",worldX}, {"world_y",-.2}, {"world_z",worldZ},
            {"mission",state}, {"map_coordinates_valid",aligned}, {"map_odom_stamp",now}, {"flight_bounds",bounds}};
        if (taskPeer && taskPeer->state() == QAbstractSocket::ConnectedState)
            taskPeer->write(QJsonDocument(telemetry).toJson(QJsonDocument::Compact)+'\n');
        if (mapPeer && mapPeer->state() == QAbstractSocket::ConnectedState) {
            QJsonObject header{{"protocol",1}, {"session",mapSession}, {"frame","world"},
                {"stamp",now}, {"seq",++sequence}, {"demo",false}, {"type","cloud"}, {"point_count",1}};
            mapPeer->write(mapFrame(header,QByteArray(16,0)));
            header["type"] = "pose"; header["seq"] = ++sequence;
            header["position"] = QJsonArray{worldX,-.2,worldZ}; header["orientation"] = QJsonArray{0,0,0,1};
            mapPeer->write(mapFrame(header));
        }
    }
    void prepareDraft() { mission.recordHome(); mission.setTarget(1.1,.3); }
};
}

class MissionExecutionTest : public QObject {
    Q_OBJECT
private slots:
    void explicitUploadStartAndCompletion() {
        MissionHarness h;
        QTRY_VERIFY_WITH_TIMEOUT(h.mission.canRecord(),4000);
        h.prepareDraft(); QVERIFY(h.mission.canUpload()); QVERIFY(h.commands.isEmpty());
        h.mission.upload(2);
        QTRY_VERIFY(h.mission.canStart()); QCOMPARE(h.commands.size(),1);
        const auto upload = h.commands.first();
        QCOMPARE(upload.value("operation").toString(),QString("upload"));
        QCOMPARE(upload.value("protocol").toInt(),2);
        QCOMPARE(upload.value("frame_id").toString(),QString("world"));
        QCOMPARE(upload.value("map_session_id").toString(),QString("task-session"));
        QCOMPARE(upload.value("revision").toInt(),1);
        QVERIFY(!upload.value("request_id").toString().isEmpty());
        QVERIFY(upload.value("automatic").toBool()); QVERIFY(upload.value("require_map_alignment").toBool());
        const auto origin = upload.value("ground_origin").toObject();
        QVERIFY(qAbs(origin.value("x").toDouble()-.4)<1e-6);
        QVERIFY(qAbs(origin.value("z").toDouble()-.2)<1e-6);
        const auto points = upload.value("waypoints").toArray(); QCOMPARE(points.size(),2);
        QCOMPARE(points[0].toObject().value("id").toInt(),1);
        QVERIFY(qAbs(points[0].toObject().value("x").toDouble()-1.1)<1e-6);
        QCOMPARE(points[0].toObject().value("hover_sec").toDouble(),2.);
        QCOMPARE(points[1].toObject().value("id").toInt(),2);
        QVERIFY(qAbs(points[1].toObject().value("x").toDouble()-.4)<1e-6);
        QVERIFY(qAbs(points[1].toObject().value("z").toDouble()-1.2)<1e-6);
        QTest::qWait(150); QCOMPARE(h.commands.size(),1); // Upload ACK never starts flight.
        h.mission.start(); h.mission.start();
        QTRY_COMPARE(h.commands.size(),2); QTRY_VERIFY(h.mission.active());
        const auto start = h.commands[1];
        QCOMPARE(start.value("operation").toString(),QString("start"));
        QCOMPARE(start.value("mission_id"),upload.value("mission_id"));
        QCOMPARE(start.value("map_session_id"),upload.value("map_session_id"));
        QVERIFY(!start.contains("waypoints"));
        for (const QString &stage : {QString("TAKING_OFF"),QString("FLYING"),QString("HOVERING"),QString("LANDING")}) {
            h.state["state"] = stage;
            if (stage == "LANDING") h.state["completed"] = 2;
            QTRY_COMPARE(h.link.mission().value("state").toString(),stage);
            QVERIFY(h.mission.active()); QVERIFY(!h.mission.canStart());
            QVERIFY(!h.mission.canEdit()); QVERIFY(!h.mission.canRecord());
        }
        QVERIFY(h.mission.phase().contains(QStringLiteral("正在自动降落")));
        h.state["state"] = "COMPLETED";
        QTRY_VERIFY(!h.mission.active()); QVERIFY(h.mission.phase().contains(QStringLiteral("往返完成")));
        h.mission.start(); QTest::qWait(100); QCOMPARE(h.commands.size(),2);
    }

    void uploadAckMustMatchTransaction_data() {
        QTest::addColumn<QString>("fault");
        for (const char *fault : {"request","mission","revision","session","total","state","automatic","rejected"})
            QTest::newRow(fault) << QString(fault);
    }
    void uploadAckMustMatchTransaction() {
        QFETCH(QString,fault);
        MissionHarness h; h.autoAck = false;
        QTRY_VERIFY_WITH_TIMEOUT(h.mission.canRecord(),4000);
        h.prepareDraft(); h.mission.upload(1);
        QTRY_COMPARE(h.commands.size(),1);
        auto status = h.uploadedState(h.commands[0]);
        if (fault == "mission") status["mission_id"] = "another-mission";
        if (fault == "revision") status["revision"] = 2;
        if (fault == "session") status["map_session_id"] = "another-session";
        if (fault == "total") status["total"] = 1;
        if (fault == "state") status["state"] = "IDLE";
        if (fault == "automatic") status["automatic"] = false;
        h.state = status;
        QSignalSpy finished(&h.link,&GroundLink::commandFinished);
        h.acknowledge(h.commands[0],status,fault != "rejected",fault == "request" ? "unrelated" : QString());
        if (fault == "request") { QTest::qWait(120); QVERIFY(h.link.busy()); QCOMPARE(finished.count(),0); }
        else QTRY_COMPARE(finished.count(),1);
        QVERIFY(!h.mission.canStart()); h.mission.start();
        QTest::qWait(100); QCOMPARE(h.commands.size(),1);
    }

    void changingDraftOrPreflightBlocksStart_data() {
        QTest::addColumn<QString>("change");
        for (const char *change : {"target","height","map-session","task-session","bounds","position",
                                  "takeoff-height","paused","alignment","odometry","controller","armed"})
            QTest::newRow(change) << QString(change);
    }
    void changingDraftOrPreflightBlocksStart() {
        QFETCH(QString,change);
        MissionHarness h;
        QTRY_VERIFY_WITH_TIMEOUT(h.mission.canRecord(),4000);
        h.prepareDraft(); h.mission.upload(1); QTRY_VERIFY(h.mission.canStart());
        if (change == "target") h.mission.setTarget(1.2,.3);
        if (change == "height") h.mission.setHeight(1.3);
        if (change == "map-session") h.mapSession = "new-map-session";
        if (change == "task-session") h.state["map_session_id"] = "new-task-session";
        if (change == "bounds") h.bounds[3] = 1.8;
        if (change == "position") h.worldX = .6;
        if (change == "takeoff-height") h.state["takeoff_height"] = 1.2;
        if (change == "paused") h.map.setPaused(true);
        if (change == "alignment") h.aligned = false;
        if (change == "odometry") h.odomFresh = false;
        if (change == "controller") h.state["takeoff_ready"] = false;
        if (change == "armed") h.armed = true;
        QTRY_VERIFY(!h.mission.canStart()); h.mission.start();
        QTest::qWait(100); QCOMPARE(h.commands.size(),1);
    }

    void cancellationIsExplicitAndFailureIsLatched_data() {
        QTest::addColumn<QString>("stage");
        for (const char *stage : {"READY","TAKING_OFF","FLYING","HOVERING","LANDING"})
            QTest::newRow(stage) << QString(stage);
    }
    void cancellationIsExplicitAndFailureIsLatched() {
        QFETCH(QString,stage);
        MissionHarness h;
        QTRY_VERIFY_WITH_TIMEOUT(h.mission.canRecord(),4000);
        h.prepareDraft(); h.mission.upload(1); QTRY_VERIFY(h.mission.canStart());
        h.state["state"] = stage;
        QTRY_COMPARE(h.link.mission().value("state").toString(),stage);
        QVERIFY(h.mission.canCancel()); h.mission.cancel();
        QTRY_COMPARE(h.commands.size(),2);
        QCOMPARE(h.commands[1].value("operation").toString(),QString("cancel"));
        QCOMPARE(h.commands[1].value("mission_id"),h.commands[0].value("mission_id"));
        QCOMPARE(h.commands[1].value("revision").toInt(),1);
        QTRY_COMPARE(h.link.mission().value("state").toString(),QString("FAILED"));
        QVERIFY(!h.mission.canStart()); QVERIFY(!h.mission.canUpload());
        h.mission.start(); h.mission.upload(1); QTest::qWait(100); QCOMPARE(h.commands.size(),2);
    }

    void timeoutAndLateAckNeverStartOrReplay() {
        MissionHarness h; h.autoAck = false;
        QTRY_VERIFY_WITH_TIMEOUT(h.mission.canRecord(),4000);
        QSignalSpy finished(&h.link,&GroundLink::commandFinished);
        h.prepareDraft(); h.mission.upload(1); QTRY_COMPARE(h.commands.size(),1);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,6500);
        QVERIFY(!finished[0][1].toBool()); QVERIFY(!h.link.busy());
        QVERIFY(h.mission.status().contains(QStringLiteral("未知")));
        h.state = h.uploadedState(h.commands[0]); h.acknowledge(h.commands[0],h.state);
        QTest::qWait(150); QCOMPARE(finished.count(),1); QVERIFY(!h.mission.canStart());
        h.mission.start(); QTest::qWait(100); QCOMPARE(h.commands.size(),1);
    }

    void reconnectQueriesButNeverReplaysStart() {
        MissionHarness h;
        QTRY_VERIFY_WITH_TIMEOUT(h.mission.canRecord(),4000);
        h.prepareDraft(); h.mission.upload(1); QTRY_VERIFY(h.mission.canStart());
        h.autoAck = false; QSignalSpy finished(&h.link,&GroundLink::commandFinished);
        h.mission.start(); QTRY_COMPARE(h.commands.size(),2);
        h.state["state"] = "TAKING_OFF"; h.state["takeoff_ready"] = false;
        h.taskPeer->abort(); QTRY_VERIFY(!h.link.connected());
        QTRY_COMPARE(finished.count(),1); QVERIFY(!finished[0][1].toBool());
        QVERIFY(!h.mission.homeValid()); QVERIFY(!h.mission.canStart());
        h.autoAck = true;
        h.link.connectToGateway("127.0.0.1",h.taskServer.serverPort());
        QTRY_VERIFY(h.link.connected());
        QTest::qWait(200); QCOMPARE(h.commands.size(),2);
        h.link.queryMission(); QTRY_COMPARE(h.commands.size(),3);
        QCOMPARE(h.commands[2].value("operation").toString(),QString("get_status"));
        QTRY_VERIFY(!h.link.busy());
        RoundTripMission reopened(&h.link,&h.map,h.mapServer.serverPort());
        QVERIFY(!reopened.canStart()); QVERIFY(reopened.canCancel());
        reopened.start(); h.mission.start(); QTest::qWait(100); QCOMPARE(h.commands.size(),3);
    }

    void transportRejectsStaleIdentity_data() {
        QTest::addColumn<QString>("operation"); QTest::addColumn<QString>("session");
        QTest::addColumn<QString>("missionId"); QTest::addColumn<int>("revision");
        QTest::newRow("old-session-upload") << QString("upload") << QString("old-session") << QString("new-mission") << 1;
        QTest::newRow("old-session-start") << QString("start") << QString("old-session") << QString("mission-1") << 1;
        QTest::newRow("old-mission-start") << QString("start") << QString("task-session") << QString("old-mission") << 1;
        QTest::newRow("old-revision-start") << QString("start") << QString("task-session") << QString("mission-1") << 2;
        QTest::newRow("old-mission-cancel") << QString("cancel") << QString("task-session") << QString("old-mission") << 1;
        QTest::newRow("old-revision-cancel") << QString("cancel") << QString("task-session") << QString("mission-1") << 2;
    }
    void transportRejectsStaleIdentity() {
        QFETCH(QString,operation); QFETCH(QString,session); QFETCH(QString,missionId); QFETCH(int,revision);
        MissionHarness h;
        h.state["mission_id"] = "mission-1"; h.state["revision"] = 1;
        h.state["state"] = "READY"; h.state["automatic"] = true; h.state["total"] = 2;
        QTRY_VERIFY(h.link.connected());
        QVERIFY(h.link.sendCommand(operation,session,missionId,revision).isEmpty());
        QVERIFY(!h.link.busy()); QTest::qWait(80); QVERIFY(h.commands.isEmpty());
    }

    void legacyTelemetryCannotControlMissions() {
        MissionHarness h; h.protocol = 1;
        QTRY_VERIFY(h.link.connected()); QCOMPARE(h.link.protocolVersion(),1);
        QVERIFY(!h.mission.contextReady()); QVERIFY(!h.mission.canRecord());
        QVERIFY(h.link.sendCommand("start","task-session","mission-1",1).isEmpty());
        h.mission.upload(1); h.mission.start(); QTest::qWait(100); QVERIFY(h.commands.isEmpty());
    }

    void malformedAckLeavesRequestPending_data() {
        QTest::addColumn<QString>("field");
        for (const char *field : {"frame_id","total","completed","takeoff_height"})
            QTest::newRow(field) << QString(field);
    }
    void malformedAckLeavesRequestPending() {
        QFETCH(QString,field);
        MissionHarness h; h.autoAck = false;
        QTRY_VERIFY_WITH_TIMEOUT(h.mission.canRecord(),4000);
        h.prepareDraft(); h.mission.upload(1); QTRY_COMPARE(h.commands.size(),1);
        auto invalid = h.uploadedState(h.commands[0]);
        if (field == "frame_id") invalid[field] = "gps";
        if (field == "total") invalid[field] = 201;
        if (field == "completed") invalid[field] = 3;
        if (field == "takeoff_height") invalid[field] = "1.0";
        QSignalSpy finished(&h.link,&GroundLink::commandFinished);
        h.acknowledge(h.commands[0],invalid); QTest::qWait(100);
        QVERIFY(h.link.busy()); QVERIFY(!h.mission.canStart()); QCOMPARE(finished.count(),0);
        h.state = h.uploadedState(h.commands[0]); h.acknowledge(h.commands[0],h.state);
        QTRY_COMPARE(finished.count(),1); QTRY_VERIFY(h.mission.canStart());
        QCOMPARE(h.commands.size(),1);
    }
};

QTEST_GUILESS_MAIN(MissionExecutionTest)
#include "tst_mission_execution.moc"
