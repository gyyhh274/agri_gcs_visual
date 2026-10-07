#include "OfflineMapSource.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFutureWatcher>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>

namespace {
constexpr double pi = 3.14159265358979323846;
struct Scan {
    bool valid = false;
    int minimum = 23, maximum = 0;
    int count = 0;
    double left = 1, right = 0, top = 1, bottom = 0;
    QString error;
    QString attribution = QStringLiteral("本地瓦片 · 请核对地图数据许可");
    double centerLatitude = 0, centerLongitude = 0;
};
bool numeric(const QString &name, int *value)
{
    bool ok = false;
    *value = name.toInt(&ok);
    return ok && QString::number(*value) == name;
}
Scan scan(const QString &path, bool tms)
{
    Scan r;
    const QDir root(path);
    if (path.isEmpty() || !root.exists()) {
        r.error = QStringLiteral("地图目录不存在，请选择包含 z/x/y 瓦片的目录");
        return r;
    }
    for (const QString &level : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        int z;
        if (!numeric(level, &z) || z < 0 || z > 22) continue;
        const int n = 1 << z;
        QDir zdir(root.filePath(level));
        for (const QString &column : zdir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            int x;
            if (!numeric(column, &x) || x < 0 || x >= n) continue;
            QDirIterator files(zdir.filePath(column), QDir::Files);
            while (files.hasNext()) {
                files.next();
                const QFileInfo f = files.fileInfo();
                if (!QStringList{"png", "jpg", "jpeg", "webp"}.contains(f.suffix())) continue;
                int y;
                if (!numeric(f.completeBaseName(), &y) || y < 0 || y >= n || f.size() == 0) continue;
                if (tms) y = n - 1 - y;
                r.valid = true;
                ++r.count;
                r.minimum = std::min(r.minimum, z);
                // Use the most detailed coverage; coarse boundary tiles can extend
                // many kilometres beyond a small satellite scene.
                if (z > r.maximum) { r.left = 1; r.right = 0; r.top = 1; r.bottom = 0; }
                r.maximum = std::max(r.maximum, z);
                if (z == r.maximum) {
                    r.left = std::min(r.left, double(x) / n);
                    r.right = std::max(r.right, double(x + 1) / n);
                    r.top = std::min(r.top, double(y) / n);
                    r.bottom = std::max(r.bottom, double(y + 1) / n);
                }
            }
        }
    }
    if (!r.valid) r.error = QStringLiteral("未找到瓦片：需要数字目录 z/x/y.png（或 jpg/jpeg/webp），不是地图截图或 MBTiles");
    else {
        r.centerLongitude = (r.left + r.right) / 2 * 360 - 180;
        r.centerLatitude = std::atan(std::sinh(pi * (1 - r.top - r.bottom))) * 180 / pi;
        QFile metadata(root.filePath("metadata.json"));
        if (metadata.size() < 1024 * 1024 && metadata.open(QIODevice::ReadOnly)) {
            const auto object = QJsonDocument::fromJson(metadata.readAll()).object();
            const auto center = object.value("center").toArray();
            const QString attribution = object.value("attribution").toString();
            if (!attribution.isEmpty() && attribution.size() < 512) r.attribution = attribution;
            if (center.size() == 2 && center[0].isDouble() && center[1].isDouble()
                && std::isfinite(center[0].toDouble()) && std::isfinite(center[1].toDouble())
                && std::abs(center[0].toDouble()) <= 180 && std::abs(center[1].toDouble()) <= 85.05112878) {
                r.centerLongitude = center[0].toDouble(); r.centerLatitude = center[1].toDouble();
            }
        }
    }
    return r;
}
double latitude(double y) { return std::atan(std::sinh(pi * (1 - 2 * y))) * 180 / pi; }
}

OfflineMapSource::OfflineMapSource(QObject *parent) : QObject(parent) {}

void OfflineMapSource::configure(const QString &directory, bool tms, bool persist)
{
    QString path = directory.trimmed();
    if (path.startsWith(QStringLiteral("file:"))) path = QUrl(path).toLocalFile();
    if (path.startsWith(QStringLiteral("~/"))) path = QDir::home().filePath(path.mid(2));
    m_directory = path.isEmpty() ? QString() : QDir(path).absolutePath();
    m_tms = tms;
    m_available = false;
    const int generation = ++m_generation;
    if (persist) {
        QSettings settings;
        settings.setValue(QStringLiteral("OfflineMap/directory"), m_directory);
        settings.setValue(QStringLiteral("OfflineMap/tms"), tms);
    }
    if (path.isEmpty()) {
        m_scanning = false;
        m_status = QStringLiteral("未配置离线地图 · 点击地图目录设置");
        emit changed();
        return;
    }
    m_scanning = true;
    m_status = QStringLiteral("正在后台扫描离线瓦片…");
    emit changed();
    auto *watcher = new QFutureWatcher<Scan>(this);
    connect(watcher, &QFutureWatcher<Scan>::finished, this, [this, watcher, generation]() {
        const Scan r = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation) return;
        m_scanning = false;
        m_available = r.valid;
        m_attribution = r.attribution;
        if (r.valid) {
            m_minZoom = r.minimum;
            m_maxZoom = r.maximum;
            m_west = r.left * 360 - 180;
            m_east = r.right * 360 - 180;
            m_north = latitude(r.top);
            m_south = latitude(r.bottom);
            m_centerLatitude = r.centerLatitude;
            m_centerLongitude = r.centerLongitude;
            m_status = QStringLiteral("离线 %1 · %2 张瓦片 · 级别 %3–%4").arg(m_tms ? "TMS" : "XYZ").arg(r.count).arg(r.minimum).arg(r.maximum);
        } else {
            m_status = r.error;
        }
        emit changed();
    });
    // Worker owns only value copies: closing a page/app cannot leave a dangling QObject.
    watcher->setFuture(QtConcurrent::run(scan, m_directory, tms));
}
