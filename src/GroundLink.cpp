#include "GroundLink.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QtGlobal>
#include <cmath>

namespace {
constexpr int kMaxFrameBytes = 65536;
constexpr qint64 kStaleMs = 3500;
bool finiteNumber(const QJsonObject &object, const char *key, double *value)
{
    const QJsonValue field = object.value(QLatin1String(key));
    if (!field.isDouble() || !std::isfinite(field.toDouble())) return false;
    *value = field.toDouble();
    return true;
}
bool validMission(const QJsonObject &obj) {
    const auto integer = [&](const char *key, double limit) {
        const auto v = obj.value(key);
        const double n = v.toDouble(-1);
        return v.isDouble() && std::isfinite(n) && n >= 0 && n <= limit && std::floor(n) == n;
    };
    const QStringList states = {"IDLE", "READY", "FLYING", "HOVERING", "COMPLETED", "FAILED", "LANDING", "TAKING_OFF"};
    double value;
    if (obj.value("frame_id") != "world" || obj.value("map_session_id").toString().isEmpty()
        || obj.value("map_session_id").toString().size() > 80 || !obj.value("mission_id").isString()
        || obj.value("mission_id").toString().size() > 80 || !states.contains(obj.value("state").toString())
        || !integer("revision", 4294967295.) || !integer("current_waypoint_id", 4294967295.)
        || !integer("completed", 200) || !integer("total", 200)
        || obj.value("completed").toInt() > obj.value("total").toInt()
        || !obj.value("odom_fresh").isBool() || !obj.value("flight_ready").isBool()) return false;
    for (const char *key : {"world_x", "world_y", "world_z", "max_abs_xy", "min_z", "max_z"})
        if (!finiteNumber(obj, key, &value) || std::abs(value) > 1e6) return false;
    if (obj.contains("automatic_supported")) {
        if (!obj.value("automatic_supported").isBool() || !obj.value("automatic").isBool() ||
            !obj.value("takeoff_ready").isBool() || !finiteNumber(obj,"takeoff_height",&value) || value < 0 || value > 20) return false;
    }
    return obj.value("max_abs_xy").toDouble() > 0 && obj.value("min_z").toDouble() < obj.value("max_z").toDouble();
}
}

GroundLink::GroundLink(QObject *parent) : QObject(parent)
{
    m_socket.setReadBufferSize(kMaxFrameBytes + 1);
    m_commandTimeout.setSingleShot(true);
    m_commandTimeout.setInterval(5000);
    connect(&m_commandTimeout, &QTimer::timeout, this, [this] {
        finishCommand(false, QStringLiteral("命令结果未知，请查询任务状态；不会自动重发"));
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this]() {
        m_buffer += m_socket.readAll();
        if (m_buffer.size() > kMaxFrameBytes && !m_buffer.contains('\n')) {
            m_status = QStringLiteral("机载数据帧过大，已断开");
            m_socket.abort();
            emit changed();
            return;
        }
        int newline = -1;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(newline);
            m_buffer.remove(0, newline + 1);
            if (line.size() > kMaxFrameBytes) {
                m_status = QStringLiteral("机载数据帧过大，已断开");
                m_socket.abort();
                emit changed();
                return;
            }
            consumeLine(line);
        }
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this]() {
        m_connecting = false;
        resetTelemetry();
        if (!m_status.contains(QStringLiteral("过大")) &&
            !m_status.contains(QStringLiteral("超时")) &&
            m_status != QStringLiteral("已手动断开机载链路"))
            m_status = QStringLiteral("机载链路已断开");
        emit changed();
    });
    connect(&m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, [this](QAbstractSocket::SocketError) {
        m_connecting = false;
        resetTelemetry();
        m_status = QStringLiteral("连接失败：%1").arg(m_socket.errorString());
        emit changed();
    });
    m_watchdog.setInterval(500);
    connect(&m_watchdog, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastFrameMs > 0 && now - m_lastFrameMs > kStaleMs) {
            m_status = QStringLiteral("机载遥测超时，已断开");
            m_connecting = false;
            resetTelemetry();
            m_socket.abort();
            emit changed();
        }
    });
    m_watchdog.start();
}

GroundLink::~GroundLink() {
    m_socket.disconnect(this);
    m_watchdog.stop(); m_commandTimeout.stop();
    m_socket.abort();
}
void GroundLink::finishCommand(bool accepted, const QString &reason, const QVariantMap &mission) {
    if (m_pendingId.isEmpty()) return;
    const QString id = m_pendingId;
    m_pendingId.clear(); m_commandTimeout.stop();
    emit commandFinished(id, accepted, reason, mission);
    emit changed();
}
QString GroundLink::sendCommand(const QString &operation, const QString &session, const QString &mission,
                               int revision, const QVariantList &waypoints, const QVariantMap &automatic) {
    if (!m_connected || m_protocol != 2 || busy()
        || (operation != "upload" && operation != "start" && operation != "get_status" && operation != "cancel")) return {};
    QJsonObject obj{{"type", "command"}, {"protocol", 2}, {"operation", operation},
                    {"map_session_id", session}, {"mission_id", mission}, {"revision", revision},
                    {"require_map_alignment", true}};
    m_pendingId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    obj["request_id"] = m_pendingId;
    if (operation == "upload") {
        obj["frame_id"] = "world";
        obj["waypoints"] = QJsonArray::fromVariantList(waypoints);
        if (!automatic.isEmpty()) {
            obj["automatic"] = automatic.value("automatic").toBool();
            obj["ground_origin"] = QJsonObject::fromVariantMap(automatic.value("ground_origin").toMap());
        }
    }
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > kMaxFrameBytes || m_socket.write(bytes) != bytes.size()) {
        m_pendingId.clear(); return {};
    }
    m_commandTimeout.start();
    emit changed();
    return m_pendingId;
}
void GroundLink::queryMission() { sendCommand("get_status", {}, {}, 0); }

void GroundLink::connectToGateway(const QString &host, int port)
{
    const QString trimmed = host.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QChar::Space) || port < 1 || port > 65535) {
        m_status = QStringLiteral("机载 IP 或端口无效");
        emit changed();
        return;
    }
    m_socket.abort();
    m_buffer.clear();
    resetTelemetry();
    m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
    m_endpoint = QStringLiteral("%1:%2").arg(trimmed).arg(port);
    m_connecting = true;
    m_status = QStringLiteral("正在连接 %1").arg(m_endpoint);
    emit changed();
    m_socket.connectToHost(trimmed, static_cast<quint16>(port));
}

void GroundLink::disconnectFromGateway()
{
    m_socket.abort();
    m_connecting = false;
    resetTelemetry();
    m_status = QStringLiteral("已手动断开机载链路");
    emit changed();
}

void GroundLink::resetTelemetry()
{
    finishCommand(false, QStringLiteral("连接中断，命令结果未知；请重连后查询"));
    m_protocol = 0; m_mission.clear(); m_bounds.clear(); m_mapValid = false; m_mapStamp = 0;
    m_connected = false;
    m_fcuConnected = false;
    m_armed = false;
    m_missionReady = false;
    m_odomFresh = false;
    m_mode.clear();
    m_batteryPercent = -1.0;
    m_worldX = m_worldY = m_worldZ = 0.0;
    m_lastFrameMs = 0;
}

void GroundLink::consumeLine(const QByteArray &line)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return;
    const QJsonObject object = document.object();
    if (object.value("type") == "ack" && object.value("protocol").toInt() == 2) {
        if (m_connected && m_protocol == 2 && !m_pendingId.isEmpty()
            && object.value("request_id").toString() == m_pendingId
            && object.value("accepted").isBool() && validMission(object.value("mission").toObject())) {
            m_mission = object.value("mission").toObject().toVariantMap();
            finishCommand(object.value("accepted").toBool(), object.value("reason").toString().left(512), m_mission);
        }
        return;
    }
    const int protocol = object.value("protocol").toInt();
    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("telemetry") ||
        (protocol != 1 && protocol != 2) ||
        object.value(QStringLiteral("server")).toString() != QStringLiteral("agri-onboard-ros")) return;
    const QStringList booleanKeys = {QStringLiteral("fcu_connected"), QStringLiteral("armed"),
                                     QStringLiteral("mission_ready"), QStringLiteral("odom_fresh")};
    for (const QString &key : booleanKeys)
        if (!object.value(key).isBool()) return;
    if (protocol == 2 && !validMission(object.value("mission").toObject())) return;
    double battery = -1.0, x = 0.0, y = 0.0, z = 0.0;
    if (!finiteNumber(object, "battery_percent", &battery) ||
        !finiteNumber(object, "world_x", &x) || !finiteNumber(object, "world_y", &y) ||
        !finiteNumber(object, "world_z", &z) || battery < -1.0 || battery > 100.0 ||
        object.value(QStringLiteral("mode")).toString().size() > 64) return;
    m_protocol = protocol;
    m_mission = protocol == 2 ? object.value("mission").toObject().toVariantMap() : QVariantMap();
    m_mapValid = protocol == 2 && object.value("map_coordinates_valid").isBool() && object.value("map_coordinates_valid").toBool();
    m_mapStamp = object.value("map_odom_stamp").toDouble();
    m_bounds = object.value("flight_bounds").toArray().toVariantList();
    if (m_bounds.size() != 6) { m_bounds.clear(); m_mapValid = false; }
    for (int i = 0; i < m_bounds.size(); ++i) {
        const double v = m_bounds[i].toDouble();
        if (!object.value("flight_bounds").toArray()[i].isDouble() || !std::isfinite(v)
            || (i < 3 && v >= m_bounds[i+3].toDouble())) { m_bounds.clear(); m_mapValid = false; break; }
    }
    m_fcuConnected = object.value(QStringLiteral("fcu_connected")).toBool();
    m_armed = object.value(QStringLiteral("armed")).toBool();
    m_missionReady = object.value(QStringLiteral("mission_ready")).toBool();
    m_odomFresh = object.value(QStringLiteral("odom_fresh")).toBool();
    m_mode = object.value(QStringLiteral("mode")).toString();
    m_batteryPercent = battery;
    m_worldX = x;
    m_worldY = y;
    m_worldZ = z;
    m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
    m_connected = true;
    m_connecting = false;
    m_status = protocol == 2 ? QStringLiteral("机载任务网关已连接") : QStringLiteral("机载遥测已连接（只读）");
    emit changed();
}
