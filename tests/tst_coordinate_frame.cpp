#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QScopedPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include "../src/GroundLink.h"

class CoordinateFrameTest : public QObject {
    Q_OBJECT
    QQmlEngine m_engine;
    QScopedPointer<QObject> m_scope;
    QTemporaryDir m_settings;
    QVariant eval(const QString &expression) {
        QQmlExpression script(m_engine.rootContext(), m_scope.data(), expression);
        const auto result = script.evaluate();
        if (script.hasError()) qFatal("Coordinate expression failed: %s", qPrintable(script.error().toString()));
        return result;
    }
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("AgriGcsCoordinateQA");
        QCoreApplication::setApplicationName("FrameCheck");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QQmlComponent component(&m_engine);
        component.setData("import QtQuick 2.15; import \"qrc:/qml/components\"; QtObject { property var frame: IndoorFrame }", QUrl());
        m_scope.reset(component.create());
        QVERIFY2(m_scope, qPrintable(component.errorString()));
    }
    void operatorDirectionsAreDefault() {
        eval("frame.bindSession('site-default')");
        QCOMPARE(eval("frame.rotationDeg").toDouble(), 90.0);
        QVERIFY(eval("frame.forwardAxes").toBool());
        // Operator's actual observations: after the -90 setup: backward increased Y and right decreased X.
        // Correcting by 180 degrees yields +90: forward world +X, right world -Y.
        QVERIFY(eval("{var p=frame.toDisplay(2,0); Math.abs(p.x)<1e-9 && Math.abs(p.y-2)<1e-9}").toBool());
        QVERIFY(eval("{var p=frame.toDisplay(0,-2); Math.abs(p.x-2)<1e-9 && Math.abs(p.y)<1e-9}").toBool());
        QVERIFY(eval("{var p=frame.toDisplay(-2,0); Math.abs(p.x)<1e-9 && Math.abs(p.y+2)<1e-9}").toBool());
        QVERIFY(eval("{var p=frame.toDisplay(0,2); Math.abs(p.x+2)<1e-9 && Math.abs(p.y)<1e-9}").toBool());
        QVERIFY(eval("{var p=frame.toWorld(0,2); Math.abs(p.x-2)<1e-9 && Math.abs(p.y)<1e-9}").toBool());
        QVERIFY(eval("{var p=frame.toWorld(2,0); Math.abs(p.x)<1e-9 && Math.abs(p.y+2)<1e-9}").toBool());
    }
    void restartRetainsConfiguredRotation() {
        eval("frame.bindSession('saved-axes'); frame.setRotation(90,'saved-axes')");
        QQmlEngine restarted;
        QQmlComponent component(&restarted);
        component.setData("import QtQuick 2.15; import \"qrc:/qml/components\"; QtObject { property var frame: IndoorFrame }", QUrl());
        QScopedPointer<QObject> scope(component.create()); QVERIFY2(scope, qPrintable(component.errorString()));
        QQmlExpression check(restarted.rootContext(), scope.data(), "frame.bindSession('after-restart'); frame.rotationDeg === 90 && frame.forwardAxes && !frame.calibrated");
        QVERIFY(check.evaluate().toBool()); QVERIFY(!check.hasError());
    }
    void baselineDefinesPositiveY() {
        eval("frame.bindSession('baseline')");
        QVERIFY(eval("frame.beginCalibration(2,3,'baseline')").toBool());
        QVERIFY(!eval("frame.finishCalibration(2.1,3.1,'baseline')").toBool());
        QVERIFY(eval("frame.calibrationPending").toBool());
        QVERIFY(eval("frame.finishCalibration(4,5,'baseline')").toBool());
        QVERIFY(qAbs(eval("frame.rotationDeg").toDouble() - 45.0) < 1e-8);
        QVERIFY(qAbs(eval("frame.toDisplay(2,2).x").toDouble()) < 1e-8);
        QVERIFY(eval("frame.toDisplay(2,2).y").toDouble() > 2.8);
        QVERIFY(qAbs(eval("frame.toDisplay(2,-2).y").toDouble()) < 1e-8);
        QVERIFY(eval("frame.toDisplay(2,-2).x").toDouble() > 2.8);
        QVERIFY(eval("frame.toDisplay(0,0).x === 0 && frame.toDisplay(0,0).y === 0").toBool());
    }
    void inversePreservesWorldRoute() {
        eval("frame.bindSession('roundtrip')");
        for (double rotation : {-179.0, -90.0, -37.5, 0.0, 45.0, 90.0, 135.0}) {
            QVERIFY(eval(QString("frame.setRotation(%1,'roundtrip')").arg(rotation)).toBool());
            QVERIFY(eval("{ var p=frame.toDisplay(6.15,-0.02); var w=frame.toWorld(p.x,p.y); Math.abs(w.x-6.15)<1e-9 && Math.abs(w.y+0.02)<1e-9 }").toBool());
        }
        eval("frame.setRotation(90,'roundtrip')");
        QVERIFY(qAbs(eval("frame.toDisplay(6,0).x").toDouble()) < 1e-8);
        QCOMPARE(eval("frame.toDisplay(6,0).y").toDouble(), 6.0);
        QVERIFY(qAbs(eval("frame.toWorld(0,6).y").toDouble()) < 1e-8);
        QCOMPARE(eval("frame.toWorld(0,6).x").toDouble(), 6.0);
    }
    void reconnectAndNewSession() {
        eval("frame.bindSession('session-a'); frame.setRotation(90,'session-a')");
        eval("frame.bindSession(''); frame.bindSession('session-a')");
        QCOMPARE(eval("frame.rotationDeg").toDouble(), 90.0);
        QVERIFY(eval("frame.calibrated").toBool());
        eval("frame.beginCalibration(0,0,'session-a'); frame.bindSession('session-b')");
        QVERIFY(!eval("frame.calibrationPending || frame.calibrated").toBool());
        QCOMPARE(eval("frame.rotationDeg").toDouble(), 90.0);
        QVERIFY(!eval("frame.finishCalibration(1,0,'session-a')").toBool());
        QVERIFY(!eval("frame.setRotation(45,'session-a')").toBool());
        QCOMPARE(eval("frame.rotationDeg").toDouble(), 90.0);
    }
    void mapClicksAndDroneUseSameTransform() {
        GroundLink link;
        m_engine.rootContext()->setContextProperty("groundLink", &link);
        QQmlComponent component(&m_engine);
        component.setData("import QtQuick 2.15; import \"qrc:/qml/components\"; IndoorMapPanel { width: 960; height: 540 }", QUrl());
        QScopedPointer<QObject> map(component.create());
        QVERIFY2(map, qPrintable(component.errorString()));
        eval("frame.bindSession('map-test'); frame.setRotation(90,'map-test')");
        QQmlExpression forward(m_engine.rootContext(), map.data(), "pointFor(3,0).x === width/2 && Math.abs(pointFor(3,0).y-(height/2-90))<1e-9");
        QVERIFY(forward.evaluate().toBool());
        QQmlExpression inverse(m_engine.rootContext(), map.data(), "Math.abs(coordinateAt(width/2,height/2-90).x-3)<1e-9 && Math.abs(coordinateAt(width/2,height/2-90).y)<1e-9");
        QVERIFY(inverse.evaluate().toBool());
        QQmlExpression zoomAnchor(m_engine.rootContext(), map.data(), "{ var before=coordinateAt(300,200); zoomAt(1.3,300,200); var after=coordinateAt(300,200); Math.abs(before.x-after.x)<1e-9 && Math.abs(before.y-after.y)<1e-9 }");
        QVERIFY(zoomAnchor.evaluate().toBool());
        QVERIFY(!map->property("calibrationAllowed").toBool());
        map.reset();
        m_engine.rootContext()->setContextProperty("groundLink", nullptr);
    }
};
QTEST_MAIN(CoordinateFrameTest)
#include "tst_coordinate_frame.moc"
