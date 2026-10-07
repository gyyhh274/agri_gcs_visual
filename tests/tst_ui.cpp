#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QLineF>
#include <cmath>
#include <QQuickWindow>
#include <QQuickItemGrabResult>
#include <QWheelEvent>
#include <QTouchDevice>
#include "../src/AsyncTileMapItem.h"
#include "../src/OfflineMapSource.h"
#include "../src/NestPosition.h"
#include "../src/AircraftProfile.h"
#include "../src/GroundLink.h"

class UiSmoke : public QObject
{
    Q_OBJECT
    OfflineMapSource m_source;
    NestPosition m_nest{&m_source};
    AircraftProfile m_aircraft;
    GroundLink m_groundLink;

    QObject *load(QQmlApplicationEngine &engine, int page, int tab = 0, int section = 0)
    {
        engine.rootContext()->setContextProperty("offlineMapSource", &m_source);
        engine.rootContext()->setContextProperty("nestPosition", &m_nest);
        engine.rootContext()->setContextProperty("aircraftProfile", &m_aircraft);
        engine.rootContext()->setContextProperty("groundLink", &m_groundLink);
        engine.setInitialProperties({{"pageIndex", page}, {"previewTab", tab},
                                     {"previewSection", section}, {"previewMode", true}});
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        return engine.rootObjects().isEmpty() ? nullptr : engine.rootObjects().first();
    }

    static QVariant evaluate(QObject *object, const QString &script)
    {
        QQmlExpression expression(QQmlEngine::contextForObject(object), object, script);
        QVariant result = expression.evaluate();
        if (expression.hasError())
            qWarning() << expression.error();
        return result;
    }

private slots:
    void init() {
        QSettings settings;
        settings.remove("MissionDemo");
        settings.remove("OfflineMap");
        settings.remove("GroundStationUi");
        settings.sync();
        m_source.configure("", false, false);
        m_nest.resetDemo();
        m_aircraft.reload();
    }
    void pages_data()
    {
        QTest::addColumn<int>("page");
        QTest::addColumn<int>("tab");
        QTest::addColumn<int>("section");
        for (int tab = 0; tab < 4; ++tab)
            QTest::newRow(qPrintable(QString("task-%1").arg(tab))) << 0 << tab << 0;
        for (int section = 0; section < 6; ++section)
            QTest::newRow(qPrintable(QString("logs-%1").arg(section))) << 1 << 0 << section;
        for (int section = 0; section < 8; ++section)
            QTest::newRow(qPrintable(QString("settings-%1").arg(section))) << 2 << 0 << section;
    }

    void pages()
    {
        QFETCH(int, page);
        QFETCH(int, tab);
        QFETCH(int, section);
        QQmlApplicationEngine engine;
        QObject *root = load(engine, page, tab, section);
        QVERIFY(root);
        const QString name = page == 0 ? "taskPage" : page == 1 ? "logPage" : "settingsPage";
        QObject *content = root->findChild<QObject *>(name);
        QVERIFY(content);
        QVERIFY(content->property("width").toDouble() > 0);
        QCOMPARE(content->property(page == 0 ? "contentTab" : "sectionIndex").toInt(),
                 page == 0 ? tab : section);
        QCoreApplication::processEvents();
    }

    void routeValidation()
    {
        QQmlApplicationEngine engine;
        QObject *root = load(engine, 0);
        QVERIFY(root);
        QObject *task = root->findChild<QObject *>("taskPage");
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 0);
        evaluate(task, "addWaypoint(29.13678,119.63764)");
        evaluate(task, "importRoute('[{\"lat\":999,\"lon\":121}]')");
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 1);
        evaluate(task, "importRoute('[{\"lat\":31.2,\"lon\":121.4,\"alt\":80}]')");
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 1);
        QCOMPARE(evaluate(task, "JSON.parse(routeJson())[0].alt").toString(), QString("80"));
    }

    void taskTabMapStable_data()
    {
        QTest::addColumn<QSize>("windowSize");
        QTest::newRow("1536x1024") << QSize(1536,1024);
        QTest::newRow("1280x800") << QSize(1280,800);
        QTest::newRow("1920x1080") << QSize(1920,1080);
    }

    void taskTabMapStable()
    {
        QFETCH(QSize, windowSize);
        const QString tiles = qEnvironmentVariable("AGRI_GCS_TEST_TILES");
        if (tiles.isEmpty()) QSKIP("Pass AGRI_GCS_TEST_TILES for tab map stability");
        m_source.configure(tiles,false,false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_source.scanning(),5000);
        QVERIFY(m_source.available());
        QQmlApplicationEngine engine;
        auto *window = qobject_cast<QQuickWindow *>(load(engine,0)); QVERIFY(window);
        window->resize(windowSize); QTest::qWait(200);
        auto *task = window->findChild<QObject *>("taskPage");
        auto *panel = window->findChild<QQuickItem *>("taskMapPanel");
        auto *map = window->findChild<AsyncTileMapItem *>("offlineTileMap");
        auto *tabs = window->findChild<QQuickItem *>("taskContentTabs");
        auto *data = window->findChild<QQuickItem *>("taskDataPanel");
        auto *marker = window->findChild<QQuickItem *>("nestMapMarker");
        QVERIFY(task && panel && map && tabs && data && marker);
        map->zoomBy(1.25); map->panBy(35,-22);
        QTest::qWait(200); QTRY_VERIFY_WITH_TIMEOUT(!map->loading(),5000);
        evaluate(task,QString("addWaypoint(%1,%2)").arg(m_source.centerLatitude(),0,'f',7).arg(m_source.centerLongitude(),0,'f',7));
        const QString route = evaluate(task,"routeJson()").toString();
        const QRectF initialGeometry = panel->mapRectToScene(panel->boundingRect());
        const QRectF initialDataGeometry = data->mapRectToScene(data->boundingRect());
        const QPointF initialTabs = tabs->mapToScene(QPointF());
        const QPointF initialMarker = marker->mapToScene(QPointF(19,19));
        const QGeoCoordinate center = map->center();
        const qreal zoom = map->zoomLevel();
        const QGeoCoordinate topLeft = map->toCoordinate(QPointF(0,0));
        const QGeoCoordinate bottomRight = map->toCoordinate(QPointF(map->width(),map->height()));
        QSignalSpy centerChanged(map,SIGNAL(centerChanged()));
        QSignalSpy zoomChanged(map,SIGNAL(zoomLevelChanged()));
        for (int index : {1,2,3,0,3,2,1,0}) {
            // Repeater delegates are visual children, not necessarily QObject descendants.
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,tabs->mapToScene(QPointF(index*120+60,tabs->height()/2)).toPoint());
            QTest::qWait(30);
            QCOMPARE(task->property("contentTab").toInt(),index);
            if (index == 2) {
                auto *photos = window->findChild<QQuickItem *>("taskPhotoScroll"); QVERIFY(photos);
                QVERIFY(photos->height() > 0);
                QVERIFY(photos->property("contentHeight").toDouble() > photos->height());
                QVERIFY(photos->property("clip").toBool());
                photos->setProperty("contentY",20);
            }
            QCOMPARE(window->findChild<AsyncTileMapItem *>("offlineTileMap"),map);
            QCOMPARE(panel->mapRectToScene(panel->boundingRect()),initialGeometry);
            QCOMPARE(data->mapRectToScene(data->boundingRect()),initialDataGeometry);
            QCOMPARE(tabs->mapToScene(QPointF()),initialTabs);
            QCOMPARE(map->center(),center); QCOMPARE(map->zoomLevel(),zoom);
            QVERIFY(topLeft.distanceTo(map->toCoordinate(QPointF(0,0))) < 0.001);
            QVERIFY(bottomRight.distanceTo(map->toCoordinate(QPointF(map->width(),map->height()))) < 0.001);
            QCOMPARE(marker->mapToScene(QPointF(19,19)),initialMarker);
            QCOMPARE(evaluate(task,"routeJson()").toString(),route);
        }
        QCOMPARE(centerChanged.count(),0); QCOMPARE(zoomChanged.count(),0);
    }

    void routeEditing()
    {
        QQmlApplicationEngine engine;
        QObject *root = load(engine, 0);
        QVERIFY(root);
        QObject *task = root->findChild<QObject *>("taskPage");
        QVERIFY(evaluate(task, "addWaypoint(29.13678,119.63764)").toBool());
        QVERIFY(evaluate(task, "addWaypoint(29.137,119.638)").toBool());
        QVERIFY(!evaluate(task, "addWaypoint(91,119)").toBool());
        QVERIFY(evaluate(task, "moveWaypoint(0,29.138,119.639)").toBool());
        QCOMPARE(evaluate(task, "JSON.parse(routeJson())[0].lat").toString(), QString("29.1380000"));
        evaluate(task, "deleteWaypoint(0)");
        QCOMPARE(evaluate(task, "JSON.parse(routeJson())[0].no").toString(), QString("1"));
        QCOMPARE(task->property("selectedWaypoint").toInt(), 0);
        evaluate(task, "saveRoute()");
        engine.clearComponentCache();
        QQmlApplicationEngine reloaded;
        root = load(reloaded, 0);
        QVERIFY(root);
        task = root->findChild<QObject *>("taskPage");
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 1);
        evaluate(task, "clearRoute(); saveRoute()");
        QQmlApplicationEngine empty;
        root = load(empty, 0);
        QVERIFY(root);
        QCOMPARE(evaluate(root->findChild<QObject *>("taskPage"), "JSON.parse(routeJson()).length").toInt(), 0);
    }

    void offlineMap()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        // Real PNG fixture in WGS84 XYZ coordinates; never presented as campus data.
        QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor("#238a61"));
        for (int z : {15,16}) {
            const int x = int((119.63764 + 180) / 360 * (1 << z));
            const double radians = 29.13678 * 3.141592653589793 / 180;
            const int y = int((1 - std::asinh(std::tan(radians)) / 3.141592653589793) / 2 * (1 << z));
            QString folder = directory.path() + QString("/%1/%2").arg(z).arg(x);
            QVERIFY(QDir().mkpath(folder));
            QVERIFY(image.save(folder + QString("/%1.png").arg(y)));
        }
        m_source.configure(directory.path(), false, false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_source.scanning(), 5000);
        QVERIFY(m_source.available());
        QCOMPARE(m_source.minimumZoom(), 15);
        QCOMPARE(m_source.maximumZoom(), 16);
        QVERIFY(m_source.south() < 29.13678 && m_source.north() > 29.13678);
        QQmlApplicationEngine engine;
        QObject *root = load(engine, 0);
        QVERIFY(root);
        auto *map = root->findChild<AsyncTileMapItem *>("offlineTileMap");
        QVERIFY(map);
        QTRY_VERIFY(map->mapReady());
        QTRY_VERIFY_WITH_TIMEOUT(map->cacheTileCount() > 0, 5000);
        map->setCenter(QGeoCoordinate(29.13678,119.63764));
        map->setZoomLevel(15);
        const QPointF point(300,200);
        QVERIFY(QLineF(point, map->fromCoordinate(map->toCoordinate(point))).length() < 0.00001);
        const QGeoCoordinate anchor = map->toCoordinate(point);
        map->zoomBy(1, point.x(), point.y());
        QCOMPARE(map->zoomLevel(), qreal(16));
        QVERIFY(anchor.distanceTo(map->toCoordinate(point)) < 0.01);
        const QGeoCoordinate before = map->center();
        map->panBy(40,20);
        QVERIFY(before.distanceTo(map->center()) > 1);
        QObject *panel = root->findChild<QObject *>("taskMapPanel");
        QVERIFY(panel);
        QVERIFY(evaluate(panel, "addAt(400,220)").toBool());
        QObject *task = root->findChild<QObject *>("taskPage");
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 1);
        QVERIFY(evaluate(panel, "hitWaypoint(400,220)").toInt() == 0);
        evaluate(task, "moveWaypoint(0,29.13678,119.63764)");
        QVERIFY(evaluate(panel, "pointFor(29.13678,119.63764).x").toDouble() > 0);
        m_source.configure(directory.path() + "/absent", false, false);
        QTRY_VERIFY(!m_source.scanning());
        QVERIFY(!m_source.available());
        QVERIFY(!evaluate(panel, "addAt(400,220)").toBool());
    }

    void bundledMouseInteraction()
    {
        const QString tiles = qEnvironmentVariable("AGRI_GCS_TEST_TILES");
        if (tiles.isEmpty()) QSKIP("Pass AGRI_GCS_TEST_TILES to verify real bundled tiles and mouse events");
        m_source.configure(tiles, false, false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_source.scanning(), 5000);
        QVERIFY(m_source.available());
        QQmlApplicationEngine engine;
        auto *window = qobject_cast<QQuickWindow *>(load(engine, 0));
        QVERIFY(window);
        QTest::qWait(250);
        QObject *task = window->findChild<QObject *>("taskPage");
        auto *panel = window->findChild<QQuickItem *>("taskMapPanel");
        auto *map = window->findChild<AsyncTileMapItem *>("offlineTileMap");
        QVERIFY(task && panel && map);
        const double centerLat = m_source.centerLatitude();
        map->setCenter(QGeoCoordinate(centerLat,m_source.centerLongitude()));
        map->setZoomLevel(16);
        QTRY_VERIFY_WITH_TIMEOUT(map->cacheTileCount() > 0, 5000);
        panel->setProperty("editMode", 1);
        auto scene = [panel](int x, int y) { return panel->mapToScene(QPointF(x,y)).toPoint(); };
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, scene(450,225));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, scene(490,250));
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 2);
        const double latitude = evaluate(task, "Number(JSON.parse(routeJson())[0].lat)").toDouble();
        QVERIFY(std::abs(latitude - centerLat) < 0.0000002);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(450,225));
        QTest::mouseMove(window, scene(480,190), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scene(480,190));
        QVERIFY(evaluate(task, "Number(JSON.parse(routeJson())[0].lat)").toDouble() > latitude);
        QCOMPARE(task->property("selectedWaypoint").toInt(), 0);
        const QGeoCoordinate before = map->center();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scene(650,300));
        QTest::mouseMove(window, scene(600,280), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, scene(600,280));
        QVERIFY(before.distanceTo(map->center()) > 1);
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 2);
        const QPoint wheelPosition = scene(405,225);
        const QGeoCoordinate anchored = map->toCoordinate(QPointF(405,225));
        QWheelEvent wheel(wheelPosition, window->mapToGlobal(wheelPosition), QPoint(), QPoint(0,120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        QCOMPARE(map->zoomLevel(), qreal(16.5));
        QVERIFY(anchored.distanceTo(map->toCoordinate(QPointF(405,225))) < 0.01);
        QTRY_VERIFY_WITH_TIMEOUT(!map->loading(), 5000);
        const QString screenshot = qEnvironmentVariable("AGRI_GCS_TEST_PREVIEW");
        if (!screenshot.isEmpty()) {
            auto grab = window->contentItem()->grabToImage();
            QVERIFY(grab);
            QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
            QVERIFY(ready.wait(5000));
            QVERIFY(grab->image().save(screenshot));
        }
        // Synthetic two-finger events exercise the real QML PinchArea, not just zoomBy().
        auto *device = QTest::createTouchDevice(QTouchDevice::TouchScreen);
        const qreal beforePinch = map->zoomLevel();
        QTest::touchEvent(window, device).press(0, scene(200,320)).press(1, scene(320,320));
        QTest::qWait(20);
        QTest::touchEvent(window, device).move(0, scene(190,320)).move(1, scene(330,320));
        QTest::qWait(20);
        QTest::touchEvent(window, device).move(0, scene(160,320)).move(1, scene(360,320));
        QTest::qWait(20);
        QTest::touchEvent(window, device).release(0, scene(160,320)).release(1, scene(360,320));
        QVERIFY(map->zoomLevel() > beforePinch + 0.1);
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 2);
        const QString route = evaluate(task, "routeJson()").toString();
        evaluate(task, "saveRoute()");
        QQmlApplicationEngine reloaded;
        QObject *restored = load(reloaded, 0);
        QVERIFY(restored);
        QCOMPARE(evaluate(restored->findChild<QObject *>("taskPage"), "routeJson()").toString(), route);
    }

    void satelliteSource()
    {
        const QString tiles = qEnvironmentVariable("AGRI_GCS_TEST_SATELLITE");
        if (tiles.isEmpty()) QSKIP("Pass AGRI_GCS_TEST_SATELLITE to verify the satellite map");
        m_source.configure(tiles, false, false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_source.scanning(), 5000);
        QVERIFY(m_source.available());
        QCOMPARE(m_source.minimumZoom(), 13);
        QCOMPARE(m_source.maximumZoom(), 18);
        QVERIFY(m_source.attribution().contains("Maxar"));
        QVERIFY(m_source.attribution().contains("CC BY-NC"));
        QVERIFY(m_source.centerLatitude() < -36.7 && m_source.centerLatitude() > -36.8);
        QVERIFY(m_source.centerLongitude() > 174.6 && m_source.centerLongitude() < 174.8);
        QQmlApplicationEngine engine;
        QObject *root = load(engine, 0);
        QVERIFY(root);
        auto *map = root->findChild<AsyncTileMapItem *>("offlineTileMap");
        QVERIFY(map);
        QTest::qWait(200);
        map->setCenter(QGeoCoordinate(m_source.centerLatitude(),m_source.centerLongitude()));
        map->setZoomLevel(18);
        QTRY_VERIFY_WITH_TIMEOUT(!map->loading() && map->cacheTileCount() > 0, 5000);
        auto centerLoaded = [map]() {
            QImage painted(int(map->width()), int(map->height()), QImage::Format_ARGB32_Premultiplied);
            QPainter painter(&painted); map->paint(&painter); painter.end();
            const QColor center = painted.pixelColor(painted.width()/2,painted.height()/2);
            return center != QColor("#263640") && center != QColor("#222e37");
        };
        // Zoom changes schedule async requests on the next event-loop turn;
        // old-level cache entries alone are not evidence that level 18 has loaded.
        QTRY_VERIFY_WITH_TIMEOUT(centerLoaded(), 5000);
        map->zoomBy(10); QCOMPARE(map->zoomLevel(), qreal(18));
        map->zoomBy(-20); QCOMPARE(map->zoomLevel(), qreal(13));
    }

    void nestPersistence()
    {
        QVERIFY(!m_nest.configured());
        QVERIFY(m_nest.name().contains("示例"));
        QVERIFY(!m_nest.configure("", "174.6", "机巢"));
        QVERIFY(!m_nest.configure("NaN", "174.6", "机巢"));
        QVERIFY(!m_nest.configure("86", "174.6", "机巢"));
        QVERIFY(!m_nest.configure("-36.73", "181", "机巢"));
        QVERIFY(m_nest.configure(" -36.7305527 ", "174.6762603", "测试机巢"));
        NestPosition restored(&m_source);
        QVERIFY(restored.configured());
        QCOMPARE(restored.latitude(), m_nest.latitude());
        QCOMPARE(restored.longitude(), m_nest.longitude());
        QCOMPARE(restored.name(), QString("测试机巢"));
        QVERIFY(!m_nest.configure("garbage", "", "bad"));
        QCOMPARE(m_nest.name(), QString("测试机巢"));
        m_source.configure("", false, false);
        QCOMPARE(m_nest.latitude(), qreal(-36.7305527));
        QCOMPARE(m_nest.longitude(), qreal(174.6762603));
    }

    void nestMapInteraction()
    {
        const QString tiles = qEnvironmentVariable("AGRI_GCS_TEST_SATELLITE");
        if (tiles.isEmpty()) QSKIP("Pass AGRI_GCS_TEST_SATELLITE for nest interaction");
        m_source.configure(tiles, false, false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_source.scanning(), 5000);
        QQmlApplicationEngine engine;
        auto *window = qobject_cast<QQuickWindow *>(load(engine, 0));
        QVERIFY(window);
        QTest::qWait(200);
        auto *map = window->findChild<AsyncTileMapItem *>("offlineTileMap");
        auto *marker = window->findChild<QQuickItem *>("nestMapMarker");
        auto *panel = window->findChild<QQuickItem *>("taskMapPanel");
        auto *task = window->findChild<QObject *>("taskPage");
        QVERIFY(map && marker && panel && task);
        QTRY_VERIFY(marker->isVisible());
        auto aligned = [this,map,marker]() {
            const QPointF expected = map->fromCoordinate(QGeoCoordinate(m_nest.latitude(),m_nest.longitude()));
            return QLineF(expected,QPointF(marker->x()+19,marker->y()+19)).length() < 0.0001;
        };
        QVERIFY(aligned());
        panel->setProperty("editMode", 1);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, marker->mapToScene(QPointF(19,19)).toPoint());
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 0);
        QVERIFY(panel->property("nestDetailsVisible").toBool());
        map->panBy(40,20); QVERIFY(aligned());
        map->zoomBy(1); QVERIFY(aligned());
        // Setting a nest requires confirmation, and never appends a route point.
        QVERIFY(evaluate(panel, "proposeNest(520,300)").toBool());
        QVERIFY(!m_nest.configured());
        auto *dialog = window->findChild<QObject *>("nestPositionDialog");
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QVERIFY(!m_nest.configured());
        panel->setProperty("editMode", 2);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,panel->mapToScene(QPointF(520,300)).toPoint());
        QVERIFY(dialog->property("visible").toBool());
        auto *field = window->findChild<QObject *>("nestLatitudeField");
        QVERIFY(field);
        const double latitude = field->property("text").toString().toDouble();
        const double longitude = window->findChild<QObject *>("nestLongitudeField")->property("text").toString().toDouble();
        auto *saveButton = window->findChild<QObject *>("nestSaveButton");
        QVERIFY(saveButton);
        field->setProperty("text", "999");
        QVERIFY(QMetaObject::invokeMethod(saveButton,"clicked"));
        QVERIFY(!m_nest.configured()); QVERIFY(dialog->property("visible").toBool());
        field->setProperty("text", QString::number(latitude,'f',7));
        QVERIFY(QMetaObject::invokeMethod(saveButton,"clicked"));
        QVERIFY(m_nest.configured());
        QCOMPARE(m_nest.latitude(), latitude); QCOMPARE(m_nest.longitude(), longitude);
        QCOMPARE(evaluate(task, "JSON.parse(routeJson()).length").toInt(), 0);
        QVERIFY(aligned());
        evaluate(panel,"locateNest()");
        QVERIFY(map->center().distanceTo(QGeoCoordinate(latitude,longitude)) < 0.01);
        evaluate(task,"clearRoute()"); QVERIFY(m_nest.configured());
        const QString preview = qEnvironmentVariable("AGRI_GCS_TEST_NEST_PREVIEW");
        if (!preview.isEmpty()) {
            QTest::qWait(250);
            auto grab = window->contentItem()->grabToImage(); QVERIFY(grab);
            QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready); QVERIFY(ready.wait(5000));
            QVERIFY(grab->image().save(preview));
        }
        m_source.configure("",false,false); QCOMPARE(m_nest.latitude(),latitude);
        QTRY_VERIFY(!marker->isVisible());
    }

    void nestSettings()
    {
        QQmlApplicationEngine engine;
        auto *root = load(engine, 2, 0, 2); QVERIFY(root);
        auto *settings = root->findChild<QObject *>("settingsPage"); QVERIFY(settings);
        const QString tiles = qEnvironmentVariable("AGRI_GCS_TEST_SATELLITE");
        if (!tiles.isEmpty()) {
            m_source.configure(tiles,false,false);
            QTRY_VERIFY(!m_source.scanning());
            QTRY_COMPARE(evaluate(settings,"values.nestLatitude").toString(),QString::number(m_nest.latitude(),'f',7));
        }
        evaluate(settings,"setValue('nestLatitude','-36.73'); setValue('nestLongitude','174.67'); setValue('nestName','基地机巢'); save()");
        QVERIFY(m_nest.configured()); QCOMPARE(m_nest.name(),QString("基地机巢"));
        QCOMPARE(m_nest.latitude(),qreal(-36.73));
        evaluate(settings,"setValue('nestLatitude','999'); save()");
        QCOMPARE(m_nest.latitude(),qreal(-36.73));
        QQmlApplicationEngine reloaded;
        auto *other = load(reloaded,2,0,2); QVERIFY(other);
        QCOMPARE(evaluate(other->findChild<QObject *>("settingsPage"),"values.nestLatitude").toString(),QString("-36.7300000"));
    }

    void tileSourceValidation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        m_source.configure(directory.path(), false, false);
        QTRY_VERIFY(!m_source.scanning());
        QVERIFY(!m_source.available());
        QVERIFY(m_source.status().contains("未找到瓦片"));
        const int z = 16;
        const int x = int((119.63764 + 180) / 360 * (1 << z));
        const int y = int((1 - std::asinh(std::tan(29.13678 * 3.141592653589793 / 180)) / 3.141592653589793) / 2 * (1 << z));
        const QString folder = directory.path() + QString("/%1/%2").arg(z).arg(x);
        QVERIFY(QDir().mkpath(folder));
        QImage tile(256,256,QImage::Format_ARGB32_Premultiplied);
        tile.fill(QColor("#238a61"));
        QVERIFY(tile.save(folder + QString("/%1.png").arg((1 << z) - 1 - y)));
        QVERIFY(QDir().mkpath(directory.path() + "/0/0"));
        QVERIFY(tile.save(directory.path() + "/0/0/0.png"));
        m_source.configure(directory.path(), true, false);
        QTRY_VERIFY(!m_source.scanning());
        QVERIFY(m_source.available());
        QVERIFY(m_source.south() < 29.13678 && m_source.north() > 29.13678);
        // Coarse global coverage must not make "fit coverage" zoom away from the scene.
        QVERIFY(m_source.east() - m_source.west() < 0.01);
        QQmlApplicationEngine engine;
        auto *root = load(engine,0);
        QVERIFY(root);
        auto *map = root->findChild<AsyncTileMapItem *>("offlineTileMap");
        QVERIFY(map);
        QTRY_VERIFY_WITH_TIMEOUT(map->cacheTileCount() > 0, 5000);
        QImage painted(int(map->width()), int(map->height()), QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&painted); map->paint(&painter); painter.end();
        QCOMPARE(painted.pixelColor(painted.width()/2, painted.height()/2), QColor("#238a61"));
        // Switching directories invalidates cached images and late scan results.
        m_source.configure(directory.path(), false, false);
        m_source.configure("", false, false);
        QTest::qWait(100);
        QVERIFY(!m_source.available());
        QVERIFY(m_source.directory().isEmpty());
    }

    void logFilters()
    {
        QQmlApplicationEngine engine;
        QObject *root = load(engine, 1);
        QVERIFY(root);
        QObject *logs = root->findChild<QObject *>("logPage");
        QCOMPARE(evaluate(logs, "filteredEvents.length").toInt(), 24);
        logs->setProperty("keyword", QString("拍照完成"));
        evaluate(logs, "applyFilters()");
        QCOMPARE(evaluate(logs, "filteredEvents.length").toInt(), 6);
        logs->setProperty("deviceFilter", QString("机巢"));
        evaluate(logs, "applyFilters()");
        QCOMPARE(evaluate(logs, "filteredEvents.length").toInt(), 0);
        QCOMPARE(logs->property("selectedEvent").toInt(), -1);
        logs->setProperty("keyword", QString());
        evaluate(logs, "applyFilters()");
        QCOMPARE(evaluate(logs, "filteredEvents.length").toInt(), 5);
    }

    void settingsValues()
    {
        QQmlApplicationEngine engine;
        QObject *root = load(engine, 2);
        QVERIFY(root);
        QObject *settings = root->findChild<QObject *>("settingsPage");
        evaluate(settings, "setValue('droneIp', '192.168.50.10')");
        settings->setProperty("sectionIndex", 1);
        QCOMPARE(evaluate(settings, "values.droneIp").toString(), QString("192.168.50.10"));
        settings->setProperty("sectionIndex", 0);
        QCOMPARE(evaluate(settings, "values.droneIp").toString(), QString("192.168.50.10"));
        evaluate(settings, "save()");
        QCOMPARE(evaluate(settings, "JSON.parse(savedConfiguration).droneIp").toString(),
                 QString("192.168.50.10"));
    }

    void legacyDemoEndpointIsNotUsedForGateway()
    {
        QSettings persisted;
        persisted.setValue("GroundStationUi/configuration",
                           QStringLiteral("{\"droneIp\":\"192.168.1.10\",\"dronePort\":\"14550\",\"droneAuto\":true,\"aircraftModel\":\"ZJNU_AIR\"}"));
        persisted.sync();
        QQmlApplicationEngine engine;
        QObject *root = load(engine, 2);
        QVERIFY(root);
        QObject *settings = root->findChild<QObject *>("settingsPage");
        QVERIFY(settings);
        QCOMPARE(evaluate(settings, "values.droneIp").toString(), QStringLiteral("127.0.0.1"));
        QCOMPARE(evaluate(settings, "values.dronePort").toString(), QStringLiteral("8765"));
        QCOMPARE(evaluate(settings, "values.aircraftModel").toString(), QStringLiteral("ZJNU_AIR"));
        QVERIFY(!m_groundLink.connected());
    }

    void aircraftProfileSync()
    {
        QQmlApplicationEngine engine;
        QObject *root = load(engine, 2, 0, 1);
        QVERIFY(root);
        QObject *settings = root->findChild<QObject *>("settingsPage");
        QVERIFY(settings);
        QCOMPARE(m_aircraft.model(), QString("DJI M30T"));
        evaluate(settings, "setValue('aircraftName', '测试无人机 01'); setValue('aircraftModel', 'ZJNU_AIR'); setValue('autopilot', 'ArduPilot')");
        // Edited fields are drafts until the user presses Save.
        QCOMPARE(m_aircraft.model(), QString("DJI M30T"));
        evaluate(settings, "save()");
        QCOMPARE(m_aircraft.name(), QString("测试无人机 01"));
        QCOMPARE(m_aircraft.model(), QString("ZJNU_AIR"));
        QCOMPARE(m_aircraft.autopilot(), QString("ArduPilot"));
        QCOMPARE(evaluate(settings, "JSON.parse(savedConfiguration).aircraftModel").toString(),
                 QString("ZJNU_AIR"));
        const QString serialized = evaluate(settings, "savedConfiguration").toString();

        root->setProperty("pageIndex", 0);
        QTRY_VERIFY(root->findChild<QObject *>("taskPage"));
        auto *name = root->findChild<QObject *>("taskAircraftName");
        auto *model = root->findChild<QObject *>("taskAircraftModel");
        auto *autopilot = root->findChild<QObject *>("taskAutopilot");
        QVERIFY(name && model && autopilot);
        QCOMPARE(name->property("text").toString(), QString("测试无人机 01"));
        QCOMPARE(model->property("text").toString(), QString("型号        ZJNU_AIR"));
        QCOMPARE(autopilot->property("text").toString(), QString("飞控        ArduPilot"));

        // A new process reads the saved JSON from QSettings at startup.
        QSettings persisted;
        persisted.setValue("GroundStationUi/configuration", serialized);
        persisted.sync();
        AircraftProfile restored;
        QCOMPARE(restored.name(), m_aircraft.name());
        QCOMPARE(restored.model(), m_aircraft.model());
        QCOMPARE(restored.autopilot(), m_aircraft.autopilot());
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Agri GCS UI Tests");
    app.setApplicationName("UiSmoke");
    QQuickStyle::setStyle("Fusion");
    qmlRegisterType<AsyncTileMapItem>("Agri.Map", 1, 0, "AsyncTileMap");
    UiSmoke test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_ui.moc"
