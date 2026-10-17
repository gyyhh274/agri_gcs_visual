#include "MappingClient.h"
#include <QDateTime>
#include <QDir>
#include <QFutureWatcher>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace {
constexpr int MaxPoints = 1500000;
constexpr int MaxHeader = 16384;
constexpr int MaxPayload = 32 * 1024 * 1024;
constexpr int MaxBuffer = MaxPayload + MaxHeader + 12;
constexpr int CloudBudget = 8000;
constexpr float VoxelSize = 0.025f;
float readFloat(const char *p) {
    quint32 bits = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
    float value; std::memcpy(&value, &bits, sizeof(value)); return value;
}
bool finiteNumber(const QJsonValue &v, double limit = 1e6) {
    return v.isDouble() && std::isfinite(v.toDouble()) && std::abs(v.toDouble()) <= limit;
}
}

MappingClient::MappingClient(QObject *parent) : QObject(parent) {
    bool validLimit = false;
    const int configuredLimit = qEnvironmentVariableIntValue("AGRI_GCS_MAP_MAX_POINTS", &validLimit);
    m_maxPoints = validLimit ? qBound(1000, configuredLimit, MaxPoints) : MaxPoints;
    QSettings settings;
    m_host = settings.value("RealtimeMapping/host", "127.0.0.1").toString();
    m_port = settings.value("RealtimeMapping/port", 37262).toInt();
    if (m_port < 1 || m_port > 65535) m_port = 37262;
    m_status = QStringLiteral("实时建图未连接");
    m_clock.start();
    m_socket.setReadBufferSize(MaxBuffer);
    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(2000);
    m_processTimer.setSingleShot(true);
    m_snapshotTimer.setInterval(100);
    m_staleTimer.setInterval(250);
    m_demoTimer.setInterval(100);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &MappingClient::connectSocket);
    connect(&m_processTimer, &QTimer::timeout, this, &MappingClient::processInput);
    connect(&m_snapshotTimer, &QTimer::timeout, this, &MappingClient::publishSnapshot);
    connect(&m_staleTimer, &QTimer::timeout, this, &MappingClient::checkFreshness);
    connect(&m_demoTimer, &QTimer::timeout, this, &MappingClient::demoStep);
    connect(&m_socket, &QTcpSocket::readyRead, this, &MappingClient::readSocket);
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_connected = true;
        setStatus(QStringLiteral("已连接，等待建图数据"));
        emit changed();
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        m_connected = false;
        m_stale = true;
        m_poseFresh = false;
        m_dirty = true;
        m_input.clear();
        m_cloudPayload.clear();
        m_cloudPoints.clear();
        m_cloudApplyOffset = -1;
        if (m_enabled && !m_demoTimer.isActive()) {
            if (!m_status.startsWith(QStringLiteral("协议错误")))
                setStatus(QStringLiteral("连接断开，正在重试"));
            m_reconnectTimer.start();
        }
        emit changed();
    });
    connect(&m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this,
            [this](QAbstractSocket::SocketError) {
        if (m_enabled && !m_demoTimer.isActive()) {
            m_connected = false;
            m_stale = true;
            setStatus(QStringLiteral("建图服务不可达：%1").arg(m_socket.errorString()));
            m_reconnectTimer.start();
            emit changed();
        }
    });
    m_snapshotTimer.start();
    m_staleTimer.start();
    publishSnapshot();
}

MappingClient::~MappingClient() {
    // QTcpSocket can emit disconnected from its destructor, after later members
    // have already died. Disconnect callbacks while all state is still alive.
    QObject::disconnect(&m_socket, nullptr, this, nullptr);
    m_reconnectTimer.stop(); m_snapshotTimer.stop(); m_staleTimer.stop();
    m_processTimer.stop(); m_demoTimer.stop();
    m_socket.abort();
}

void MappingClient::setStatus(const QString &value) {
    if (m_status == value) return;
    m_status = value;
    emit changed();
}
void MappingClient::setEnabled(bool value) {
    if (m_enabled == value && !m_demoTimer.isActive()) return;
    if (m_demoTimer.isActive()) stopDemo();
    m_enabled = value;
    if (value) connectSocket();
    else {
        m_reconnectTimer.stop();
        m_socket.abort();
        m_connected = false;
        m_poseFresh = false;
        m_stale = true;
        m_dirty = true;
        setStatus(QStringLiteral("实时建图已断开"));
    }
    emit changed();
}
void MappingClient::setHost(const QString &value) {
    const QString host = value.trimmed();
    if (host.isEmpty() || host.size() > 253 || m_host == host) return;
    m_host = host;
    QSettings().setValue("RealtimeMapping/host", m_host);
    emit changed();
    if (m_enabled && !m_demoTimer.isActive()) reconnect();
}
void MappingClient::setPort(int value) {
    if (value < 1 || value > 65535 || value == m_port) return;
    m_port = value;
    QSettings().setValue("RealtimeMapping/port", m_port);
    emit changed();
    if (m_enabled && !m_demoTimer.isActive()) reconnect();
}
void MappingClient::setPaused(bool value) {
    if (m_paused == value) return;
    if (!value) m_discardThroughBytes = m_input.size() + m_socket.bytesAvailable();
    m_paused = value;
    if (value) {
        m_cloudPayload.clear();
        m_cloudPoints.clear();
        m_cloudApplyOffset = -1;
    }
    emit changed();
}
void MappingClient::reconnect() {
    if (m_demoTimer.isActive()) stopDemo();
    m_enabled = true;
    m_reconnectTimer.stop();
    m_socket.abort();
    connectSocket();
    emit changed();
}
void MappingClient::connectSocket() {
    if (!m_enabled || m_demoTimer.isActive()) return;
    m_reconnectTimer.stop();
    m_socket.abort();
    m_reconnectTimer.stop();
    m_input.clear();
    m_cloudPayload.clear();
    m_cloudPoints.clear();
    m_cloudApplyOffset = -1;
    m_haveSequence = false;
    m_discardThroughBytes = 0;
    setStatus(QStringLiteral("正在连接 %1:%2").arg(m_host).arg(m_port));
    m_socket.connectToHost(m_host, quint16(m_port));
}
void MappingClient::resetData() {
    m_poseStamp = 0;
    m_points.clear(); m_voxels.clear(); m_trajectory.clear();
    m_nextEviction = 0;
    m_poseFresh = false; m_stale = true;
    m_lastDataMs = -1; m_lastPoseMs = -1;
    m_position = QVector3D(); m_orientation = QQuaternion();
    m_dirty = true;
}
void MappingClient::clearMap() {
    m_discardThroughBytes = m_input.size() + m_socket.bytesAvailable();
    m_cloudPayload.clear(); m_cloudPoints.clear(); m_cloudApplyOffset = -1;
    resetData();
    publishSnapshot();
    emit changed();
}
MappingVoxelKey MappingClient::voxelKey(const MapPoint &p) {
    return {qint32(std::floor(p.x / VoxelSize)), qint32(std::floor(p.y / VoxelSize)), qint32(std::floor(p.z / VoxelSize))};
}
void MappingClient::insertPoint(const MapPoint &point) {
    const auto key = voxelKey(point);
    const auto it = m_voxels.constFind(key);
    if (it != m_voxels.cend()) { m_points[it.value()] = point; return; }
    int index;
    if (m_points.size() < m_maxPoints) {
        index = m_points.size(); m_points.append(point);
    } else {
        index = m_nextEviction;
        m_voxels.remove(voxelKey(m_points.at(index)));
        m_points[index] = point;
        m_nextEviction = (m_nextEviction + 1) % m_maxPoints;
    }
    m_voxels.insert(key, index);
}
void MappingClient::readSocket() {
    const qint64 space = MaxBuffer - m_input.size();
    if (space > 0) m_input.append(m_socket.read(qMin<qint64>(space, 512 * 1024)));
    if (!m_processTimer.isActive()) m_processTimer.start(0);
}
void MappingClient::protocolError(const QString &message) {
    setStatus(QStringLiteral("协议错误：%1").arg(message));
    m_socket.abort();
    m_connected = false; m_stale = true; m_poseFresh = false; m_dirty = true;
    m_input.clear(); m_cloudPayload.clear(); m_cloudPoints.clear(); m_cloudApplyOffset = -1;
    if (m_enabled) m_reconnectTimer.start();
    emit changed();
}
void MappingClient::processInput() {
    if (!m_cloudPayload.isEmpty() || m_cloudApplyOffset >= 0) {
        processCloud();
        if (!m_cloudPayload.isEmpty() || m_cloudApplyOffset >= 0) { m_processTimer.start(0); return; }
    }
    QElapsedTimer budget; budget.start();
    int frames = 0;
    while (m_input.size() >= 12 && frames++ < 32 && budget.elapsed() < 4) {
        if (m_input.left(4) != "AGM1") { protocolError(QStringLiteral("无效帧标记")); return; }
        const quint32 headerLength = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(m_input.constData() + 4));
        const quint32 payloadLength = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(m_input.constData() + 8));
        if (!headerLength || headerLength > MaxHeader || payloadLength > MaxPayload) {
            protocolError(QStringLiteral("帧长度超限")); return;
        }
        const int total = 12 + int(headerLength) + int(payloadLength);
        if (m_input.size() < total) break;
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(m_input.mid(12, int(headerLength)), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) { protocolError(QStringLiteral("无效 JSON")); return; }
        const QByteArray payload = m_input.mid(12 + int(headerLength), int(payloadLength));
        const QJsonObject header = doc.object();
        const bool discard = m_discardThroughBytes > 0;
        m_discardThroughBytes = qMax<qint64>(0, m_discardThroughBytes - total);
        m_input.remove(0, total);
        if (discard && (header.value("type") == "cloud" || header.value("type") == "pose")) {
            if (!acceptHeader(header)) return;
        } else if (!handleFrame(header, payload)) return;
        if (!m_cloudPayload.isEmpty()) { m_processTimer.start(0); return; }
    }
    if (m_socket.bytesAvailable() > 0 && m_input.size() < MaxBuffer) {
        readSocket();
    } else if (m_input.size() >= 12) {
        // Only reschedule a complete frame; incomplete TCP fragments await readyRead.
        const quint32 h = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(m_input.constData() + 4));
        const quint32 p = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(m_input.constData() + 8));
        if (h > MaxHeader || p > MaxPayload || quint64(m_input.size()) >= quint64(12) + h + p)
            m_processTimer.start(0);
    }
}

bool MappingClient::acceptHeader(const QJsonObject &header) {
    const QString type = header.value("type").toString();
    const QString session = header.value("session").toString();
    const QString frame = header.value("frame").toString();
    const auto sequenceValue = header.value("seq");
    const double sequence = sequenceValue.toDouble(-1);
    if (header.value("protocol").toInt(-1) != 1 || session.isEmpty() || session.size() > 128 ||
        frame.isEmpty() || frame.size() > 128 || !sequenceValue.isDouble() || !std::isfinite(sequence) ||
        sequence < 0 || sequence > 9007199254740991.0 || std::floor(sequence) != sequence ||
        !finiteNumber(header.value("stamp"), 1e12) ||
        (type != "hello" && type != "status" && type != "reset" && type != "cloud" && type != "pose")) {
        protocolError(QStringLiteral("帧字段不完整或无效")); return false;
    }
    const bool originChanged = session != m_session || frame != m_frameId;
    if (originChanged) {
        resetData();
        m_session = session; m_frameId = frame;
        m_haveSequence = false;
    }
    const quint64 seq = quint64(sequence);
    if (m_haveSequence && seq <= m_lastSequence) {
        protocolError(QStringLiteral("帧序号未递增")); return false;
    }
    m_haveSequence = true; m_lastSequence = seq;
    if (header.contains("demo")) m_demo = header.value("demo").toBool(false);
    emit changed();
    return true;
}

bool MappingClient::handleFrame(const QJsonObject &header, const QByteArray &payload) {
    if (!acceptHeader(header)) return false;
    const QString type = header.value("type").toString();
    if (type != "cloud" && !payload.isEmpty()) {
        protocolError(QStringLiteral("控制帧包含未知负载")); return false;
    }
    if (type == "reset") { resetData(); emit changed(); return true; }
    if (type == "hello" || type == "status") {
        const QString message = header.value("message").toString().left(512);
        const QString state = header.value("state").toString();
        if (state == "waiting_ros" || state == "waiting_cloud" || state == "stale") {
            m_lastDataMs = -1; m_stale = true;
            if (state == "waiting_ros") { m_poseFresh = false; m_lastPoseMs = -1; m_dirty = true; }
            emit changed();
        }
        if (!message.isEmpty()) setStatus(message);
        return true;
    }
    if (type == "pose") {
        const auto p = header.value("position").toArray();
        const auto q = header.value("orientation").toArray();
        if (p.size() != 3 || q.size() != 4) { protocolError(QStringLiteral("无效位姿维度")); return false; }
        for (const auto &value : p) if (!finiteNumber(value)) { protocolError(QStringLiteral("无效位置数值")); return false; }
        for (const auto &value : q) if (!finiteNumber(value, 2.0)) { protocolError(QStringLiteral("无效姿态数值")); return false; }
        QQuaternion orientation(float(q.at(3).toDouble()), float(q.at(0).toDouble()), float(q.at(1).toDouble()), float(q.at(2).toDouble()));
        if (orientation.lengthSquared() < 1e-8f) { protocolError(QStringLiteral("零长度四元数")); return false; }
        if (m_paused) return true;
        m_position = QVector3D(float(p.at(0).toDouble()), float(p.at(1).toDouble()), float(p.at(2).toDouble()));
        m_orientation = orientation.normalized();
        m_poseStamp = header.value("stamp").toDouble();
        m_lastPoseMs = m_clock.elapsed();
        m_poseFresh = true;
        if (m_trajectory.isEmpty() || (m_trajectory.last() - m_position).lengthSquared() >= 0.01f) {
            if (m_trajectory.size() >= 10000) m_trajectory.remove(0, 1000);
            m_trajectory.append(m_position);
        }
        m_dirty = true;
        return true;
    }
    const auto countValue = header.value("point_count");
    const double count = countValue.toDouble(-1);
    if (!countValue.isDouble() || count < 0 || count > MaxPoints || std::floor(count) != count ||
        payload.size() != qint64(count) * 16 ||
        (header.contains("snapshot") && !header.value("snapshot").isBool()) || (header.contains("stale") && !header.value("stale").isBool())) {
        protocolError(QStringLiteral("点云数量与负载不一致")); return false;
    }
    if (m_paused) return true;
    if (payload.isEmpty()) {
        if (header.value("snapshot").toBool()) { m_points.clear(); m_voxels.clear(); m_nextEviction = 0; m_dirty = true; }
        return true;
    }
    m_cloudHeader = header;
    m_cloudPayload = payload;
    m_cloudPoints.clear();
    m_cloudPoints.reserve(int(count));
    m_cloudOffset = 0;
    m_cloudApplyOffset = -1;
    return true;
}

void MappingClient::processCloud() {
    if (m_paused) { m_cloudPayload.clear(); m_cloudPoints.clear(); m_cloudApplyOffset = -1; return; }
    if (m_cloudApplyOffset < 0) {
        const int end = qMin(m_cloudPayload.size(), m_cloudOffset + CloudBudget * 16);
        for (; m_cloudOffset < end; m_cloudOffset += 16) {
            const char *p = m_cloudPayload.constData() + m_cloudOffset;
            const MapPoint point{readFloat(p), readFloat(p + 4), readFloat(p + 8), readFloat(p + 12)};
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
                !std::isfinite(point.intensity) || std::abs(point.x) > 1e6f || std::abs(point.y) > 1e6f || std::abs(point.z) > 1e6f) {
                protocolError(QStringLiteral("点云包含非有限或越界数值")); return;
            }
            m_cloudPoints.append(point);
        }
        if (m_cloudOffset < m_cloudPayload.size()) return;
        // Validation completes before any points from this frame are committed.
        m_cloudPayload.clear();
        if (m_cloudHeader.value("snapshot").toBool()) { m_points.clear(); m_voxels.clear(); m_nextEviction = 0; }
        m_cloudApplyOffset = 0;
    }
    const int end = qMin(m_cloudPoints.size(), m_cloudApplyOffset + CloudBudget);
    for (; m_cloudApplyOffset < end; ++m_cloudApplyOffset) insertPoint(m_cloudPoints.at(m_cloudApplyOffset));
    m_dirty = true;
    if (m_cloudApplyOffset < m_cloudPoints.size()) return;
    m_cloudPoints.clear(); m_cloudApplyOffset = -1;
    const bool cachedStale = m_cloudHeader.value("stale").toBool(false);
    m_lastDataMs = cachedStale ? -1 : m_clock.elapsed(); m_stale = cachedStale;
    if (cachedStale) {
        setStatus(QStringLiteral("已载入历史点云，等待机载更新"));
        emit changed();
        return;
    }
    setStatus(m_demo ? QStringLiteral("模拟演示数据（非机载实测）") : QStringLiteral("实时点云接收中"));
    emit changed();
}

void MappingClient::publishSnapshot() {
    if (!m_dirty) return;
    auto copy = std::make_shared<MappingSnapshot>();
    copy->points = m_points; copy->trajectory = m_trajectory;
    copy->position = m_position; copy->orientation = m_orientation;
    copy->hasPose = m_poseFresh; copy->revision = ++m_revision;
    m_snapshot = std::move(copy);
    m_dirty = false;
    emit snapshotChanged();
    emit changed();
}
void MappingClient::checkFreshness() {
    const auto now = m_clock.elapsed();
    const bool stale = m_lastDataMs < 0 || now - m_lastDataMs > 3000 || (!m_connected && !m_demoTimer.isActive());
    const bool poseFresh = m_lastPoseMs >= 0 && now - m_lastPoseMs <= 3000 && (m_connected || m_demoTimer.isActive());
    if (stale != m_stale) {
        m_stale = stale;
        if (stale && m_lastDataMs >= 0 && !m_paused)
            setStatus(QStringLiteral("点云超过 3 秒未更新，保留最后地图"));
        emit changed();
    }
    if (poseFresh != m_poseFresh) { m_poseFresh = poseFresh; m_dirty = true; emit changed(); }
}

QString MappingClient::defaultExportPath() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty()) base = QDir::homePath();
    return QDir(base).filePath("AgriGCS/maps/agri-map-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".pcd");
}
void MappingClient::savePcd(const QUrl &url) {
    if (m_exporting) { emit exportFinished(false, QStringLiteral("地图正在导出")); return; }
    publishSnapshot();
    const auto copy = m_snapshot;
    if (!copy || copy->points.isEmpty()) { emit exportFinished(false, QStringLiteral("没有可导出的点云")); return; }
    QString path = url.isEmpty() ? defaultExportPath() : (url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!url.isEmpty() && !url.isLocalFile() && !url.scheme().isEmpty()) { emit exportFinished(false, QStringLiteral("仅支持本地 PCD 文件")); return; }
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) { emit exportFinished(false, QStringLiteral("请输入本地绝对路径")); return; }
    m_exporting = true;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, path] {
        const QString error = watcher->result();
        m_exporting = false;
        if (error.isEmpty()) { m_lastExportPath = path; emit changed(); }
        emit exportFinished(error.isEmpty(), error.isEmpty() ? path : error);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([copy, path] {
        const QFileInfo info(path);
        if (!QDir().mkpath(info.absolutePath())) return QStringLiteral("无法创建导出目录");
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return file.errorString();
        const auto count = QByteArray::number(copy->points.size());
        const QByteArray header = "# .PCD v0.7 - Point Cloud Data file format\nVERSION 0.7\nFIELDS x y z intensity\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\nWIDTH " + count + "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " + count + "\nDATA binary\n";
        if (file.write(header) != header.size()) return file.errorString();
        QByteArray bytes; bytes.resize(copy->points.size() * 16);
        int offset = 0;
        for (const auto &point : copy->points) {
            const float values[] = {point.x, point.y, point.z, point.intensity};
            for (float value : values) {
                quint32 bits; std::memcpy(&bits, &value, sizeof(bits));
                qToLittleEndian<quint32>(bits, reinterpret_cast<uchar *>(bytes.data() + offset));
                offset += 4;
            }
        }
        if (file.write(bytes) != bytes.size()) return file.errorString();
        if (!file.commit()) return file.errorString();
        return QString();
    }));
}

void MappingClient::startDemo() {
    m_enabled = false; m_reconnectTimer.stop(); m_socket.abort(); m_reconnectTimer.stop();
    m_input.clear(); m_cloudPayload.clear(); m_cloudPoints.clear(); m_cloudApplyOffset = -1;
    m_demo = true; m_connected = false; m_paused = false;
    m_session = "local-synthetic-demo"; m_frameId = "demo_world";
    resetData(); m_demoTick = 0; m_demoCursor = 0;
    m_demoScene.clear();
    constexpr double Pi = 3.14159265358979323846;
    for (int row = -3; row <= 3; ++row) for (int col = -4; col <= 4; ++col) {
        const float cx = col * 5.0f, cy = row * 6.0f;
        for (int h = 0; h < 28; ++h) for (int angle = 0; angle < 16; ++angle) {
            const double a = angle * 2 * Pi / 16;
            m_demoScene.append({cx + float(0.26 * std::cos(a)), cy + float(0.26 * std::sin(a)), h * 0.16f, 80.0f});
        }
        for (int angle = 0; angle < 48; ++angle) for (int level = 0; level < 8; ++level) {
            const double a = angle * 2 * Pi / 48;
            const double radius = 1.8 * std::sin((level + 1) * Pi / 9);
            m_demoScene.append({cx + float(radius * std::cos(a)), cy + float(radius * std::sin(a)), 3.3f + level * 0.28f, 130.0f});
        }
    }
    for (int x = -75; x <= 75; ++x) for (int y = -70; y <= 70; ++y)
        m_demoScene.append({x * 0.35f, y * 0.35f, float(0.07 * std::sin(x * 0.1) * std::cos(y * 0.13)), 25.0f});
    setStatus(QStringLiteral("模拟演示数据（非机载实测）"));
    m_demoTimer.start(); demoStep(); publishSnapshot(); emit changed();
}
void MappingClient::stopDemo() {
    if (!m_demoTimer.isActive()) return;
    m_demoTimer.stop(); m_demoScene.clear();
    m_stale = true; m_poseFresh = false; m_dirty = true;
    setStatus(QStringLiteral("演示已停止，保留演示地图"));
    emit changed();
}
void MappingClient::demoStep() {
    if (m_paused || m_demoScene.isEmpty()) return;
    ++m_demoTick;
    const double t = m_demoTick * 0.025;
    m_position = QVector3D(float(14 * std::cos(t)), float(12 * std::sin(t)), 2.0f);
    m_orientation = QQuaternion::fromAxisAndAngle(0, 0, 1, float(t * 180 / 3.14159265358979323846 + 90));
    m_poseFresh = true; m_stale = false;
    m_lastDataMs = m_lastPoseMs = m_clock.elapsed();
    if (m_trajectory.size() >= 10000) m_trajectory.remove(0, 1000);
    m_trajectory.append(m_position);
    for (int i = 0; i < 14000; ++i) {
        const auto &point = m_demoScene.at(m_demoCursor);
        const float dx = point.x - m_position.x(), dy = point.y - m_position.y();
        if (dx * dx + dy * dy < 360.0f) insertPoint(point);
        m_demoCursor = (m_demoCursor + 1) % m_demoScene.size();
    }
    m_dirty = true;
}
