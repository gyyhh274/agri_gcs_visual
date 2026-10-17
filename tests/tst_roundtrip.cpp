#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QtEndian>
#include <cstring>
#include <cmath>
#include "GroundLink.h"
#include "MappingClient.h"
#include "RoundTripMission.h"
#include "PointCloudView.h"

static QByteArray mapFrame(QJsonObject header, QByteArray payload = {}) {
    auto raw = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray prefix(12,0); std::memcpy(prefix.data(),"AGM1",4);
    qToLittleEndian<quint32>(raw.size(),reinterpret_cast<uchar*>(prefix.data()+4));
    qToLittleEndian<quint32>(payload.size(),reinterpret_cast<uchar*>(prefix.data()+8));
    return prefix+raw+payload;
}
class RoundTripTest : public QObject {
    Q_OBJECT
private slots:
    void pickProjectionMatchesEveryCameraAngle() {
        PointCloudView view; view.setWidth(800); view.setHeight(600); view.setTopDown(true);
        for (double angle : {-135.,-45.,0.,90.}) {
            view.setYaw(angle); view.pan(10,-5); view.zoom(.5);
            const QPointF screen = view.projectPoint(.7,-.4,1.2);
            const auto point = view.worldAt(screen.x(),screen.y(),1.2);
            QVERIFY(point.value("valid").toBool());
            QVERIFY(std::abs(point.value("x").toDouble()-.7) < .0001);
            QVERIFY(std::abs(point.value("y").toDouble()+.4) < .0001);
        }
        view.setTopDown(false);
        QVERIFY(!view.worldAt(400,300,1).value("valid").toBool());
    }
    void uploadStartReturnCancelAndSessionInvalidation() {
        QTcpServer taskServer, mapServer;
        QVERIFY(taskServer.listen(QHostAddress::LocalHost)); QVERIFY(mapServer.listen(QHostAddress::LocalHost));
        GroundLink link; MappingClient map;
        RoundTripMission mission(&link,&map,mapServer.serverPort());
        link.connectToGateway("127.0.0.1",taskServer.serverPort());
        map.setHost("127.0.0.1"); map.setPort(mapServer.serverPort()); map.setEnabled(true);
        QTRY_VERIFY(taskServer.hasPendingConnections() && mapServer.hasPendingConnections());
        auto *taskPeer = taskServer.nextPendingConnection(); auto *mapPeer = mapServer.nextPendingConnection();
        QList<QJsonObject> commands;
        QJsonObject state{{"frame_id","world"},{"map_session_id","task-session"},{"mission_id",""},
            {"revision",0},{"state","IDLE"},{"current_waypoint_id",0},{"completed",0},{"total",0},
            {"reason",""},{"odom_fresh",true},{"flight_ready",true},{"world_x",.4},{"world_y",-.2},{"world_z",1.2},
            {"max_abs_xy",30.},{"min_z",.3},{"max_z",2.5}};
        QByteArray incoming;
        connect(taskPeer,&QTcpSocket::readyRead,this,[&] {
            incoming += taskPeer->readAll();
            while (incoming.contains('\n')) {
                int end = incoming.indexOf('\n'); const auto command = QJsonDocument::fromJson(incoming.left(end)).object(); incoming.remove(0,end+1);
                commands.append(command);
                if (command.value("operation") == "upload") {
                    state["mission_id"] = command.value("mission_id"); state["revision"] = 1;
                    state["state"] = "READY"; state["total"] = 2; state["completed"] = 0;
                } else if (command.value("operation") == "start") state["state"] = "FLYING";
                else if (command.value("operation") == "cancel") state["state"] = "FAILED";
                QJsonObject ack{{"type","ack"},{"protocol",2},{"request_id",command.value("request_id")},
                                {"accepted",true},{"reason","accepted"},{"mission",state}};
                taskPeer->write(QJsonDocument(ack).toJson(QJsonDocument::Compact)+'\n');
            }
        });
        int seq = 0; QString mapSession = "map-session"; bool aligned = true;
        QTimer ticker;
        connect(&ticker,&QTimer::timeout,this,[&] {
            const double now = QDateTime::currentMSecsSinceEpoch()/1000.;
            QJsonObject telemetry{{"type","telemetry"},{"protocol",2},{"server","agri-onboard-ros"},
                {"fcu_connected",true},{"armed",true},{"mission_ready",true},{"odom_fresh",true},{"mode","OFFBOARD"},
                {"battery_percent",80.},{"world_x",.4},{"world_y",-.2},{"world_z",1.2},{"mission",state},
                {"map_coordinates_valid",aligned},{"map_odom_stamp",now},{"flight_bounds",QJsonArray{-2,-2,.3,2,2,2.5}}};
            taskPeer->write(QJsonDocument(telemetry).toJson(QJsonDocument::Compact)+'\n');
            QJsonObject h{{"protocol",1},{"session",mapSession},{"frame","world"},{"stamp",now},{"seq",++seq},{"demo",false},
                          {"type","cloud"},{"point_count",1}};
            mapPeer->write(mapFrame(h,QByteArray(16,0)));
            h["type"]="pose"; h["seq"]=++seq; h["position"]=QJsonArray{.4,-.2,1.2}; h["orientation"]=QJsonArray{0,0,0,1};
            mapPeer->write(mapFrame(h));
        });
        ticker.start(50);
        QTRY_VERIFY_WITH_TIMEOUT(mission.canRecord(),4000);
        QVERIFY(commands.isEmpty()); QVERIFY(!mission.canStart());
        mission.recordHome(); mission.setTarget(1.1,.3);
        QVERIFY(mission.canUpload()); mission.upload(2);
        QTRY_COMPARE(commands.size(),1);
        const auto points = commands[0].value("waypoints").toArray();
        QCOMPARE(points.size(),2); QCOMPARE(points[1].toObject().value("x").toDouble(),double(float(.4)));
        QCOMPARE(points[1].toObject().value("z").toDouble(),double(float(1.2)));
        QVERIFY(commands[0].value("require_map_alignment").toBool());
        QTRY_VERIFY(mission.canStart()); mission.start();
        QTRY_COMPARE(commands.size(),2); QTRY_VERIFY(mission.active());
        state["completed"]=1; state["state"]="FLYING";
        QTRY_VERIFY(mission.phase().contains(QStringLiteral("返回")));
        state["completed"]=2; state["state"]="COMPLETED";
        QTRY_VERIFY(mission.phase().contains(QStringLiteral("往返完成")));
        QVERIFY(!mission.canStart()); QCOMPARE(commands.size(),2);
        QTRY_VERIFY(mission.canRecord()); mission.recordHome(); mission.setTarget(.9,.2); mission.upload(0);
        QTRY_COMPARE(commands.size(),3); QTRY_VERIFY(mission.canStart());
        aligned = false; QTRY_VERIFY(!mission.contextReady()); mission.start(); QCOMPARE(commands.size(),3);
        aligned = true; QTRY_VERIFY(mission.canStart()); mission.start(); QTRY_COMPARE(commands.size(),4);
        RoundTripMission reconnected(&link,&map,mapServer.serverPort());
        QVERIFY(!reconnected.canStart()); QVERIFY(reconnected.phase().contains(QStringLiteral("机载已有任务")));
        QTRY_VERIFY(reconnected.canCancel()); reconnected.cancel(); QTRY_COMPARE(commands.size(),5);
        QCOMPARE(commands[4].value("operation").toString(),QString("cancel"));
        QTRY_VERIFY(!mission.active());
        mapSession = "new-map-session";
        QTRY_VERIFY(!mission.homeValid()); QVERIFY(!mission.canStart());
        mission.start(); QTest::qWait(100); QCOMPARE(commands.size(),5);
    }
};
QTEST_MAIN(RoundTripTest)
#include "tst_roundtrip.moc"
