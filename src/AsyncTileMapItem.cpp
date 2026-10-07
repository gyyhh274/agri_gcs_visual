#include "AsyncTileMapItem.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaximumLatitude = 85.05112877980659;

int environmentInt(const char* name, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    return ok ? std::clamp(value, minimum, maximum) : fallback;
}
}

AsyncTileMapItem::AsyncTileMapItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
    setOpaquePainting(true);
    setMipmap(false);
    setRenderTarget(QQuickPaintedItem::Image);

    m_tilePool.setObjectName(QStringLiteral("AgriTileDecodePool"));
    m_tilePool.setMaxThreadCount(environmentInt("AGRI_GCS_TILE_THREADS", 2, 1, 4));
    m_tilePool.setExpiryTimeout(10000);
    m_maxPendingRequests = environmentInt("AGRI_GCS_TILE_PENDING", 10, 2, 24);
    m_maxCacheTiles = environmentInt("AGRI_GCS_TILE_CACHE_COUNT", 96, 16, 512);

    m_scheduleTimer.setSingleShot(true);
    m_scheduleTimer.setInterval(12);
    connect(&m_scheduleTimer, &QTimer::timeout, this, &AsyncTileMapItem::scheduleVisibleTiles);
    connect(this, &QQuickItem::visibleChanged, this, [this]() {
        if (isVisible())
            requestSchedule();
    });
}

AsyncTileMapItem::~AsyncTileMapItem()
{
    ++m_generation;
    m_tilePool.clear();
    m_tilePool.waitForDone(1500);
}

void AsyncTileMapItem::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    m_componentCompleted = true;
    validateDirectory();
    requestSchedule();
}

void AsyncTileMapItem::geometryChanged(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChanged(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        markViewportChanged();
        requestSchedule();
    }
}

void AsyncTileMapItem::setRootDirectory(const QString& directory)
{
    QString normalized = directory.trimmed();
    if (normalized.startsWith(QStringLiteral("~/")))
        normalized = QDir::home().filePath(normalized.mid(2));
    if (!normalized.isEmpty())
        normalized = QDir::cleanPath(normalized);

    if (m_rootDirectory == normalized)
        return;

    m_rootDirectory = normalized;
    ++m_generation;
    m_pendingKeys.clear();
    m_missingKeys.clear();
    clearCache();
    emit rootDirectoryChanged();
    emit loadingChanged();
    validateDirectory();
    requestSchedule();
}

void AsyncTileMapItem::setTmsScheme(bool enabled)
{
    if (m_tmsScheme == enabled)
        return;
    m_tmsScheme = enabled;
    ++m_generation;
    m_pendingKeys.clear();
    m_missingKeys.clear();
    clearCache();
    emit tmsSchemeChanged();
    emit loadingChanged();
    requestSchedule();
}

void AsyncTileMapItem::setCenter(const QGeoCoordinate& coordinate)
{
    if (!coordinate.isValid())
        return;

    const QGeoCoordinate normalized(clampLatitude(coordinate.latitude()),
                                    wrapLongitude(coordinate.longitude()),
                                    coordinate.altitude());
    if (m_center == normalized)
        return;

    m_center = normalized;
    emit centerChanged();
    markViewportChanged();
    requestSchedule();
}

void AsyncTileMapItem::setZoomLevel(qreal zoom)
{
    if (!std::isfinite(zoom))
        return;
    const qreal bounded = std::clamp(zoom, m_minimumZoomLevel, m_maximumZoomLevel);
    if (qFuzzyCompare(m_zoomLevel + 1.0, bounded + 1.0))
        return;

    m_zoomLevel = bounded;
    emit zoomLevelChanged();
    markViewportChanged();
    requestSchedule();
}

void AsyncTileMapItem::setMinimumZoomLevel(qreal zoom)
{
    const qreal normalized = std::clamp(zoom, 0.0, 24.0);
    if (qFuzzyCompare(m_minimumZoomLevel + 1.0, normalized + 1.0))
        return;
    m_minimumZoomLevel = normalized;
    if (m_maximumZoomLevel < m_minimumZoomLevel)
        m_maximumZoomLevel = m_minimumZoomLevel;
    emit zoomRangeChanged();
    setZoomLevel(m_zoomLevel);
}

void AsyncTileMapItem::setMaximumZoomLevel(qreal zoom)
{
    const qreal normalized = std::clamp(zoom, 0.0, 24.0);
    if (qFuzzyCompare(m_maximumZoomLevel + 1.0, normalized + 1.0))
        return;
    m_maximumZoomLevel = normalized;
    if (m_minimumZoomLevel > m_maximumZoomLevel)
        m_minimumZoomLevel = m_maximumZoomLevel;
    emit zoomRangeChanged();
    setZoomLevel(m_zoomLevel);
}

int AsyncTileMapItem::cacheTileCount() const
{
    QMutexLocker locker(&m_cacheMutex);
    return m_cache.size();
}

void AsyncTileMapItem::validateDirectory()
{
    const bool valid = !m_rootDirectory.isEmpty() && QFileInfo(m_rootDirectory).isDir();
    if (m_mapReady != valid) {
        m_mapReady = valid;
        emit mapReadyChanged();
    }

    if (!valid) {
        setError(InvalidDirectory,
                 QStringLiteral("离线地图目录不存在：%1").arg(m_rootDirectory));
        setStatus(QStringLiteral("地图关闭，飞控和其他界面仍可继续使用"));
        update();
        return;
    }

    setError(NoError, {});
    setStatus(QStringLiteral("异步地图就绪 · 读取/解码线程 %1 · 最大并发 %2")
                  .arg(m_tilePool.maxThreadCount())
                  .arg(m_maxPendingRequests));
    update();
}

void AsyncTileMapItem::setError(MapError error, const QString& text)
{
    if (m_error == error && m_errorString == text)
        return;
    m_error = error;
    m_errorString = text;
    emit errorChanged();
}

void AsyncTileMapItem::setStatus(const QString& text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void AsyncTileMapItem::markViewportChanged()
{
    ++m_viewportRevision;
    emit viewportRevisionChanged();
    update();
}

void AsyncTileMapItem::requestSchedule()
{
    if (!m_componentCompleted)
        return;
    m_scheduleTimer.start();
}

QList<AsyncTileMapItem::VisibleTile> AsyncTileMapItem::visibleTiles() const
{
    QList<VisibleTile> tiles;
    if (!m_mapReady || width() <= 1.0 || height() <= 1.0)
        return tiles;

    const int zoom = std::clamp(static_cast<int>(std::floor(m_zoomLevel)),
                                static_cast<int>(std::floor(m_minimumZoomLevel)),
                                static_cast<int>(std::floor(m_maximumZoomLevel)));
    const double scale = std::exp2(static_cast<double>(m_zoomLevel) - zoom);
    const double displayedTileSize = kTileSize * scale;
    const double centerX = longitudeToWorldX(m_center.longitude(), zoom) * scale;
    const double centerY = latitudeToWorldY(m_center.latitude(), zoom) * scale;
    const double left = centerX - width() / 2.0;
    const double top = centerY - height() / 2.0;

    const int firstX = static_cast<int>(std::floor(left / displayedTileSize)) - 1;
    const int lastX = static_cast<int>(std::floor((left + width()) / displayedTileSize)) + 1;
    const int firstY = static_cast<int>(std::floor(top / displayedTileSize)) - 1;
    const int lastY = static_cast<int>(std::floor((top + height()) / displayedTileSize)) + 1;
    const int tileCount = 1 << zoom;
    const double centerTileX = longitudeToWorldX(m_center.longitude(), zoom) / kTileSize;
    const double centerTileY = latitudeToWorldY(m_center.latitude(), zoom) / kTileSize;

    for (int y = firstY; y <= lastY; ++y) {
        if (y < 0 || y >= tileCount)
            continue;
        for (int x = firstX; x <= lastX; ++x) {
            if (x < 0 || x >= tileCount)
                continue;
            VisibleTile tile;
            tile.zoom = zoom;
            tile.x = x;
            tile.y = y;
            tile.key = tileKey(zoom, x, y);
            const double dx = (x + 0.5) - centerTileX;
            const double dy = (y + 0.5) - centerTileY;
            tile.priority = dx * dx + dy * dy;
            tiles.append(tile);
        }
    }

    std::sort(tiles.begin(), tiles.end(), [](const VisibleTile& a, const VisibleTile& b) {
        return a.priority < b.priority;
    });
    return tiles;
}

void AsyncTileMapItem::scheduleVisibleTiles()
{
    if (!m_mapReady || !isVisible())
        return;

    const QList<VisibleTile> tiles = visibleTiles();
    bool loadingWas = loading();
    for (const VisibleTile& tile : tiles) {
        if (m_pendingKeys.size() >= m_maxPendingRequests)
            break;

        QImage cached;
        if (cachedImage(tile.key, &cached) || m_missingKeys.contains(tile.key)
            || m_pendingKeys.contains(tile.key)) {
            continue;
        }
        submitTileRequest(tile);
    }

    if (loadingWas != loading())
        emit loadingChanged();
}

QString AsyncTileMapItem::tileKey(int zoom, int x, int y) const
{
    return QStringLiteral("%1/%2/%3/%4")
        .arg(m_generation)
        .arg(zoom)
        .arg(x)
        .arg(y);
}

QString AsyncTileMapItem::locateTileFile(int zoom, int x, int sourceY) const
{
    const QString base = QDir(m_rootDirectory).filePath(
        QStringLiteral("%1/%2/%3").arg(zoom).arg(x).arg(sourceY));
    static const QStringList extensions = {
        QStringLiteral("png"), QStringLiteral("jpg"),
        QStringLiteral("jpeg"), QStringLiteral("webp")
    };
    for (const QString& extension : extensions) {
        const QString path = base + QLatin1Char('.') + extension;
        const QFileInfo info(path);
        if (info.isFile())
            return info.absoluteFilePath();
    }
    return {};
}

void AsyncTileMapItem::submitTileRequest(const VisibleTile& tile)
{
    const int generation = m_generation;
    const QString rootDirectory = m_rootDirectory;
    const bool tmsScheme = m_tmsScheme;
    const QString key = tile.key;
    const int zoom = tile.zoom;
    const int x = tile.x;
    const int y = tile.y;

    m_pendingKeys.insert(key);
    QPointer<AsyncTileMapItem> guard(this);

    QtConcurrent::run(&m_tilePool, [guard, generation, rootDirectory, tmsScheme, key, zoom, x, y]() {
        const int tileCount = 1 << zoom;
        const int sourceY = tmsScheme ? tileCount - 1 - y : y;
        const QString base = QDir(rootDirectory).filePath(
            QStringLiteral("%1/%2/%3").arg(zoom).arg(x).arg(sourceY));

        static const QStringList extensions = {
            QStringLiteral("png"), QStringLiteral("jpg"),
            QStringLiteral("jpeg"), QStringLiteral("webp")
        };

        QString filePath;
        for (const QString& extension : extensions) {
            const QString candidate = base + QLatin1Char('.') + extension;
            if (QFileInfo(candidate).isFile()) {
                filePath = candidate;
                break;
            }
        }

        QImage image;
        QString errorText;
        if (filePath.isEmpty()) {
            errorText = QStringLiteral("missing");
        } else {
            QImageReader reader(filePath);
            reader.setAutoTransform(true);
            image = reader.read();
            if (image.isNull()) {
                errorText = reader.errorString();
            } else if (image.size() != QSize(kTileSize, kTileSize)) {
                image = image.scaled(kTileSize,
                                     kTileSize,
                                     Qt::IgnoreAspectRatio,
                                     Qt::FastTransformation);
            }
            if (!image.isNull() && image.format() != QImage::Format_ARGB32_Premultiplied)
                image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }

        if (!guard)
            return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, key, generation, image, errorText]() {
                if (guard)
                    guard->completeTileRequest(key, generation, image, errorText);
            },
            Qt::QueuedConnection);
    });
}

void AsyncTileMapItem::completeTileRequest(const QString& key,
                                           int generation,
                                           const QImage& image,
                                           const QString& errorText)
{
    const bool loadingWas = loading();
    m_pendingKeys.remove(key);

    if (generation == m_generation) {
        if (!image.isNull()) {
            insertCache(key, image);
        } else {
            m_missingKeys.insert(key);
            if (errorText != QStringLiteral("missing") && !errorText.isEmpty()) {
                setStatus(QStringLiteral("部分瓦片读取失败：%1；其他功能不受影响")
                              .arg(errorText));
            }
            if (m_missingKeys.size() > 4096)
                m_missingKeys.clear();
        }
    }

    if (loadingWas != loading())
        emit loadingChanged();
    update();

    // Back-pressure: only submit a small bounded batch. When one request
    // completes, schedule the next visible tile without flooding the CPU.
    requestSchedule();
}

void AsyncTileMapItem::insertCache(const QString& key, const QImage& image)
{
    {
        QMutexLocker locker(&m_cacheMutex);
        m_cache.insert(key, image);
        m_lruKeys.removeAll(key);
        m_lruKeys.append(key);
        trimCacheLocked();
    }
    emit cacheChanged();
}

bool AsyncTileMapItem::cachedImage(const QString& key, QImage* image) const
{
    QMutexLocker locker(&m_cacheMutex);
    const auto iterator = m_cache.constFind(key);
    if (iterator == m_cache.constEnd())
        return false;
    if (image)
        *image = iterator.value();
    return true;
}

void AsyncTileMapItem::trimCacheLocked()
{
    while (m_lruKeys.size() > m_maxCacheTiles) {
        const QString oldest = m_lruKeys.takeFirst();
        m_cache.remove(oldest);
    }
}

void AsyncTileMapItem::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->fillRect(boundingRect(), QColor(QStringLiteral("#222e37")));

    if (!m_mapReady || width() <= 1.0 || height() <= 1.0)
        return;

    const int zoom = std::clamp(static_cast<int>(std::floor(m_zoomLevel)),
                                static_cast<int>(std::floor(m_minimumZoomLevel)),
                                static_cast<int>(std::floor(m_maximumZoomLevel)));
    const double scale = std::exp2(static_cast<double>(m_zoomLevel) - zoom);
    const double displayedTileSize = kTileSize * scale;
    const double centerX = longitudeToWorldX(m_center.longitude(), zoom) * scale;
    const double centerY = latitudeToWorldY(m_center.latitude(), zoom) * scale;
    const double left = centerX - width() / 2.0;
    const double top = centerY - height() / 2.0;

    const QList<VisibleTile> tiles = visibleTiles();
    for (const VisibleTile& tile : tiles) {
        const QRectF target(tile.x * displayedTileSize - left,
                            tile.y * displayedTileSize - top,
                            displayedTileSize + 0.5,
                            displayedTileSize + 0.5);
        QImage image;
        if (cachedImage(tile.key, &image)) {
            painter->drawImage(target, image);
        } else {
            painter->fillRect(target, QColor(QStringLiteral("#263640")));
            painter->setPen(QColor(QStringLiteral("#314753")));
            painter->drawRect(target.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }
}

QGeoCoordinate AsyncTileMapItem::toCoordinate(const QPointF& point, bool clipToView) const
{
    QPointF local = point;
    if (clipToView) {
        local.setX(std::clamp(local.x(), 0.0, width()));
        local.setY(std::clamp(local.y(), 0.0, height()));
    }

    const int zoom = std::clamp(static_cast<int>(std::floor(m_zoomLevel)), 0, 24);
    const double scale = std::exp2(static_cast<double>(m_zoomLevel) - zoom);
    const double centerX = longitudeToWorldX(m_center.longitude(), zoom) * scale;
    const double centerY = latitudeToWorldY(m_center.latitude(), zoom) * scale;
    const double worldX = (centerX + local.x() - width() / 2.0) / scale;
    const double worldY = (centerY + local.y() - height() / 2.0) / scale;
    return QGeoCoordinate(worldYToLatitude(worldY, zoom),
                          wrapLongitude(worldXToLongitude(worldX, zoom)));
}

QPointF AsyncTileMapItem::fromCoordinate(const QGeoCoordinate& coordinate, bool clipToView) const
{
    if (!coordinate.isValid())
        return QPointF(-10000.0, -10000.0);

    const int zoom = std::clamp(static_cast<int>(std::floor(m_zoomLevel)), 0, 24);
    const double scale = std::exp2(static_cast<double>(m_zoomLevel) - zoom);
    const double centerX = longitudeToWorldX(m_center.longitude(), zoom) * scale;
    const double centerY = latitudeToWorldY(m_center.latitude(), zoom) * scale;
    QPointF result(longitudeToWorldX(coordinate.longitude(), zoom) * scale - centerX + width() / 2.0,
                   latitudeToWorldY(coordinate.latitude(), zoom) * scale - centerY + height() / 2.0);
    if (clipToView) {
        result.setX(std::clamp(result.x(), 0.0, width()));
        result.setY(std::clamp(result.y(), 0.0, height()));
    }
    return result;
}

void AsyncTileMapItem::panBy(qreal deltaX, qreal deltaY)
{
    if (!m_mapReady)
        return;
    const QGeoCoordinate newCenter = toCoordinate(QPointF(width() / 2.0 - deltaX,
                                                          height() / 2.0 - deltaY),
                                                   false);
    setCenter(newCenter);
}

void AsyncTileMapItem::zoomBy(qreal delta, qreal anchorX, qreal anchorY)
{
    if (!m_mapReady || qFuzzyIsNull(delta))
        return;

    if (anchorX < 0.0 || anchorY < 0.0) {
        anchorX = width() / 2.0;
        anchorY = height() / 2.0;
    }

    const QGeoCoordinate anchorCoordinate = toCoordinate(QPointF(anchorX, anchorY), false);
    const qreal oldZoom = m_zoomLevel;
    setZoomLevel(oldZoom + delta);
    if (qFuzzyCompare(oldZoom + 1.0, m_zoomLevel + 1.0))
        return;

    // Keep the geographic point under the finger/cursor stationary.
    const QPointF movedPoint = fromCoordinate(anchorCoordinate, false);
    panBy(anchorX - movedPoint.x(), anchorY - movedPoint.y());
}

void AsyncTileMapItem::fitBounds(double northLatitude,
                                 double southLatitude,
                                 double westLongitude,
                                 double eastLongitude)
{
    if (width() <= 1.0 || height() <= 1.0)
        return;

    northLatitude = clampLatitude(northLatitude);
    southLatitude = clampLatitude(southLatitude);
    if (northLatitude < southLatitude)
        std::swap(northLatitude, southLatitude);

    const double xWest = (wrapLongitude(westLongitude) + 180.0) / 360.0;
    const double xEast = (wrapLongitude(eastLongitude) + 180.0) / 360.0;
    double spanX = std::abs(xEast - xWest);
    if (spanX > 0.5)
        spanX = 1.0 - spanX;
    spanX = std::max(spanX, 1.0 / std::exp2(m_maximumZoomLevel));

    const auto normalizedY = [](double latitude) {
        const double radians = clampLatitude(latitude) * kPi / 180.0;
        return (1.0 - std::asinh(std::tan(radians)) / kPi) / 2.0;
    };
    const double yNorth = normalizedY(northLatitude);
    const double ySouth = normalizedY(southLatitude);
    const double spanY = std::max(std::abs(ySouth - yNorth),
                                  1.0 / std::exp2(m_maximumZoomLevel));

    const double usableWidth = std::max(64.0, width() - 48.0);
    const double usableHeight = std::max(64.0, height() - 48.0);
    const double zoomX = std::log2(usableWidth / (kTileSize * spanX));
    const double zoomY = std::log2(usableHeight / (kTileSize * spanY));
    const qreal fittedZoom = std::clamp(std::floor(std::min(zoomX, zoomY)),
                                        m_minimumZoomLevel,
                                        m_maximumZoomLevel);

    double centerX = (xWest + xEast) / 2.0;
    if (std::abs(xEast - xWest) > 0.5) {
        centerX = std::fmod((xWest + xEast + 1.0) / 2.0, 1.0);
    }
    const double centerY = (yNorth + ySouth) / 2.0;
    const double longitude = centerX * 360.0 - 180.0;
    const double latitude = std::atan(std::sinh(kPi * (1.0 - 2.0 * centerY))) * 180.0 / kPi;

    setCenter(QGeoCoordinate(latitude, longitude));
    setZoomLevel(fittedZoom);
}

void AsyncTileMapItem::fitCoordinates(const QVariantList& coordinates)
{
    bool found = false;
    double north = -90.0;
    double south = 90.0;
    double west = 180.0;
    double east = -180.0;

    for (const QVariant& value : coordinates) {
        const QGeoCoordinate coordinate = value.value<QGeoCoordinate>();
        if (!coordinate.isValid())
            continue;
        found = true;
        north = std::max(north, coordinate.latitude());
        south = std::min(south, coordinate.latitude());
        west = std::min(west, coordinate.longitude());
        east = std::max(east, coordinate.longitude());
    }

    if (!found)
        return;
    if (qFuzzyCompare(north + 1.0, south + 1.0)) {
        north += 0.0008;
        south -= 0.0008;
    }
    if (qFuzzyCompare(west + 1.0, east + 1.0)) {
        west -= 0.0008;
        east += 0.0008;
    }
    fitBounds(north, south, west, east);
}

void AsyncTileMapItem::reload()
{
    ++m_generation;
    m_pendingKeys.clear();
    m_missingKeys.clear();
    clearCache();
    validateDirectory();
    requestSchedule();
    emit loadingChanged();
}

void AsyncTileMapItem::clearCache()
{
    {
        QMutexLocker locker(&m_cacheMutex);
        m_cache.clear();
        m_lruKeys.clear();
    }
    emit cacheChanged();
    update();
}

double AsyncTileMapItem::longitudeToWorldX(double longitude, int zoom)
{
    const double worldSize = kTileSize * std::exp2(zoom);
    return (wrapLongitude(longitude) + 180.0) / 360.0 * worldSize;
}

double AsyncTileMapItem::latitudeToWorldY(double latitude, int zoom)
{
    const double worldSize = kTileSize * std::exp2(zoom);
    const double radians = clampLatitude(latitude) * kPi / 180.0;
    return (1.0 - std::asinh(std::tan(radians)) / kPi) / 2.0 * worldSize;
}

double AsyncTileMapItem::worldXToLongitude(double worldX, int zoom)
{
    const double worldSize = kTileSize * std::exp2(zoom);
    return worldX / worldSize * 360.0 - 180.0;
}

double AsyncTileMapItem::worldYToLatitude(double worldY, int zoom)
{
    const double worldSize = kTileSize * std::exp2(zoom);
    const double normalized = worldY / worldSize;
    return std::atan(std::sinh(kPi * (1.0 - 2.0 * normalized))) * 180.0 / kPi;
}

double AsyncTileMapItem::clampLatitude(double latitude)
{
    return std::clamp(latitude, -kMaximumLatitude, kMaximumLatitude);
}

double AsyncTileMapItem::wrapLongitude(double longitude)
{
    double wrapped = std::fmod(longitude + 180.0, 360.0);
    if (wrapped < 0.0)
        wrapped += 360.0;
    return wrapped - 180.0;
}
