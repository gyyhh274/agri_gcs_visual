#include "GroundLink.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QtGlobal>
#include <QNetworkProxy>
#include <QSet>
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
}

GroundLink::GroundLink(QObject *parent) : QObject(parent)
{
    m_socket.setProxy(QNetworkProxy::NoProxy);
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
        if (m_buffer.size() > kMaxFrameBytes) {
            m_status = QStringLiteral("机载数据帧过大，已断开");
            m_socket.abort();
            emit changed();
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
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (now - it.value().sentMs <= 5000) { ++it; continue; }
            const QString id = it.key(), operation = it.value().operation;
            it = m_pending.erase(it);
            emit commandFinished(id, operation, false, QStringLiteral("确认超时；执行结果未知，请查询机载状态后再操作"));
            emit changed();
        }
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

GroundLink::~GroundLink()
{
    // QTcpSocket can emit disconnected during member destruction. Stop callbacks
    // before QString/mission members are destroyed, including pending-command teardown.
    m_watchdog.stop();
    m_socket.disconnect(this);
    m_socket.abort();
}

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
    const auto pending = m_pending;
    m_pending.clear();
    m_mission.clear();
    m_protocol = 0;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit commandFinished(it.key(), it.value().operation, false, QStringLiteral("链路中断；执行结果未知，重连后查询状态"));
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
    const QString type = object.value(QStringLiteral("type")).toString();
    const int protocol = object.value(QStringLiteral("protocol")).toInt();
    if (protocol == 2 && type == QStringLiteral("ack")) {
        const QString id = object.value(QStringLiteral("request_id")).toString();
        if (!m_pending.contains(id) || !object.value(QStringLiteral("accepted")).isBool()
                || !consumeMission(object.value(QStringLiteral("mission")).toObject())) return;
        const QString operation = m_pending.take(id).operation;
        emit changed();
        emit commandFinished(id, operation, object.value(QStringLiteral("accepted")).toBool(),
                             object.value(QStringLiteral("reason")).toString());
        return;
    }
    if (protocol == 2 && type == QStringLiteral("error")) {
        emit notified(object.value(QStringLiteral("reason")).toString().left(300));
        return;
    }
    if (type != QStringLiteral("telemetry") ||
        (protocol != 1 && protocol != 2) ||
        object.value(QStringLiteral("server")).toString() != QStringLiteral("agri-onboard-ros")) return;
    const QStringList booleanKeys = {QStringLiteral("fcu_connected"), QStringLiteral("armed"),
                                     QStringLiteral("mission_ready"), QStringLiteral("odom_fresh")};
    for (const QString &key : booleanKeys)
        if (!object.value(key).isBool()) return;
    double battery = -1.0, x = 0.0, y = 0.0, z = 0.0;
    if (!finiteNumber(object, "battery_percent", &battery) ||
        !finiteNumber(object, "world_x", &x) || !finiteNumber(object, "world_y", &y) ||
        !finiteNumber(object, "world_z", &z) || battery < -1.0 || battery > 100.0 ||
        object.value(QStringLiteral("mode")).toString().size() > 64) return;
    if (protocol == 2 && !consumeMission(object.value(QStringLiteral("mission")).toObject())) return;
    if (protocol == 1) m_mission.clear();
    m_protocol = protocol;
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
    m_status = protocol == 2 ? QStringLiteral("机载任务链路已连接 · world 局部坐标")
        : m_fcuConnected ? QStringLiteral("机载电脑与 PX4 已连接（只读）")
                         : QStringLiteral("已连机载电脑，PX4 未连接（只读）");
    emit changed();
}

bool GroundLink::consumeMission(const QJsonObject &object)
{
    const QString session = object.value("map_session_id").toString();
    const QString state = object.value("state").toString();
    const QStringList states = {"IDLE", "READY", "FLYING", "HOVERING", "COMPLETED", "FAILED", "LANDING"};
    if (session.isEmpty() || session.size() > 80 || object.value("frame_id").toString() != "world"
            || !states.contains(state) || object.value("mission_id").toString().size() > 80
            || !object.value("odom_fresh").isBool() || !object.value("flight_ready").isBool()) return false;
    for (const char *key : {"revision", "current_waypoint_id", "completed", "total"}) {
        double value;
        if (!finiteNumber(object, key, &value) || value < 0 || value > 4294967295.0 || std::floor(value) != value) return false;
    }
    if (object.value("total").toInt() > 200 || object.value("completed").toInt() > object.value("total").toInt()) return false;
    for (const char *key : {"world_x", "world_y", "world_z", "max_abs_xy", "min_z", "max_z"}) {
        double value;
        if (!finiteNumber(object, key, &value)) return false;
    }
    if (object.value("max_abs_xy").toDouble() <= 0 || object.value("min_z").toDouble() < 0
            || object.value("max_z").toDouble() <= object.value("min_z").toDouble()) return false;
    m_mission = object.toVariantMap();
    return true;
}

QString GroundLink::sendCommand(const QString &operation, const QString &session,
                               const QString &missionId, int revision, const QVariantList &waypoints, int replaceFrom)
{
    if (!remoteAvailable() || busy() || m_socket.state() != QAbstractSocket::ConnectedState) {
        emit notified(QStringLiteral("需要连接新版任务网关，并等待上一条命令确认"));
        return {};
    }
    if (operation != "get_status" && session != m_mission.value("map_session_id").toString()) {
        emit notified(QStringLiteral("定位会话已变化，请重新检查局部航线"));
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject command{{"type", "command"}, {"protocol", 2}, {"request_id", id}, {"operation", operation},
                        {"map_session_id", session}, {"mission_id", missionId}, {"revision", revision}};
    if (operation == "upload" || operation == "update") {
        command.insert("frame_id", "world");
        command.insert("waypoints", QJsonArray::fromVariantList(waypoints));
    }
    if (operation == "update") command.insert("replace_from", replaceFrom);
    const QByteArray frame = QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n';
    if (frame.size() > kMaxFrameBytes || m_socket.write(frame) != frame.size()) {
        emit notified(QStringLiteral("命令未完整写入；请重连并查询机载状态"));
        m_socket.abort();
        return {};
    }
    m_pending.insert(id, {operation, QDateTime::currentMSecsSinceEpoch()});
    emit changed();
    return id;
}

QString GroundLink::uploadLocalRoute(const QVariantList &waypoints, const QString &session)
{
    if (waypoints.isEmpty() || waypoints.size() > 200) {
        emit notified(QStringLiteral("航点数量必须为 1–200"));
        return {};
    }
    if (!validateWaypoints(waypoints)) return {};
    return sendCommand("upload", session, QUuid::createUuid().toString(QUuid::WithoutBraces), 1, waypoints);
}

QString GroundLink::updateLocalRoute(const QVariantList &suffix, int replaceFrom, const QString &missionId,
                                    int revision, const QString &session)
{
    const QString state = m_mission.value("state").toString();
    const int total = m_mission.value("total").toInt();
    const int completed = m_mission.value("completed").toInt();
    const bool continuation = state == "COMPLETED" || (state == "READY" && completed > 0);
    if (!remoteAvailable() || (state != "FLYING" && state != "HOVERING" && !continuation) || missionId.isEmpty()
            || missionId != m_mission.value("mission_id").toString()
            || revision <= 1 || qint64(revision) != m_mission.value("revision").toLongLong() + 1
            || (continuation ? replaceFrom < completed || replaceFrom + suffix.size() <= completed : replaceFrom <= completed)
            || replaceFrom > total
            || replaceFrom + suffix.size() > 200) {
        emit notified(QStringLiteral("任务版本或执行进度已变化；只能更新当前执行点之后的航点，请查询机载状态"));
        return {};
    }
    // An empty suffix deletes only future points; replaceFrom == total appends.
    if (!validateWaypoints(suffix)) return {};
    return sendCommand("update", session, missionId, revision, suffix, replaceFrom);
}

bool GroundLink::validateWaypoints(const QVariantList &waypoints)
{
    const double xy = m_mission.value("max_abs_xy").toDouble();
    const double minZ = m_mission.value("min_z").toDouble(), maxZ = m_mission.value("max_z").toDouble();
    QSet<quint32> ids;
    for (const QVariant &value : waypoints) {
        const QVariantMap point = value.toMap();
        for (const char *key : {"id", "x", "y", "z", "hover_sec"}) {
            const QJsonValue number = QJsonValue::fromVariant(point.value(key));
            if (!number.isDouble() || !std::isfinite(number.toDouble())) {
                emit notified(QStringLiteral("航点包含无效数值")); return false;
            }
        }
        const double id = point.value("id").toDouble();
        if (id < 1 || id > 4294967295.0 || std::floor(id) != id || ids.contains(quint32(id))) {
            emit notified(QStringLiteral("航点 ID 必须为不重复的正整数")); return false;
        }
        ids.insert(quint32(id));
        if (std::abs(point.value("x").toDouble()) > xy || std::abs(point.value("y").toDouble()) > xy
                || point.value("z").toDouble() < minZ || point.value("z").toDouble() > maxZ
                || point.value("hover_sec").toDouble() < 0 || point.value("hover_sec").toDouble() > 60) {
            emit notified(QStringLiteral("航点超出机载配置范围；Z 应为 %1–%2 m").arg(minZ).arg(maxZ));
            return false;
        }
    }
    return true;
}

QString GroundLink::returnToHome(const QString &missionId, int revision, const QString &session)
{
    const QString state = m_mission.value("state").toString();
    if (!remoteAvailable() || !m_armed || !m_odomFresh || missionId.isEmpty()
            || missionId != m_mission.value("mission_id").toString()
            || revision != m_mission.value("revision").toInt()
            || (state != "READY" && state != "FLYING" && state != "HOVERING" && state != "COMPLETED")) {
        emit notified(QStringLiteral("返航需要有效空中任务、定位和当前任务版本"));
        return {};
    }
    return sendCommand("return", session, missionId, revision);
}

QString GroundLink::startUploadedMission(const QString &missionId, int revision, const QString &session)
{
    return sendCommand("start", session, missionId, revision);
}

void GroundLink::queryMission()
{
    sendCommand("get_status", {}, {}, 0);
}
