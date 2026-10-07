#pragma once

#include <QGeoCoordinate>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickPaintedItem>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>

class AsyncTileMapItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString rootDirectory READ rootDirectory WRITE setRootDirectory NOTIFY rootDirectoryChanged)
    Q_PROPERTY(bool tmsScheme READ tmsScheme WRITE setTmsScheme NOTIFY tmsSchemeChanged)
    Q_PROPERTY(QGeoCoordinate center READ center WRITE setCenter NOTIFY centerChanged)
    Q_PROPERTY(qreal zoomLevel READ zoomLevel WRITE setZoomLevel NOTIFY zoomLevelChanged)
    Q_PROPERTY(qreal minimumZoomLevel READ minimumZoomLevel WRITE setMinimumZoomLevel NOTIFY zoomRangeChanged)
    Q_PROPERTY(qreal maximumZoomLevel READ maximumZoomLevel WRITE setMaximumZoomLevel NOTIFY zoomRangeChanged)
    Q_PROPERTY(bool mapReady READ mapReady NOTIFY mapReadyChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int pendingRequests READ pendingRequests NOTIFY loadingChanged)
    Q_PROPERTY(int error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int viewportRevision READ viewportRevision NOTIFY viewportRevisionChanged)
    Q_PROPERTY(int cacheTileCount READ cacheTileCount NOTIFY cacheChanged)

public:
    enum MapError {
        NoError = 0,
        InvalidDirectory = 1,
        TileReadError = 2
    };
    Q_ENUM(MapError)

    explicit AsyncTileMapItem(QQuickItem* parent = nullptr);
    ~AsyncTileMapItem() override;

    QString rootDirectory() const { return m_rootDirectory; }
    void setRootDirectory(const QString& directory);

    bool tmsScheme() const { return m_tmsScheme; }
    void setTmsScheme(bool enabled);

    QGeoCoordinate center() const { return m_center; }
    void setCenter(const QGeoCoordinate& coordinate);

    qreal zoomLevel() const { return m_zoomLevel; }
    void setZoomLevel(qreal zoom);

    qreal minimumZoomLevel() const { return m_minimumZoomLevel; }
    void setMinimumZoomLevel(qreal zoom);

    qreal maximumZoomLevel() const { return m_maximumZoomLevel; }
    void setMaximumZoomLevel(qreal zoom);

    bool mapReady() const { return m_mapReady; }
    bool loading() const { return !m_pendingKeys.isEmpty(); }
    int pendingRequests() const { return m_pendingKeys.size(); }
    int error() const { return static_cast<int>(m_error); }
    QString errorString() const { return m_errorString; }
    QString statusText() const { return m_statusText; }
    int viewportRevision() const { return m_viewportRevision; }
    int cacheTileCount() const;

    Q_INVOKABLE QGeoCoordinate toCoordinate(const QPointF& point, bool clipToView = false) const;
    Q_INVOKABLE QPointF fromCoordinate(const QGeoCoordinate& coordinate, bool clipToView = false) const;
    Q_INVOKABLE void panBy(qreal deltaX, qreal deltaY);
    Q_INVOKABLE void zoomBy(qreal delta, qreal anchorX = -1.0, qreal anchorY = -1.0);
    Q_INVOKABLE void fitBounds(double northLatitude,
                               double southLatitude,
                               double westLongitude,
                               double eastLongitude);
    Q_INVOKABLE void fitCoordinates(const QVariantList& coordinates);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void clearCache();

    void paint(QPainter* painter) override;

signals:
    void rootDirectoryChanged();
    void tmsSchemeChanged();
    void centerChanged();
    void zoomLevelChanged();
    void zoomRangeChanged();
    void mapReadyChanged();
    void loadingChanged();
    void errorChanged();
    void statusTextChanged();
    void viewportRevisionChanged();
    void cacheChanged();

protected:
    void geometryChanged(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void componentComplete() override;

private:
    struct VisibleTile {
        int zoom = 0;
        int x = 0;
        int y = 0;
        QString key;
        qreal priority = 0.0;
    };

    static constexpr int kTileSize = 256;

    void validateDirectory();
    void setError(MapError error, const QString& text);
    void setStatus(const QString& text);
    void markViewportChanged();
    void requestSchedule();
    void scheduleVisibleTiles();
    QList<VisibleTile> visibleTiles() const;
    QString tileKey(int zoom, int x, int y) const;
    QString locateTileFile(int zoom, int x, int sourceY) const;
    void submitTileRequest(const VisibleTile& tile);
    void completeTileRequest(const QString& key, int generation, const QImage& image, const QString& errorText);
    void insertCache(const QString& key, const QImage& image);
    bool cachedImage(const QString& key, QImage* image) const;
    void trimCacheLocked();

    static double longitudeToWorldX(double longitude, int zoom);
    static double latitudeToWorldY(double latitude, int zoom);
    static double worldXToLongitude(double worldX, int zoom);
    static double worldYToLatitude(double worldY, int zoom);
    static double clampLatitude(double latitude);
    static double wrapLongitude(double longitude);

    QString m_rootDirectory;
    bool m_tmsScheme = false;
    QGeoCoordinate m_center{0.0, 0.0};
    qreal m_zoomLevel = 15.0;
    qreal m_minimumZoomLevel = 0.0;
    qreal m_maximumZoomLevel = 19.0;
    bool m_mapReady = false;
    MapError m_error = NoError;
    QString m_errorString;
    QString m_statusText{QStringLiteral("等待地图目录")};
    int m_viewportRevision = 0;
    int m_generation = 1;
    bool m_componentCompleted = false;

    QThreadPool m_tilePool;
    QTimer m_scheduleTimer;
    QSet<QString> m_pendingKeys;
    QSet<QString> m_missingKeys;
    int m_maxPendingRequests = 10;

    mutable QMutex m_cacheMutex;
    QHash<QString, QImage> m_cache;
    QStringList m_lruKeys;
    int m_maxCacheTiles = 96;
};
