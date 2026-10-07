#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include "AsyncTileMapItem.h"
#include "OfflineMapSource.h"
#include "NestPosition.h"
#include "AircraftProfile.h"
#include "GroundLink.h"
#include "GimbalLink.h"

namespace {
void reportError(const QString &message)
{
    std::fprintf(stderr, "%s\n", qPrintable(message));
}

void saveScreenshot(QQuickWindow *window, const QString &path)
{
    if (!window || !window->contentItem()) {
        reportError(QStringLiteral("Screenshot failed: no Quick window."));
        QCoreApplication::exit(4);
        return;
    }

    const QSharedPointer<QQuickItemGrabResult> grab =
        window->contentItem()->grabToImage(QSize(window->width(), window->height()));
    if (grab.isNull()) {
        reportError(QStringLiteral("Screenshot failed: could not request item capture."));
        QCoreApplication::exit(4);
        return;
    }

    QObject::connect(grab.data(), &QQuickItemGrabResult::ready,
                     QCoreApplication::instance(), [grab, path]() {
        const QImage image = grab->image();
        const QFileInfo destination(path);
        const bool saved = !image.isNull()
            && QDir().mkpath(destination.absolutePath())
            && image.save(destination.absoluteFilePath());
        const QFileInfo writtenFile(destination.absoluteFilePath());
        if (!saved || !writtenFile.isFile() || writtenFile.size() == 0) {
            reportError(QStringLiteral("Screenshot failed: %1").arg(path));
            QCoreApplication::exit(4);
            return;
        }
        std::fprintf(stdout, "Screenshot saved: %s (%d x %d)\n",
                     qPrintable(writtenFile.absoluteFilePath()), image.width(), image.height());
        QCoreApplication::exit(0);
    });
    window->update();
}
}

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    // The renderer is selected before creating any Qt Quick windows.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--software") == 0)
            qputenv("QT_QUICK_BACKEND", "software");
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("无人机地面站"));
    app.setApplicationVersion(QStringLiteral("1.4.0"));
    app.setOrganizationName(QStringLiteral("Agri GCS"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Orange Pi 5 Max / Qt Quick ground station visual project"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("page"), QStringLiteral("Initial page: 0 task, 1 logs, 2 settings."), QStringLiteral("index"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("tab"), QStringLiteral("Initial task tab: 0..3."), QStringLiteral("index"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("section"), QStringLiteral("Initial sidebar: logs 0..5, settings 0..7."), QStringLiteral("index"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("size"), QStringLiteral("Window size in logical pixels, e.g. 1536x1024."), QStringLiteral("widthxheight"), QStringLiteral("1536x1024")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Save a screenshot and exit (PNG recommended)."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("screenshot-delay"), QStringLiteral("Delay before capturing the screenshot, in ms (default 1000)."), QStringLiteral("ms"), QStringLiteral("1000")});
    parser.addOption({QStringLiteral("video-diag"), QStringLiteral("Log video pipeline diagnostics every second.")});
    parser.addOption({QStringLiteral("gimbal-test"), QStringLiteral("Run a gimbal control self test and exit.")});
    parser.addOption({QStringLiteral("gimbal-host"), QStringLiteral("Gimbal relay host used by --gimbal-test."), QStringLiteral("host"), QStringLiteral("192.168.2.113")});
    parser.addOption({QStringLiteral("gimbal-port"), QStringLiteral("Gimbal relay port used by --gimbal-test."), QStringLiteral("port"), QStringLiteral("37260")});
    parser.addOption({QStringLiteral("fullscreen"), QStringLiteral("Open full screen.")});
    parser.addOption({QStringLiteral("software"), QStringLiteral("Use the Qt Quick software renderer.")});
    parser.addOption({QStringLiteral("overview"), QStringLiteral("Use the settings overview layout without sidebar.")});
    parser.addOption({QStringLiteral("tiles"), QStringLiteral("Offline z/x/y tile directory (no network fallback)."), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("tile-scheme"), QStringLiteral("Offline tile scheme: xyz or tms."), QStringLiteral("scheme")});
    parser.process(app);

    const QString scheme = parser.value(QStringLiteral("tile-scheme")).toLower();
    if (parser.isSet(QStringLiteral("tile-scheme")) && scheme != "xyz" && scheme != "tms") {
        reportError(QStringLiteral("--tile-scheme must be xyz or tms."));
        return 2;
    }

    bool pageValid = false;
    bool tabValid = false;
    bool sectionValid = false;
    const int previewPage = parser.value(QStringLiteral("page")).toInt(&pageValid);
    const int previewTab = parser.value(QStringLiteral("tab")).toInt(&tabValid);
    const int previewSection = parser.value(QStringLiteral("section")).toInt(&sectionValid);
    const QRegularExpression sizePattern(QStringLiteral("^(\\d{2,5})[xX](\\d{2,5})$"));
    const auto sizeMatch = sizePattern.match(parser.value(QStringLiteral("size")));
    const int width = sizeMatch.captured(1).toInt();
    const int height = sizeMatch.captured(2).toInt();
    const QString screenshotPath = parser.value(QStringLiteral("screenshot"));
    const int screenshotDelay = parser.value(QStringLiteral("screenshot-delay")).toInt();
    if (!pageValid || previewPage < 0 || previewPage > 2
            || !tabValid || previewTab < 0 || previewTab > 3
            || !sectionValid || previewSection < 0 || previewSection > (previewPage == 2 ? 7 : previewPage == 1 ? 5 : 0)
            || !sizeMatch.hasMatch() || width < 320 || height < 240
            || width > 8192 || height > 8192
            || screenshotDelay < 0 || screenshotDelay > 60000
            || (parser.isSet(QStringLiteral("screenshot")) && screenshotPath.isEmpty())) {
        reportError(QStringLiteral("Invalid options: --page=0..2, --tab=0..3, --section=logs 0..5/settings 0..7/task 0, --size=320x240..8192x8192, --screenshot-delay=0..60000; screenshot path cannot be empty."));
        return 2;
    }

    const QStringList installedFonts = QFontDatabase().families();
    const QStringList preferredFonts = {
        QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Source Han Sans SC"),
        QStringLiteral("PingFang SC"), QStringLiteral("Microsoft YaHei"),
        QStringLiteral("WenQuanYi Micro Hei")
    };
    for (const QString &family : preferredFonts) {
        if (installedFonts.contains(family)) {
            QFont font = app.font();
            font.setFamily(family);
            app.setFont(font);
            break;
        }
    }
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    qmlRegisterType<AsyncTileMapItem>("Agri.Map", 1, 0, "AsyncTileMap");
    OfflineMapSource mapSource;
    NestPosition nestPosition(&mapSource);
    AircraftProfile aircraftProfile;
    GroundLink groundLink;
    GimbalLink gimbalLink;
    QSettings settings;
    QString tileDirectory = parser.isSet(QStringLiteral("tiles")) ? parser.value(QStringLiteral("tiles"))
        : qEnvironmentVariable("AGRI_GCS_TILE_DIR", settings.value("OfflineMap/directory").toString());
    bool bundledSource = false;
    if (tileDirectory.isEmpty() && !parser.isSet(QStringLiteral("tiles"))) {
        QStringList candidates;
        // New installs prefer satellite imagery; preserve explicitly saved user sources.
        for (const QString &mapName : {QStringLiteral("maxar-auckland"), QStringLiteral("zjnu-jinhua")}) {
            candidates << QDir::current().filePath("maps/" + mapName)
                << QDir(QCoreApplication::applicationDirPath()).filePath("../maps/" + mapName)
                << QDir(QCoreApplication::applicationDirPath()).filePath("../share/agri_gcs_visual/maps/" + mapName)
                << QDir(QCoreApplication::applicationDirPath()).filePath("../../../../maps/" + mapName);
        }
        for (const QString &candidate : candidates) {
            if (QFileInfo(candidate).isDir()) { tileDirectory = candidate; bundledSource = true; break; }
        }
    }
    const bool tms = parser.isSet(QStringLiteral("tile-scheme")) ? scheme == "tms"
        : bundledSource ? false : settings.value("OfflineMap/tms", false).toBool();
    mapSource.configure(tileDirectory, tms, false);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("offlineMapSource", &mapSource);
    engine.rootContext()->setContextProperty("nestPosition", &nestPosition);
    engine.rootContext()->setContextProperty("aircraftProfile", &aircraftProfile);
    engine.rootContext()->setContextProperty("groundLink", &groundLink);
    engine.rootContext()->setContextProperty("gimbalLink", &gimbalLink);
    engine.setInitialProperties({
        {QStringLiteral("pageIndex"), previewPage},
        {QStringLiteral("previewTab"), previewTab},
        {QStringLiteral("previewSection"), previewSection},
        {QStringLiteral("previewMode"), !screenshotPath.isEmpty()},
        {QStringLiteral("overviewSettings"), parser.isSet(QStringLiteral("overview"))},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("minimumWidth"), qMin(width, 1024)},
        {QStringLiteral("minimumHeight"), qMin(height, 680)}
    });
    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
        reportError(QStringLiteral("Could not load qrc:/qml/Main.qml."));
        return 3;
    }
    QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window)
        return 3;
    if (parser.isSet(QStringLiteral("fullscreen")))
        window->showFullScreen();
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(screenshotDelay, &app, [window, screenshotPath]() {
            saveScreenshot(window, screenshotPath);
        });        QTimer::singleShot(screenshotDelay + 12000, &app, []() {
            reportError(QStringLiteral("Screenshot failed: rendering timed out."));
            QCoreApplication::exit(4);
        });
    }

    // 云台自检：验证"下发指令 → 读回真实角度"的完整回路（现场排障用）
    if (parser.isSet(QStringLiteral("gimbal-test"))) {
        const QString host = parser.value(QStringLiteral("gimbal-host"));
        const int port = parser.value(QStringLiteral("gimbal-port")).toInt();
        auto report = [&gimbalLink](const QString &tag) {
            std::fprintf(stdout,
                         "[gimbal-test] %-20s connected=%d yaw=%.1f pitch=%.1f roll=%.1f "
                         "zoom=%.1f mode=%s record=%s\n",
                         qPrintable(tag), gimbalLink.connected() ? 1 : 0,
                         gimbalLink.yaw(), gimbalLink.pitch(), gimbalLink.roll(),
                         gimbalLink.zoom(), qPrintable(gimbalLink.modeName()),
                         qPrintable(gimbalLink.recordStatusName()));
            std::fflush(stdout);
        };
        QTimer::singleShot(300, &app, [&gimbalLink, host, port]() {
            gimbalLink.connectToGimbal(host, port);
        });
        QTimer::singleShot(2600, &app, [report, &gimbalLink]() {
            report(QStringLiteral("连接后"));
            gimbalLink.setAttitude(gimbalLink.yaw(), gimbalLink.pitch() - 8.0);
            report(QStringLiteral("已下发 pitch-8"));
        });
        QTimer::singleShot(5200, &app, [report, &gimbalLink]() {
            report(QStringLiteral("pitch-8 回读"));
            gimbalLink.setAttitude(gimbalLink.yaw(), gimbalLink.pitch() + 8.0);
            report(QStringLiteral("已下发 pitch+8 复原"));
        });
        QTimer::singleShot(8000, &app, [report, &gimbalLink]() {
            report(QStringLiteral("pitch 复原回读"));
            // 航向角：对外约定「正值 = 向右」，下发 +30
            gimbalLink.setAttitude(30.0, 0.0);
        });
        QTimer::singleShot(10200, &app, [report, &gimbalLink]() {
            report(QStringLiteral("右转30°回读"));
            // 拨杆通道自检：向右推杆 1.2 秒后停止
            gimbalLink.setRotateRate(60, 0);
        });
        QTimer::singleShot(11400, &app, [&gimbalLink]() { gimbalLink.stopRotate(); });
        QTimer::singleShot(12800, &app, [report, &app]() {
            report(QStringLiteral("拨杆右推后"));
            app.exit(0);
        });
    }

    return app.exec();
}
