#include "GroundLink.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>
#include <cmath>

namespace {
constexpr int kMaxFrameBytes = 16384;
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
    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("telemetry") ||
        object.value(QStringLiteral("protocol")).toInt() != 1 ||
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
    m_status = m_fcuConnected ? QStringLiteral("机载电脑与 PX4 已连接（只读）")
                              : QStringLiteral("已连机载电脑，PX4 未连接（只读）");
    emit changed();
}
