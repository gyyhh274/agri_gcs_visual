#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QTemporaryDir>
#include <QPointer>
#include <QTest>
#include <QElapsedTimer>
#include <stdexcept>
#include "AsyncTileMapItem.h"
#include "OfflineMapSource.h"
#include "NestPosition.h"
#include "AircraftProfile.h"
#include "GroundLink.h"
#include "GimbalLink.h"
#include "PhotoLibrary.h"
#include "RelayDiscovery.h"
#include "NativeVideoItem.h"

static void check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate> static void until(Predicate predicate)
{
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 2500) QTest::qWait(10);
    check(predicate(), "Timed out waiting for Qt state");
}
static QVariant eval(QObject *object, const QString &code)
{
    QQmlExpression expression(QQmlEngine::contextForObject(object), object, code);
    const auto result = expression.evaluate();
    if (expression.hasError()) throw std::runtime_error(expression.error().toString().toStdString());
    return result;
}

int main(int argc, char **argv)
{
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("AgriGcsTaskSessionQA"); app.setApplicationName("SessionCheck");
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QQuickStyle::setStyle("Fusion");
    qmlRegisterType<AsyncTileMapItem>("Agri.Map", 1, 0, "AsyncTileMap");
    qmlRegisterType<NativeVideoItem>("Agri.Video", 1, 0, "NativeVideoItem");
    try {
        const QString oldRoute = "[{\"id\":1,\"x\":-2.36,\"y\":2.4,\"z\":1,\"hover_sec\":2}]";
        QSettings settings;
        settings.setValue("IndoorMissionV1/routeJson", oldRoute); settings.sync();
        OfflineMapSource source; NestPosition nest(&source); AircraftProfile aircraft;
        GroundLink link; GimbalLink gimbal; PhotoLibrary photos, videos; RelayDiscovery discovery;
        QQmlApplicationEngine engine;
        auto *ctx = engine.rootContext();
        ctx->setContextProperty("offlineMapSource", &source); ctx->setContextProperty("nestPosition", &nest);
        ctx->setContextProperty("aircraftProfile", &aircraft); ctx->setContextProperty("groundLink", &link);
        ctx->setContextProperty("gimbalLink", &gimbal); ctx->setContextProperty("photoLibrary", &photos);
        ctx->setContextProperty("videoLibrary", &videos); ctx->setContextProperty("relayDiscovery", &discovery);
        ctx->setContextProperty("initialTab", QVariant(0)); ctx->setContextProperty("initialPage", QVariant(0)); ctx->setContextProperty("initialSection", QVariant(0));
        QQmlComponent config(&engine);
        config.setData("import QtQuick 2.15; import \"components\"; QtObject { Component.onCompleted: { VideoConfig.host = \"127.0.0.1\"; VideoConfig.enabled = false } }", QUrl("qrc:/qml/qa-init.qml"));
        QScopedPointer<QObject> setup(config.create()); check(bool(setup), "Cannot isolate video config");
        engine.setInitialProperties({{"previewMode", true}});
        engine.load(QUrl("qrc:/qml/Main.qml")); check(!engine.rootObjects().isEmpty(), "Cannot load Main.qml");
        auto *window = engine.rootObjects().first();
        QPointer<QObject> task = window->findChild<QObject *>("taskPage"); check(task, "No task page");
        check(eval(task, "JSON.parse(localJson())[0].x").toDouble() == -2.36, "Saved fixture not loaded");
        eval(task, "moveLocal(0,1.35,1.57)");
        const auto moved = eval(task, "localJson()").toString();
        auto switchPage = [&](int index) { window->setProperty("pageIndex", index); QCoreApplication::processEvents(); };
        switchPage(2);
        QPointer<QObject> settingsPage = window->findChild<QObject *>("settingsPage");
        check(settingsPage, "No settings page");
        check(task, "Switching pages destroyed the task and lost the edited route");
        check(!task->property("visible").toBool(), "Task page still visible behind settings");
        if (auto *stream = task->findChild<QObject *>("liveVideoStream"))
            check(!stream->property("enabled").toBool(), "Hidden task video still enabled");
        switchPage(0);
        check(settingsPage, "Switching pages destroyed a retained video surface");
        check(window->findChild<QObject *>("taskPage") == task, "Task page was recreated");
        check(eval(task, "localJson()").toString() == moved, "Page switch restored the old negative X");

        QTcpServer server; check(server.listen(QHostAddress::LocalHost), "Cannot listen on loopback");
        link.connectToGateway("127.0.0.1", server.serverPort()); until([&] { return server.hasPendingConnections(); });
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        QJsonObject mission{{"frame_id", "world"}, {"map_session_id", "qa-session"}, {"mission_id", ""}, {"revision", 0},
            {"state", "IDLE"}, {"current_waypoint_id", 0}, {"completed", 0}, {"total", 0}, {"reason", ""},
            {"odom_fresh", true}, {"flight_ready", false}, {"world_x", 0.}, {"world_y", 0.}, {"world_z", 0.},
            {"max_abs_xy", 30.}, {"min_z", .3}, {"max_z", 2.5}};
        auto send = [&](QJsonObject frame) { peer->write(QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n'); peer->flush(); };
        send({{"type", "telemetry"}, {"protocol", 2}, {"server", "agri-onboard-ros"},
            {"fcu_connected", true}, {"armed", false}, {"mission_ready", false}, {"odom_fresh", true}, {"mode", "STABILIZED"},
            {"battery_percent", 67.}, {"world_x", 0.}, {"world_y", 0.}, {"world_z", 0.}, {"mission", mission}});
        until([&] { return link.remoteAvailable(); });
        eval(task, "reviewedSession = 'qa-session'; uploadLocal()"); until([&] { return peer->canReadLine(); });
        const auto upload = QJsonDocument::fromJson(peer->readLine()).object();
        check(upload.value("operation") == "upload", "Expected upload frame");
        check(upload.value("waypoints").toArray()[0].toObject().value("x").toDouble() == 1.35, "Uploaded the old X");
        switchPage(1); // ACK arrives while the task page is hidden.
        mission["mission_id"] = upload.value("mission_id"); mission["revision"] = 1; mission["total"] = 1; mission["state"] = "READY";
        send({{"type", "ack"}, {"protocol", 2}, {"request_id", upload.value("request_id")},
            {"accepted", true}, {"reason", "Accepted"}, {"mission", mission}});
        until([&] { return !link.busy(); }); switchPage(0);
        check(task && task->property("routeMatches").toBool(), "Hidden page lost upload confirmation");
        check(task->property("reviewedSession").toString() == "qa-session", "Page switch lost coordinate review");
        check(eval(task, "localJson()").toString() == moved, "ACK changed the edited coordinates");
        settings.sync();
        check(settings.value("IndoorMissionV1/routeJson").toString() == moved, "Accepted upload did not replace old saved route");
        {
            QQmlApplicationEngine restoredEngine;
            for (const char *name : {"offlineMapSource", "nestPosition", "aircraftProfile", "groundLink", "gimbalLink",
                                    "photoLibrary", "videoLibrary", "relayDiscovery", "initialTab", "initialPage", "initialSection"})
                restoredEngine.rootContext()->setContextProperty(name, ctx->contextProperty(name));
            QQmlComponent restoredConfig(&restoredEngine);
            restoredConfig.setData("import QtQuick 2.15; import \"components\"; QtObject { Component.onCompleted: { VideoConfig.host = \"127.0.0.1\"; VideoConfig.enabled = false } }", QUrl("qrc:/qml/qa-init.qml"));
            QScopedPointer<QObject> restoredSetup(restoredConfig.create()); check(bool(restoredSetup), "Cannot isolate restored video");
            restoredEngine.setInitialProperties({{"previewMode", true}});
            restoredEngine.load(QUrl("qrc:/qml/Main.qml"));
            check(!restoredEngine.rootObjects().isEmpty(), "Cannot reload saved session");
            auto *restored = restoredEngine.rootObjects().first()->findChild<QObject *>("taskPage");
            check(restored, "Cannot reload task from saved settings");
            check(eval(restored, "localJson()").toString() == moved, "A fresh page loaded the old route after upload");
            check(!restored->property("confirmedRoute").toBool(), "Restart incorrectly restored upload confirmation");
            check(restored->property("reviewedSession").toString().isEmpty(), "Restart incorrectly restored coordinate review");
        }
        for (int page : {2, 1, 0, 1, 2, 0}) switchPage(page);
        check(settingsPage && window->findChild<QObject *>("settingsPage") == settingsPage, "Settings page was recreated on navigation");
        check(task && task->property("routeMatches").toBool(), "Repeated switching lost confirmed route");
        eval(task, "moveLocal(0,2.25,1.5)"); const auto draft = eval(task, "localJson()").toString();
        switchPage(2); switchPage(0);
        check(eval(task, "localJson()").toString() == draft, "Switching pages lost a new unsent draft");
        check(!task->property("routeMatches").toBool(), "Unsent draft was marked as uploaded");
        QTest::qWait(50); check(!peer->bytesAvailable(), "Page switching unexpectedly transmitted commands");
        qInfo("Task session checks passed: unsaved coordinates, hidden upload ACK, confirmed route persistence, repeated page switching and no implicit commands.");
        return 0;
    } catch (const std::exception &error) { qCritical("Task session check failed: %s", error.what()); return 1; }
}
