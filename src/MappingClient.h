#pragma once

#include "MappingTypes.h"
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <memory>

struct MappingVoxelKey {
    qint32 x, y, z;
    bool operator==(const MappingVoxelKey &o) const { return x == o.x && y == o.y && z == o.z; }
};
inline uint qHash(const MappingVoxelKey &key, uint seed = 0) noexcept {
    // Nearby integer voxels need avalanche mixing. XORing shifted Qt integer
    // hashes puts a room-sized dense cloud into only a few hundred buckets.
    const auto mix = [](quint64 value) {
        value += Q_UINT64_C(0x9e3779b97f4a7c15);
        value = (value ^ (value >> 30)) * Q_UINT64_C(0xbf58476d1ce4e5b9);
        value = (value ^ (value >> 27)) * Q_UINT64_C(0x94d049bb133111eb);
        return value ^ (value >> 31);
    };
    const quint64 xy = quint64(quint32(key.x)) | (quint64(quint32(key.y)) << 32);
    const quint64 hash = mix(mix(xy) ^ quint64(quint32(key.z)) ^ (quint64(seed) << 32));
    return uint(hash ^ (hash >> 32));
}

class MappingClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY changed)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString frameId READ frameId NOTIFY changed)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY changed)
    Q_PROPERTY(int trajectoryCount READ trajectoryCount NOTIFY changed)
    Q_PROPERTY(bool hasPose READ hasPose NOTIFY changed)
    Q_PROPERTY(float positionX READ positionX NOTIFY changed)
    Q_PROPERTY(float positionY READ positionY NOTIFY changed)
    Q_PROPERTY(float positionZ READ positionZ NOTIFY changed)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)
    Q_PROPERTY(bool stale READ stale NOTIFY changed)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY changed)
    Q_PROPERTY(QString defaultExportPath READ defaultExportPath NOTIFY changed)
    Q_PROPERTY(QString lastExportPath READ lastExportPath NOTIFY changed)
public:
    explicit MappingClient(QObject *parent = nullptr);
    ~MappingClient() override;
    bool connected() const { return m_connected; }
    bool enabled() const { return m_enabled; }
    QString host() const { return m_host; }
    int port() const { return m_port; }
    QString status() const { return m_status; }
    QString frameId() const { return m_frameId; }
    int pointCount() const { return m_points.size(); }
    int trajectoryCount() const { return m_trajectory.size(); }
    bool hasPose() const { return m_poseFresh; }
    float positionX() const { return m_position.x(); }
    float positionY() const { return m_position.y(); }
    float positionZ() const { return m_position.z(); }
    bool demo() const { return m_demo; }
    bool stale() const { return m_stale; }
    bool paused() const { return m_paused; }
    QString defaultExportPath() const;
    QString lastExportPath() const { return m_lastExportPath; }
    void setEnabled(bool enabled);
    void setHost(const QString &host);
    void setPort(int port);
    void setPaused(bool paused);
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void clearMap();
    Q_INVOKABLE void savePcd(const QUrl &url = QUrl());
    Q_INVOKABLE void startDemo();
    Q_INVOKABLE void stopDemo();
    std::shared_ptr<const MappingSnapshot> snapshot() const { return m_snapshot; }

signals:
    void changed();
    void snapshotChanged();
    void exportFinished(bool success, const QString &message);

private:
    void connectSocket();
    void readSocket();
    void processInput();
    void protocolError(const QString &message);
    bool acceptHeader(const QJsonObject &header);
    bool handleFrame(const QJsonObject &header, const QByteArray &payload);
    void processCloud();
    void insertPoint(const MapPoint &point);
    void resetData();
    void publishSnapshot();
    void checkFreshness();
    void demoStep();
    void setStatus(const QString &status);
    static MappingVoxelKey voxelKey(const MapPoint &point);

    QTcpSocket m_socket;
    QTimer m_reconnectTimer, m_snapshotTimer, m_staleTimer, m_processTimer, m_demoTimer;
    QElapsedTimer m_clock;
    QString m_host, m_status, m_session, m_frameId, m_lastExportPath;
    int m_port = 37262;
    int m_maxPoints = 1500000;
    bool m_connected = false, m_enabled = false, m_demo = false, m_stale = true;
    bool m_paused = false, m_dirty = true, m_poseFresh = false, m_exporting = false;
    bool m_haveSequence = false;
    quint64 m_lastSequence = 0, m_revision = 0;
    qint64 m_lastDataMs = -1, m_lastPoseMs = -1, m_discardThroughBytes = 0;
    QByteArray m_input;
    QJsonObject m_cloudHeader;
    QByteArray m_cloudPayload;
    QVector<MapPoint> m_cloudPoints;
    int m_cloudOffset = 0, m_cloudApplyOffset = -1;
    QVector<MapPoint> m_points;
    QHash<MappingVoxelKey, int> m_voxels;
    int m_nextEviction = 0;
    QVector<QVector3D> m_trajectory;
    QVector3D m_position;
    QQuaternion m_orientation;
    std::shared_ptr<const MappingSnapshot> m_snapshot;
    QVector<MapPoint> m_demoScene;
    int m_demoTick = 0, m_demoCursor = 0;
};
